// Alternative engine.logger implementation: proves provider interchangeability.
#include <northstarcpp/logger_plugin.h>
#include <cstring>
#include <new>

namespace {
struct Stub { unsigned count=0; uint32_t minimum=NORTHSTAR_LOG_TRACE; };

void* create(NorthStarLoggerConfig const* config) {
    return config && config->size==sizeof(NorthStarLoggerConfig) ? new(std::nothrow) Stub : nullptr;
}
void destroy(void* ptr) {delete static_cast<Stub*>(ptr);}
int write(void* ptr,NorthStarLogRecord const* record) {
    if(!ptr || !record || record->size!=sizeof(NorthStarLogRecord) ||
       record->level>NORTHSTAR_LOG_FATAL) return 0;
    if(record->level>=static_cast<Stub*>(ptr)->minimum)
        ++static_cast<Stub*>(ptr)->count;
    return 1;
}
int flush(void* ptr) {return ptr ? 1:0;}
int level(void* ptr,uint32_t value) {
    if(!ptr||value>NORTHSTAR_LOG_FATAL)return 0;
    static_cast<Stub*>(ptr)->minimum=value;
    return 1;
}
char const* last_error() {return "Mock logger rejected the call";}
NorthStarLoggerApi const api{create,destroy,write,flush,level};
NorthStarModule const module{sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_LOGGER,"Test Logger","0.0.1",
    NORTHSTARCPP_LOGGER_ABI,sizeof(api),&api,last_error};
}
extern "C" __declspec(dllexport)
NorthStarModule const* northstar_module_query(
    uint32_t host,char const* capability,uint32_t version) {
    return host==NORTHSTARCPP_MODULE_ABI && version==NORTHSTARCPP_LOGGER_ABI &&
           capability && std::strcmp(capability,NORTHSTARCPP_CAPABILITY_LOGGER)==0
        ? &module : nullptr;
}
