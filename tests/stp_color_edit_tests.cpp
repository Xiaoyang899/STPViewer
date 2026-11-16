#include "assembly_export.h"
#include "color_edits.h"
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <TDataStd_Name.hxx>
#include <Quantity_Color.hxx>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <iostream>
#include <stdexcept>
#include <cmath>
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void name(const TDF_Label& l,const QString& n){TDataStd_Name::Set(l,TCollection_ExtendedString(reinterpret_cast<const Standard_ExtCharacter*>(n.utf16())));}
gp_Trsf shift(double x){gp_Trsf t;t.SetTranslation(gp_Vec(x,0,0));return t;}
bool colorsClose(QColor a,QColor b){return std::abs(a.red()-b.red())<=1&&std::abs(a.green()-b.green())<=1&&std::abs(a.blue()-b.blue())<=1;}
Stp::CadDocument fixture(){
    Stp::CadDocument d;d.sourcePath="source.step";d.xde=new TDocStd_Document("BinXCAF");XCAFDoc_DocumentTool::Set(d.xde->Main());
    XCAFDoc_DocumentTool::SetLengthUnit(d.xde,.001);auto shapes=XCAFDoc_DocumentTool::ShapeTool(d.xde->Main());auto colors=XCAFDoc_DocumentTool::ColorTool(d.xde->Main());
    const auto p=shapes->AddShape(BRepPrimAPI_MakeBox(8,8,8).Shape(),false);name(p,QString::fromUtf8("Café"));
    colors->SetColor(p,Quantity_Color(.1,.2,.85,Quantity_TOC_sRGB),XCAFDoc_ColorGen);
    TopTools_IndexedMapOfShape faces;TopExp::MapShapes(shapes->GetShape(p),TopAbs_FACE,faces);
    colors->SetColor(shapes->AddSubShape(p,faces(2)),Quantity_Color(0.,.8,0.,Quantity_TOC_sRGB),XCAFDoc_ColorSurf);
    const auto same=shapes->AddShape(BRepPrimAPI_MakeBox(8,7,8).Shape(),false);name(same,QString::fromUtf8(" Café "));
    const auto other=shapes->AddShape(BRepPrimAPI_MakeBox(8,8,7).Shape(),false);name(other,"Housing");
    TopoDS_Compound compound;BRep_Builder().MakeCompound(compound);const auto root=shapes->AddShape(compound,false);name(root,"Root");
    for(int i=0;i<3;++i)name(shapes->AddComponent(root,p,TopLoc_Location(shift(i*8))),QString(QChar('A'+i)));
    name(shapes->AddComponent(root,same,TopLoc_Location(shift(24))),"D");name(shapes->AddComponent(root,other,TopLoc_Location(shift(32))),"E");
    shapes->UpdateAssemblies();Stp::rebuildIndex(d);return d;
}
int leaf(const Stp::CadDocument& d,const QString& suffix){for(size_t i=0;i<d.occurrences.size();++i)if(d.occurrences[i].partIndex>=0&&d.occurrences[i].path.endsWith("/"+suffix))return int(i);throw std::runtime_error("missing leaf");}
int faceColors(const Stp::CadDocument& d,int o,const QColor& color){
    auto ct=XCAFDoc_DocumentTool::ColorTool(d.xde->Main());auto shapes=XCAFDoc_DocumentTool::ShapeTool(d.xde->Main());
    TDF_LabelSequence subs;shapes->GetSubShapes(d.parts[size_t(d.occurrences[size_t(o)].partIndex)].label,subs);int count=0;
    for(int i=1;i<=subs.Length();++i){if(shapes->GetShape(subs(i)).ShapeType()!=TopAbs_FACE)continue;Quantity_Color q;if(!ct->GetColor(subs(i),XCAFDoc_ColorSurf,q))continue;double r,g,b;q.Values(r,g,b,Quantity_TOC_sRGB);if(colorsClose(QColor::fromRgbF(r,g,b),color))++count;}return count;
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
    auto source=fixture();const int a=leaf(source,"A"),b=leaf(source,"B"),c=leaf(source,"C"),d=leaf(source,"D"),e=leaf(source,"E");
    const auto originalShape=source.parts[size_t(source.occurrences[size_t(a)].partIndex)].shape;
    const auto random=Stp::randomColorsByName(source,{},34789);require(random==Stp::randomColorsByName(source,{},34789),"random seed replay changed");
    require(random!=Stp::randomColorsByName(source,{},34790),"new random seed unchanged");require(random.size()==5&&random.faces.empty(),"random did not replace entire leaf state");
    require(random.at(a)==random.at(b)&&random.at(a)==random.at(c)&&random.at(a)==random.at(d),"definition name NFC grouping or same-name distinct definitions failed");
    require(random.at(a)!=random.at(e),"adjacent different-name groups share a color");
    auto moved=Stp::randomColorsByName(source,{{e,shift(1000)}},34789);require(moved.size()==5,"applied-placement random scope changed");
    const QColor red("#e52235"),cyan("#15c9d9"),green("#00cc00");Stp::ColorEdits state{{a,red},{b,red}};
    state.faces[{a,1}]=cyan;state.faces[{b,1}]=cyan;Stp::validateColorEdits(source,state);
    require(Stp::colorFaceIndex(source,a,Stp::colorFace(source,{a,1}))==1,"canonical face identity not roundtrippable");
    auto faceOnly=state;static_cast<Stp::ColorEdits::Base&>(faceOnly).clear();require(!faceOnly.empty()&&faceOnly!=Stp::ColorEdits{},"face state absent from empty/equality");
    auto cleared=state;cleared.clear();require(cleared.empty()&&cleared.faces.empty(),"clear retained face colors");
    std::atomic_bool cancel(false);QTemporaryDir temp;const auto path=temp.filePath(QString::fromUtf8("逐面 着色.step"));
    Stp::exportPlacedAssembly(source,{{a,shift(3)}},path,{},cancel,state);auto read=Stp::loadStep(path,{},cancel);
    const int aa=leaf(*read,"A"),bb=leaf(*read,"B"),cc=leaf(*read,"C");
    require(read->occurrences[size_t(aa)].partIndex==read->occurrences[size_t(bb)].partIndex,"identical per-instance face styles were not deduplicated");
    require(read->occurrences[size_t(aa)].partIndex!=read->occurrences[size_t(cc)].partIndex,"face colors leaked into original sibling definition");
    require(faceColors(*read,aa,cyan)==1&&faceColors(*read,bb,cyan)==1,"STEP face override missing");
    require(faceColors(*read,aa,green)==0&&faceColors(*read,cc,green)==1,"uniform face precedence or original sibling face color changed");
    require(colorsClose(Stp::occurrenceBaseColor(*read,aa),red),"instance main color missing");
    size_t leaves=0;for(const auto& o:read->occurrences)if(o.partIndex>=0)++leaves;require(leaves==5&&read->parts.size()==4,"extra free parts or lost instances");
    Stp::ColorEdits faces;faces.faces[{c,1}]=cyan;Stp::exportPlacedAssembly(source,{},path,{},cancel,faces);read=Stp::loadStep(path,{},cancel);
    require(faceColors(*read,leaf(*read,"C"),cyan)==1&&faceColors(*read,leaf(*read,"C"),green)==1,"face-only edit discarded other original face colors");
    require(faceColors(*read,leaf(*read,"A"),cyan)==0,"face-only edit changed another occurrence");
    Stp::exportPlacedAssembly(source,{},path,{},cancel,random);read=Stp::loadStep(path,{},cancel);
    require(read->parts.size()==source.parts.size(),"whole random export duplicated identical instance style definitions");
    require(source.parts[size_t(source.occurrences[size_t(a)].partIndex)].shape.IsSame(originalShape)&&faceColors(source,a,cyan)==0&&faceColors(source,a,green)==1,"source geometry or styles mutated");
    bool rejected=false;try{auto bad=faces;bad.faces[{a,10000}]=red;Stp::validateColorEdits(source,bad);}catch(const std::invalid_argument&){rejected=true;}require(rejected,"invalid face key accepted");
    QFile file(path);require(file.open(QIODevice::ReadOnly),"read saved file");const auto bytes=file.readAll();file.close();cancel=true;
    rejected=false;try{Stp::exportPlacedAssembly(source,{},path,{},cancel,state);}catch(const std::exception&){rejected=true;}cancel=false;file.open(QIODevice::ReadOnly);require(rejected&&file.readAll()==bytes,"cancel damaged output");
    std::cout<<"PASS: name grouping/seed replay, neighboring contrast, instance-specific canonical faces, style precedence, face-only/source preservation, STEP roundtrip/style dedup, cancellation\n";return 0;
}catch(const Standard_Failure& x){std::cerr<<x.GetMessageString()<<'\n';return 1;}catch(const std::exception& x){std::cerr<<x.what()<<'\n';return 1;}}
