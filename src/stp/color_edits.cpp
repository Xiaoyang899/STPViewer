#include "color_edits.h"
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_VisMaterialTool.hxx>
#include <XCAFDoc_VisMaterial.hxx>
#include <Quantity_ColorRGBA.hxx>
#include <stdexcept>
#include <algorithm>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <cmath>
#include <random>
#include <set>
#include <array>
#include <limits>

namespace Stp {
void validateColorEdits(const CadDocument& doc, const ColorEdits& edits) {
    for (const auto& [index,color] : edits)
        if (index < 0 || size_t(index) >= doc.occurrences.size()
            || doc.occurrences[size_t(index)].partIndex < 0 || !color.isValid() || color.alpha() != 255)
            throw std::invalid_argument("零件着色参数无效");
    for(const auto& [key,color]:edits.faces)
        if(colorFace(doc,key).IsNull()||!color.isValid()||color.alpha()!=255)
            throw std::invalid_argument("面着色参数无效");
}
QColor occurrenceBaseColor(const CadDocument& doc, int index) {
    if (doc.xde.IsNull() || index < 0 || size_t(index) >= doc.occurrences.size()) return QColor("#cccccc");
    const auto colors = XCAFDoc_DocumentTool::ColorTool(doc.xde->Main());
    Quantity_ColorRGBA color(Quantity_Color(0.8,0.8,0.8,Quantity_TOC_sRGB));
    const auto apply = [&](const TDF_Label& label) {
        if (label.IsNull()) return;
        const auto mat = XCAFDoc_VisMaterialTool::GetShapeMaterial(label);
        if (!mat.IsNull()) color = mat->BaseColor();
        Quantity_ColorRGBA value;
        if (colors->GetColor(label,XCAFDoc_ColorGen,value)) color=value;
        if (colors->GetColor(label,XCAFDoc_ColorSurf,value)) color=value;
    };
    std::vector<int> chain;
    for (int i=index; i>=0; i=doc.occurrences[size_t(i)].parent) chain.push_back(i);
    for (auto it=chain.rbegin();it!=chain.rend();++it) {
        const auto& o=doc.occurrences[size_t(*it)]; apply(o.definitionLabel); apply(o.label);
    }
    const auto& rgb=color.GetRGB();
    double r,g,b; rgb.Values(r,g,b,Quantity_TOC_sRGB);
    return QColor::fromRgbF(r,g,b,color.Alpha()).convertTo(QColor::Rgb);
}
TopoDS_Face colorFace(const CadDocument& doc,const FaceColorKey& key) {
    if(key.occurrence<0||size_t(key.occurrence)>=doc.occurrences.size()||key.face<1)return {};
    const int p=doc.occurrences[size_t(key.occurrence)].partIndex;
    if(p<0||size_t(p)>=doc.parts.size())return {};
    TopTools_IndexedMapOfShape faces;TopExp::MapShapes(doc.parts[size_t(p)].shape,TopAbs_FACE,faces);
    return key.face<=faces.Extent()?TopoDS::Face(faces(key.face)):TopoDS_Face();
}
int colorFaceIndex(const CadDocument& doc,int occurrence,const TopoDS_Shape& face) {
    if(occurrence<0||size_t(occurrence)>=doc.occurrences.size()||face.IsNull()||face.ShapeType()!=TopAbs_FACE)return 0;
    const int p=doc.occurrences[size_t(occurrence)].partIndex;if(p<0||size_t(p)>=doc.parts.size())return 0;
    TopTools_IndexedMapOfShape faces;TopExp::MapShapes(doc.parts[size_t(p)].shape,TopAbs_FACE,faces);
    return faces.FindIndex(face);
}
QString colorNameGroup(const CadDocument& doc,int occurrence) {
    if(occurrence<0||size_t(occurrence)>=doc.occurrences.size())return {};
    const int p=doc.occurrences[size_t(occurrence)].partIndex;if(p<0||size_t(p)>=doc.parts.size())return {};
    const auto& part=doc.parts[size_t(p)];const auto name=part.name.normalized(QString::NormalizationForm_C).trimmed();
    return name.isEmpty()?QStringLiteral("id:")+part.id:QStringLiteral("name:")+name;
}
namespace {
struct ColorBox { std::array<double,3> low{},high{};int occurrence=-1,group=-1; };
double boxGap(const ColorBox& a,const ColorBox& b) {
    double d=0;for(int k=0;k<3;++k){const double v=std::max({0.,a.low[k]-b.high[k],b.low[k]-a.high[k]});d+=v*v;}return std::sqrt(d);
}
std::array<double,3> labColor(const QColor& c) {
    const auto linear=[](double x){return x<=.04045?x/12.92:std::pow((x+.055)/1.055,2.4);};
    const double r=linear(c.redF()),g=linear(c.greenF()),b=linear(c.blueF());
    const double l=std::cbrt(.4122214708*r+.5363325363*g+.0514459929*b);
    const double m=std::cbrt(.2119034982*r+.6806995451*g+.1073969566*b);
    const double ss=std::cbrt(.0883024619*r+.2817188376*g+.6299787005*b);
    return {{.2104542553*l+.793617785*m-.0040720468*ss,1.9779984951*l-2.428592205*m+.4505937099*ss,.0259040371*l+.7827717662*m-.808675766*ss}};
}
}
ColorEdits randomColorsByName(const CadDocument& doc,const PlacementEdits& placements,quint32 seed,std::atomic_bool* cancel) {
    validatePlacementEdits(doc,placements);
    const auto check=[&]{if(cancel&&cancel->load())throw std::runtime_error("随机着色已取消");};
    std::map<QString,int> names;for(size_t i=0;i<doc.occurrences.size();++i)if(doc.occurrences[i].partIndex>=0)names.emplace(colorNameGroup(doc,int(i)),0);
    int count=0;for(auto& entry:names)entry.second=count++;
    std::vector<ColorBox> boxes;std::vector<double> diagonals;
    std::vector<std::optional<std::array<double,6>>> local(doc.parts.size());
    for(size_t i=0;i<doc.parts.size();++i){check();Bnd_Box b;BRepBndLib::AddOptimal(doc.parts[i].shape,b,false,false);
        if(!b.IsVoid()&&!b.IsOpen()){std::array<double,6> v;b.Get(v[0],v[1],v[2],v[3],v[4],v[5]);local[i]=v;}}
    for(size_t i=0;i<doc.occurrences.size();++i){check();const auto& o=doc.occurrences[i];if(o.partIndex<0||!local[size_t(o.partIndex)])continue;
        const auto v=*local[size_t(o.partIndex)];const auto t=placedWorldTransform(doc,placements,int(i));ColorBox b;b.occurrence=int(i);b.group=names.at(colorNameGroup(doc,int(i)));
        b.low.fill(std::numeric_limits<double>::infinity());b.high.fill(-std::numeric_limits<double>::infinity());
        for(int corner=0;corner<8;++corner){gp_Pnt p(v[(corner&1)?3:0],v[(corner&2)?4:1],v[(corner&4)?5:2]);p.Transform(t);
            for(int k=0;k<3;++k){b.low[k]=std::min(b.low[k],p.Coord(k+1));b.high[k]=std::max(b.high[k],p.Coord(k+1));}}
        double d=0;for(int k=0;k<3;++k)d+=std::pow(b.high[k]-b.low[k],2);diagonals.push_back(std::sqrt(d));boxes.push_back(b);
    }
    double cell=1.;if(!diagonals.empty()){std::sort(diagonals.begin(),diagonals.end());cell=std::max(.1,diagonals[diagonals.size()/2]);}
    const double proximity=std::max(.1,cell*.08);
    using Cell=std::array<long long,3>;std::map<Cell,std::vector<int>> grid;
    std::vector<int> large;std::vector<std::map<int,double>> graph(static_cast<size_t>(count));
    const auto link=[&](const ColorBox& a,const ColorBox& b){if(a.group==b.group)return;const double gap=boxGap(a,b);if(gap>proximity)return;
        const double w=1./(1.+gap/proximity);auto& ab=graph[size_t(a.group)][b.group];ab=std::max(ab,w);auto& ba=graph[size_t(b.group)][a.group];ba=std::max(ba,w);};
    const auto coord=[&](double x){return static_cast<long long>(std::floor(std::clamp(x/cell,-1.e12,1.e12)));};
    for(size_t i=0;i<boxes.size();++i){check();const auto& b=boxes[i];for(int j:large)link(b,boxes[size_t(j)]);
        Cell lo,hi;long double cells=1;for(int k=0;k<3;++k){lo[k]=coord(b.low[k]-proximity);hi[k]=coord(b.high[k]+proximity);cells*=hi[k]-lo[k]+1;}
        if(cells>64){
            // Very large enclosure boxes are compared linearly, with a fixed cap;
            // dense assemblies remain bounded instead of degenerating to all pairs.
            if(large.size()<32){for(size_t j=0;j<i;++j)link(b,boxes[j]);large.push_back(int(i));}continue;
        }
        for(long long x=lo[0];x<=hi[0];++x)for(long long y=lo[1];y<=hi[1];++y)for(long long z=lo[2];z<=hi[2];++z){auto& occupants=grid[Cell{{x,y,z}}];
            for(int j:occupants)link(b,boxes[size_t(j)]);
            if(occupants.size()<64&&std::none_of(occupants.begin(),occupants.end(),[&](int j){return boxes[size_t(j)].group==b.group;}))occupants.push_back(int(i));
        }
    }
    std::mt19937 rng(seed);std::vector<QColor> palette;std::vector<std::array<double,3>> perceptual;
    const double offset=double(rng()%36000)/100.;
    for(int i=0;i<96;++i){const double hue=std::fmod(offset+i*137.507764,360.)/360.;const QColor c=QColor::fromHsvF(hue,.64+.12*(i%3),.72+.12*((i/3)%3)).toRgb();palette.push_back(c);perceptual.push_back(labColor(c));}
    const auto distance=[&](int a,int b){double d=0;for(int k=0;k<3;++k)d+=std::pow(perceptual[size_t(a)][k]-perceptual[size_t(b)][k],2);return std::sqrt(d);};
    std::vector<int> order;std::vector<quint32> tie(static_cast<size_t>(count));for(int i=0;i<count;++i){order.push_back(i);tie[size_t(i)]=rng();}
    std::sort(order.begin(),order.end(),[&](int a,int b){return graph[size_t(a)].size()!=graph[size_t(b)].size()?graph[size_t(a)].size()>graph[size_t(b)].size():tie[size_t(a)]<tie[size_t(b)];});
    std::vector<int> assigned(size_t(count),-1);std::vector<int> used(palette.size());
    for(int group:order){check();double best=-1;int chosen=0;const int start=int(rng()%palette.size());
        for(int p=0;p<int(palette.size());++p){const int candidate=(p+start)%int(palette.size());double closest=1.;bool neighbor=false;
            for(const auto& [other,weight]:graph[size_t(group)])if(assigned[size_t(other)]>=0){neighbor=true;closest=std::min(closest,distance(candidate,assigned[size_t(other)])/weight);}
            const double score=(neighbor?closest:.1)-.0005*used[size_t(candidate)];if(score>best){best=score;chosen=candidate;}}
        assigned[size_t(group)]=chosen;++used[size_t(chosen)];
    }
    ColorEdits result;for(size_t i=0;i<doc.occurrences.size();++i)if(doc.occurrences[i].partIndex>=0)result[int(i)]=palette[size_t(assigned[size_t(names.at(colorNameGroup(doc,int(i))))])];
    return result;
}

}
