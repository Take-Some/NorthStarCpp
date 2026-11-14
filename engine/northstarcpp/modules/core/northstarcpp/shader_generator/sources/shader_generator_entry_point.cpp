////////////////////////////////////////////////////////////////////////////
//	Created		: 18.09.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <stdexcept>
#include <string_view>

namespace fs = std::filesystem;

namespace {

std::set<std::string> options;

bool is_shader(fs::path const& path)
{
	std::string const extension = path.extension().string();
	return extension == ".ps" || extension == ".vs" || extension == ".gs"
		|| extension == ".cs" || extension == ".ds" || extension == ".hs";
}

void read_shader(fs::path const& root, fs::path const& path, total_info_type& files, bool required = true)
{
	std::string const name = path.string();
	auto const [entry, inserted] = files.try_emplace(name);
	if (!inserted)
		return;

	std::ifstream input(path, std::ios::binary);
	if (!input) {
		files.erase(entry);
		if (required)
			throw std::runtime_error("Cannot open shader source: " + name);
		return;
	}

	file_info_type& info = entry->second;
	info.body.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
	if (input.bad())
		throw std::runtime_error("Cannot read shader source: " + name);

	static std::regex const include_pattern(R"rx(^\s*#\s*include\s+"([^"]+)")rx");
	static std::regex const ifdef_pattern(R"(^\s*#\s*(?:ifdef|ifndef)\s+([a-zA-Z_$][a-zA-Z0-9_$]*))");
	static std::regex const if_pattern(R"(^\s*#\s*(?:if|elif)\s+(.+))");
	static std::regex const identifier(R"([a-zA-Z_$][a-zA-Z0-9_$]*)");

	bool in_block_comment = false;
	std::size_t start = 0;
	while (start < info.body.size()) {
		auto const end = info.body.find('\n', start);
		std::string line = info.body.substr(start, end == std::string::npos ? end : end - start);
		start = end == std::string::npos ? info.body.size() : end + 1;
		if (!line.empty() && line.back() == '\r')
			line.pop_back();

		std::string directive;
		bool in_string = false;
		for (std::size_t i = 0; i < line.size(); ++i) {
			if (in_block_comment) {
				if (line.compare(i, 2, "*/") == 0) {
					in_block_comment = false;
					++i;
				}
				continue;
			}
			if (!in_string && line.compare(i, 2, "//") == 0)
				break;
			if (!in_string && line.compare(i, 2, "/*") == 0) {
				in_block_comment = true;
				directive += ' ';
				++i;
				continue;
			}
			if (line[i] == '"')
				in_string = !in_string;
			directive += line[i];
		}

		std::smatch match;
		if (std::regex_search(directive, match, include_pattern)) {
			fs::path const include = (root / fs::path(match[1].str())).lexically_normal().make_preferred();
			read_shader(root, include, files, false);
			if (files.find(include.string()) != files.end())
				info.include_ids.insert(include.string());
		} else if (std::regex_search(directive, match, ifdef_pattern)) {
			info.external_definitions.insert(match[1].str());
		} else if (std::regex_search(directive, match, if_pattern)) {
			std::string const condition = match[1].str();
			for (std::sregex_iterator token(condition.begin(), condition.end(), identifier), end_token;
				token != end_token; ++token) {
				if (token->str() != "defined")
					info.external_definitions.insert(token->str());
			}
		}
	}
}

void collect_definitions(total_info_type const& files, std::string const& name,
	std::set<std::string>& visited, unique_definitions_type& definitions)
{
	if (!visited.insert(name).second)
		return;

	auto const& info = files.at(name);
	definitions.insert(info.personal_unique_definitions.begin(), info.personal_unique_definitions.end());
	for (auto const& include : info.include_ids)
		collect_definitions(files, include, visited, definitions);
}

void resolve_definitions(total_info_type& files)
{
	for (auto& [name, info] : files)
		info.personal_unique_definitions.assign(info.external_definitions.begin(), info.external_definitions.end());

	for (auto& [name, info] : files) {
		std::set<std::string> visited;
		collect_definitions(files, name, visited, info.external_definitions);
		info.unique_definitions.assign(info.external_definitions.begin(), info.external_definitions.end());
	}
}

void print_results(total_info_type const& files, fs::path const& root)
{
	std::cout << "Definitions per file (" << files.size() << "):\n";
	unique_definitions_type all_definitions;
	for (auto const& [name, info] : files) {
		std::cout << fs::path(name).lexically_relative(root).string() << " ("
			<< info.unique_definitions.size() << "):\n";
		for (auto const& definition : info.unique_definitions) {
			std::cout << "\t" << definition << " (";
			std::set<std::string> pending{ name };
			std::set<std::string> visited;
			bool first = true;
			while (!pending.empty()) {
				std::string const dependency = *pending.begin();
				pending.erase(pending.begin());
				if (!visited.insert(dependency).second)
					continue;
				auto const& dependency_info = files.at(dependency);
				if (std::binary_search(dependency_info.personal_unique_definitions.begin(),
					dependency_info.personal_unique_definitions.end(), definition)) {
					std::cout << (first ? " " : ", ") << fs::path(dependency).lexically_relative(root).string();
					first = false;
				}
				pending.insert(dependency_info.include_ids.begin(), dependency_info.include_ids.end());
			}
			std::cout << " )\n";
		}
		all_definitions.insert(info.unique_definitions.begin(), info.unique_definitions.end());
	}
	std::cout << "\nAll the definitions found (" << all_definitions.size() << "):\n";
	for (auto const& definition : all_definitions)
		std::cout << "\t" << definition << '\n';
}

void print_usage(std::ostream& out)
{
	out << "Usage: shader_generator <shader-file-or-directory> <-r1|-r2|-r3|-r4>"
		" [-verbose] [-mp] [-c] [-d] [-cm]\n";
}

} // namespace

bool shader_option_enabled(char const* option)
{
	return options.find(option) != options.end();
}

int main(int argc, char* argv[])
{
	try {
		fs::path input;
		int render_profiles = 0;
		for (int i = 1; i < argc; ++i) {
			std::string_view const arg = argv[i];
			if (arg == "--help" || arg == "-h") {
				print_usage(std::cout);
				return 0;
			}
			if (arg == "-r1" || arg == "-r2" || arg == "-r3" || arg == "-r4") {
				render_profiles += options.emplace(arg).second;
			} else if (arg == "-verbose" || arg == "-mp" || arg == "-c" || arg == "-d" || arg == "-cm") {
				options.emplace(arg);
			} else if (!arg.empty() && arg.front() != '-' && input.empty()) {
				input = argv[i];
			} else {
				std::cerr << "Unexpected argument: " << arg << '\n';
				print_usage(std::cerr);
				return 2;
			}
		}
		if (input.empty() || render_profiles != 1) {
			print_usage(std::cerr);
			return 2;
		}

		input = fs::absolute(input).lexically_normal().make_preferred();
		if (input.has_relative_path() && !input.has_filename())
			input = input.parent_path();
		total_info_type files;
		fs::path root;
		if (fs::is_directory(input)) {
			root = input;
			fs::path const output = root / "objects";
			for (fs::recursive_directory_iterator file(root), end; file != end; ++file) {
				if (file->path() == output && file->is_directory())
					file.disable_recursion_pending();
				if (file->is_regular_file() && is_shader(file->path()))
					read_shader(root, file->path().lexically_normal().make_preferred(), files);
			}
		} else if (fs::is_regular_file(input)) {
			if (!is_shader(input))
				throw std::runtime_error("Unsupported shader extension: " + input.string());
			root = input.parent_path();
			read_shader(root, input, files);
		} else {
			throw std::runtime_error("Shader path not found: " + input.string());
		}
		if (files.empty())
			throw std::runtime_error("No shaders found in: " + input.string());

		resolve_definitions(files);
		if (shader_option_enabled("-verbose"))
			print_results(files, root);
		return compile_shaders(files, root.string().c_str());
	} catch (std::exception const& error) {
		std::cerr << "Shader generator: " << error.what() << '\n';
		return 1;
	}
}
