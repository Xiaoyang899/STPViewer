#include "bom_export.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QtEndian>
#include <zlib.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
QByteArray readFile(const QString& path) {
    QFile file(path);require(file.open(QIODevice::ReadOnly),"cannot read test output");return file.readAll();
}
// Inspect real compressed XLSX XML, not a mirrored intermediate export model.
QByteArray entry(const QString& path,const QByteArray& name) {
    const auto bytes=readFile(path);
    const auto word=[&](qsizetype at) {require(at>=0&&at+2<=bytes.size(),"ZIP word bounds");return qFromLittleEndian<quint16>(bytes.constData()+at);};
    const auto dword=[&](qsizetype at) {require(at>=0&&at+4<=bytes.size(),"ZIP dword bounds");return qFromLittleEndian<quint32>(bytes.constData()+at);};
    const auto end=bytes.lastIndexOf("PK\005\006");require(end>=0,"ZIP end missing");
    qsizetype at=dword(end+16);
    for(int i=0;i<word(end+10);++i) {
        require(bytes.mid(at,4)==QByteArray("PK\001\002",4),"invalid ZIP central entry");
        const auto names=word(at+28),extras=word(at+30),comments=word(at+32);
        if(bytes.mid(at+46,names)==name) {
            const auto method=word(at+10);const auto packed=dword(at+20),size=dword(at+24),crc=dword(at+16);
            const qsizetype local=dword(at+42);
            require(bytes.mid(local,4)==QByteArray("PK\003\004",4),"invalid ZIP local entry");
            const qsizetype start=local+30+word(local+26)+word(local+28);
            require(start>=0&&start+packed<=bytes.size()&&size<16*1024*1024,"invalid ZIP entry size");
            QByteArray result;
            if(method==0) result=bytes.mid(start,packed);
            else {
                require(method==8,"unsupported test ZIP compression");result.resize(size);
                z_stream stream {};
                stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(bytes.constData()+start));stream.avail_in=packed;
                stream.next_out=reinterpret_cast<Bytef*>(result.data());stream.avail_out=size;
                require(inflateInit2(&stream,-MAX_WBITS)==Z_OK,"ZIP inflate init");
                const auto status=inflate(&stream,Z_FINISH);const bool valid=status==Z_STREAM_END&&stream.total_out==size;
                inflateEnd(&stream);require(valid,"ZIP inflate");
            }
            require(result.size()==size&&crc32(0,reinterpret_cast<const Bytef*>(result.constData()),size)==crc,"ZIP CRC mismatch");return result;
        }
        at+=46+names+extras+comments;
    }
    throw std::runtime_error("XLSX entry missing");
}
QStringList sharedStrings(const QString& path) {
    QStringList result;QXmlStreamReader xml(entry(path,"xl/sharedStrings.xml"));
    while(!xml.atEnd()) {
        xml.readNext();
        if(xml.isStartElement()&&xml.name()==QStringLiteral("si")) {
            QString text;
            while(!xml.atEnd()) {xml.readNext();if(xml.isEndElement()&&xml.name()==QStringLiteral("si"))break;
                if(xml.isStartElement()&&xml.name()==QStringLiteral("t"))text+=xml.readElementText();}
            result<<text;
        }
    }
    require(!xml.hasError(),"invalid shared string XML");return result;
}
struct Row {int level=0;bool hidden=false,collapsed=false;};
struct Cell {QString value;int style=0;bool formula=false;};
struct Sheet {
    QMap<int,Row> rows;
    QMap<QString,Cell> cells;
    int freezeRows=0,freezeColumns=0,maxLevel=0;
    bool summariesAbove=false,selected=false,filter=false;
};
Sheet sheet(const QString& path,int number) {
    Sheet result;const auto strings=sharedStrings(path);
    QXmlStreamReader xml(entry(path,"xl/worksheets/sheet"+QByteArray::number(number)+".xml"));
    while(!xml.atEnd()) {
        xml.readNext();if(!xml.isStartElement())continue;const auto attrs=xml.attributes();
        if(xml.name()==QStringLiteral("row"))result.rows.insert(attrs.value("r").toInt(),
            {attrs.value("outlineLevel").toInt(),attrs.value("hidden")==QLatin1String("1"),attrs.value("collapsed")==QLatin1String("1")});
        else if(xml.name()==QStringLiteral("outlinePr")) result.summariesAbove=attrs.value("summaryBelow")==QLatin1String("0");
        else if(xml.name()==QStringLiteral("sheetFormatPr"))result.maxLevel=attrs.value("outlineLevelRow").toInt();
        else if(xml.name()==QStringLiteral("sheetView"))result.selected=attrs.value("tabSelected")==QLatin1String("1");
        else if(xml.name()==QStringLiteral("autoFilter"))result.filter=true;
        else if(xml.name()==QStringLiteral("pane")) {
            require(attrs.value("state")==QLatin1String("frozen"),"pane must be frozen");
            result.freezeColumns=attrs.value("xSplit").toInt();result.freezeRows=attrs.value("ySplit").toInt();
        } else if(xml.name()==QStringLiteral("c")) {
            const QString ref=attrs.value("r").toString();const bool shared=attrs.value("t")==QLatin1String("s");
            Cell cell;cell.style=attrs.value("s").toInt();
            while(!xml.atEnd()) {xml.readNext();if(xml.isEndElement()&&xml.name()==QStringLiteral("c"))break;
                if(xml.isStartElement()&&xml.name()==QStringLiteral("f"))cell.formula=true;
                if(xml.isStartElement()&&xml.name()==QStringLiteral("v"))cell.value=xml.readElementText();}
            if(shared) {bool ok=false;const int index=cell.value.toInt(&ok);require(ok&&index>=0&&index<strings.size(),"invalid string index");cell.value=strings[index];}
            result.cells.insert(ref,cell);
        }
    }
    require(!xml.hasError(),"invalid worksheet XML");return result;
}
QVector<int> indents(const QString& path) {
    QVector<int> result;QXmlStreamReader xml(entry(path,"xl/styles.xml"));bool inCellFormats=false;
    while(!xml.atEnd()) {
        xml.readNext();
        if(xml.isStartElement()&&xml.name()==QStringLiteral("cellXfs"))inCellFormats=true;
        else if(xml.isEndElement()&&xml.name()==QStringLiteral("cellXfs"))inCellFormats=false;
        else if(inCellFormats&&xml.isStartElement()&&xml.name()==QStringLiteral("xf"))result.push_back(0);
        else if(inCellFormats&&xml.isStartElement()&&xml.name()==QStringLiteral("alignment"))result.back()=xml.attributes().value("indent").toInt();
    }
    require(!xml.hasError(),"invalid styles XML");return result;
}
QString cell(const Sheet& sheet,const QString& column,int row) {return sheet.cells.value(column+QString::number(row)).value;}
int add(Stp::CadDocument& doc,int parent,const QString& name,int part=-1) {
    Stp::Occurrence occurrence;const int index=int(doc.occurrences.size());
    occurrence.id=QStringLiteral("instance-%1").arg(index);occurrence.parent=parent;occurrence.partIndex=part;occurrence.name=name;
    occurrence.path=parent<0?name:doc.occurrences[size_t(parent)].path+QLatin1Char('/')+name;
    if(part>=0) occurrence.definitionLabel=doc.parts[size_t(part)].label;
    doc.occurrences.push_back(occurrence);if(parent>=0)doc.occurrences[size_t(parent)].children.push_back(index);return index;
}
Stp::CadDocument fixture() {
    Stp::CadDocument doc;doc.xde=new TDocStd_Document("BinXCAF");doc.sourcePath=QStringLiteral("测试装配.step");doc.sourceUnits="millimetre";
    for(int i=0;i<2;++i) {Stp::PartDefinition part;part.id=QStringLiteral("definition-%1").arg(i);part.name=QStringLiteral("同名零件");
        part.label=doc.xde->Main().FindChild(i+1,true);doc.parts.push_back(part);}
    add(doc,-1,QStringLiteral("总装 A"));                   // 0
    add(doc,0,QStringLiteral("重复子装配"));               // 1
    add(doc,1,QStringLiteral("同名零件"),0);               // 2
    add(doc,1,QStringLiteral("同名零件"),1);               // 3
    add(doc,0,QStringLiteral("重复子装配"));               // 4
    add(doc,4,QStringLiteral("同名零件"),0);               // 5
    add(doc,0,QStringLiteral("=HYPERLINK(\"https://example.invalid\")"),0); // 6
    add(doc,-1,QStringLiteral("总装 B"));                   // 7
    add(doc,7,QStringLiteral("同名零件"),1);               // 8
    add(doc,-1,QStringLiteral("独立零件"),0);              // 9
    return doc;
}
void checkTree(const QString& path,const Stp::CadDocument& doc) {
    const auto tree=sheet(path,2),summary=sheet(path,1),positions=sheet(path,3);
    require(tree.rows.size()==int(doc.occurrences.size())+1,"tree omitted or duplicated instances");
    require(cell(tree,"A",1)==QStringLiteral("层级序号")&&cell(tree,"B",1)==QStringLiteral("名称")
        &&cell(tree,"C",1)==QStringLiteral("类型")&&cell(tree,"D",1)==QStringLiteral("本行数量"),"tree-first column headings");
    require(tree.freezeRows==1&&tree.freezeColumns==2,"tree header and names must remain visible");
    require(tree.summariesAbove&&tree.selected,"tree must open active with parent outline above children");
    require(!tree.filter&&summary.filter&&positions.filter,"tree order must not be exposed to table sorting/filtering");
    QXmlStreamReader workbook(entry(path,"xl/workbook.xml"));bool active=false;int sheets=0;
    while(!workbook.atEnd()) {workbook.readNext();if(!workbook.isStartElement())continue;
        if(workbook.name()==QStringLiteral("workbookView"))active=workbook.attributes().value("activeTab")==QLatin1String("1");
        if(workbook.name()==QStringLiteral("sheet"))++sheets;}
    require(active&&sheets==4&&!workbook.hasError(),"preserve four sheets and activate hierarchy");
    const QStringList numbers={"1","1.1","1.1.1","1.1.2","1.2","1.2.1","1.3","2","2.1","3"};
    const auto formats=indents(path);
    for(int i=0;i<int(doc.occurrences.size());++i) {
        const auto& occurrence=doc.occurrences[size_t(i)];int depth=0,parent=occurrence.parent;
        while(parent>=0){++depth;parent=doc.occurrences[size_t(parent)].parent;}
        const int row=i+2;
        require(cell(tree,"A",row)==numbers[i]&&cell(tree,"B",row)==occurrence.name,"preorder numbers/names mismatch");
        require(cell(tree,"D",row)=="1"&&cell(tree,"E",row)==occurrence.id,"one row must mean one distinct instance");
        require(cell(tree,"F",row)==(occurrence.parent<0?QString():doc.occurrences[size_t(occurrence.parent)].id),"parent ID lost");
        require(cell(tree,"G",row)==QString::number(depth)&&cell(tree,"H",row)==Stp::labelEntry(occurrence.definitionLabel)
            &&cell(tree,"I",row)==occurrence.path,"source depth, definition or path lost");
        const auto state=tree.rows.value(row);require(state.level==depth&&state.hidden==(depth>1)
            &&state.collapsed==(depth==1&&!occurrence.children.empty()),"native outline state does not match tree");
        const auto name=tree.cells.value("B"+QString::number(row));require(formats.at(name.style)==depth&&!name.formula,"name indentation/formula safety");
    }
    require(cell(summary,"E",2)=="4"&&cell(summary,"E",3)=="2","summary quantities changed or same names merged");
    require(positions.rows.size()==7,"positions must include six leaf instances");
}
void checkScope(const QString& path,const Stp::CadDocument& doc) {
    const auto scope=Stp::scopedBomDocument(doc,{5,5,8});Stp::exportBom(scope,path);
    const auto tree=sheet(path,2),summary=sheet(path,1);const QStringList ids={"instance-0","instance-4","instance-5","instance-7","instance-8"};
    const QStringList numbers={"1","1.1","1.1.1","2","2.1"};
    require(tree.rows.size()==6,"scope must retain only selected leaves and ancestors");
    for(int i=0;i<ids.size();++i)require(cell(tree,"E",i+2)==ids[i]&&cell(tree,"A",i+2)==numbers[i],"scope tree order or instances changed");
    require(cell(summary,"E",2)=="1"&&cell(summary,"E",3)=="1","selected duplicated input must not duplicate quantities");
    require(Stp::partQuantities(doc)==std::vector<int>({4,2}),"export scope mutated source quantities");
}
void checkDeep(const QString& path) {
    Stp::CadDocument doc;doc.parts.resize(1);doc.parts[0].name=QStringLiteral("底层零件");doc.parts[0].id="deep-part";
    int parent=-1;for(int depth=0;depth<=18;++depth)parent=add(doc,parent,QStringLiteral("节点 %1").arg(depth),depth==18?0:-1);
    Stp::exportBom(doc,path);const auto tree=sheet(path,2);const auto styles=indents(path);
    require(tree.rows.size()==20&&tree.maxLevel==7,"deep nodes must remain while Excel outline levels cap at seven");
    for(int depth=0;depth<=18;++depth) {
        const int row=depth+2;const auto state=tree.rows.value(row);const auto name=tree.cells.value("B"+QString::number(row));
        require(state.level==std::min(depth,7)&&state.hidden==(depth>1),"deep native outline bounds");
        require(styles.at(name.style)==std::min(depth,15),"deep name indent bounds");
        require(cell(tree,"G",row)==QString::number(depth)&&cell(tree,"E",row)==QStringLiteral("instance-%1").arg(depth),"deep node identity/actual depth lost");
    }
    require(cell(tree,"A",20).count('.')==18,"full hierarchical number lost after outline cap");
    require(sharedStrings(path).join('\n').contains(QStringLiteral("Excel 原生大纲最多支持 7 个嵌套分组级别")),"deep outline limit must be explained in workbook");
}
void checkPreorder(const QString& path) {
    Stp::CadDocument doc;doc.parts.resize(1);doc.parts[0].name="Part";
    add(doc,-1,"Root A");add(doc,-1,"Root B");add(doc,0,"A Child",0);add(doc,1,"B Child",0);
    Stp::exportBom(doc,path);const auto tree=sheet(path,2);
    require(cell(tree,"E",2)=="instance-0"&&cell(tree,"E",3)=="instance-2"
        &&cell(tree,"E",4)=="instance-1"&&cell(tree,"E",5)=="instance-3","tree groups must keep descendants contiguous");
}
void checkAtomic(const QString& path,const Stp::CadDocument& doc) {
    Stp::exportBom(doc,path);const auto original=readFile(path);std::atomic_bool cancel(true);bool rejected=false;
    try{Stp::exportBom(doc,path,&cancel);}catch(const std::exception&){rejected=true;}
    require(rejected&&readFile(path)==original,"cancelled tree export replaced target");
    auto invalid=doc;invalid.occurrences[0].parent=1;rejected=false;
    try{Stp::exportBom(invalid,path);}catch(const std::exception&){rejected=true;}
    require(rejected&&readFile(path)==original,"invalid tree export replaced target or silently omitted nodes");
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        QTemporaryDir temporary;require(temporary.isValid(),"test temporary directory");
        const QDir out(argc>1?QString::fromLocal8Bit(argv[1]):temporary.path());require(QDir().mkpath(out.path()),"evidence directory");
        const auto doc=fixture();const auto complete=out.filePath(QStringLiteral("装配层级.xlsx"));Stp::exportBom(doc,complete);checkTree(complete,doc);
        checkScope(out.filePath(QStringLiteral("部分勾选.xlsx")),doc);checkDeep(out.filePath(QStringLiteral("十九层装配.xlsx")));
        checkPreorder(out.filePath(QStringLiteral("多根乱序输入.xlsx")));checkAtomic(out.filePath(QStringLiteral("取消保留.xlsx")),doc);
        std::cout<<"PASS: native Excel outlines, parent-above-child groups, active tree sheet, frozen names, indentation, multi-root/repeated instances, scoped quantities, deep-tree limits, formula-safe names and atomic cancellation\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
