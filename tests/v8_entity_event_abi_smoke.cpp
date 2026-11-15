// Actual provider-neutral C++ -> V8 batched entity-event regression.
// Requires a working development V8 provider in this isolated test build.
#include <northstarcpp/scripting.hpp>
#include <iostream>
#include <stdexcept>
#include <string>

static void require(bool ok, char const* explanation)
{
    if (!ok) throw std::runtime_error(explanation);
}

static void install(northstarcpp::ScriptRuntime& runtime)
{
    runtime.evaluate(
        "globalThis.received = [];"
        "engine.on('entity.update', (entity) => received.push(entity.index));"
        "engine.on('northstar._entityBatch', (packet) => {"
        "  for (const entity of packet.items) engine.emit(packet.event, entity);"
        "});void 0;");
}

int main()
{
    try {
        northstarcpp::ScriptRuntime runtime;
        require(runtime.provider_name().find("V8") != std::string::npos,
            "Expected real V8 provider, not a scripting mock");
        install(runtime);
        for (int offset : {0, 256, 512}) {
            std::string payload = "{\"event\":\"entity.update\",\"items\":[";
            for (int i = offset; i < (offset == 512 ? 513 : offset + 256); ++i) {
                if (i != offset) payload.push_back(',');
                payload += "{\"id\":\"" + std::to_string(i + 1000) +
                    "\",\"index\":" + std::to_string(i) + "}";
            }
            payload += "]}";
            runtime.emit_event("northstar._entityBatch", payload);
        }
        require(runtime.evaluate("received.length") == "513",
            "V8 dropped a batched entity event");
        require(runtime.evaluate("received.every((value, index) => value === index)") == "true",
            "V8 reordered or corrupted batched entity events");
        runtime.reset();
        require(runtime.evaluate("typeof received") == "\"undefined\"",
            "Script reset retained stale context state");
        install(runtime);
        runtime.emit_event("northstar._entityBatch",
            "{\"event\":\"entity.update\",\"items\":[{\"id\":\"5\",\"index\":5}]}");
        require(runtime.evaluate("received.join(',')") == "\"5\"",
            "Fresh V8 context did not receive entity events");
        std::cout << "V8 entity event ABI batch: PASS\n";
        return 0;
    } catch (std::exception const& error) {
        std::cerr << "V8 entity event ABI batch: FAIL: " << error.what() << '\n';
        return 1;
    }
}
