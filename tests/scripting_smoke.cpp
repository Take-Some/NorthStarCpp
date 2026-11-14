// engine.scripting: plugin negotiation, C++ SDK and genuine V8 execution smoke.
#include <northstarcpp/scripting.hpp>
#include <northstarcpp/assets.hpp>
#include <northstarcpp/scripting_plugin.h>
#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool condition, char const* reason) {
    if (!condition) throw std::runtime_error(reason);
}
void inspect_module(char const* dll) {
    HMODULE library = LoadLibraryA(dll);
    check(library != nullptr, "Cannot load scripting provider DLL");
    auto query = reinterpret_cast<NorthStarModuleQuery>(
        GetProcAddress(library, "northstar_module_query"));
    check(query != nullptr, "Provider missing module query");
    auto descriptor = query(NORTHSTARCPP_MODULE_ABI,
        NORTHSTARCPP_CAPABILITY_SCRIPTING, NORTHSTARCPP_SCRIPTING_ABI);
    check(descriptor && descriptor->api_size >= sizeof(NorthStarScriptingApi) &&
        descriptor->api_version == NORTHSTARCPP_SCRIPTING_ABI,
        "Invalid scripting provider ABI");
    check(query(NORTHSTARCPP_MODULE_ABI+1, NORTHSTARCPP_CAPABILITY_SCRIPTING,
        NORTHSTARCPP_SCRIPTING_ABI) == nullptr, "Wrong host ABI accepted");
    check(query(NORTHSTARCPP_MODULE_ABI, NORTHSTARCPP_CAPABILITY_SCRIPTING,
        NORTHSTARCPP_SCRIPTING_ABI+1) == nullptr, "Wrong scripting ABI accepted");
    auto api = static_cast<NorthStarScriptingApi const*>(descriptor->api);
    check(api && api->create && api->destroy && api->evaluate && api->reset,
          "Incomplete scripting provider");
    NorthStarScriptingConfig invalid{sizeof(NorthStarScriptingConfig),0,100,1024};
    check(api->create(&invalid) == nullptr, "Invalid scripting limits accepted");
    NorthStarScriptingConfig valid{sizeof(NorthStarScriptingConfig),64,1000,1024};
    void* ctx = api->create(&valid);
    check(ctx != nullptr, "Cannot create C ABI scripting context");
    NorthStarScriptResult result{sizeof(NorthStarScriptResult),nullptr,0};
    check(!api->evaluate(ctx,"ok",2,nullptr,nullptr),
          "Null output accepted");
    api->destroy(ctx);
    FreeLibrary(library);
}
void mock_test(char const* asset_root) {
    northstarcpp::ScriptRuntime a;
    check(a.provider_name() == "Mock Scripting", "Did not load test provider");
    check(a.provider_version() == "2.0.0", "Wrong provider version");
    check(a.evaluate("1+2") == "3", "Mock scripting evaluation failed");
    northstarcpp::AssetManager asset_manager;
    asset_manager.mount_directory("", asset_root);
    check(a.evaluate_asset(asset_manager, "scripting_smoke.js") == "3",
          "JavaScript asset VFS evaluation failed");
    bool rejected_asset = false;
    try { a.evaluate_asset(asset_manager, "missing.ysc"); }
    catch (std::invalid_argument const&) { rejected_asset = true; }
    check(rejected_asset, "Non-JavaScript asset accepted");
    check(a.evaluate("counter++") == "1", "Missing persistent state");
    check(a.evaluate("counter++") == "2", "Counter did not persist");
    northstarcpp::ScriptRuntime b;
    check(b.evaluate("counter++") == "1", "Contexts share global state");
    a.bind("engine.mock","add",[](std::string_view args) {
        if (args != "[4,5]") throw std::runtime_error("Wrong bridge arguments");
        return std::string("9");
    });
    check(a.evaluate("host") == "9", "Native host callback failed");
    a.unbind("engine.mock","add");
    a.reset();
    check(a.evaluate("counter++") == "1", "Reset did not clear context");
    bool threw = false;
    try { a.evaluate("raise"); }
    catch (std::runtime_error const& e) {
        threw = std::string(e.what()).find("Mock script exception") != std::string::npos;
    }
    check(threw, "Provider error missing from C++ exception");
    threw = false;
    try { a.evaluate(std::string(NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES + 1,'x')); }
    catch (std::invalid_argument const&) { threw = true; }
    check(threw, "Oversized source accepted");
}
void test_service_override(bool v8) {
    northstarcpp::ScriptRuntime runtime;
    runtime.bind("engine.scene", "objectCount", [](std::string_view) { return "7"; });
    runtime.bind("engine.scene", "original", [](std::string_view) { return "11"; });
    int fallback_calls = 0;
    runtime.bind("engine.scene", "fail", [&](std::string_view) {
        ++fallback_calls;
        return "null";
    });
    auto eval = [&](char const* ns, char const* method) {
        auto const capability = std::string("engine.") + ns;
        if (v8)
            return runtime.evaluate(capability + "." + method + "()");
        return runtime.evaluate("call " + capability + " " + method);
    };
    check(eval("scene", "objectCount") == "73", "Service did not replace the builtin method");
    check(eval("scene", "original") == "11", "Unclaimed method lost its builtin implementation");
    check(eval("fixture", "answer") == "42", "Plugin-only namespace could not be called");
    bool rejected = false;
    try { eval("scene", "fail"); }
    catch (std::runtime_error const& e) {
        rejected = std::string(e.what()).find("Fixture service error") != std::string::npos;
    }
    check(rejected && fallback_calls == 0, "Provider failure must propagate without a retry");
    rejected = false;
    try { eval("scene", "crossNamespace"); }
    catch (std::runtime_error const& e) {
        rejected = std::string(e.what()).find("scoped to its namespace") != std::string::npos;
    }
    check(rejected, "Provider fallback crossed capability boundaries");
    runtime.reset();
    check(eval("scene", "objectCount") == "73", "Provider did not survive runtime reset");
}

void v8_test(char const* asset_root) {
    northstarcpp::ScriptingOptions options;
    options.timeout_ms = 200;
    northstarcpp::ScriptRuntime js(options);
    check(js.provider_name() == "V8", "Not a real V8 provider");
    check(js.evaluate("1 + 2") == "3", "V8 arithmetic failed");
    northstarcpp::AssetManager asset_manager;
    asset_manager.mount_directory("", asset_root);
    check(js.evaluate_asset(asset_manager, "scripting_smoke.js") == "3",
          "V8 asset VFS evaluation failed");
    js.bind("engine.mock","add",[](std::string_view args) {
        if (args != "[4,5]") throw std::runtime_error("Wrong V8 bridge args");
        return std::string("9");
    });
    check(js.evaluate("engine.mock.add(4,5)") == "9",
          "V8 proxy-to-host binding failed");
    check(js.evaluate("engine.call('engine.mock','add',4,5)") == "9",
          "V8 direct host call failed");
    check(js.evaluate("typeof engine.physics.applyImpulse") == "\"function\"",
          "V8 engine namespaces are not dynamically visible");
    bool binding_threw = false;
    try { js.evaluate("engine.physics.applyImpulse(1,2,3)"); }
    catch (std::runtime_error const& e) {
        binding_threw = std::string(e.what()).find("not registered") != std::string::npos;
    }
    check(binding_threw, "Unregistered native API method was accessible");
    check(js.evaluate("globalThis.totalDt=0; engine.on('update',dt=>totalDt+=dt); true") == "true",
          "Script event subscription failed");
    check(js.emit_event("update","0.25") == "1", "Host event not delivered");
    check(js.evaluate("totalDt") == "0.25", "Engine event payload missing");
    js.unbind("engine.mock","add");
    binding_threw = false;
    try { js.evaluate("engine.mock.add(4,5)"); }
    catch (std::runtime_error const&) { binding_threw=true; }
    check(binding_threw, "Revoked native API binding remained accessible");
    check(js.evaluate("({engine: 'NorthStarCpp', value: [1,2,3]})") ==
          "{\"engine\":\"NorthStarCpp\",\"value\":[1,2,3]}",
          "V8 JSON result failed");
    check(js.evaluate("let counter = 7; counter") == "7",
          "V8 global creation failed");
    check(js.evaluate("counter += 5") == "12", "V8 state did not persist");
    bool threw = false;
    try { js.evaluate("throw new Error('example failure')", "example.js"); }
    catch (std::runtime_error const& e) {
        auto what = std::string(e.what());
        threw = what.find("example failure") != std::string::npos &&
                what.find("example.js") != std::string::npos;
    }
    check(threw, "V8 exception lacks origin or message");
    // A normal JS exception must not discard the persistent context; this is
    // especially important for process-isolated V8 workers.
    check(js.evaluate("counter") == "12",
          "V8 lost the JavaScript context after an ordinary exception");
    threw = false;
    try { js.evaluate("while(true) {}", "loop.js"); }
    catch (std::runtime_error const& e) {
        threw = std::string(e.what()).find("timed out") != std::string::npos;
    }
    check(threw, "Infinite JS loop did not time out");
    js.reset();
    check(js.evaluate("typeof counter") == "\"undefined\"",
          "V8 reset failed");
    check(js.evaluate("'Привет, V8'") == "\"Привет, V8\"",
          "V8 UTF-8 roundtrip failed");
}
}
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    try {
        std::string mode = argv[1];
        if (mode == "reject" && argc == 2) {
            try { northstarcpp::ScriptRuntime js; }
            catch (std::runtime_error const& e) {
                if (std::string(e.what()).find("engine.scripting") != std::string::npos)
                    return 0;
            }
            throw std::runtime_error("Wrong provider was not rejected");
        }
        if (mode == "service" || mode == "service-v8") {
            if (argc != 2) return 2;
            test_service_override(mode == "service-v8");
            return 0;
        }
        if (argc != 4) return 2;
        inspect_module(argv[2]);
        if (mode == "mock") mock_test(argv[3]);
        else if (mode == "v8") v8_test(argv[3]);
        else return 2;
        std::cout << "engine.scripting " << mode << " PASS\n";
        return 0;
    } catch (std::exception const& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
