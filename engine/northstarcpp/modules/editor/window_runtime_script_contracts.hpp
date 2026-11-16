#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
namespace northstarcpp {
struct UiDocument;
struct CharacterAssetSpec;
namespace runtime_script_contracts {
std::vector<std::string> script_args(std::string_view json);
UiDocument parse_script_ui_document(std::string_view args_json);
std::size_t script_index(std::string const& value);
double script_number(std::string const& value);
CharacterAssetSpec parse_script_character(std::string_view json);
}
}
