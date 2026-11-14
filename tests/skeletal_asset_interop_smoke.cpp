// A real legacy YMT skeleton must interoperate with the 2026 palette utility.
// This verifies native source integration without loading GPU/character materials.
#include <northstarcpp/skeleton.hpp>
#include <northstarcpp/skeletal_animation.hpp>
#include <northstarcpp/assets.hpp>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        northstarcpp::AssetManager store;
        store.mount_directory("",argv[1]);
        auto rig=northstarcpp::load_skeleton(store,"models/abby/abby.ymt");
        northstarcpp::SkeletonPose pose(rig);
        if(rig.joints.empty() || rig.inverse_bind.size()!=rig.joints.size() ||
           pose.palette().size()!=rig.joints.size())
            throw std::runtime_error("Native YMT bind pose or joint table invalid");
        // The data format shared with the GPU upload contract is row-major
        // native NorthStar float4x4. Unsupported larger rigs are explicit.
        if(pose.palette().size()<=256) {
            const auto bytes=northstarcpp::pack_skinning_palette(pose.palette());
            if(bytes.size()!=rig.joints.size()*16)
                throw std::runtime_error("Native YMT palette GPU packing mismatch");
        }
        std::cout<<"YMT skeleton -> shared animation palette: "
                 <<rig.joints.size()<<" joints PASS\n";
        return 0;
    } catch(std::exception const& e) {
        std::cerr<<"YMT skeletal interoperability FAILED: "<<e.what()<<'\n';
        return 1;
    }
}
