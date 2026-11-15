// NorthStarCpp engine.ui provider contract, ABI 1.
// Backend-neutral C ABI: works across builtin and external module boundaries.
#pragma once
#include <northstarcpp/module.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NORTHSTARCPP_UI_ABI 1u
#define NORTHSTAR_UI_INVALID_HANDLE UINT64_C(0)

// No pointers or C++ ownership types cross the plugin boundary.
// All structures are size-tagged to permit append-only extensions.
typedef uint64_t NorthStarUIHandle;

typedef enum NorthStarUIStatus {
    NORTHSTAR_UI_OK = 0,
    NORTHSTAR_UI_INVALID_ARGUMENT = 1,
    NORTHSTAR_UI_UNSUPPORTED = 2,
    NORTHSTAR_UI_NOT_FOUND = 3,
    NORTHSTAR_UI_BACKEND_FAILURE = 4
} NorthStarUIStatus;

typedef enum NorthStarUIElementKind {
    NORTHSTAR_UI_ROOT = 1,
    NORTHSTAR_UI_PANEL = 2,
    NORTHSTAR_UI_TEXT = 3,
    NORTHSTAR_UI_BUTTON = 4,
    NORTHSTAR_UI_IMAGE = 5,
    NORTHSTAR_UI_INPUT = 6,
    NORTHSTAR_UI_CUSTOM = 100
} NorthStarUIElementKind;

typedef enum NorthStarUIValueKind {
    NORTHSTAR_UI_VALUE_BOOL = 1,
    NORTHSTAR_UI_VALUE_NUMBER = 2,
    NORTHSTAR_UI_VALUE_STRING = 3,
    NORTHSTAR_UI_VALUE_COLOR_RGBA8 = 4
} NorthStarUIValueKind;

typedef enum NorthStarUIEventKind {
    NORTHSTAR_UI_EVENT_CLICK = 1,
    NORTHSTAR_UI_EVENT_CHANGE = 2,
    NORTHSTAR_UI_EVENT_FOCUS = 3,
    NORTHSTAR_UI_EVENT_BLUR = 4,
    NORTHSTAR_UI_EVENT_KEY = 5,
    NORTHSTAR_UI_EVENT_POINTER = 6,
    NORTHSTAR_UI_EVENT_CUSTOM = 100
} NorthStarUIEventKind;

typedef enum NorthStarUIBackendFeatures {
    NORTHSTAR_UI_FEATURE_DESKTOP = 1u << 0,
    NORTHSTAR_UI_FEATURE_OFFSCREEN = 1u << 1,
    NORTHSTAR_UI_FEATURE_WORLD_SPACE = 1u << 2,
    NORTHSTAR_UI_FEATURE_ACCESSIBILITY = 1u << 3,
    NORTHSTAR_UI_FEATURE_GPU_TEXTURE = 1u << 4
} NorthStarUIBackendFeatures;

// Byte strings are UTF-8, with explicit length, and are borrowed for the call.
typedef struct NorthStarUIString {
    const char* data;
    uint64_t length;
} NorthStarUIString;

typedef struct NorthStarUIConfig {
    uint32_t size;
    uint32_t flags;       // reserved, must be zero in ABI 1
    void* host_context;   // opaque, borrowed for the lifetime of the UI context
} NorthStarUIConfig;

typedef struct NorthStarUIElement {
    uint32_t size;
    uint32_t kind;                // NorthStarUIElementKind
    NorthStarUIHandle parent;     // zero for a root
    NorthStarUIString name;       // stable logical identifier within a context
    uint32_t flags;               // reserved, must be zero in ABI 1
    uint32_t reserved;
} NorthStarUIElement;

typedef struct NorthStarUIValue {
    uint32_t size;
    uint32_t kind;                // NorthStarUIValueKind
    double number;                // numeric values
    uint32_t rgba8;               // packed 0xRRGGBBAA
    uint32_t boolean;             // 0 or 1
    NorthStarUIString string;     // UTF-8, only for string values
} NorthStarUIValue;

typedef struct NorthStarUIEvent {
    uint32_t size;
    uint32_t kind;               // NorthStarUIEventKind
    NorthStarUIHandle target;
    NorthStarUIString name;       // e.g. action name for CUSTOM
    NorthStarUIValue value;
} NorthStarUIEvent;

typedef void (*NorthStarUIEventCallback)(
    void* user_data, const NorthStarUIEvent* event);

// Optional provider-owned output texture; valid only until the next provider
// render/resize/destroy operation. Its backend-specific handle is opaque.
typedef struct NorthStarUITexture {
    uint32_t size;
    uint32_t backend;            // provider-defined; 0 means no GPU texture
    uint64_t resource;
    uint32_t width;
    uint32_t height;
    uint32_t reserved;
} NorthStarUITexture;

// Provider must keep API table, function pointers and module metadata alive.
// All functions return NorthStarUIStatus; errors must not throw across C ABI.
// Calls are serialized on provider's UI thread unless provider documents more.
// Callbacks execute synchronously during pump_events only. Do not reenter API.
typedef struct NorthStarUIApi {
    uint32_t size;                // sizeof(NorthStarUIApi)
    uint32_t features;            // bitmask of NorthStarUIBackendFeatures

    int32_t (*create_context)(const NorthStarUIConfig*, void** context);
    int32_t (*destroy_context)(void* context);
    int32_t (*create_element)(void* context, const NorthStarUIElement*,
                              NorthStarUIHandle* out_handle);
    int32_t (*destroy_element)(void* context, NorthStarUIHandle handle);
    int32_t (*set_property)(void* context, NorthStarUIHandle handle,
                            NorthStarUIString key, const NorthStarUIValue* value);
    // Copies a property into a caller-owned UTF-8 buffer; supports querying
    // required bytes by passing output=NULL and capacity=0.
    int32_t (*get_property_json)(void* context, NorthStarUIHandle handle,
                                 NorthStarUIString key, char* output,
                                 uint64_t capacity, uint64_t* required_bytes);
    int32_t (*set_event_callback)(void* context, NorthStarUIEventCallback,
                                  void* user_data);
    int32_t (*dispatch_event)(void* context, const NorthStarUIEvent*);
    int32_t (*pump_events)(void* context);
    int32_t (*resize)(void* context, uint32_t width, uint32_t height,
                      float dpi_scale);
    int32_t (*render)(void* context, NorthStarUITexture* output);
} NorthStarUIApi;

// ABI 1 remains unchanged. Optional ABI 2 adds software raster and hit testing.
#define NORTHSTARCPP_UI_RASTER_ABI 2u
typedef struct NorthStarUIRect {
    float x, y, width, height; // physical pixels, top-left origin
} NorthStarUIRect;
typedef struct NorthStarUIRasterApi {
    uint32_t size;
    uint32_t flags; // zero for ABI 2
    int32_t (*draw_rgba)(void* context, uint8_t* pixels, uint32_t stride,
                         uint32_t width, uint32_t height);
    int32_t (*get_rect)(void* context, NorthStarUIHandle handle,
                         NorthStarUIRect* out);
    // phase: 0 up, 1 down, 2 move. Coordinates are physical pixels.
    int32_t (*pointer)(void* context, float x, float y, uint32_t phase);
} NorthStarUIRasterApi;

// Optional ABI 3: text input, IME composition, wheel and keyboard focus.
// ABI 1 and raster ABI 2 retain their exact original layouts.
#define NORTHSTARCPP_UI_INPUT_ABI 3u
typedef struct NorthStarUIInputApi {
    uint32_t size;
    uint32_t flags; // zero
    // UTF-8 text comes from the OS input method; key is platform-independent
    // ASCII/control code (8=Backspace, 9=Tab, 13=Enter). A key-up has down=0.
    int32_t (*keyboard)(void* context,uint32_t key,uint32_t down,
                        NorthStarUIString utf8_text);
    // commit=0 updates preedit only; commit=1 appends committed UTF-8 text.
    int32_t (*composition)(void* context,NorthStarUIString utf8_text,uint32_t commit);
    // Scroll the nearest hovered scrollable parent in pixel units.
    int32_t (*wheel)(void* context,float delta_x,float delta_y);
    // Returns focused input handle or zero.
    int32_t (*focused)(void* context,NorthStarUIHandle* out);
} NorthStarUIInputApi;

#ifdef __cplusplus
}
#endif
