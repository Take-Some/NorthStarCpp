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
        if(argc!=2)throw std::runtime_error("Expected MT28 .nvehicle input");
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
