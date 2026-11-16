// Per-project editable collision channel names and response presets.
// XML stays outside cooked physics runtime, which uses numeric channel IDs.
#pragma once
#include <northstarcpp/collision_authoring.hpp>
#include <QFile>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QStringList>
#include <QRegularExpression>
#include <array>
#include <filesystem>
#include <vector>
#include <stdexcept>
namespace northstarcpp::editor::collision_project {
struct Preset {
    QString name;
    std::uint32_t channel=0;
    std::array<collision_authoring::Response,16> responses=
        collision_authoring::block_all();
};
struct Settings {
    std::array<QString,16> names;
    std::vector<Preset> presets;
};
inline bool valid_name(QString const& str) {
    static QRegularExpression const pattern("^[A-Za-z][A-Za-z0-9_-]{0,39}$");
    return pattern.match(str).hasMatch();
}
inline Settings defaults() {
    Settings s;
    for(std::size_t i=0;i<6;++i)
        s.names[i]=QString::fromLatin1(collision_authoring::channel_names[i]);
    for(std::size_t i=6;i<16;++i)
        s.names[i]=QString("Custom%1").arg(i-5);
    auto preset=[&](QString const& name,std::uint32_t type,int response) {
        Preset p;
        p.name=name;p.channel=type;
        p.responses.fill(static_cast<collision_authoring::Response>(response));
        s.presets.push_back(p);
    };
    preset("BlockAll",0,2);
    preset("OverlapAll",0,1);
    preset("NoCollision",0,0);
    preset("Trigger",0,1);
    preset("Pawn",2,2);
    preset("Vehicle",4,2);
    return s;
}
inline std::filesystem::path path(std::filesystem::path const& project_root) {
    return project_root/"config"/"collision_profiles.xml";
}
inline Settings load(std::filesystem::path const& project_root) {
    Settings result=defaults();
    auto filename=path(project_root);
    QFile file(QString::fromStdWString(filename.wstring()));
    if(!file.exists())return result;
    if(!file.open(QIODevice::ReadOnly)||file.size()>65536)
        throw std::runtime_error("Cannot read project collision profiles");
    QXmlStreamReader xml(&file);
    if(!xml.readNextStartElement()||xml.name()!=QString("collisionSettings")||
       xml.attributes().value("version")!="1")
        throw std::runtime_error("Collision profiles XML header invalid");
    while(xml.readNextStartElement()) {
        if(xml.name()==QString("channel")) {
            auto attrs=xml.attributes();
            bool ok=false;
            auto const index=attrs.value("index").toUInt(&ok);
            auto name=attrs.value("name").toString();
            if(!ok||index<6||index>=16||!valid_name(name))
                throw std::runtime_error("Invalid custom collision channel");
            result.names[index]=name;
            xml.skipCurrentElement();
        } else if(xml.name()==QString("preset")) {
            auto attrs=xml.attributes();
            Preset p;
            p.name=attrs.value("name").toString();
            bool ok=false;
            p.channel=attrs.value("channel").toUInt(&ok);
            auto responses=attrs.value("responses").toString();
            if(!valid_name(p.name)||!ok||p.channel>=16||
               responses.size()!=16||result.presets.size()>=32)
                throw std::runtime_error("Invalid collision preset XML");
            for(int i=0;i<16;++i) {
                int const r=responses[i].digitValue();
                if(r<0||r>2)throw std::runtime_error("Invalid collision response");
                p.responses[i]=static_cast<collision_authoring::Response>(r);
            }
            auto found=std::find_if(result.presets.begin(),result.presets.end(),
                [&](Preset const& other){return other.name==p.name;});
            if(found!=result.presets.end())*found=std::move(p);
            else result.presets.push_back(std::move(p));
            xml.skipCurrentElement();
        } else xml.skipCurrentElement();
    }
    if(xml.hasError())throw std::runtime_error("Malformed collision profile XML");
    return result;
}
inline void save(std::filesystem::path const& root,Settings const& s) {
    for(std::size_t i=6;i<16;++i)
        if(!valid_name(s.names[i]))
            throw std::invalid_argument("Invalid custom channel name");
    for(auto const& p:s.presets)
        if(!valid_name(p.name)||p.channel>=16)
            throw std::invalid_argument("Invalid preset name or channel");
    auto file_path=path(root);
    std::filesystem::create_directories(file_path.parent_path());
    QSaveFile file(QString::fromStdWString(file_path.wstring()));
    if(!file.open(QIODevice::WriteOnly))
        throw std::runtime_error("Cannot save project collision presets");
    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement("collisionSettings");
    xml.writeAttribute("version","1");
    for(std::size_t i=6;i<16;++i) {
        xml.writeEmptyElement("channel");
        xml.writeAttribute("index",QString::number(i));
        xml.writeAttribute("name",s.names[i]);
    }
    for(auto const& p:s.presets) {
        xml.writeEmptyElement("preset");
        xml.writeAttribute("name",p.name);
        xml.writeAttribute("channel",QString::number(p.channel));
        QString row;
        for(auto response:p.responses)
            row+=QChar('0'+static_cast<int>(response));
        xml.writeAttribute("responses",row);
    }
    xml.writeEndElement();
    xml.writeEndDocument();
    if(file.error()!=QFileDevice::NoError||!file.commit())
        throw std::runtime_error("Atomic collision profiles XML save failed");
}
inline void apply(collision_authoring::Collider& collider,Preset const& p) {
    collider.object_type=static_cast<collision_authoring::ObjectType>(p.channel);
    collider.responses=p.responses;
    collider.preset=p.name.toStdString();
}
} // namespace northstarcpp::editor::collision_project
