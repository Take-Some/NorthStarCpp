#include <northstarcpp/scene.hpp>
#include <northstarcpp/collision_authoring.hpp>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){
try{
    namespace fs=std::filesystem;
    if(argc!=3)throw std::invalid_argument("Usage: mt28_collider_author INPUT.nscn OUTPUT.nscn");
    auto const source=fs::u8path(argv[1]),out=fs::u8path(argv[2]);
    if(fs::exists(out)||out.extension()!=".nscn")
        throw std::invalid_argument("Output must be a new .nscn file");
    auto scene=northstarcpp::load_scene(source);
    auto it=std::find_if(scene.objects.begin(),scene.objects.end(),
        [](auto const& obj){return obj.name=="Mahir MT28 - Native Vehicle";});
    if(it==scene.objects.end()||!it->node.id)
        throw std::runtime_error("MT28 entity missing from scene");
    northstarcpp::collision_authoring::Collider c;
    c.shape=northstarcpp::collision_authoring::Shape::Box;
    c.dimensions={2.48f,3.42f,9.8f};
    c.offset={0.f,.03f,0.f};
    northstarcpp::collision_authoring::assign(scene,it->node.id,c);
    northstarcpp::save_scene(scene,out);
    auto const verify=northstarcpp::load_scene(out);
    auto const parsed=northstarcpp::collision_authoring::lookup(verify,it->node.id);
    if(!parsed || verify.objects.size()!=scene.objects.size() ||
       parsed->dimensions.z!=c.dimensions.z)
        throw std::runtime_error("Vehicle collider NEF8 roundtrip mismatch");
    std::cout<<"MT28 collider authored: 2.48m x 3.42m x 9.8m\n";
    return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<"\n";return 1;}
}
