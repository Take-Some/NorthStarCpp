// Non-destructive per-entry editing for both NEF8 YMAT families:
// kind 4 XML material libraries and kind 30 JSON material libraries.
// The compressed container remains NEF8, with all other material records intact.
#include "qt_material_library_edit.hpp"
#include "qt_material_authoring.hpp"
#include <northstarcpp/shared_assets.hpp>
#include <northstarcpp/material_ymat_authoring.hpp>
#include <QColor>
#include <QFile>
#include <QLockFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace northstarcpp::editor::material_library_edit {
namespace {
namespace fs=std::filesystem;
std::string bytes(QString const& q){return q.toUtf8().toStdString();}
QString string(fs::path const& path) {
    auto s=path.generic_u8string();
    return QString::fromUtf8(s.data(),int(s.size()));
}
struct Reference {QString file;QString entry;};
Reference split(QString const& reference) {
    auto const at=reference.lastIndexOf('@');
    auto path=at<0?reference:reference.left(at);
    auto entry=at<0?QString{}:reference.mid(at+1);
    if(!path.endsWith(".ymat",Qt::CaseInsensitive) ||
       (at>=0 && (entry.isEmpty()||entry.contains('@') ||
                   entry.contains('/')||entry.contains('\\')||entry.contains(':'))))
        throw std::invalid_argument("Expected YMAT path or YMAT@material-name");
    return {path,entry};
}
std::vector<std::uint8_t> read(fs::path const& root,QString const& file) {
    auto path=material_authoring::resolve_existing(root,file);
    QFile input(string(path));
    if(!input.open(QIODevice::ReadOnly) ||
       input.size()<128 || input.size()>64*1024*1024)
        throw std::runtime_error("Cannot read bounded native YMAT library");
    auto raw=input.readAll();
    return std::vector<std::uint8_t>(raw.begin(),raw.end());
}
QString selected_name(QString const& from,
                      std::vector<shared_assets::Material> const& list) {
    if(!from.isEmpty())return from;
    if(list.size()!=1)
        throw std::invalid_argument(
            "YMAT is a multi-material library: select a named material entry");
    return QString::fromUtf8(list.front().name.c_str());
}
QString texture_field(QString const& slot) {
    if(slot=="base_color"||slot=="normal"||slot=="roughness"||
       slot=="metallic"||slot=="emissive")
        return slot+"_texture";
    return {};
}
double linear_to_srgb(double v) {
    v=std::clamp(v,0.,1.);
    return v<=0.0031308?v*12.92:1.055*std::pow(v,1./2.4)-0.055;
}
double srgb_to_linear(double v) {
    v=std::clamp(v,0.,1.);
    return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);
}
QString color_hex(QJsonArray const& values) {
    int rgb[3]{255,255,255};
    for(int i=0;i<3 && i<values.size();++i)
        rgb[i]=int(std::lround(255*linear_to_srgb(values[i].toDouble())));
    return QColor(rgb[0],rgb[1],rgb[2]).name(QColor::HexRgb);
}
QJsonArray color_values(QString const& color) {
    QColor c(color);
    if(!c.isValid())throw std::invalid_argument("Invalid YMAT Base Color");
    return QJsonArray{srgb_to_linear(c.redF()),
                      srgb_to_linear(c.greenF()),
                      srgb_to_linear(c.blueF()),1.0};
}
QJsonArray typed_values(shared_assets::MaterialParameter const& value) {
    QJsonArray array;
    for(float x:value.values)array.append(double(x));
    return array;
}
QJsonObject material_descriptor(shared_assets::Material const& source) {
    QJsonObject doc{{"schema",1},
        {"name",QString::fromUtf8(source.name.c_str())},
        {"shader",QString::fromUtf8(source.shader.c_str())},
        {"blend_mode",source.blend=="alpha"?"translucent":
                      QString::fromUtf8(source.blend.c_str())},
        {"two_sided",source.two_sided},
        {"alpha_cutoff",double(source.alpha_cutoff)},
        {"base_color","#ffffff"},
        {"metallic",0.0},{"roughness",0.5}};
    QJsonArray allParameters;
    for(auto const& param:source.parameters) {
        QString const name=QString::fromUtf8(param.name.c_str());
        QJsonArray values=typed_values(param);
        allParameters.append(QJsonObject{{"name",name},
            {"type",QString::fromUtf8(param.type.c_str())},
            {"value",values}});
        if(name=="metallic" && values.size()>0)
            doc["metallic"]=values[0];
        if(name=="roughness" && values.size()>0)
            doc["roughness"]=values[0];
        if(name=="base_color" && values.size()>=3)
            doc["base_color"]=color_hex(values);
    }
    doc["parameters"]=allParameters;
    QJsonArray allTextures;
    for(auto const& binding:source.textures) {
        QString const slot=QString::fromUtf8(binding.slot.c_str());
        QString const ref=QString::fromUtf8(binding.reference.c_str());
        allTextures.append(QJsonObject{{"slot",slot},{"ref",ref}});
        if(auto key=texture_field(slot);!key.isEmpty())doc[key]=ref;
    }
    doc["textures"]=allTextures;
    if(!source.graph_source.empty()) {
        QJsonParseError error{};
        auto graph=QJsonDocument::fromJson(
            QByteArray::fromStdString(source.graph_source),&error);
        if(error.error==QJsonParseError::NoError && graph.isObject())
            doc["graph"]=graph.object();
    }
    return doc;
}
QJsonArray params(QJsonObject const& document) {
    QJsonArray all=document.value("parameters").toArray();
    auto set=[&](QString const& name,QString const& type,
                 QJsonArray values) {
        for(int i=0;i<all.size();++i) {
            auto entry=all[i].toObject();
            if(entry.value("name").toString()!=name)continue;
            entry["type"]=type;
            entry["value"]=values;
            all.replace(i,entry);
            return;
        }
        all.append(QJsonObject{{"name",name},{"type",type},{"value",values}});
    };
    // Preserve native linear RGB precision, the original alpha component,
    // and the original declared type unless Base Color actually changed.
    // An unrelated Roughness edit must not quantize an existing HDR-ish
    // color through #RRGGBB or silently reset alpha to 1.
    QJsonArray nativeColor=color_values(document.value("base_color").toString());
    QString colorType="color";
    for(auto const& raw:all) {
        auto param=raw.toObject();
        if(param.value("name").toString()!="base_color")continue;
        auto original=param.value("value").toArray();
        colorType=param.value("type").toString("color");
        if(original.size()>=3 &&
           color_hex(original).compare(document.value("base_color").toString(),
                                      Qt::CaseInsensitive)==0)
            nativeColor=original;
        else if(original.size()>=4)
            nativeColor[3]=original[3];
        break;
    }
    set("base_color",colorType,nativeColor);
    set("metallic","float",QJsonArray{document.value("metallic").toDouble()});
    set("roughness","float",QJsonArray{document.value("roughness").toDouble()});
    return all;
}
QJsonArray textures(QJsonObject const& doc) {
    QJsonArray result=doc.value("textures").toArray();
    for(QString const slot:{"base_color","normal","roughness","metallic","emissive"}) {
        QString const field=texture_field(slot);
        if(!doc.contains(field))continue;
        auto ref=doc.value(field).toString();
        bool exists=false;
        for(int i=0;i<result.size();++i) {
            auto row=result[i].toObject();
            if(row.value("slot").toString()!=slot)continue;
            row["ref"]=ref;
            result.replace(i,row);
            exists=true;
            break;
        }
        if(!exists && !ref.isEmpty())
            result.append(QJsonObject{{"slot",slot},{"ref",ref}});
    }
    return result;
}
QString value_string(QJsonArray const& array) {
    QStringList list;
    for(auto const& entry:array)
        list.push_back(QString::number(entry.toDouble(),'g',9));
    return list.join(',');
}
QMap<QString,QJsonObject> index(QJsonArray const& entries,
                                 QString const& nameKey) {
    QMap<QString,QJsonObject> result;
    for(auto const& raw:entries) {
        auto entry=raw.toObject();
        auto key=entry.value(nameKey).toString();
        if(key.isEmpty()||result.contains(key))
            throw std::invalid_argument("Duplicate or missing YMAT material field");
        result.insert(key,entry);
    }
    return result;
}
// Retain unrelated XML material nodes and every unknown element/attribute
// while patching the explicitly selected Material element.
void writeTypedXmlValue(QXmlStreamWriter& writer,QJsonValue const& value,
                        QString const& name={},unsigned depth=0) {
    if(depth>96)throw std::invalid_argument("Material Graph XML nesting limit");
    writer.writeStartElement("Value");
    if(!name.isEmpty())writer.writeAttribute("name",name);
    switch(value.type()) {
        case QJsonValue::Object: {
            writer.writeAttribute("type","object");
            auto object=value.toObject();
            for(auto it=object.begin();it!=object.end();++it)
                writeTypedXmlValue(writer,it.value(),it.key(),depth+1);
            break;
        }
        case QJsonValue::Array: {
            writer.writeAttribute("type","array");
            for(auto const& item:value.toArray())
                writeTypedXmlValue(writer,item,{},depth+1);
            break;
        }
        case QJsonValue::String:
            writer.writeAttribute("type","string");
            writer.writeCharacters(value.toString());
            break;
        case QJsonValue::Double:
            if(!std::isfinite(value.toDouble()))
                throw std::invalid_argument("Nonfinite Material Graph XML number");
            writer.writeAttribute("type","number");
            writer.writeCharacters(QString::number(value.toDouble(),'g',17));
            break;
        case QJsonValue::Bool:
            writer.writeAttribute("type","bool");
            writer.writeCharacters(value.toBool()?"true":"false");
            break;
        default:writer.writeAttribute("type","null");break;
    }
    writer.writeEndElement();
}
void writeMaterialGraphXml(QXmlStreamWriter& writer,
                           QJsonObject const& graph) {
    writer.writeStartElement("Graph");
    writeTypedXmlValue(writer,graph);
    writer.writeEndElement();
}
QByteArray patch_xml(QString const& document,QString const& name,
                     QJsonObject const& edited) {
    QXmlStreamReader reader(document);
    QByteArray output;
    QXmlStreamWriter writer(&output);
    writer.setAutoFormatting(false);
    auto newParameters=index(params(edited),"name");
    auto newTextures=index(textures(edited),"slot");
    QSet<QString> visitedParams,visitedTextures;
    bool active=false,found=false,hasSurface=false,hasParams=false;
    bool hasTextures=false,hasGraph=false;
    int depth=0;
    auto attrib=[&](QXmlStreamAttributes attributes,
                    QString const& key,QString const& value) {
        for(int i=0;i<attributes.size();++i)
            if(attributes[i].name()==key) {
                attributes[i]=QXmlStreamAttribute(key,value);
                return attributes;
            }
        attributes.append(key,value);
        return attributes;
    };
    auto param=[&](QJsonObject const& p) {
        writer.writeEmptyElement("Param");
        writer.writeAttribute("name",p.value("name").toString());
        writer.writeAttribute("type",p.value("type").toString());
        writer.writeAttribute("value",value_string(p.value("value").toArray()));
    };
    auto texture=[&](QJsonObject const& t) {
        if(t.value("ref").toString().isEmpty())return;
        writer.writeEmptyElement("Texture");
        writer.writeAttribute("slot",t.value("slot").toString());
        writer.writeAttribute("ref",t.value("ref").toString());
    };
    auto addUnseenParams=[&] {
        for(auto it=newParameters.begin();it!=newParameters.end();++it)
            if(!visitedParams.contains(it.key()))param(it.value());
    };
    auto addUnseenTextures=[&] {
        for(auto it=newTextures.begin();it!=newTextures.end();++it)
            if(!visitedTextures.contains(it.key()))texture(it.value());
    };
    while(!reader.atEnd()) {
        auto type=reader.readNext();
        if(type==QXmlStreamReader::StartDocument) {
            writer.writeStartDocument();continue;
        }
        if(type==QXmlStreamReader::EndDocument) {
            writer.writeEndDocument();continue;
        }
        if(type==QXmlStreamReader::StartElement) {
            auto element=reader.name().toString();
            auto attributes=reader.attributes();
            if(element=="Material" && depth==1 &&
               attributes.value("name")==name) {
                if(found)throw std::invalid_argument("Duplicate YMAT material entry");
                found=active=true;hasSurface=hasParams=hasTextures=hasGraph=false;
                visitedParams.clear();visitedTextures.clear();
                attributes=attrib(attributes,"shader",
                    edited.value("shader").toString());
            }
            if(active && element=="Surface") {
                hasSurface=true;
                attributes=attrib(attributes,"blend",
                    edited.value("blend_mode").toString()=="translucent"?
                    "alpha":edited.value("blend_mode").toString());
                attributes=attrib(attributes,"two_sided",
                    edited.value("two_sided").toBool()?"True":"False");
                attributes=attrib(attributes,"alpha_cutoff",
                    QString::number(edited.value("alpha_cutoff").toDouble(),'g',9));
            }
            if(active && element=="Params")hasParams=true;
            if(active && element=="Textures")hasTextures=true;
            if(active && element=="Param") {
                auto key=attributes.value("name").toString();
                auto it=newParameters.find(key);
                if(it==newParameters.end() && edited.contains("parameters")) {
                    reader.skipCurrentElement();continue;
                }
                if(it!=newParameters.end()) {
                    visitedParams.insert(key);
                    attributes=attrib(attributes,"type",
                        it->value("type").toString());
                    attributes=attrib(attributes,"value",
                        value_string(it->value("value").toArray()));
                }
            }
            if(active && element=="Texture") {
                auto key=attributes.value("slot").toString();
                auto it=newTextures.find(key);
                if(it==newTextures.end() && edited.contains("textures")) {
                    reader.skipCurrentElement();continue;
                }
                if(it!=newTextures.end()) {
                    visitedTextures.insert(key);
                    if(it->value("ref").toString().isEmpty()) {
                        reader.skipCurrentElement();continue;
                    }
                    attributes=attrib(attributes,"ref",
                        it->value("ref").toString());
                }
            }
            if(active && element=="Graph") {
                hasGraph=true;
                reader.skipCurrentElement();
                if(edited.value("graph").isObject())
                    writeMaterialGraphXml(writer,
                        edited.value("graph").toObject());
                continue;
            }
            writer.writeStartElement(reader.qualifiedName().toString());
            for(auto const& ns:reader.namespaceDeclarations())
                writer.writeNamespace(ns.namespaceUri().toString(),
                                      ns.prefix().toString());
            writer.writeAttributes(attributes);
            ++depth;
        }else if(type==QXmlStreamReader::EndElement) {
            auto element=reader.name().toString();
            if(active && element=="Params")addUnseenParams();
            if(active && element=="Textures")addUnseenTextures();
            if(active && element=="Material" && depth==2) {
                if(!hasSurface) {
                    writer.writeEmptyElement("Surface");
                    writer.writeAttribute("blend",edited.value("blend_mode").toString());
                    writer.writeAttribute("two_sided",
                        edited.value("two_sided").toBool()?"True":"False");
                    writer.writeAttribute("alpha_cutoff",QString::number(
                        edited.value("alpha_cutoff").toDouble(),'g',9));
                }
                if(!hasParams) {
                    writer.writeStartElement("Params");
                    addUnseenParams();writer.writeEndElement();
                }
                if(!hasTextures && !newTextures.isEmpty()) {
                    writer.writeStartElement("Textures");
                    addUnseenTextures();writer.writeEndElement();
                }
                if(!hasGraph && edited.value("graph").isObject())
                    writeMaterialGraphXml(writer,
                        edited.value("graph").toObject());
                active=false;
            }
            writer.writeEndElement();
            --depth;
        }else if(type==QXmlStreamReader::Characters)
            writer.writeCharacters(reader.text().toString());
        else if(type==QXmlStreamReader::Comment)
            writer.writeComment(reader.text().toString());
        else if(type==QXmlStreamReader::ProcessingInstruction)
            writer.writeProcessingInstruction(reader.processingInstructionTarget().toString(),
                                              reader.processingInstructionData().toString());
        else if(type==QXmlStreamReader::DTD)
            writer.writeDTD(reader.text().toString());
    }
    if(reader.hasError())
        throw std::runtime_error(("Invalid editable YMAT XML: "+
                                  reader.errorString()).toStdString());
    if(!found)throw std::invalid_argument("Named YMAT material was not found");
    return output;
}
} // namespace
QJsonObject load(fs::path const& root,QString const& reference) {
    auto ref=split(reference);
    auto original=read(root,ref.file);
    auto library=shared_assets::read_ymat_library(original);
    auto list=shared_assets::load_materials_from_bytes(original,bytes(ref.file));
    auto selected=selected_name(ref.entry,list);
    auto it=std::find_if(list.begin(),list.end(),[&](auto const& material) {
        return QString::fromUtf8(material.name.c_str())==selected;
    });
    if(it==list.end())
        throw std::invalid_argument("Selected YMAT entry is not in the library");
    auto descriptor=material_descriptor(*it);
    if(library.kind==30) {
        auto parsed=QJsonDocument::fromJson(QByteArray::fromStdString(library.body));
        if(!parsed.isObject())
            throw std::invalid_argument("Invalid native JSON YMAT body");
        for(auto const& value:parsed.object().value("materials").toArray()) {
            auto object=value.toObject();
            if(object.value("name").toString()!=selected)continue;
            QJsonParseError error{};
            auto nested=QJsonDocument::fromJson(
                object.value("editor_source").toString().toUtf8(),&error);
            if(error.error==QJsonParseError::NoError && nested.isObject() &&
               nested.object().value("name").toString()==selected) {
                auto document=nested.object();
                // The material may have parameters unknown to the older
                // single-material editor: expose them without dropping any.
                document["parameters"]=descriptor.value("parameters");
                document["textures"]=descriptor.value("textures");
                return document;
            }
        }
    }
    return descriptor;
}
void save(fs::path const& root,QString const& reference,
          QJsonObject const& document) {
    auto ref=split(reference);
    auto target=material_authoring::resolve_existing(root,ref.file);
    // Serialize concurrent editor windows before reading and replacing
    // the library, so independent entry edits cannot overwrite each other.
    QLockFile lock(string(target)+".lock");
    lock.setStaleLockTime(30000);
    if(!lock.tryLock(5000))
        throw std::runtime_error("YMAT library is being saved by another editor");
    auto original=read(root,ref.file);
    auto library=shared_assets::read_ymat_library(original);
    auto before=shared_assets::load_materials_from_bytes(original,bytes(ref.file));
    auto selected=selected_name(ref.entry,before);
    if(document.value("name").toString()!=selected)
        throw std::invalid_argument(
            "Changing YMAT entry identity is not supported by the material editor");
    auto found=std::count_if(before.begin(),before.end(),[&](auto const& record) {
        return QString::fromUtf8(record.name.c_str())==selected;
    });
    if(found!=1)throw std::invalid_argument("Ambiguous YMAT selected material");
    // All edit paths converge on a single XML YMAT writer. Existing kind30
    // libraries are read/imported and converted, never written back as JSON.
    auto canonical=shared_assets::migrate_ymat_to_xml(original);
    auto xmlLibrary=shared_assets::read_ymat_library(canonical);
    if(xmlLibrary.kind!=4)
        throw std::runtime_error("YMAT canonical migration failed");
    auto replacement=patch_xml(
        QString::fromUtf8(xmlLibrary.body.data(),int(xmlLibrary.body.size())),
        selected,document).toStdString();
    // Single writer for all imports and existing libraries:
    // XML kind4 with no legacy JSON metadata, regardless of input format.
    auto packed=shared_assets::encode_xml_ymat(replacement,before.size());
    auto after=shared_assets::load_materials_from_bytes(packed,bytes(ref.file));
    if(after.size()!=before.size())
        throw std::runtime_error("Edited YMAT dropped material records");
    for(std::size_t i=0;i<before.size();++i)
        if(before[i].name!=after[i].name)
            throw std::runtime_error("Edited YMAT changed material identity/order");
    // Save only after the entire NEF8 file and all material records validate.
    QSaveFile output(string(target));
    if(!output.open(QIODevice::WriteOnly))
        throw std::runtime_error("Cannot begin atomic YMAT library save");
    QByteArray bytesToWrite(
        reinterpret_cast<char const*>(packed.data()),qsizetype(packed.size()));
    if(output.write(bytesToWrite)!=bytesToWrite.size() || !output.commit())
        throw std::runtime_error("Cannot atomically replace YMAT library");
}
} // namespace northstarcpp::editor::material_library_edit
