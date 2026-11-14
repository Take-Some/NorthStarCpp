// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#include <northstarcpp/ecs_api.h>
#include <northstarcpp/module.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
thread_local std::string error_text;
struct Schema { size_t size, align; };
struct Record {
    NsEntity id = 0;
    std::unordered_map<std::string,std::vector<unsigned char>> components;
};
struct World {
    NsEntity next = 1;
    std::unordered_map<std::string,Schema> schemas;
    std::vector<Record> entities;
    Record& record(NsEntity id) {
        auto it=std::find_if(entities.begin(),entities.end(),
            [id](Record const& r){return r.id==id;});
        if (it==entities.end()) throw std::runtime_error("Invalid entity");
        return *it;
    }
    Schema schema(char const* key) const {
        if(!key)throw std::runtime_error("Null key");
        auto it=schemas.find(key);
        if(it==schemas.end())throw std::runtime_error("Unknown component type");
        return it->second;
    }
};
template<class F> int checked(F&& f) {
    try { error_text.clear();f();return 1; }
    catch(std::exception const& e) {error_text=e.what();}
    catch(...) {error_text="Mock provider error";}
    return 0;
}
World& require(void* ptr) {
    if(!ptr)throw std::runtime_error("Null world");
    return *static_cast<World*>(ptr);
}
char const* error() {return error_text.c_str();}
char const* provider(){return "Test memory ECS";}
void* make(){return new World;}
void destroy(void* w){delete static_cast<World*>(w);}
int define(void* w,char const* key,size_t size,size_t alignment){
    return checked([&]{
        auto& a=require(w);
        if(!key||!*key||size==0||alignment==0)
            throw std::runtime_error("Bad component schema");
        auto it=a.schemas.find(key);
        if(it!=a.schemas.end()) {
            if(it->second.size!=size||it->second.align!=alignment)
                throw std::runtime_error("Conflicting schema");
        } else a.schemas.emplace(key,Schema{size,alignment});
    });
}
NsEntity spawn(void* w) {
    NsEntity value=0;
    if(!checked([&]{
        auto& a=require(w);
        if(a.entities.size()>=16384)throw std::runtime_error("Too many entities");
        value=a.next++;
        a.entities.push_back(Record{value,{}});
    }))return 0;
    return value;
}
int remove_entity(void* w,NsEntity id){
    return checked([&]{
        auto& a=require(w);a.record(id);
        a.entities.erase(std::remove_if(a.entities.begin(),a.entities.end(),
            [id](Record const& r){return r.id==id;}),a.entities.end());
    });
}
int alive(void* w,NsEntity id){
    if(!w)return 0;
    auto& v=static_cast<World*>(w)->entities;
    return std::any_of(v.begin(),v.end(),[id](Record const& r){return r.id==id;});
}
size_t count(void* w){return w?static_cast<World*>(w)->entities.size():SIZE_MAX;}
NsEntity nth(void* w,size_t i) {
    if(!w)return 0;
    auto& v=static_cast<World*>(w)->entities;
    return i<v.size()?v[i].id:0;
}
int set(void* w,NsEntity id,char const* key,void const* ptr,size_t bytes){
    return checked([&]{
        auto& a=require(w);
        if(!ptr||a.schema(key).size!=bytes)throw std::runtime_error("Payload mismatch");
        auto& r=a.record(id);auto& b=r.components[key];
        b.assign(static_cast<unsigned char const*>(ptr),static_cast<unsigned char const*>(ptr)+bytes);
    });
}
int get(void* w,NsEntity id,char const* key,void* ptr,size_t bytes){
    return checked([&]{
        auto& a=require(w);
        if(!ptr||a.schema(key).size!=bytes)throw std::runtime_error("Read mismatch");
        auto& r=a.record(id);
        auto it=r.components.find(key);
        if(it==r.components.end())throw std::runtime_error("Missing component");
        std::memcpy(ptr,it->second.data(),bytes);
    });
}
int has(void* w,NsEntity id,char const* key){
    if(!w||!key)return 0;
    auto& a=*static_cast<World*>(w);
    auto it=std::find_if(a.entities.begin(),a.entities.end(),
        [id](Record const& r){return r.id==id;});
    return it!=a.entities.end() && it->components.count(key)!=0;
}
int remove_component(void* w,NsEntity id,char const* key){
    return checked([&]{
        auto& a=require(w);a.schema(key);
        a.record(id).components.erase(key);
    });
}
size_t query(void* w,char const* const* required,size_t n,NsEntity* dest,size_t capacity) {
    size_t total=SIZE_MAX;
    if(!checked([&]{
        auto& a=require(w);
        if(n && !required)throw std::runtime_error("Null query");
        for(size_t i=0;i<n;++i)a.schema(required[i]);
        total=0;
        for(auto const& r:a.entities) {
            bool hit=true;
            for(size_t i=0;i<n;++i)hit &= r.components.count(required[i])!=0;
            if(hit){
                if(dest&&total<capacity)dest[total]=r.id;
                ++total;
            }
        }
    }))return SIZE_MAX;
    return total;
}
int progress(void* w,float dt,float dx,float dz){
    return checked([&]{
        auto& a=require(w);
        if(!std::isfinite(dt)||!std::isfinite(dx)||!std::isfinite(dz))
            throw std::runtime_error("Invalid time");
        if(dt<=0)return;
        dx=std::clamp(dx,-1.f,1.f);dz=std::clamp(dz,-1.f,1.f);
        float factor=4.f*std::min(dt,.1f)/std::max(1.f,std::hypot(dx,dz));
        for(auto& r:a.entities) {
            auto c=r.components.find(NS_COMPONENT_CONTROLLER);
            auto p=r.components.find(NS_COMPONENT_POSITION);
            if(c==r.components.end()||p==r.components.end()||!c->second[0])continue;
            NsPosition pos{};
            std::memcpy(&pos,p->second.data(),sizeof(pos));
            pos.x=std::clamp(pos.x+dx*factor,-1000.f,1000.f);
            pos.z=std::clamp(pos.z+dz*factor,-1000.f,1000.f);
            std::memcpy(p->second.data(),&pos,sizeof(pos));
        }
    });
}
NsEcsApi const api{
 NORTHSTARCPP_ECS_API_VERSION,sizeof(NsEcsApi),
 provider,error,make,destroy,define,spawn,remove_entity,alive,
 count,nth,set,get,has,remove_component,query,progress
};
NorthStarModule const descriptor{
 sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_ECS,
 "MemoryMock","1.0",NORTHSTARCPP_ECS_API_VERSION,sizeof(NsEcsApi),&api,error
};
}
extern "C" NORTHSTARCPP_MODULE_EXPORT const NorthStarModule*
northstar_module_query(uint32_t host,char const* capability,uint32_t version) {
    if(host!=NORTHSTARCPP_MODULE_ABI||version!=NORTHSTARCPP_ECS_API_VERSION||
        !capability||std::strcmp(capability,NORTHSTARCPP_CAPABILITY_ECS)!=0)return nullptr;
    return &descriptor;
}
