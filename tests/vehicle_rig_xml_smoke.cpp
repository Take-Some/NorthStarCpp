// Project-local .nvehicle XML format validation and 8-wheel MT28 regression.
#include "../engine/northstarcpp/modules/host/vehicle_rig_xml.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
int main(int argc,char** argv) {
    try {
        if(argc==4 && std::string(argv[1])=="--rustsea") {
            auto read=[](char const* filename) {
                std::ifstream stream(filename,std::ios::binary);
                if(!stream)throw std::runtime_error("Missing Rust Sea vehicle rig");
                std::string xml((std::istreambuf_iterator<char>(stream)),
                                std::istreambuf_iterator<char>());
                return northstarcpp::parse_vehicle_rig_xml(xml);
            };
            auto bastion=read(argv[2]);
            auto enemy=read(argv[3]);
            if(bastion.contact_mode!="flat" || enemy.contact_mode!="flat" ||
               bastion.wheels.size()!=14 || enemy.wheels.size()!=8 ||
               bastion.setup.mass!=95000.f || enemy.setup.mass!=26000.f ||
               bastion.seats.size()!=1 || enemy.seats.size()!=1 ||
               bastion.seats[0].role!=1 || enemy.seats[0].role!=1 ||
               bastion.wheels[0].radius!=1.88f ||
               bastion.wheels.back().mount[0]!=23.f)
                throw std::runtime_error("Rust Sea city vehicle rig mismatch");
            for(auto const& wheel:bastion.wheels)
                if(wheel.drive_factor!=1.f || wheel.brake_factor!=1.f ||
                   wheel.steer_factor!=0.f)
                    throw std::runtime_error("Bastion tracked drive configuration invalid");
            std::cout<<"Sea of Rust vehicle rigs: Bastion 14 wheels/95t "
                     <<"and Rust Wren 8 wheels/26t PASS" << std::endl;
            return 0;
        }
        if(argc<2 || argc>3)throw std::runtime_error("Expected MT28 rig and optional powertrain sample");
        std::ifstream source(argv[1],std::ios::binary);
        if(!source)throw std::runtime_error("Cannot open MT28 rig XML");
        std::string data((std::istreambuf_iterator<char>(source)),
                          std::istreambuf_iterator<char>());
        auto rig=northstarcpp::parse_vehicle_rig_xml(data);
        auto descriptor=rig.view();
        if(descriptor.wheel_count!=8 || descriptor.seat_count!=21 ||
           descriptor.mass<10000.f || descriptor.mass>20000.f ||
           rig.contact_mode!="physics" || rig.wheels[0].radius<=0.f ||
           rig.seats[0].role!=1 || rig.wheel_visual_nodes.size()!=8 ||
           rig.doors.size()!=2 || rig.door_visual_nodes.size()!=2 ||
           rig.door_visual_nodes[0]!="MT28 Door Debug Left" ||
           rig.door_visual_nodes[1]!="MT28 Door Debug Right" ||
           rig.doors[0].open_angle<=0.f ||
           rig.doors[1].open_angle>=0.f)
            throw std::runtime_error("MT28 wheel/seat/weight contract mismatch");
        // v3 adds an independent, strictly validated powertrain section.
        auto upgraded=data;
        auto const marker=std::string("version=\"2\"");
        auto const location=upgraded.find(marker);
        if(location==std::string::npos)
            throw std::runtime_error("Expected legacy v2 fixture");
        upgraded.replace(location,marker.size(),"version=\"3\"");
        auto const powertrain=std::string(
            "<Powertrain automatic=\"1\" reverse_ratio=\"3.1\" "
            "final_drive=\"3.8\" efficiency=\"0.88\" "
            "peak_torque=\"300\" idle_rpm=\"850\" "
            "redline_rpm=\"6000\" shift_up_rpm=\"4400\" "
            "shift_down_rpm=\"1450\" shift_seconds=\"0.11\">"
            "<Gear ratio=\"3.3\"/><Gear ratio=\"2.2\"/>"
            "<Gear ratio=\"1.4\"/><Gear ratio=\"0.92\"/>"
            "</Powertrain>");
        auto const end=upgraded.find("</NorthStarVehicleRig>");
        if(end==std::string::npos)throw std::runtime_error("Broken XML fixture");
        upgraded.insert(end,powertrain);
        auto version3=northstarcpp::parse_vehicle_rig_xml(upgraded);
        if(!version3.powertrain || version3.powertrain->forward_gear_count!=4 ||
           version3.powertrain->ratios[3]!=.92f)
            throw std::runtime_error("Vehicle v3 Powertrain XML rejected");
        bool invalidPowertrain=false;
        auto invalid=upgraded;
        auto const ratio=invalid.find("ratio=\"2.2\"");
        if(ratio==std::string::npos)
            throw std::runtime_error("Expected second gear fixture");
        invalid.replace(ratio,std::string("ratio=\"2.2\"").size(),
                       "ratio=\"4.0\"");
        try {northstarcpp::parse_vehicle_rig_xml(invalid);}
        catch(std::exception const&) {invalidPowertrain=true;}
        if(!invalidPowertrain)
            throw std::runtime_error("Invalid gear order accepted");
        if(argc>=3) {
            std::ifstream example(argv[2],std::ios::binary);
            if(!example)throw std::runtime_error("Powertrain v3 example missing");
            std::string sample((std::istreambuf_iterator<char>(example)),
                               std::istreambuf_iterator<char>());
            auto parsed=northstarcpp::parse_vehicle_rig_xml(sample);
            if(!parsed.powertrain || parsed.contact_mode!="flat" ||
               parsed.wheels.size()!=8 || parsed.powertrain->forward_gear_count!=4)
                throw std::runtime_error("Powertrain v3 sample invalid");
        }
        bool bad=false;
        try {northstarcpp::parse_vehicle_rig_xml(
            "<NorthStarVehicleRig version=\"1\"/>");}
        catch(std::exception const&) {bad=true;}
        if(!bad)throw std::runtime_error("Unsupported XML version accepted");
        std::cout<<"MT28 .nvehicle: 8 wheels / 21 seats / XML schema PASS\n";
        return 0;
    } catch(std::exception const& e) {
        std::cerr<<e.what()<<"\n";
        return 1;
    }
}
