// Native V8 provider for NorthStarCpp engine.scripting ABI 2.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
// Requires a V8 monolithic SDK built for the active Windows C++ toolchain.
#include <northstarcpp/scripting_plugin.h>
#include "module_provider.hpp"
#include <libplatform/libplatform.h>
#include <v8.h>
#include <algorithm>
#include <array>
#include <atomic>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <cstdio>
#ifdef NORTHSTARCPP_V8_DIAGNOSTICS
#define NS_V8_TRACE(message) do { std::fprintf(stderr,"[V8] %s\n",message); std::fflush(stderr); } while(false)
#else
#define NS_V8_TRACE(message) do {} while(false)
#endif
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <thread>

namespace {
using northstarcpp::module_provider::guarded;
using northstarcpp::module_provider::last_error;

void require(bool condition, char const* message) {
    if (!condition) throw std::invalid_argument(message);
}

void initialize_v8() {
    static std::once_flag once;
    // V8's default platform owns workers which must not be joined by
    // FreeLibrary/DllMain. Keep this provider loaded for the process lifetime.
    static v8::Platform* platform = nullptr;
    std::call_once(once, [] {
        HMODULE pinned = nullptr;
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&initialize_v8), &pinned))
            throw std::runtime_error("Cannot pin the native V8 provider DLL");
        NS_V8_TRACE("init/icu");
        std::array<char,32768> executable{};
        auto const length=GetModuleFileNameA(nullptr,executable.data(),
                                           static_cast<DWORD>(executable.size()));
        if (!length || length>=executable.size())
            throw std::runtime_error("Unable to determine executable path for V8 ICU");
        v8::V8::InitializeICUDefaultLocation(executable.data());
        NS_V8_TRACE("init/external");
        v8::V8::InitializeExternalStartupData(nullptr);
        NS_V8_TRACE("init/platform/new");
        auto owned_platform = v8::platform::NewDefaultPlatform();
        NS_V8_TRACE("init/platform/install");
        v8::V8::InitializePlatform(owned_platform.get());
        NS_V8_TRACE("init/initialize");
        v8::V8::Initialize();
        platform = owned_platform.release();
        NS_V8_TRACE("init/ready");
    });
}

v8::Local<v8::String> js_string(v8::Isolate* isolate,
                               char const* str, int length) {
    v8::Local<v8::String> value;
    if (!v8::String::NewFromUtf8(isolate, str, v8::NewStringType::kNormal,
                                length).ToLocal(&value))
        throw std::runtime_error("Cannot allocate V8 source string");
    return value;
}

std::string diagnostic(v8::Isolate* isolate, v8::Local<v8::Context> context,
                       v8::TryCatch& caught, char const* origin) {
    v8::String::Utf8Value what(isolate, caught.Exception());
    std::string message = origin && *origin ? origin : "<script>";
    if (auto details = caught.Message(); !details.IsEmpty()) {
        int line = details->GetLineNumber(context).FromMaybe(0);
        if (line > 0) message += ":" + std::to_string(line);
    }
    message += ": ";
    message += *what ? *what : "JavaScript exception";
    return message;
}

// V8 isolates are not pre-emptible from their own JavaScript thread.
// The watchdog calls TerminateExecution from a separate native thread.
struct Deadline {
    std::mutex mutex;
    std::condition_variable ready;
    bool finished = false;
    std::atomic<bool>& expired;
    std::thread worker;
    Deadline(v8::Isolate* isolate, std::uint32_t ms, std::atomic<bool>& has_expired)
        : expired(has_expired) {
        worker = std::thread([this, isolate, ms] {
            std::unique_lock<std::mutex> lock(mutex);
            if (!ready.wait_for(lock, std::chrono::milliseconds(ms),
                                [this] { return finished; })) {
                expired.store(true, std::memory_order_relaxed);
                isolate->TerminateExecution();
            }
        });
    }
    ~Deadline() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            finished = true;
        }
        ready.notify_one();
        if (worker.joinable()) worker.join();
    }
};

void native_dispatch(v8::FunctionCallbackInfo<v8::Value> const& info);

struct Context {
    std::mutex mutex;
    std::uint32_t timeout_ms = 0;
    std::uint32_t max_source_bytes = 0;
    std::unique_ptr<v8::ArrayBuffer::Allocator> allocator;
    v8::Isolate* isolate = nullptr;
    v8::Global<v8::Context> global;
    std::string output{"null"};
    NorthStarScriptHostCall host_call = nullptr;
    void* host_user = nullptr;

    explicit Context(NorthStarScriptingConfig const& config)
        : timeout_ms(config.timeout_ms), max_source_bytes(config.max_source_bytes) {
        NS_V8_TRACE("context/init");
        initialize_v8();
        NS_V8_TRACE("context/allocator");
        allocator.reset(v8::ArrayBuffer::Allocator::NewDefaultAllocator());
        NS_V8_TRACE("context/params");
        v8::Isolate::CreateParams params;
        params.array_buffer_allocator = allocator.get();
        v8::ResourceConstraints constraints;
        constraints.ConfigureDefaultsFromHeapSize(
            0, static_cast<std::size_t>(config.heap_limit_mb) * 1024u * 1024u);
        params.constraints = constraints;
        NS_V8_TRACE("context/isolate/new");
        isolate = v8::Isolate::New(params);
        NS_V8_TRACE("context/isolate/created");
        if (!isolate) throw std::runtime_error("Cannot create V8 isolate");
        NS_V8_TRACE("context/reset");
        try { reset_locked(); }
        catch (...) { isolate->Dispose(); isolate = nullptr; throw; }
    }
    ~Context() {
        // All active evaluation calls must have returned before destroy().
        if (isolate) {
            v8::Locker locker(isolate);
            global.Reset();
            // End the Locker scope before Dispose: no V8 handles may survive.
        }
        if (isolate) isolate->Dispose();
    }
    void reset_locked() {
        NS_V8_TRACE("reset or eval/locker");
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handles(isolate);
        auto context = v8::Context::New(isolate);
        if (context.IsEmpty())
            throw std::runtime_error("Cannot create V8 global context");
        v8::Context::Scope context_scope(context);
        v8::TryCatch caught(isolate);
        v8::Local<v8::Function> callback;
        if (!v8::Function::New(context, native_dispatch,
                v8::External::New(isolate,this)).ToLocal(&callback) ||
            !context->Global()->Set(context,js_string(isolate,"__northstar_native",18),
                                    callback).FromMaybe(false))
            throw std::runtime_error("Cannot install V8 engine API callback");
        static char const bootstrap[] = R"NSJS(
(() => {
    "use strict";
    const native = globalThis.__northstar_native;
    const subscriptions = new Map();
    const exposed = (namespaceName) => new Proxy(Object.create(null), {
        get(_target, method) {
            if (typeof method !== "string" || method === "then") return undefined;
            return (...args) => native(namespaceName, method, args);
        }
    });
    const host = {
        call(namespaceName, method, ...args) {
            return native(namespaceName, method, args);
        },
        api(namespaceName) {
            if (typeof namespaceName !== "string" || !namespaceName.startsWith("engine."))
                throw new TypeError("Expected engine capability namespace");
            return exposed(namespaceName);
        },
        on(name, callback) {
            if (typeof name !== "string" || typeof callback !== "function")
                throw new TypeError("engine.on requires event name and callback");
            let handlers = subscriptions.get(name);
            if (!handlers) subscriptions.set(name, handlers = new Set());
            handlers.add(callback);
            return () => handlers.delete(callback);
        },
        off(name, callback) {
            return subscriptions.get(name)?.delete(callback) ?? false;
        },
        emit(name, payload) {
            let delivered = 0;
            for (const callback of [...(subscriptions.get(name) ?? [])]) {
                callback(payload);
                delivered++;
            }
            return delivered;
        }
    };
    const engine = new Proxy(Object.freeze(host), {
        get(target, property) {
            if (Object.prototype.hasOwnProperty.call(target, property))
                return target[property];
            if (typeof property !== "string" || property === "then")
                return undefined;
            return exposed("engine." + property);
        }
    });
    Object.defineProperty(globalThis,"engine",
        {value:engine, enumerable:false, writable:false, configurable:false});
})();
)NSJS";
        auto source = js_string(isolate,bootstrap,static_cast<int>(sizeof(bootstrap)-1));
        v8::Local<v8::Script> script;
        v8::Local<v8::Value> ignored;
        if (!v8::Script::Compile(context, source).ToLocal(&script) ||
            !script->Run(context).ToLocal(&ignored))
            throw std::runtime_error(diagnostic(isolate,context,caught,"<engine-bootstrap>"));
        global.Reset(isolate, context);
        output = "null";
    }
    void evaluate_locked(char const* source, std::uint32_t length,
                         char const* origin) {
        require(length <= max_source_bytes, "JavaScript source too large");
        require(source || length == 0, "JavaScript source pointer is null");
        if (length && std::memchr(source, 0, length))
            throw std::invalid_argument("Embedded NUL in JavaScript source");
        require(!origin || std::strlen(origin) <= 1024,
                "JavaScript origin exceeds 1024 bytes");

        v8::Locker locker(isolate);
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handles(isolate);
        auto local = global.Get(isolate);
        v8::Context::Scope context_scope(local);
        v8::TryCatch caught(isolate);
        std::string failure;
        std::string json;
        std::atomic<bool> expired{false};
        {
            Deadline deadline(isolate, timeout_ms, expired);
            auto script_text = js_string(isolate, source ? source : "",
                                         static_cast<int>(length));
            auto filename = js_string(isolate, origin ? origin : "<script>",
                                      static_cast<int>(std::strlen(origin ? origin : "<script>")));
            v8::ScriptOrigin location(filename);
            v8::Local<v8::Script> script;
            if (!v8::Script::Compile(local, script_text, &location).ToLocal(&script)) {
                failure = diagnostic(isolate, local, caught, origin);
            } else {
                v8::Local<v8::Value> completion;
                if (!script->Run(local).ToLocal(&completion)) {
                    failure = diagnostic(isolate, local, caught, origin);
                } else if (completion->IsUndefined()) {
                    json = "null";
                } else {
                    v8::Local<v8::String> serialized;
                    if (!v8::JSON::Stringify(local, completion).ToLocal(&serialized)) {
                        failure = diagnostic(isolate, local, caught, origin);
                    } else {
                        v8::String::Utf8Value utf8(isolate, serialized);
                        if (!*utf8) failure = "Could not encode JavaScript result";
                        else json.assign(*utf8, static_cast<std::size_t>(utf8.length()));
                    }
                }
            }
        }
        bool const timed_out = expired.load(std::memory_order_relaxed) ||
                               isolate->IsExecutionTerminating();
        if (timed_out) {
            isolate->CancelTerminateExecution();
            throw std::runtime_error("JavaScript execution timed out");
        }
        if (!failure.empty()) throw std::runtime_error(failure);
        if (json.size() > NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES * 4ull)
            throw std::length_error("Serialized JavaScript result too large");
        output = std::move(json);
    }
};

void native_dispatch(v8::FunctionCallbackInfo<v8::Value> const& info) {
    auto* isolate = info.GetIsolate();
    auto fail = [isolate](std::string const& message) {
        isolate->ThrowException(v8::Exception::Error(
            js_string(isolate,message.data(),static_cast<int>(message.size()))));
    };
    if (!info.Data()->IsExternal() || info.Length()!=3 ||
        !info[0]->IsString() || !info[1]->IsString() || !info[2]->IsArray()) {
        fail("Invalid engine API invocation");
        return;
    }
    auto* state = static_cast<Context*>(info.Data().As<v8::External>()->Value());
    if (!state || !state->host_call) {
        fail("Engine API dispatcher is not installed");
        return;
    }
    auto local = isolate->GetCurrentContext();
    v8::String::Utf8Value capability(isolate,info[0]);
    v8::String::Utf8Value method(isolate,info[1]);
    v8::Local<v8::String> args;
    if (!*capability || !*method ||
        !v8::JSON::Stringify(local,info[2]).ToLocal(&args)) {
        fail("Cannot serialize engine API arguments");
        return;
    }
    v8::String::Utf8Value json_args(isolate,args);
    if (!*json_args || json_args.length() > static_cast<int>(NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES)) {
        fail("Engine API arguments exceed 1 MiB");
        return;
    }
    char const* response = nullptr;
    std::uint32_t response_bytes = 0;
    int const ok = state->host_call(state->host_user,*capability,*method,
        *json_args,static_cast<std::uint32_t>(json_args.length()),
        &response,&response_bytes);
    if (!response || response_bytes > NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES * 4u) {
        fail("Invalid response from native engine API");
        return;
    }
    if (!ok) {
        fail(std::string(response,response_bytes));
        return;
    }
    auto serialized = js_string(isolate,response,static_cast<int>(response_bytes));
    v8::Local<v8::Value> parsed;
    if (!v8::JSON::Parse(local,serialized).ToLocal(&parsed)) {
        fail("Native engine API returned invalid JSON");
        return;
    }
    info.GetReturnValue().Set(parsed);
}

void* create(NorthStarScriptingConfig const* config) {
    Context* context = nullptr;
    guarded([&] {
        require(config && config->size >= sizeof(NorthStarScriptingConfig),
                "Invalid scripting config");
        require(config->heap_limit_mb >= 16 && config->heap_limit_mb <= 512,
                "V8 heap must be 16..512 MB");
        require(config->timeout_ms >= 1 && config->timeout_ms <= 60000,
                "V8 timeout must be 1..60000 ms");
        require(config->max_source_bytes >= 1 &&
                config->max_source_bytes <= NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES,
                "V8 maximum source size must be 1..1048576 bytes");
        context = new Context(*config);
    });
    return context;
}
void destroy(void* context) { delete static_cast<Context*>(context); }
int evaluate(void* raw, char const* source, std::uint32_t length,
             char const* origin, NorthStarScriptResult* result) {
    return guarded([&] {
        require(raw && result && result->size >= sizeof(*result),
                "Missing V8 context or output");
        auto& context = *static_cast<Context*>(raw);
        std::lock_guard<std::mutex> lock(context.mutex);
        context.evaluate_locked(source, length, origin);
        result->json_utf8 = context.output.data();
        result->json_bytes = static_cast<std::uint32_t>(context.output.size());
    });
}
int reset(void* raw) {
    return guarded([&] {
        require(raw, "Missing V8 context");
        auto& context = *static_cast<Context*>(raw);
        std::lock_guard<std::mutex> lock(context.mutex);
        context.reset_locked();
    });
}

int set_host_call(void* raw, NorthStarScriptHostCall callback, void* user) {
    return guarded([&] {
        require(raw, "Missing V8 context");
        auto& context = *static_cast<Context*>(raw);
        std::lock_guard<std::mutex> lock(context.mutex);
        context.host_call = callback;
        context.host_user = user;
    });
}
NorthStarScriptingApi const api{create, destroy, evaluate, reset, set_host_call};
NorthStarModule const module{
    sizeof(NorthStarModule), NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_SCRIPTING, "V8", "2.0.0",
    NORTHSTARCPP_SCRIPTING_ABI, sizeof(api), &api, last_error
};
} // namespace

extern "C" const NorthStarModule* northstar_module_query(
    std::uint32_t host, char const* capability, std::uint32_t version) {
    return northstarcpp::module_provider::match(module, host, capability, version);
}
