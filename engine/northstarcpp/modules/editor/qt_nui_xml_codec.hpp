#pragma once
// Qt authoring facade for canonical .nui XML v3. QJsonObject is an internal
// editable document tree only; persisted source is NEVER JSON.
#include "../shared/ui_xml_codec.hpp"
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonDocument>
#include <QXmlStreamWriter>
#include <QByteArray>
#include <QBuffer>
#include <QRegularExpression>
#include <stdexcept>
#include <cmath>

namespace northstarcpp::editor::nui_qt {
inline QJsonObject parse(QByteArray const& bytes) {
    auto normalized=northstarcpp::nui_xml::compile(
        std::string_view(bytes.constData(),static_cast<size_t>(bytes.size())));
    QJsonParseError error;
    auto decoded=QJsonDocument::fromJson(
        QByteArray(normalized.data(),static_cast<qsizetype>(normalized.size())),&error);
    if(error.error!=QJsonParseError::NoError || !decoded.isObject())
        throw std::invalid_argument("Invalid normalized UI XML document");
    return decoded.object();
}
inline QString valueType(QJsonValue const& value) {
    if(value.isObject() && value.toObject().contains("token"))return "token";
    if(value.isBool())return "bool";
    if(value.isDouble())return "number";
    if(value.isString()){
        static const QRegularExpression color("^#[0-9a-fA-F]{8}$");
        return color.match(value.toString()).hasMatch()?"color":"string";
    }
    throw std::invalid_argument("Unsupported UI property value");
}
inline QJsonValue typedEdit(QString type,QString source) {
    type=type.trimmed().toLower();
    if(type=="string") {
        if(source.toUtf8().size()>2048)throw std::invalid_argument("UI string too long");
        return source;
    }
    if(type=="bool"){
        auto s=source.trimmed();
        if(s!="true"&&s!="false")throw std::invalid_argument("Boolean requires true/false");
        return s=="true";
    }
    if(type=="number"){
        bool ok=false;
        auto n=source.trimmed().toDouble(&ok);
        if(!ok||!std::isfinite(n)||std::abs(n)>1000000)
            throw std::invalid_argument("Invalid UI number");
        return n;
    }
    if(type=="color"){
        static QRegularExpression const hex("^#[0-9a-fA-F]{8}$");
        if(!hex.match(source).hasMatch())
            throw std::invalid_argument("Color requires #RRGGBBAA");
        return source;
    }
    if(type=="token"){
        auto name=source.trimmed();
        if(name.startsWith("$token:"))name.remove(0,7);
        static QRegularExpression const id("^[A-Za-z][A-Za-z0-9_.-]{0,63}$");
        if(!id.match(name).hasMatch())
            throw std::invalid_argument("Invalid design token reference");
        return QJsonObject{{"token",name}};
    }
    throw std::invalid_argument("Unknown UI property type: "+type.toStdString());
}
inline void property(QXmlStreamWriter& writer,QString const& name,
                     QJsonValue const& value) {
    writer.writeEmptyElement("Property");
    writer.writeAttribute("name",name);
    auto kind=valueType(value);
    if(kind=="token") {
        auto ref=value.toObject();
        if(ref.size()!=1 || !ref.value("token").isString())
            throw std::invalid_argument("Malformed UI token reference");
        writer.writeAttribute("token",ref.value("token").toString());
    } else {
        writer.writeAttribute("type",kind);
        writer.writeAttribute("value",kind=="bool"?(value.toBool()?"true":"false"):
            kind=="number"?QString::number(value.toDouble(),'g',16):value.toString());
    }
}
inline QByteArray serialize(QJsonObject const& document) {
    QByteArray encoded;
    QXmlStreamWriter xml(&encoded);
    xml.setAutoFormatting(true);
    xml.setAutoFormattingIndent(2);
    xml.writeStartDocument("1.0");
    xml.writeStartElement("NorthStarUI");
    xml.writeAttribute("version","3");
    xml.writeAttribute("target",document.value("target").toString("screen"));
    for(auto key:{"layer","entityId","width","height","worldWidth","worldHeight"}){
        auto value=document.value(key);
        if(value.isUndefined())continue;
        xml.writeAttribute(key,value.isDouble()?
            QString::number(value.toDouble(),'g',16):value.toString());
    }
    auto tokens=document.value("tokens").toObject();
    if(!tokens.isEmpty()) {
        xml.writeStartElement("Tokens");
        for(auto it=tokens.begin();it!=tokens.end();++it) {
            auto type=valueType(it.value());
            if(type=="token")throw std::invalid_argument("Nested token reference forbidden");
            xml.writeEmptyElement("Token");
            xml.writeAttribute("name",it.key());
            xml.writeAttribute("type",type);
            xml.writeAttribute("value",type=="bool"?(it.value().toBool()?"true":"false"):
                type=="number"?QString::number(it.value().toDouble(),'g',16):
                it.value().toString());
        }
        xml.writeEndElement();
    }
    auto styles=document.value("styles").toObject();
    if(!styles.isEmpty()) {
        xml.writeStartElement("Styles");
        for(auto it=styles.begin();it!=styles.end();++it){
            xml.writeStartElement("Style");
            xml.writeAttribute("name",it.key());
            auto props=it.value().toObject();
            for(auto v=props.begin();v!=props.end();++v)
                property(xml,v.key(),v.value());
            xml.writeEndElement();
        }
        xml.writeEndElement();
    }
    auto components=document.value("components").toObject();
    if(!components.isEmpty()){
        xml.writeStartElement("Components");
        for(auto it=components.begin();it!=components.end();++it){
            auto templateInfo=it.value().toObject();
            xml.writeStartElement("Component");
            xml.writeAttribute("name",it.key());
            xml.writeAttribute("type",templateInfo.value("type").toString());
            auto props=templateInfo.value("props").toObject();
            for(auto prop=props.begin();prop!=props.end();++prop)
                property(xml,prop.key(),prop.value());
            xml.writeEndElement();
        }
        xml.writeEndElement();
    }
    xml.writeStartElement("Elements");
    auto nodes=document.value("nodes").toArray();
    for(auto item:nodes){
        auto node=item.toObject();
        auto kind=node.value("type").toString();
        QString tag;
        if(kind=="panel")tag="Panel";
        else if(kind=="text")tag="Text";
        else if(kind=="button")tag="Button";
        else if(kind=="image")tag="Image";
        else if(kind=="input")tag="Input";
        else throw std::invalid_argument("Unknown XML UI widget");
        auto component=node.value("component").toString();
        xml.writeStartElement(component.isEmpty()?tag:QString("Use"));
        xml.writeAttribute("id",node.value("id").toString());
        if(!component.isEmpty())xml.writeAttribute("component",component);
        auto parent=node.value("parent").toString();
        if(!parent.isEmpty())xml.writeAttribute("parent",parent);
        auto style=node.value("style").toString();
        if(!style.isEmpty())xml.writeAttribute("style",style);
        auto props=node.value("props").toObject();
        for(auto it=props.begin();it!=props.end();++it)
            property(xml,it.key(),it.value());
        xml.writeEndElement();
    }
    xml.writeEndElement(); // Elements
    auto actions=document.value("actions").toArray();
    if(!actions.isEmpty()){
        xml.writeStartElement("Actions");
        for(auto const& element:actions){
            auto action=element.toObject();
            xml.writeEmptyElement("Action");
            for(auto key:{"id","event","emit","animation"}){
                auto value=action.value(key).toString();
                if(!value.isEmpty())xml.writeAttribute(key,value);
            }
        }
        xml.writeEndElement();
    }
    auto animations=document.value("animations").toArray();
    if(!animations.isEmpty()){
        xml.writeStartElement("Animations");
        for(auto const& element:animations){
            auto anim=element.toObject();
            xml.writeEmptyElement("Animation");
            for(auto key:{"name","id","property","from","to","duration","easing","trigger"}){
                auto value=anim.value(key);
                if(!value.isUndefined())xml.writeAttribute(key,
                    value.isDouble()?QString::number(value.toDouble(),'g',16):value.toString());
            }
        }
        xml.writeEndElement();
    }
    auto breakpoints=document.value("breakpoints").toArray();
    if(!breakpoints.isEmpty()){
        xml.writeStartElement("Breakpoints");
        for(auto const& element:breakpoints){
            auto rule=element.toObject();
            xml.writeStartElement("Breakpoint");
            xml.writeAttribute("minWidth",QString::number(rule.value("minWidth").toDouble()));
            xml.writeAttribute("maxWidth",QString::number(rule.value("maxWidth").toDouble()));
            auto overrides=rule.value("overrides").toObject();
            for(auto it=overrides.begin();it!=overrides.end();++it){
                xml.writeStartElement("Override");
                xml.writeAttribute("id",it.key());
                auto props=it.value().toObject();
                for(auto prop=props.begin();prop!=props.end();++prop)
                    property(xml,prop.key(),prop.value());
                xml.writeEndElement();
            }
            xml.writeEndElement();
        }
        xml.writeEndElement();
    }
    xml.writeEndElement(); // NorthStarUI
    xml.writeEndDocument();
    if(encoded.size()>262144)
        throw std::length_error("UI XML exceeds 256 KiB");
    // Round-trip all typed properties and catch unknown style refs before save.
    auto canonical=parse(encoded);
    if(canonical.value("nodes").toArray()!=document.value("nodes").toArray() ||
       canonical.value("components").toObject()!=document.value("components").toObject() ||
       canonical.value("actions").toArray()!=document.value("actions").toArray() ||
       canonical.value("animations").toArray()!=document.value("animations").toArray() ||
       canonical.value("breakpoints").toArray()!=document.value("breakpoints").toArray())
        throw std::invalid_argument("UI document failed XML round-trip");
    return encoded;
}
} // namespace northstarcpp::editor::nui_qt
