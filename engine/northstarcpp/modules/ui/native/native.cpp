// NorthStarCpp engine.ui.native — backend-independent logical UI provider.
// Provides the element tree and event model; GPU/window presentation is intentionally
// unsupported until a renderer adapter is installed.
#include <northstarcpp/ui_plugin.h>
#include "module_provider.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#ifdef NORTHSTARCPP_UI_HAS_FONT_SHAPING
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>
#include <cstdlib>
#include <filesystem>
#endif

namespace {
using northstarcpp::module_provider::last_error;
struct Value {
    uint32_t kind = 0;
    double number = 0;
    uint32_t rgba8 = 0;
    bool boolean = false;
    std::string string;
};
struct Node {
    uint32_t kind = 0;
    uint64_t parent = 0;
    std::string name;
    std::map<std::string, Value> properties;
};
struct Event {
    uint32_t kind = 0;
    uint64_t target = 0;
    std::string name;
    Value value;
};
struct Context {
    uint64_t next = 1;
    std::unordered_map<uint64_t, Node> nodes;
    std::deque<Event> events;
    NorthStarUIEventCallback callback = nullptr;
    void* user = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    float dpi = 1;
    bool pumping = false;
    uint64_t pressed = 0;
    uint64_t hovered = 0;
    uint64_t focused = 0;
    std::string preedit;
};
thread_local char ui_error[512]{};
const char* last_ui_error() noexcept { return ui_error; }

int32_t fail(int32_t code, const char* message) noexcept {
    std::snprintf(ui_error, sizeof(ui_error), "%s", message);
    return code;
}
template<class Fn> int32_t protect(Fn&& f) noexcept {
    ui_error[0] = 0;
    try { return f(); }
    catch (const std::bad_alloc&) { return fail(NORTHSTAR_UI_BACKEND_FAILURE, "Out of memory"); }
    catch (...) { return fail(NORTHSTAR_UI_BACKEND_FAILURE, "Unexpected UI provider failure"); }
}
bool valid(NorthStarUIString s) noexcept {
    return (!s.length || s.data) && s.length <= 1048576;
}
std::string copy(NorthStarUIString s) {
    return s.length ? std::string(s.data, static_cast<size_t>(s.length)) : std::string();
}
bool valid_value(const NorthStarUIValue* value) noexcept {
    if (!value || value->size < sizeof(NorthStarUIValue)) return false;
    switch (value->kind) {
    case NORTHSTAR_UI_VALUE_BOOL: return value->boolean <= 1;
    case NORTHSTAR_UI_VALUE_NUMBER: return std::isfinite(value->number);
    case NORTHSTAR_UI_VALUE_STRING: return valid(value->string);
    case NORTHSTAR_UI_VALUE_COLOR_RGBA8: return true;
    default: return false;
    }
}
Value copy_value(const NorthStarUIValue& input) {
    Value result;
    result.kind = input.kind;
    result.number = input.number;
    result.rgba8 = input.rgba8;
    result.boolean = input.boolean != 0;
    if (input.kind == NORTHSTAR_UI_VALUE_STRING) result.string = copy(input.string);
    return result;
}
bool valid_kind(uint32_t kind) noexcept {
    return (kind >= NORTHSTAR_UI_ROOT && kind <= NORTHSTAR_UI_INPUT)
        || kind == NORTHSTAR_UI_CUSTOM;
}
bool valid_event(uint32_t kind) noexcept {
    return (kind >= NORTHSTAR_UI_EVENT_CLICK && kind <= NORTHSTAR_UI_EVENT_POINTER)
        || kind == NORTHSTAR_UI_EVENT_CUSTOM;
}
void escape_json(std::string& out, const std::string& s) {
    const char* hex = "0123456789abcdef";
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15];
            } else out.push_back(static_cast<char>(c));
        }
    }
    out.push_back('"');
}
std::string json(const Value& v) {
    switch (v.kind) {
    case NORTHSTAR_UI_VALUE_BOOL: return v.boolean ? "true" : "false";
    case NORTHSTAR_UI_VALUE_NUMBER: {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.17g", v.number);
        return buf;
    }
    case NORTHSTAR_UI_VALUE_COLOR_RGBA8: {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%u", v.rgba8);
        return buf;
    }
    case NORTHSTAR_UI_VALUE_STRING: {
        std::string result;
        escape_json(result, v.string);
        return result;
    }
    default: return "null";
    }
}
int32_t create_context(const NorthStarUIConfig* cfg, void** out) noexcept {
    return protect([&]() -> int32_t {
        if (!out || !cfg || cfg->size < sizeof(NorthStarUIConfig) || cfg->flags)
            return fail(NORTHSTAR_UI_INVALID_ARGUMENT, "Invalid UI configuration");
        *out = nullptr;
        *out = new Context();
        return NORTHSTAR_UI_OK;
    });
}
int32_t destroy_context(void* ctx) noexcept {
    return protect([&]() -> int32_t {
        if (!ctx) return fail(NORTHSTAR_UI_INVALID_ARGUMENT, "Null UI context");
        auto* state = static_cast<Context*>(ctx);
        if (state->pumping) return fail(NORTHSTAR_UI_INVALID_ARGUMENT, "Cannot destroy during callback");
        delete state;
        return NORTHSTAR_UI_OK;
    });
}
int32_t create_element(void* ctx, const NorthStarUIElement* element,
                       NorthStarUIHandle* out) noexcept {
    return protect([&]() -> int32_t {
        if (!ctx || !out || !element || element->size < sizeof(NorthStarUIElement)
            || !valid_kind(element->kind) || !valid(element->name)
            || !element->name.length || element->flags || element->reserved)
            return fail(NORTHSTAR_UI_INVALID_ARGUMENT, "Invalid UI element");
        auto& state = *static_cast<Context*>(ctx);
        if (state.pumping) return fail(NORTHSTAR_UI_INVALID_ARGUMENT, "Reentrant UI call");
        *out = NORTHSTAR_UI_INVALID_HANDLE;
        if ((element->kind == NORTHSTAR_UI_ROOT) != (element->parent == 0))
            return fail(NORTHSTAR_UI_INVALID_ARGUMENT, "Root/parent mismatch");
        if (element->parent && !state.nodes.count(element->parent))
            return fail(NORTHSTAR_UI_NOT_FOUND, "Parent not found");
        if (state.next == 0 || state.next == std::numeric_limits<uint64_t>::max())
            return fail(NORTHSTAR_UI_BACKEND_FAILURE, "Element handle limit");
        const uint64_t handle = state.next++;
        state.nodes.emplace(handle, Node{element->kind, element->parent,
                                         copy(element->name), {}});
        *out = handle;
        return NORTHSTAR_UI_OK;
    });
}
int32_t destroy_element(void* ctx, NorthStarUIHandle handle) noexcept {
    return protect([&]() -> int32_t {
        if (!ctx || !handle) return fail(NORTHSTAR_UI_INVALID_ARGUMENT, "Invalid element handle");
        auto& state = *static_cast<Context*>(ctx);
        if (state.pumping) return fail(NORTHSTAR_UI_INVALID_ARGUMENT, "Reentrant UI call");
        if (!state.nodes.count(handle)) return fail(NORTHSTAR_UI_NOT_FOUND, "Element not found");
        std::vector<uint64_t> removed{handle};
        for (size_t i=0; i<removed.size(); ++i)
            for (const auto& pair : state.nodes)
                if (pair.second.parent == removed[i]) removed.push_back(pair.first);
        for (auto id : removed) state.nodes.erase(id);
    if(!state.nodes.count(state.focused)) {
        state.focused=0;
        state.preedit.clear();
    }
    if(!state.nodes.count(state.hovered))state.hovered=0;
    if(!state.nodes.count(state.pressed))state.pressed=0;
    state.events.erase(std::remove_if(state.events.begin(), state.events.end(),
            [&](const Event& event) {
                return std::find(removed.begin(), removed.end(), event.target) != removed.end();
            }), state.events.end());
        return NORTHSTAR_UI_OK;
    });
}
int32_t set_property(void* ctx, NorthStarUIHandle handle,
                     NorthStarUIString key, const NorthStarUIValue* value) noexcept {
    return protect([&]() -> int32_t {
        if (!ctx || !handle || !valid(key) || !key.length || !valid_value(value))
            return fail(NORTHSTAR_UI_INVALID_ARGUMENT, "Invalid property");
        auto& state = *static_cast<Context*>(ctx);
        if (state.pumping) return fail(NORTHSTAR_UI_INVALID_ARGUMENT, "Reentrant UI call");
        auto it=state.nodes.find(handle);
        if (it==state.nodes.end()) return fail(NORTHSTAR_UI_NOT_FOUND, "Element not found");
        it->second.properties[copy(key)] = copy_value(*value);
        return NORTHSTAR_UI_OK;
    });
}
int32_t get_property_json(void* ctx, NorthStarUIHandle handle,
                          NorthStarUIString key, char* buffer, uint64_t capacity,
                          uint64_t* required) noexcept {
    return protect([&]() -> int32_t {
        if (!ctx || !handle || !valid(key) || !key.length || !required
            || (!buffer && capacity))
            return fail(NORTHSTAR_UI_INVALID_ARGUMENT, "Invalid property read");
        auto& state = *static_cast<Context*>(ctx);
        if (state.pumping) return fail(NORTHSTAR_UI_INVALID_ARGUMENT, "Reentrant UI call");
        auto it=state.nodes.find(handle);
        if (it==state.nodes.end()) return fail(NORTHSTAR_UI_NOT_FOUND, "Element not found");
        auto found=it->second.properties.find(copy(key));
        if (found==it->second.properties.end()) return fail(NORTHSTAR_UI_NOT_FOUND, "Property not found");
        auto value=json(found->second);
        *required = static_cast<uint64_t>(value.size() + 1); // includes NUL
        if (!buffer && !capacity) return NORTHSTAR_UI_OK;
        if (capacity < *required) return fail(NORTHSTAR_UI_INVALID_ARGUMENT, "Output buffer too small");
        std::memcpy(buffer, value.c_str(), static_cast<size_t>(*required));
        return NORTHSTAR_UI_OK;
    });
}
int32_t set_event_callback(void* ctx, NorthStarUIEventCallback callback,
                           void* user) noexcept {
    return protect([&]() -> int32_t {
        if (!ctx) return fail(NORTHSTAR_UI_INVALID_ARGUMENT, "Null UI context");
        auto& state = *static_cast<Context*>(ctx);
        if (state.pumping) return fail(NORTHSTAR_UI_INVALID_ARGUMENT, "Reentrant UI call");
        state.callback=callback;
        state.user=user;
        return NORTHSTAR_UI_OK;
    });
}
int32_t dispatch_event(void* ctx, const NorthStarUIEvent* event) noexcept {
    return protect([&]() -> int32_t {
        if (!ctx || !event || event->size < sizeof(NorthStarUIEvent)
            || !valid_event(event->kind) || !valid(event->name)
            || !valid_value(&event->value))
            return fail(NORTHSTAR_UI_INVALID_ARGUMENT, "Invalid UI event");
        auto& state = *static_cast<Context*>(ctx);
        if (state.pumping) return fail(NORTHSTAR_UI_INVALID_ARGUMENT, "Reentrant UI call");
        if (!state.nodes.count(event->target))
            return fail(NORTHSTAR_UI_NOT_FOUND, "Event target not found");
        if (state.events.size() >= 4096)
            return fail(NORTHSTAR_UI_BACKEND_FAILURE, "UI event queue full");
        state.events.push_back(Event{event->kind, event->target,
                                     copy(event->name), copy_value(event->value)});
        return NORTHSTAR_UI_OK;
    });
}
int32_t pump_events(void* ctx) noexcept {
    return protect([&]() -> int32_t {
        if (!ctx) return fail(NORTHSTAR_UI_INVALID_ARGUMENT, "Null UI context");
        auto& state = *static_cast<Context*>(ctx);
        if (state.pumping) return fail(NORTHSTAR_UI_INVALID_ARGUMENT, "Reentrant event pump");
        state.pumping = true;
        struct Reset { bool& ref; ~Reset(){ref=false;} } reset{state.pumping};
        while (!state.events.empty()) {
            Event event=std::move(state.events.front());
            state.events.pop_front();
            if (!state.callback) continue;
            NorthStarUIEvent out{};
            out.size=sizeof(out);
            out.kind=event.kind;
            out.target=event.target;
            out.name={event.name.data(), static_cast<uint64_t>(event.name.size())};
            out.value.size=sizeof(out.value);
            out.value.kind=event.value.kind;
            out.value.number=event.value.number;
            out.value.rgba8=event.value.rgba8;
            out.value.boolean=event.value.boolean;
            out.value.string={event.value.string.data(),
                              static_cast<uint64_t>(event.value.string.size())};
            state.callback(state.user, &out);
        }
        return NORTHSTAR_UI_OK;
    });
}
int32_t resize(void* ctx,uint32_t width,uint32_t height,float dpi) noexcept {
    return protect([&]() -> int32_t {
        if (!ctx || !width || !height || !std::isfinite(dpi) || dpi<=0)
            return fail(NORTHSTAR_UI_INVALID_ARGUMENT, "Invalid UI size or DPI");
        auto& state = *static_cast<Context*>(ctx);
        if (state.pumping) return fail(NORTHSTAR_UI_INVALID_ARGUMENT, "Reentrant UI call");
        state.width=width; state.height=height; state.dpi=dpi;
        return NORTHSTAR_UI_OK;
    });
}
int32_t render(void* ctx, NorthStarUITexture* output) noexcept {
    return protect([&]() -> int32_t {
        if (!ctx || !output || output->size < sizeof(NorthStarUITexture))
            return fail(NORTHSTAR_UI_INVALID_ARGUMENT, "Invalid UI render arguments");
        auto& state = *static_cast<Context*>(ctx);
        if (state.pumping) return fail(NORTHSTAR_UI_INVALID_ARGUMENT, "Reentrant UI call");
        *output = NorthStarUITexture{sizeof(NorthStarUITexture),0,0,
                                      state.width,state.height,0};
        return NORTHSTAR_UI_UNSUPPORTED;
    });
}
#include "native_raster.inc"
#include "native_input.inc"
const NorthStarUIApi api{sizeof(NorthStarUIApi),0,
    create_context,destroy_context,create_element,destroy_element,
    set_property,get_property_json,set_event_callback,dispatch_event,
    pump_events,resize,render};
const NorthStarModule module{sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_UI,"native","1.0.0",
    NORTHSTARCPP_UI_ABI,sizeof(api),&api,last_ui_error};
} // namespace

extern "C" const NorthStarModule* northstar_module_query(
    uint32_t host, const char* capability, uint32_t version) {
    if (const auto* extension=northstarcpp::module_provider::match(
            input_module,host,capability,version)) return extension;
    if (const auto* extension=northstarcpp::module_provider::match(
            raster_module,host,capability,version)) return extension;
    return northstarcpp::module_provider::match(module,host,capability,version);
}
