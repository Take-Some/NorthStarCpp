// NewViso-compatible engine.perlin JSON dispatcher.
// This dispatcher is callable by a native script host; it does not install
// or claim to implement a YSC interpreter.
#pragma once
#include <string>
#include <string_view>
namespace northstarcpp {
std::string call_perlin_service_json(std::string_view service,
                                     std::string_view method,
                                     std::string_view request_json);
}
