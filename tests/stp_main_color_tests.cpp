#include "document_workspace.h"
#include "main_window.h"
#include "viewer_widget.h"
#include "cad_job_queue.h"
#include "../src/app/qtopengl_utils.h"
#include <AIS_InteractiveContext.hxx>
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStyleFactory>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool b,const char* text){if(!b)throw std::runtime_error(text);}
void waitFor(const std::function<bool()>& f){QElapsedTimer clock;clock.start();while(!f()){if(clock.elapsed()>45000)throw std::runtime_error("main color UI timeout");QTest::qWait(20);}}
QAction* action(QObject* w,const char* name){auto a=w->findChild<QAction*>(name);require(a,name);return a;}
void select(Stp::MainWindow* w,int index){auto tree=w->findChild<QTreeWidget*>("assemblyTree");require(tree,"assemblyTree");tree->clearSelection();for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toInt()==index){(*it)->setSelected(true);return;}throw std::runtime_error("tree leaf missing");}
int firstLeaf(Stp::MainWindow* w){for(size_t i=0;i<w->document()->occurrences.size();++i)if(w->document()->occurrences[i].partIndex>=0)return int(i);throw std::runtime_error("no leaf");}
}
int main(int argc,char** argv){Mayo::QtOpenGlUtils::platformSetup(argc,argv);QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);QApplication app(argc,argv);app.setStyle(QStyleFactory::create("Fusion"));
    if(argc!=3){std::cerr<<"Usage: stp_main_color_tests sample.step evidence-directory\n";return 2;}
    QTemporaryDir settings;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());app.setOrganizationName("STPMainColorTest");app.setApplicationName("STPMainColorTest");
    const auto output=QDir(QString::fromLocal8Bit(argv[2])).absolutePath();QDir().mkpath(output);QJsonObject report;QString error;
    try{
        qputenv("CSF_ShadersDirectory",QByteArray(STP_OCCT_RESOURCE_DIR)+"/Shaders");Stp::DocumentWorkspace workspace;workspace.resize(1440,900);workspace.show();workspace.openFiles({QString::fromLocal8Bit(argv[1])});
        auto first=workspace.currentDocument();require(first,"first document");waitFor([&]{return action(first,"randomizeColors")->isEnabled();});
        auto view=first->findChild<Stp::ViewerWidget*>();auto mode=first->findChild<QComboBox*>("colorSelectionMode");require(view&&mode,"color controls missing");
        auto random=action(first,"randomizeColors"),restore=action(first,"restoreAllColors"),undo=action(first,"undoPartColor"),redo=action(first,"redoPartColor");
        require(random->text()==QStringLiteral("全局随机着色")&&restore->text()==QStringLiteral("恢复全部原色"),"global action labels");
        require(mode->count()==2&&mode->itemText(0)==QStringLiteral("选零件")&&mode->itemText(1)==QStringLiteral("选面"),"selection mode labels");
        require(!first->hasPlacementChanges()&&!restore->isEnabled()&&!undo->isEnabled(),"initial color state dirty or restorable");const int leaf=firstLeaf(first);select(first,leaf);
        require(first->setPartColors({leaf},QColor("#db253c"))&&first->setFaceColors({{leaf,1}},QColor("#15c9d9")),"manual colors setup");const auto manual=first->colorEdits();
        require(first->hasPlacementChanges()&&manual.faces.size()==1,"manual face absent from dirty state");random->trigger();const auto randomized=first->colorEdits();
        size_t leaves=0;for(const auto& o:first->document()->occurrences)if(o.partIndex>=0)++leaves;
        require(randomized.size()==leaves&&randomized.faces.empty()&&first->hasPlacementChanges(),"random did not replace manual part/face state for full assembly");
        require(workspace.grab().save(output+"/main-color-random.png"),"random UI screenshot");undo->trigger();require(first->colorEdits()==manual,"random undo did not restore both part and face colors");
        redo->trigger();require(first->colorEdits()==randomized,"random redo did not restore captured random result");restore->trigger();
        require(first->colorEdits().empty()&&!first->hasPlacementChanges()&&!restore->isEnabled(),"restore all failed to clear part/face state or dirty flag");
        undo->trigger();require(first->colorEdits()==randomized&&first->hasPlacementChanges(),"restore-all undo failed");undo->trigger();require(first->colorEdits()==manual,"undo history lost manual state");
        require(first->randomizeColors(1789),"seeded random rejected");const auto seeded=first->colorEdits();require(first->randomizeColors(1789)&&first->colorEdits()==seeded,"seed replay changed state");
        undo->trigger();require(first->colorEdits()==manual,"identical seeded random added spurious undo command");report["globalRandomFullScopeReplaceUndoRedoRestoreDirty"]=true;
        restore->trigger();require(first->setFaceColors({{leaf,1}},QColor("#e02842")),"face-only setup");select(first,leaf);
        require(action(first,"restorePartColor")->isEnabled(),"face-only instance cannot restore original part color from UI");action(first,"restorePartColor")->trigger();
        require(first->colorEdits().empty()&&!first->hasPlacementChanges(),"part restore retained face-only override");
        require(first->setPartColors({leaf},QColor("#db253c")),"blank history part setup");const auto partOnly=first->colorEdits();
        mode->setCurrentIndex(1);auto viewport=view->findChild<QWidget*>("cadViewport");require(viewport,"cadViewport");
        const double ratio=viewport->devicePixelRatioF();
        bool picked=false;
        for(int y=8;y<viewport->height()-8&&!picked;y+=13)for(int x=8;x<viewport->width()-8;x+=13){
            view->context()->MoveTo(qRound(x*ratio),qRound(y*ratio),view->view(),false);
            if(!view->context()->HasDetected())continue;
            QTest::mouseMove(viewport,QPoint(x,y));QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,QPoint(x,y));
            if(!view->selectedColorFaces().empty()){picked=true;break;}
        }
        require(picked&&view->selectedColorFaces().size()==1,"main UI real face selection failed");const auto selectedFace=view->selectedColorFaces().front();
        require(first->setFaceColors({selectedFace},QColor("#15c9d9")),"blank history selected-face recolor");const auto beforeBlank=first->colorEdits();
        QPoint blank(-1,-1);
        for(int y=6;y<viewport->height()-6&&blank.x()<0;y+=37)for(int x=6;x<viewport->width()-6;x+=37){
            view->context()->MoveTo(qRound(x*ratio),qRound(y*ratio),view->view(),false);
            if(!view->context()->HasDetected()){blank=QPoint(x,y);break;}
        }
        require(blank.x()>=0,"main UI has no verified background");const bool undoBefore=undo->isEnabled(),redoBefore=redo->isEnabled();
        QTest::mouseMove(viewport,blank);QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,blank);
        require(first->colorEdits()==beforeBlank&&first->hasPlacementChanges()&&view->faceColorPicking(),"blank click changed color/dirty/mode state");
        require(undo->isEnabled()==undoBefore&&redo->isEnabled()==redoBefore,"blank changed color history actions");
        undo->trigger();require(first->colorEdits()==partOnly,"blank click added a color undo command");redo->trigger();require(first->colorEdits()==beforeBlank,"blank changed redo snapshot");
        restore->trigger();require(first->colorEdits().empty()&&!first->hasPlacementChanges(),"restore after blank remains dirty");report["realBlankClickPreservesColorDirtyAndUndoHistory"]=true;
        mode->setCurrentIndex(1);require(mode->currentIndex()==1&&view->faceColorPicking(),"combo selection failed to activate face mode");
        require(action(first,"setPartColor")->text()==QStringLiteral("面颜色…")&&action(first,"restorePartColor")->text()==QStringLiteral("恢复选中面底色"),"face action labels not updated");
        require(!action(first,"setPartColor")->isEnabled(),"face color enabled without selected faces");mode->setCurrentIndex(0);
        require(!view->faceColorPicking()&&action(first,"setPartColor")->text()==QStringLiteral("零件颜色…"),"combo failed to restore part mode");
        mode->setCurrentIndex(1);QTest::keyClick(view->findChild<QWidget*>("cadViewport"),Qt::Key_Escape);require(mode->currentIndex()==0&&!view->faceColorPicking(),"Esc did not synchronize selection combo");
        auto grab=action(first,"togglePartGrab");grab->trigger();require(grab->isChecked()&&view->grabMode(),"grab action did not activate viewer");
        mode->setCurrentIndex(1);require(view->faceColorPicking()&&!view->grabMode()&&!grab->isChecked(),"face mode left grab action or viewer active");
        mode->setCurrentIndex(0);grab->trigger();require(grab->isChecked()&&view->grabMode(),"grab could not be re-enabled after face mode");
        grab->trigger();require(!grab->isChecked()&&!view->grabMode(),"grab could not be disabled");
        report["grabActionSynchronizedWithFaceMode"]=true;report["faceOnlyRestoreAndActualComboSignalLabels"]=true;
        require(first->setPartColors({leaf},QColor("#6c38ab"))&&first->setFaceColors({{leaf,1}},QColor("#e02842")),"document isolation setup");const auto firstState=first->colorEdits();
        const auto secondPath=settings.filePath("second.step");require(QFile::copy(QString::fromLocal8Bit(argv[1]),secondPath),"copy fixture");workspace.openFiles({secondPath});auto second=workspace.currentDocument();require(second&&second!=first,"second document");
        waitFor([&]{return action(second,"randomizeColors")->isEnabled();});require(second->colorEdits().empty()&&!second->hasPlacementChanges(),"new document inherited colors");
        action(second,"randomizeColors")->trigger();const auto secondState=second->colorEdits();require(first->colorEdits()==firstState&&second->hasPlacementChanges(),"second random changed first document");
        auto tabs=workspace.findChild<QTabWidget*>("documentTabs");tabs->setCurrentWidget(first);require(first->colorEdits()==firstState&&first->hasPlacementChanges(),"first state lost on tab switch");
        action(first,"restoreAllColors")->trigger();require(!first->hasPlacementChanges()&&second->colorEdits()==secondState,"first restore changed second document");
        tabs->setCurrentWidget(second);action(second,"undoPartColor")->trigger();require(second->colorEdits().empty()&&!second->hasPlacementChanges(),"second undo not independent");
        report["twoDocumentColorHistoryIsolation"]=true;workspace.closeAll();waitFor([&]{return workspace.documentCount()==0&&workspace.jobQueue()->idle();});report["status"]="passed";
    }catch(const Standard_Failure& e){error=QString::fromUtf8(e.GetMessageString());}catch(const std::exception& e){error=QString::fromUtf8(e.what());}
    if(!error.isEmpty()){report["status"]="failed";report["error"]=error;}QFile f(output+"/main-color-report.json");if(!f.open(QIODevice::WriteOnly))return 2;f.write(QJsonDocument(report).toJson());std::cout<<QJsonDocument(report).toJson().toStdString();return error.isEmpty()?0:1;
}
