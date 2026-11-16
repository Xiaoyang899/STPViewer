#include "stp/document_workspace.h"
#include "stp/cad_job_queue.h"
#include "stp/main_window.h"
#include "stp/viewer_widget.h"
#include "stp/fullscreen_button.h"
#include "stp/part_transform_panel.h"
#include "app/qtopengl_utils.h"

#include <Graphic3d_Camera.hxx>
#include <BRepBndLib.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFPrs_AISObject.hxx>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLayout>
#include <QOpenGLWidget>
#include <QMouseEvent>
#include <QSettings>
#include <QScreen>
#include <QSignalSpy>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void waitFor(const std::function<bool()>& condition, int timeout = 30000)
{
    QElapsedTimer timer;timer.start();
    while (!condition()) {
        if (timer.elapsed() >= timeout) throw std::runtime_error("fullscreen GUI timeout");
        QTest::qWait(20);
    }
}
void waitForStableWindow(const std::function<bool()>& condition)
{
    // Cocoa completes fullscreen/zoom transitions asynchronously. A state flag
    // can briefly have its requested value before the native animation finishes.
    QElapsedTimer deadline, stable;deadline.start();
    while (deadline.elapsed() < 10000) {
        if (condition()) {
            if (!stable.isValid()) stable.start();
            if (stable.elapsed() >= 400) return;
        } else stable.invalidate();
        QTest::qWait(20);
    }
    throw std::runtime_error("native window state did not settle");
}
void ready(Stp::MainWindow* page)
{
    require(page, "missing document");
    waitFor([page] {
        const auto action = page->findChild<QAction*>("editPlacement");
        return page->document() && !page->isBusy() && action && action->isEnabled();
    });
    QTest::qWait(150);
}
bool sameCamera(const Handle(Graphic3d_Camera)& a, const Handle(Graphic3d_Camera)& b)
{
    return a->Center().Distance(b->Center()) < 1.e-6 && a->Eye().Distance(b->Eye()) < 1.e-6
        && a->Direction().IsEqual(b->Direction(), 1.e-8) && a->Up().IsEqual(b->Up(), 1.e-8)
        && std::abs(a->Scale() - b->Scale()) < 1.e-6
        && a->ProjectionType() == b->ProjectionType();
}
QJsonObject geometryState(const QWidget& workspace,const QWidget& viewport)
{
    const auto rectangle=[](const QRect& r){return QJsonArray{r.x(),r.y(),r.width(),r.height()};};
    QJsonObject state{{"workspaceGeometry",rectangle(workspace.geometry())},
        {"frameGeometry",rectangle(workspace.frameGeometry())},
        {"viewportSize",QJsonArray{viewport.width(),viewport.height()}},
        {"devicePixelRatio",workspace.devicePixelRatioF()}};
    if(const auto screen=workspace.screen()){
        state["screenGeometry"]=rectangle(screen->geometry());
        state["availableGeometry"]=rectangle(screen->availableGeometry());
    }
    return state;
}
struct GrabTarget {
    int occurrence = -1;
    QPoint point;
};
GrabTarget grabTarget(Stp::ViewerWidget* viewer, QWidget* viewport, const Stp::CadDocument& document)
{
    const double ratio=viewport->devicePixelRatioF();
    for(size_t index=0;index<document.occurrences.size();++index){
        const int part=document.occurrences[index].partIndex;
        if(part<0||!viewer->isOccurrenceDisplayed(int(index)))continue;
        viewer->selectOccurrences({int(index)});viewer->context()->InitSelected();
        if(!viewer->context()->MoreSelected())continue;
        const auto object=Handle(XCAFPrs_AISObject)::DownCast(viewer->context()->SelectedInteractive());
        if(object.IsNull())continue;
        Bnd_Box box;BRepBndLib::Add(document.parts[size_t(part)].shape,box);if(box.IsVoid())continue;
        double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);
        const double low[]{x0,y0,z0},high[]{x1,y1,z1};
        const auto world=object->LocalTransformation()*XCAFDoc_ShapeTool::GetLocation(object->GetLabel()).Transformation();
        for(int axis=0;axis<3;++axis)for(double side:{0.,1.})for(double a:{.25,.5,.75})for(double b:{.25,.5,.75}){
            double p[3];for(int i=0;i<3;++i)p[i]=(low[i]+high[i])*.5;
            p[axis]=low[axis]+side*(high[axis]-low[axis]);
            p[(axis+1)%3]=low[(axis+1)%3]+a*(high[(axis+1)%3]-low[(axis+1)%3]);
            p[(axis+2)%3]=low[(axis+2)%3]+b*(high[(axis+2)%3]-low[(axis+2)%3]);
            const auto point=gp_Pnt(p[0],p[1],p[2]).Transformed(world);
            int x,y;viewer->view()->Convert(point.X(),point.Y(),point.Z(),x,y);
            const QPoint logical(qRound(x/ratio),qRound(y/ratio));
            if(!viewport->rect().adjusted(8,8,-8,-8).contains(logical))continue;
            viewer->context()->MoveTo(x,y,viewer->view(),false);
            if(viewer->context()->HasDetected()&&viewer->context()->DetectedInteractive()==object)
                return {int(index),logical};
        }
    }
    throw std::runtime_error("fullscreen fixture has no visible grabbable part");
}
void dragPart(QWidget* viewport,const QPoint& from)
{
    const auto to=from+QPoint(45,-25);
    QMouseEvent press(QEvent::MouseButtonPress,from,from,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(viewport,&press);
    QMouseEvent move(QEvent::MouseMove,to,to,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(viewport,&move);
    QMouseEvent release(QEvent::MouseButtonRelease,to,to,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(viewport,&release);QTest::qWait(40);
}
}

// Requires a real GUI/OpenGL desktop. Not part of headless CTest.
int main(int argc, char** argv)
{
    Mayo::QtOpenGlUtils::platformSetup(argc, argv);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    if (argc < 2 || argc > 3) {
        std::cerr << "Expected STEP fixture path [evidence directory]\n";
        return 2;
    }
    QTemporaryDir temporary;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    app.setOrganizationName("STPFullscreenTest");app.setApplicationName("STPFullscreenTest");
    qputenv("CSF_ShadersDirectory", QByteArray(STP_OCCT_RESOURCE_DIR) + "/Shaders");
    const QString output = argc == 3 ? QString::fromLocal8Bit(argv[2]) : temporary.filePath("evidence");
    QDir().mkpath(output);
    QJsonObject report;
    int result = 0;
    try {
        Stp::DocumentWorkspace workspace;
        workspace.resize(1200, 800);workspace.show();
        workspace.openFiles({QString::fromLocal8Bit(argv[1])});
        auto page = workspace.currentDocument();ready(page);
        auto tabs = workspace.findChild<QTabWidget*>("documentTabs");
        auto viewer = page->findChild<Stp::ViewerWidget*>();
        auto viewport = viewer->findChild<QOpenGLWidget*>("cadViewport");
        auto button = page->findChild<Stp::ViewportFullscreenButton*>("viewportFullscreenButton");
        auto grab = page->findChild<QToolButton*>("viewportGrabButton");
        auto normalGrab = page->findChild<QToolButton*>("partGrabButton");
        auto grabAction = page->findChild<QAction*>("togglePartGrab");
        auto placements = page->findChild<Stp::PartTransformPanel*>();
        auto assembly = page->findChild<QDockWidget*>("assemblyDock");
        auto details = page->findChild<QDockWidget*>("detailsDock");
        auto toolbar = page->findChild<QToolBar*>("mainToolbar");
        auto modelPage = page->findChild<QWidget*>("modelPage");
        require(tabs && viewport && button && grab && normalGrab && grabAction && placements
                && assembly && details && toolbar && modelPage,
                "fullscreen controls missing");
        require(button->size() == QSize(36, 36) && button->parentWidget() == viewport,
                "fullscreen button hit target or viewport parent changed");
        require(button->isVisible() && button->isEnabled(), "fullscreen button unavailable after import");
        require(grab->isHidden() && grab->defaultAction()==grabAction && normalGrab->defaultAction()==grabAction,
                "normal and fullscreen grab controls do not share one action");
        const auto document = page->document();
        const auto viewHandle = viewer->view();
        const auto aisContext = viewer->context();
        auto glContext = viewport->context();
        auto surfaceParent = viewport->parentWidget();
        const Handle(Graphic3d_Camera) camera = new Graphic3d_Camera(viewer->view()->Camera());
        int leaf = -1;
        for (size_t i = 0; i < document->occurrences.size(); ++i)
            if (document->occurrences[i].partIndex >= 0) { leaf = int(i);break; }
        require(leaf >= 0, "fixture has no leaf");
        viewer->selectOccurrences({leaf});viewer->selectionChanged({leaf});
        details->hide();page->resizeDocks({assembly}, {230}, Qt::Horizontal);
        QTest::qWait(200);
        const auto originalGeometry = workspace.geometry();
        const auto originalLayout = page->saveState();
        const auto originalMargins = modelPage->layout()->contentsMargins();
        const auto originalViewportSize = viewport->size();
        report["initialDevicePixelRatio"] = workspace.devicePixelRatioF();
        report["originalGeometryState"] = geometryState(workspace,*viewport);
        require(workspace.grab().save(output + "/normal.png"), "normal screenshot failed");
        QSignalSpy requests(button, &Stp::ViewportFullscreenButton::toggleRequested);
        QSignalSpy transitions(&workspace, &Stp::DocumentWorkspace::viewportFullscreenChanged);

        report["stage"] = "first-fullscreen-entry";
        QTest::mouseClick(button, Qt::LeftButton);
        require(!workspace.isViewportFullscreen() && requests.count() == 0,
                "fullscreen switched before the corner animation");
        QTest::mouseClick(button, Qt::LeftButton); // Repeated clicks must not queue another switch.
        waitForStableWindow([&] { return workspace.isViewportFullscreen() && workspace.isFullScreen()
            && !workspace.isViewportFullscreenTransitioning(); });
        require(requests.count() == 1 && button->isFullscreen(), "animated click toggled more than once");
        require(!tabs->tabBar()->isVisible() && !toolbar->isVisible()
                && !assembly->isVisible() && !details->isVisible(), "fullscreen left panels visible");
        require(button->isVisible(), "exit button disappeared in fullscreen");
        require(workspace.width() - viewport->width() <= 8 && workspace.height() - viewport->height() <= 8,
                "fullscreen viewport does not fill the workspace");
        require(viewport->parentWidget() == surfaceParent && viewport->context() == glContext
                && viewer->view() == viewHandle && viewer->context() == aisContext,
                "fullscreen recreated or reparented the OpenGL/CAD scene");
        require(sameCamera(viewer->view()->Camera(), camera)
                && viewer->selectedOccurrences() == std::vector<int>{leaf} && page->document() == document,
                "fullscreen changed camera, selection or document");
        require(button->pos() == QPoint(viewport->width() - 48, viewport->height() - 48),
                "fullscreen button did not stay at the lower right");
        require(grab->isVisible()&&grab->parentWidget()==viewport&&grab->size()==QSize(36,36)
                &&grab->iconSize()==QSize(28,28)&&grab->pos()==button->pos()-QPoint(44,0)
                &&!grab->geometry().intersects(button->geometry()), "fullscreen grab layout or visibility is wrong");
        require(workspace.grab().save(output + "/fullscreen.png"), "fullscreen screenshot failed");
        require(viewer->saveScreenshot(output + "/fullscreen-model.png"), "fullscreen GL screenshot failed");
        report["fullscreenSceneAndButton"] = true;

        report["stage"] = "fullscreen-grab-and-temporary-move";
        const auto initialSelection=viewer->selectedOccurrences();
        QTest::mouseClick(grab,Qt::LeftButton);
        require(grabAction->isChecked()&&grab->isChecked()&&normalGrab->isChecked()&&viewer->grabMode(),
                "fullscreen hand did not enable the shared grab mode");
        require(viewer->selectedOccurrences()==initialSelection,"overlay hand click passed through to model selection");
        QTest::qWait(220);
        require(workspace.isViewportFullscreen()&&requests.count()==1,"grab click activated fullscreen animation or exited fullscreen");
        const auto target=grabTarget(viewer,viewport,*document);
        const auto beforeMove=viewer->measurementPartEntity(target.occurrence).displayTransform;
        const auto originalWorld=document->occurrences[size_t(target.occurrence)].worldTransform;
        const auto edits=placements->committedEdits();
        const bool modifiedBeforeGrab=page->hasPlacementChanges();
        dragPart(viewport,target.point);
        const auto grabbedTransform=viewer->measurementPartEntity(target.occurrence).displayTransform;
        require(!Stp::samePlacement(beforeMove,grabbedTransform)&&sameCamera(viewer->view()->Camera(),camera),
                "fullscreen hand failed to move a real part or rotated the camera");
        require(Stp::samePlacement(originalWorld,document->occurrences[size_t(target.occurrence)].worldTransform)
                &&placements->committedEdits().size()==edits.size()&&page->hasPlacementChanges()==modifiedBeforeGrab,
                "temporary fullscreen drag changed source placement or dirty state");
        for(const auto& edit:edits)require(Stp::samePlacement(edit.second,placements->committedEdits().at(edit.first)),
                "temporary fullscreen drag changed an applied placement edit");
        require(workspace.grab().save(output+"/fullscreen-grab.png"),"fullscreen grab screenshot failed");

        viewer->setFaceColorPicking(true);
        require(viewer->faceColorPicking()&&!viewer->grabMode()&&!grabAction->isChecked()&&!grab->isChecked()&&!normalGrab->isChecked(),
                "face picking did not turn off both grab controls");
        QTest::mouseClick(grab,Qt::LeftButton);
        require(viewer->grabMode()&&!viewer->faceColorPicking(),"fullscreen grab did not cancel face picking");
        viewer->setMeasurementPicking(true,Stp::MeasurementSelection::Part);
        require(viewer->measurementPicking()&&!viewer->grabMode()&&!grab->isChecked()&&!normalGrab->isChecked(),
                "measurement picking did not turn off both grab controls");
        QTest::mouseClick(grab,Qt::LeftButton);
        require(viewer->grabMode()&&!viewer->measurementPicking(),"fullscreen grab did not cancel measurement picking");
        viewer->beginSectionPlanePick();
        require(viewer->sectionPicking()&&!viewer->grabMode()&&!grab->isChecked()&&!normalGrab->isChecked(),
                "section picking did not turn off both grab controls");
        QTest::mouseClick(grab,Qt::LeftButton);
        require(viewer->grabMode()&&!viewer->sectionPicking()&&!viewer->measurementPicking(),
                "fullscreen grab did not cancel section picking");
        require(workspace.isViewportFullscreen()&&grab->isVisible()&&requests.count()==1,
                "grab mode changes opened a panel or exited fullscreen");
        report["fullscreenGrabAndModeExclusion"] = true;

        // Escape cancels an active pick before leaving fullscreen.
        viewer->beginSectionPlanePick();require(viewer->sectionPicking(), "section pick did not start");
        QTest::keyClick(viewport, Qt::Key_Escape);
        require(workspace.isViewportFullscreen() && !viewer->sectionPicking(), "Escape did not cancel picking first");
        QTest::mouseClick(grab,Qt::LeftButton);
        require(viewer->grabMode()&&grabAction->isChecked(),"grab mode did not re-enable before leaving fullscreen");
        QTest::keyClick(viewport, Qt::Key_Escape);
        waitForStableWindow([&] { return !workspace.isViewportFullscreen() && !workspace.isFullScreen()
            && !workspace.isViewportFullscreenTransitioning(); });
        report["stage"] = "restore-exact-geometry";
        report["restoredGeometryState"] = geometryState(workspace,*viewport);
        report["restoreBeforeGeometryWaitScreenshot"] = workspace.grab().save(output+"/restore-before-geometry-wait.png")
            ? "saved" : "failed";
        waitFor([&] { return workspace.geometry() == originalGeometry && viewport->size() == originalViewportSize; }, 10000);
        QTest::qWait(100);
        require(!button->isFullscreen() && details->isHidden() && assembly->isVisible()
                && tabs->tabBar()->isVisible() && toolbar->isVisible(), "original panel visibility was not restored");
        require(page->saveState() == originalLayout && modelPage->layout()->contentsMargins() == originalMargins,
                "dock layout or central margins changed after fullscreen");
        require(sameCamera(viewer->view()->Camera(), camera), "restoring layout changed the camera");
        require(grab->isHidden()&&normalGrab->isChecked()&&grabAction->isChecked()&&viewer->grabMode()
                &&Stp::samePlacement(grabbedTransform,viewer->measurementPartEntity(target.occurrence).displayTransform),
                "leaving fullscreen lost grab mode or temporary part placement");
        normalGrab->click();
        require(!viewer->grabMode()&&!grab->isChecked()&&!grabAction->isChecked(),"normal grab button did not synchronize its fullscreen proxy");
        require(workspace.grab().save(output + "/restored.png"), "restored screenshot failed");
        report["escapeAndExactLayoutRestore"] = true;
        report["grabStateAndMoveSurviveRestore"] = true;

        report["stage"] = "rapid-native-transition-requests";
        workspace.toggleViewportFullscreen();
        workspace.exitViewportFullscreen();
        const bool asynchronousExit=workspace.isViewportFullscreenTransitioning();
        workspace.toggleViewportFullscreen();
        if(asynchronousExit){
            require(!workspace.isViewportFullscreen(),"re-entry was accepted before native exit completed");
        }else{
            // Windows can complete exit synchronously. The next request is then
            // a valid new entry, rather than an overlapping native transition.
            waitForStableWindow([&] { return workspace.isViewportFullscreen()&&workspace.isFullScreen()
                &&!workspace.isViewportFullscreenTransitioning(); });
            workspace.exitViewportFullscreen();
        }
        waitForStableWindow([&] { return !workspace.isViewportFullscreen() && !workspace.isFullScreen()
            && !workspace.isViewportFullscreenTransitioning() && workspace.geometry() == originalGeometry; });
        require(grab->isHidden()&&!grab->isChecked(),"rapid native transitions leaked fullscreen grab visibility or state");
        require(page->saveState() == originalLayout, "rapid native transition requests changed dock layout");
        report["rapidNativeTransitionRequests"] = true;
        report["rapidExitMode"] = asynchronousExit?"native asynchronous":"synchronous";

        for (int i = 0; i < 3; ++i) {
            workspace.toggleViewportFullscreen();
            waitForStableWindow([&] { return workspace.isViewportFullscreen() && workspace.isFullScreen()
                && !workspace.isViewportFullscreenTransitioning(); });
            workspace.exitViewportFullscreen();
            waitForStableWindow([&] { return !workspace.isViewportFullscreen() && !workspace.isFullScreen()
                && !workspace.isViewportFullscreenTransitioning() && workspace.geometry() == originalGeometry; });
        }
        require(page->saveState() == originalLayout && details->isHidden(), "repeated fullscreen toggles drifted layout");
        report["repeatedToggles"] = true;

        report["stage"] = "maximize-window";
        workspace.showMaximized();waitForStableWindow([&] { return workspace.isMaximized() && !workspace.isFullScreen(); });
        report["stage"] = "enter-fullscreen-from-maximized";
        report["beforeMaxEnterWindowState"] = int(workspace.windowState());
        workspace.toggleViewportFullscreen();waitForStableWindow([&] { return workspace.isViewportFullscreen() && workspace.isFullScreen()
            && !workspace.isViewportFullscreenTransitioning(); });
        report["stage"] = "restore-maximized-via-button";
        QObject::connect(&workspace, &Stp::DocumentWorkspace::viewportFullscreenChanged, &workspace,
            [&report, &workspace](bool active) {
                report["lastTransitionActive"] = active;
                report["lastWindowState"] = int(workspace.windowState());
            });
        QTest::mouseClick(button, Qt::LeftButton);
        waitForStableWindow([&] { return !workspace.isViewportFullscreen() && workspace.isMaximized() && !workspace.isFullScreen()
            && !workspace.isViewportFullscreenTransitioning(); });
        report["maximizedRestore"] = true;
        workspace.showNormal();waitForStableWindow([&] { return !workspace.isMaximized() && !workspace.isFullScreen(); });

        // Panel operations restore the normal layout before exposing their controls.
        workspace.toggleViewportFullscreen();
        waitForStableWindow([&] { return workspace.isFullScreen() && !workspace.isViewportFullscreenTransitioning(); });
        page->findChild<QAction*>("editPlacement")->trigger();
        require(!workspace.isViewportFullscreen() && details->isVisible(), "panel request did not exit fullscreen");
        waitForStableWindow([&] { return !workspace.isFullScreen() && !workspace.isViewportFullscreenTransitioning(); });
        report["panelRequestRestoresLayout"] = true;

        const auto secondPath = temporary.filePath("second.step");
        require(QFile::copy(QString::fromLocal8Bit(argv[1]), secondPath), "copy second fixture failed");
        QTest::mouseClick(button, Qt::LeftButton); // Switch document before this animation completes.
        workspace.openFiles({secondPath});
        auto second = workspace.currentDocument();ready(second);QTest::qWait(220);
        auto secondGrab=second->findChild<QToolButton*>("viewportGrabButton");
        auto secondGrabAction=second->findChild<QAction*>("togglePartGrab");
        require(secondGrab&&secondGrabAction&&secondGrab->defaultAction()==secondGrabAction
                &&secondGrabAction!=grabAction&&secondGrab->isHidden(),"fullscreen grab action leaked between document pages");
        require(second != page && !workspace.isViewportFullscreen() && !page->isViewportOnly(),
                "pending animation entered fullscreen on another document");
        tabs->setCurrentWidget(page);ready(page);
        workspace.toggleViewportFullscreen();waitForStableWindow([&] { return workspace.isFullScreen()
            && !workspace.isViewportFullscreenTransitioning(); });tabs->setCurrentWidget(second);
        require(!workspace.isViewportFullscreen() && !page->isViewportOnly() && !second->isViewportOnly(),
                "switching tabs leaked viewport-only layout");
        report["documentSwitchAndAnimationCancellation"] = true;
        waitForStableWindow([&] { return !workspace.isFullScreen() && !workspace.isViewportFullscreenTransitioning(); });

        workspace.toggleViewportFullscreen();
        waitForStableWindow([&] { return workspace.isFullScreen() && !workspace.isViewportFullscreenTransitioning(); });
        workspace.closeDocument(second);
        require(!workspace.isViewportFullscreen() && workspace.documentCount() == 1,
                "closing a fullscreen document left a detached viewport");
        waitForStableWindow([&] { return !workspace.isFullScreen() && !workspace.isViewportFullscreenTransitioning(); });
        ready(page);workspace.toggleViewportFullscreen();waitForStableWindow([&] { return workspace.isFullScreen()
            && !workspace.isViewportFullscreenTransitioning(); });
        QTest::mouseClick(grab,Qt::LeftButton);
        require(grab->isVisible()&&grabAction->isChecked()&&viewer->grabMode(),"closing another document invalidated the remaining grab overlay");
        workspace.closeAll();
        require(!workspace.isViewportFullscreen() && workspace.documentCount() == 0,
                "closing all fullscreen documents failed");
        waitFor([&] { return workspace.jobQueue()->idle(); });
        report["fullscreenDocumentClose"] = true;
        require(transitions.count() >= 16, "fullscreen transitions were not observable");
        report["passed"] = true;
        report["stage"] = "complete";
        workspace.close();QTest::qWait(100);
    } catch (const std::exception& error) {
        report["passed"] = false;report["error"] = QString::fromUtf8(error.what());
        std::cerr << error.what() << '\n';result = 1;
    }
    QFile file(output + "/fullscreen-report.json");
    if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(report).toJson());
    return result;
}
