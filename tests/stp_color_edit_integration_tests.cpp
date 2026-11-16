#include "viewer_widget.h"
#include "assembly_export.h"
#include "../src/app/qtopengl_utils.h"
#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <TDataStd_Name.hxx>
#include <V3d_TypeOfOrientation.hxx>
#include <QApplication>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void waitFor(const std::function<bool()>& f){QElapsedTimer t;t.start();while(!f()){if(t.elapsed()>45000)throw std::runtime_error("color GUI timeout");QTest::qWait(20);}}
void name(const TDF_Label& l,const char* text){TDataStd_Name::Set(l,TCollection_ExtendedString(text));}
gp_Trsf shift(double x,double y=0){gp_Trsf t;t.SetTranslation(gp_Vec(x,y,0));return t;}
std::shared_ptr<Stp::CadDocument> fixture(){auto d=std::make_shared<Stp::CadDocument>();d->sourcePath="color-fixture.step";d->xde=new TDocStd_Document("BinXCAF");
    XCAFDoc_DocumentTool::Set(d->xde->Main());XCAFDoc_DocumentTool::SetLengthUnit(d->xde,.001);auto s=XCAFDoc_DocumentTool::ShapeTool(d->xde->Main());auto c=XCAFDoc_DocumentTool::ColorTool(d->xde->Main());
    auto part=s->AddShape(BRepPrimAPI_MakeBox(20,20,20).Shape(),false);name(part,"Shared");c->SetColor(part,Quantity_Color(.1,.25,.9,Quantity_TOC_sRGB),XCAFDoc_ColorGen);
    auto different=s->AddShape(BRepPrimAPI_MakeBox(20,19,20).Shape(),false);name(different,"Other");
    const auto assembly=[&](const char* text){TopoDS_Compound x;BRep_Builder().MakeCompound(x);auto l=s->AddShape(x,false);name(l,text);return l;};
    auto sub=assembly("Sub");name(s->AddComponent(sub,part,TopLoc_Location(shift(4,7))),"Leaf");auto root=assembly("Root");
    name(s->AddComponent(root,sub,TopLoc_Location(shift(0))),"First");name(s->AddComponent(root,sub,TopLoc_Location(shift(50))),"Second");
    name(s->AddComponent(root,different,TopLoc_Location(shift(100,7))),"OtherLeaf");s->UpdateAssemblies();Stp::rebuildIndex(*d);return d;
}
QColor pixel(Stp::ViewerWidget& view,const QImage& image,gp_Pnt p){int x,y;view.view()->Convert(p.X(),p.Y(),p.Z(),x,y);require(image.rect().contains(x,y),"pixel off screen");return image.pixelColor(x,y);}
int difference(QColor a,QColor b){return std::abs(a.red()-b.red())+std::abs(a.green()-b.green())+std::abs(a.blue()-b.blue());}
}
int main(int argc,char** argv){Mayo::QtOpenGlUtils::platformSetup(argc,argv);QApplication app(argc,argv);QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());app.setOrganizationName("STPColorTest");app.setApplicationName("STPColorTest");
    const QString output=argc>1?QString::fromLocal8Bit(argv[1]):settings.filePath("evidence");QDir().mkpath(output);QJsonObject report;QString error;
    try{
        qputenv("CSF_ShadersDirectory",QByteArray(STP_OCCT_RESOURCE_DIR)+"/Shaders");auto doc=fixture();std::vector<int> leaves;
        for(size_t i=0;i<doc->occurrences.size();++i)if(doc->occurrences[i].partIndex>=0)leaves.push_back(int(i));require(leaves.size()==3,"fixture leaves");
        std::vector<bool> grabEvents;
        Stp::ViewerWidget view;view.resize(1150,760);view.show();bool ready=false;QObject::connect(&view,&Stp::ViewerWidget::sceneReady,[&]{ready=true;});
        std::atomic_bool cancel(false);Stp::prepareDisplayMeshes(*doc,1,cancel);view.setDocument(doc,{},true);waitFor([&]{return ready;});
        Stp::PlacementEdits placements{{leaves[0],shift(5)}};view.setPlacementEdits(placements);view.setBackground("white");view.view()->SetProj(V3d_Zpos);view.fitAll();QTest::qWait(150);
        const auto point=[&](int o){return gp_Pnt(10,10,20).Transformed(view.measurementPartEntity(o).displayTransform);};
        require(view.saveScreenshot(output+"/original-colors.png"),"baseline screenshot");
        const auto random=Stp::randomColorsByName(*doc,placements,9483);view.setColorEdits(random);QTest::qWait(100);require(view.saveScreenshot(output+"/random-colors.png"),"random screenshot");QImage image(output+"/random-colors.png");
        const auto first=pixel(view,image,point(leaves[0])),sibling=pixel(view,image,point(leaves[1])),other=pixel(view,image,point(leaves[2]));
        require(difference(first,sibling)<12,"same-name instances differ in framebuffer");require(difference(first,other)>35,"different-name groups indistinct in framebuffer");report["randomNameGroupingPixels"]=true;
        auto viewport=view.findChild<QWidget*>("cadViewport");require(viewport,"viewport");view.setFaceColorPicking(true);
        const double ratio=viewport->devicePixelRatioF();
        const auto clickFace=[&](int occurrence,Qt::KeyboardModifiers modifiers=Qt::NoModifier){
            int x,y;const auto world=point(occurrence);view.view()->Convert(world.X(),world.Y(),world.Z(),x,y);
            const QPoint pos(qRound(x/ratio),qRound(y/ratio));QTest::mouseMove(viewport,pos);QTest::mouseClick(viewport,Qt::LeftButton,modifiers,pos);
        };
        const auto clickBlank=[&](Qt::KeyboardModifiers modifiers=Qt::NoModifier){
            // Locate actual background in this camera, rather than assuming a
            // fixed corner is empty. No artificial ClearDetected() is involved.
            QPoint blank(-1,-1);
            for(int y=6;y<viewport->height()-6&&blank.x()<0;y+=37)
                for(int x=6;x<viewport->width()-6;x+=37){
                    view.context()->MoveTo(qRound(x*ratio),qRound(y*ratio),view.view(),false);
                    if(!view.context()->HasDetected()){blank=QPoint(x,y);break;}
                }
            require(blank.x()>=0,"no verified background point");
            QTest::mouseMove(viewport,blank);QTest::mouseClick(viewport,Qt::LeftButton,modifiers,blank);
        };
        clickBlank();require(view.faceColorPicking()&&view.selectedColorFaces().empty(),"blank before first selection");
        clickFace(leaves[0]);require(view.selectedColorFaces().size()==1,"face click not selected");
        const auto target=view.selectedColorFaces().front();require(target.occurrence==leaves[0]&&target.face>0&&!Stp::colorFace(*doc,target).IsNull(),"face click lost canonical instance");
        clickBlank();require(view.selectedColorFaces().empty(),"plain blank did not clear selected face");
        clickFace(leaves[0]);require(view.selectedColorFaces()==Stp::ColorFaceSelection{target},"reselect after blank failed");
        clickFace(leaves[1],Qt::ControlModifier);require(view.selectedColorFaces().size()==2,"Ctrl face multiselect failed");
        const auto multi=view.selectedColorFaces();clickBlank(Qt::ControlModifier);require(view.selectedColorFaces()==multi,"Ctrl blank cleared multiselection");
        clickBlank(Qt::MetaModifier);require(view.selectedColorFaces()==multi,"Meta blank cleared multiselection");
        clickFace(leaves[0]);require(view.selectedColorFaces()==Stp::ColorFaceSelection{target},"plain face did not replace multiselection");
        report["blankBeforeAfterSelectionAndModifierMultiSelection"]=true;
        auto state=random;state[leaves[0]]=QColor("#239547");state.faces[target]=QColor("#e52b24");view.setColorEdits(state);QTest::qWait(100);
        clickBlank();require(view.selectedColorFaces().empty(),"blank after recolor did not clear highlight");
        require(view.saveScreenshot(output+"/face-color.png"),"face screenshot");image.load(output+"/face-color.png");auto red=pixel(view,image,point(leaves[0]));
        require(red.red()>red.green()*1.7&&red.red()>red.blue()*1.7,"blank click changed face color or face override failed");
        require(difference(pixel(view,image,point(leaves[1])),sibling)<12,"face color leaked to repeated instance");
        clickFace(leaves[0]);require(view.selectedColorFaces()==Stp::ColorFaceSelection{target},"reselect after recolor failed");
        report["canonicalInstanceFacePickingAndPixels"]=true;report["blankAfterRecolorPreservesColorAndReselects"]=true;
        bool remeshed=false;QObject::connect(&view,&Stp::ViewerWidget::meshQualityFinished,[&](int){remeshed=true;});require(view.setMeshQuality(0),"remesh refused");waitFor([&]{return remeshed;});
        require(view.selectedColorFaces()==Stp::ColorFaceSelection{target},"remesh lost selected face identity");
        clickBlank();require(view.selectedColorFaces().empty(),"blank after remesh did not clear face");
        require(view.saveScreenshot(output+"/face-color-remeshed.png"),"remesh screenshot");image.load(output+"/face-color-remeshed.png");red=pixel(view,image,point(leaves[0]));require(red.red()>red.green()*1.7,"remesh or blank click lost face style");
        clickFace(leaves[0]);require(view.selectedColorFaces()==Stp::ColorFaceSelection{target},"reselect after remesh failed");
        report["remeshPreservesFaceColorAndSelection"]=true;report["blankAfterRebuildSafe"]=true;
        view.setMeasurementPicking(true,Stp::MeasurementSelection::Face);require(!view.faceColorPicking(),"measurement/face modes overlap");view.setFaceColorPicking(true);require(!view.measurementPicking(),"face mode kept measurement");
        view.beginSectionPlanePick();require(!view.faceColorPicking(),"section/face modes overlap");view.setFaceColorPicking(true);require(!view.sectionPicking(),"face kept section picking");
        view.setGrabMode(true);require(!view.faceColorPicking(),"grab/face modes overlap");view.setFaceColorPicking(true);require(!view.grabMode(),"face kept grab");QTest::keyClick(viewport,Qt::Key_Escape);require(!view.faceColorPicking(),"Esc kept face picking");
        view.setColorEdits({});view.setFaceColorPicking(true);clickFace(leaves[0]);clickBlank();require(view.selectedColorFaces().empty(),"blank after restore failed");QTest::qWait(100);require(view.saveScreenshot(output+"/restored-colors.png"),"restore screenshot");image.load(output+"/restored-colors.png");
        const QImage original(output+"/original-colors.png");require(difference(pixel(view,image,point(leaves[0])),pixel(view,original,point(leaves[0])))<12,"restore all lost source color");report["mutualExclusionEscapeRestore"]=true;
        Stp::exportPlacedAssembly(*doc,placements,output+"/face-colors.step",{},cancel,state);
        Handle(AIS_Shape) helper=new AIS_Shape(BRepPrimAPI_MakeBox(gp_Pnt(4,60,0),10,10,10).Shape());
        view.context()->Display(helper,false);view.context()->Deactivate(helper);view.context()->Activate(helper,AIS_Shape::SelectionMode(TopAbs_FACE),false);view.fitAll();QTest::qWait(50);
        clickFace(leaves[0]);const auto beforeNonPart=view.selectedColorFaces();require(beforeNonPart.size()==1,"nonpart selection setup");
        int hx,hy;view.view()->Convert(9,65,10,hx,hy);const QPoint helperPoint(qRound(hx/ratio),qRound(hy/ratio));
        view.context()->MoveTo(hx,hy,view.view(),false);require(view.context()->HasDetected()&&view.context()->DetectedInteractive()==helper,"helper face not detected");
        QTest::mouseMove(viewport,helperPoint);QTest::mouseClick(viewport,Qt::LeftButton,Qt::ControlModifier,helperPoint);require(view.selectedColorFaces()==beforeNonPart,"Ctrl nonpart click cleared selection");
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,helperPoint);require(view.selectedColorFaces().empty(),"unknown presentation face was accepted");
        view.context()->Deactivate(helper);view.context()->Activate(helper,0,false);clickFace(leaves[0]);
        QTest::mouseMove(viewport,helperPoint);QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,helperPoint);require(view.selectedColorFaces().empty(),"whole-solid owner was accepted as a face");
        view.context()->Remove(helper,false);report["nonPartAndNonFaceOwnersSafe"]=true;
        QObject::connect(&view,&Stp::ViewerWidget::grabModeChanged,[&](bool active){grabEvents.push_back(active);});
        view.setGrabMode(true);view.setGrabMode(true);require(grabEvents==std::vector<bool>{true},"grab signal emitted for unchanged state");
        view.setFaceColorPicking(true);require(grabEvents==std::vector<bool>({true,false}),"face mode did not emit actual grab disable");
        view.setGrabMode(false);require(grabEvents.size()==2,"unchanged grab disable emitted");
        view.setGrabMode(true);view.clearDocument();require(!view.grabMode()&&!view.faceColorPicking()&&grabEvents==std::vector<bool>({true,false,true,false}),"clear document left grab state or wrong signal count");
        ready=false;view.setDocument(doc,{},true);waitFor([&]{return ready;});view.setGrabMode(true);view.requestSessionClose();
        require(!view.grabMode()&&!view.faceColorPicking()&&grabEvents==std::vector<bool>({true,false,true,false,true,false}),"session close left grab active");
        report["grabModeSignalActualChangesAndLifecycle"]=true;report["blankAfterRestoreSafe"]=true;report["status"]="passed";
    }catch(const Standard_Failure& failure){error=QString::fromUtf8(failure.GetMessageString());}catch(const std::exception& failure){error=QString::fromUtf8(failure.what());}
    if(!error.isEmpty()){report["status"]="failed";report["error"]=error;}QFile file(output+"/color-edit-report.json");if(!file.open(QIODevice::WriteOnly))return 2;file.write(QJsonDocument(report).toJson());std::cout<<QJsonDocument(report).toJson().toStdString();return error.isEmpty()?0:1;
}
