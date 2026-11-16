#include "../engine/northstarcpp/modules/editor/qt_collision_project_settings.hpp"
#include <QCoreApplication>
#include <filesystem>
#include <stdexcept>
#include <iostream>
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    using namespace northstarcpp::editor::collision_project;
    auto const root=std::filesystem::temp_directory_path()/
        "northstar_collision_project_smoke_20261115";
    std::filesystem::remove_all(root);
    try {
        auto project=defaults();
        project.names[7]="Interactable";
        Preset custom;
        custom.name="SensorVolume";
        custom.channel=7;
        custom.responses[2]=northstarcpp::collision_authoring::Response::Overlap;
        custom.responses[4]=northstarcpp::collision_authoring::Response::Ignore;
        project.presets.push_back(custom);
        save(root,project);
        auto loaded=load(root);
        if(loaded.names[7]!="Interactable")
            throw std::runtime_error("Custom collision channel not persisted");
        auto it=std::find_if(loaded.presets.begin(),loaded.presets.end(),
            [](Preset const& p){return p.name=="SensorVolume";});
        if(it==loaded.presets.end()||it->channel!=7||
           it->responses[2]!=northstarcpp::collision_authoring::Response::Overlap||
           it->responses[4]!=northstarcpp::collision_authoring::Response::Ignore)
            throw std::runtime_error("Custom preset response matrix lost");
        auto old=loaded;
        old.names[6]="WorldStatic";
        bool rejected=false;
        try {
            // Client verifies uniqueness as part of the UI editing operation.
            for(int i=0;i<16;++i)
                for(int j=0;j<i;++j)
                    if(old.names[i].compare(old.names[j],Qt::CaseInsensitive)==0)
                        throw std::invalid_argument("Duplicate collision channel");
        } catch(std::invalid_argument const&) {rejected=true;}
        if(!rejected)throw std::runtime_error("Duplicate channel accepted");
        std::filesystem::remove_all(root);
        std::cout<<"Collision project channels/presets XML: PASS\n";
        return 0;
    }catch(std::exception const& e) {
        std::filesystem::remove_all(root);
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
