// NorthStarCpp logger capability host bridge. No file formatting lives in the host.
#include <northstarcpp/logger.hpp>
#include "module_loader.hpp"
#include <limits>
#include <stdexcept>
#include <array>
#include <utility>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace northstarcpp {

namespace {
namespace fs = std::filesystem;

fs::path host_directory() {
    std::array<wchar_t,32768> path{};
    DWORD const count=GetModuleFileNameW(nullptr,path.data(),
                                         static_cast<DWORD>(path.size()));
    if(!count || count>=path.size())
        throw std::runtime_error("Cannot locate NorthStarCpp executable directory");
    return fs::path(path.data()).parent_path();
}

LogLevel parse_log_level(std::string text) {
    std::transform(text.begin(),text.end(),text.begin(),
        [](unsigned char c){return static_cast<char>(std::toupper(c));});
    if(text=="TRACE") return LogLevel::trace;
    if(text=="DEBUG") return LogLevel::debug;
    if(text=="INFO") return LogLevel::info;
    if(text=="WARN" || text=="WARNING") return LogLevel::warn;
    if(text=="ERROR") return LogLevel::error;
    if(text=="FATAL") return LogLevel::fatal;
    throw std::invalid_argument("bootstrap.json logging.level must be TRACE, DEBUG, INFO, WARN, ERROR or FATAL");
}

bool strict_bool(boost::property_tree::ptree const& tree,
                 char const* key,bool fallback) {
    auto const raw=tree.get_optional<std::string>(key);
    if(!raw)return fallback;
    std::string value=*raw;
    std::transform(value.begin(),value.end(),value.begin(),
        [](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(value=="true" || value=="1")return true;
    if(value=="false" || value=="0")return false;
    throw std::invalid_argument(std::string("bootstrap.json logging.")+key+
                                " must be true or false");
}

std::uint64_t decimal_uint(boost::property_tree::ptree const& tree,
                           char const* key,std::uint64_t fallback) {
    auto const raw=tree.get_optional<std::string>(key);
    if(!raw)return fallback;
    if(raw->empty() || !std::all_of(raw->begin(),raw->end(),
        [](unsigned char c){return c>='0' && c<='9';}))
        throw std::invalid_argument(std::string("bootstrap.json logging.")+
            key+" must be a non-negative decimal integer");
    std::size_t parsed=0;
    auto const n=std::stoull(*raw,&parsed,10);
    if(parsed!=raw->size())
        throw std::invalid_argument(std::string("bootstrap.json logging.")+
            key+" has an invalid number");
    return n;
}

void check_configuration(ApplicationLoggerConfig const& value) {
    if(value.options.rotate_bytes>1024ull*1024ull*1024ull)
        throw std::invalid_argument("bootstrap.json logging.rotation.maxBytes must be <= 1 GiB");
    if(value.options.backup_count>16)
        throw std::invalid_argument("bootstrap.json logging.rotation.backups must be <= 16");
}

} // namespace

bool application_log_directory_overridden() {
    return GetEnvironmentVariableW(L"NORTHSTARCPP_LOG_DIRECTORY",nullptr,0)>0;
}

ApplicationLoggerConfig application_logger_configuration(std::string session) {
    ApplicationLoggerConfig result;
    result.options.session=std::move(session);
    // Logging to a file is enabled by default, while no extra console window
    // and no console sink are enabled until explicitly requested.
    result.options.sinks=NORTHSTAR_LOG_SINK_TEXT |
                          NORTHSTAR_LOG_SINK_STRUCTURED |
                          NORTHSTAR_LOG_SINK_DEBUGGER;
    auto const executable=host_directory();
    auto const bootstrap=executable/"bootstrap.json";
    std::error_code ec;
    if(fs::exists(bootstrap,ec)) {
        if(ec || !fs::is_regular_file(bootstrap,ec) ||
           fs::file_size(bootstrap,ec)>65536 || ec)
            throw std::runtime_error("Cannot read bootstrap.json for logging");
        boost::property_tree::ptree tree;
        try {
            std::ifstream stream(bootstrap,std::ios::binary);
            if(!stream) throw std::runtime_error("Unable to open bootstrap.json");
            boost::property_tree::read_json(stream,tree);
        } catch (std::exception const& error) {
            throw std::runtime_error(
                std::string("Invalid bootstrap.json logging configuration: ")+error.what());
        }
        if(auto const logging=tree.get_child_optional("logging")) {
            try {
                result.enabled=strict_bool(*logging,"enabled",true);
                result.console_visible=strict_bool(*logging,"console.visible",false);
                result.options.minimum_level=parse_log_level(
                    logging->get<std::string>("level","INFO"));
                auto const dir=logging->get<std::string>("directory","");
                if(!dir.empty()) {
                    auto const path=fs::u8path(dir);
                    result.options.directory=(path.is_absolute()
                        ? path : executable/path).lexically_normal();
                }
                result.options.rotate_bytes=decimal_uint(*logging,
                    "rotation.maxBytes",16ull*1024ull*1024ull);
                auto const backups=decimal_uint(*logging,"rotation.backups",3);
                if(backups>16) throw std::invalid_argument(
                    "bootstrap.json logging.rotation.backups must be <= 16");
                result.options.backup_count=static_cast<std::uint32_t>(backups);
                result.options.sinks=0;
                if(strict_bool(*logging,"sinks.text",true))
                    result.options.sinks|=NORTHSTAR_LOG_SINK_TEXT;
                if(strict_bool(*logging,"sinks.structured",true))
                    result.options.sinks|=NORTHSTAR_LOG_SINK_STRUCTURED;
                if(strict_bool(*logging,"sinks.debugger",true))
                    result.options.sinks|=NORTHSTAR_LOG_SINK_DEBUGGER;
                if(strict_bool(*logging,"sinks.console",result.console_visible))
                    result.options.sinks|=NORTHSTAR_LOG_SINK_CONSOLE;
                // The provider requires at least one sink, unless logging is disabled.
                if(result.enabled && !result.options.sinks)
                    throw std::invalid_argument("bootstrap.json logging.sinks requires at least one enabled output");
                check_configuration(result);
            } catch(std::exception const& error) {
                throw std::runtime_error(std::string("Invalid bootstrap.json logging: ")+error.what());
            }
        }
    } else if(ec) {
        throw std::runtime_error("Cannot inspect bootstrap.json for logging: "+ec.message());
    }

    // Explicit application/test override always wins over bootstrap.json.
    std::array<wchar_t,32768> env{};
    auto const len=GetEnvironmentVariableW(L"NORTHSTARCPP_LOG_DIRECTORY",
                                            env.data(),static_cast<DWORD>(env.size()));
    if(len>=env.size())
        throw std::invalid_argument("NorthStarCpp logger directory override is too long");
    if(len>0)
        result.options.directory=fs::path(env.data());
    return result;
}

LoggerOptions application_logger_options(std::string session) {
    return application_logger_configuration(std::move(session)).options;
}

void prepare_application_log_console(ApplicationLoggerConfig const& config) {
    if(!config.enabled || !config.console_visible) return;
    // Win32 GUI programs do not own a console. Never hide or detach a caller's
    // existing console, even when logging.console.visible is false.
    if(GetConsoleWindow()) return;
    if(!AllocConsole())
        throw std::runtime_error("Cannot allocate NorthStarCpp logging console");
    (void)freopen("CONOUT$","w",stderr);
    (void)freopen("CONOUT$","w",stdout);
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleTitleW(L"NorthStarCpp | engine.logger");
}

struct Logger::State {
    Module module{NORTHSTARCPP_CAPABILITY_LOGGER};
    NorthStarLoggerApi const& api = *module.query<NorthStarLoggerApi>(NORTHSTARCPP_LOGGER_ABI);
    void* context = nullptr;

    explicit State(LoggerOptions const& options) {

        auto const directory = options.directory.u8string();
        NorthStarLoggerConfig config{
            sizeof(NorthStarLoggerConfig),
            directory.c_str(), options.session.c_str(),
            options.sinks, static_cast<uint32_t>(options.minimum_level),
            options.rotate_bytes, options.backup_count
        };
        context = api.create(&config);
        if (!context)
            throw module.error("Cannot create logger");
    }

    ~State() noexcept {
        if (context) {
            api.flush(context);
            api.destroy(context);
        }
    }
};

Logger::Logger(LoggerOptions options) : state_(std::make_unique<State>(options)) {}
Logger::~Logger() = default;

std::string Logger::provider_name() const {return state_->module.provider();}
std::string Logger::provider_version() const {return state_->module.version();}

void Logger::write(LogLevel level, std::string_view channel, std::string_view message) {
    constexpr std::size_t max_message = 64u * 1024u;
    if (static_cast<uint32_t>(level)>NORTHSTAR_LOG_FATAL || channel.size()>128u ||
        message.size()>max_message)
        throw std::invalid_argument("Invalid logger level/channel/message size");
    NorthStarLogRecord record{sizeof(NorthStarLogRecord),
        static_cast<uint32_t>(level), channel.data(),
        static_cast<uint32_t>(channel.size()),message.data(),
        static_cast<uint32_t>(message.size())};
    if (!state_->api.write(state_->context,&record))
        throw state_->module.error("Cannot write log record");
}

void Logger::set_minimum_level(LogLevel level) {
    if (static_cast<uint32_t>(level)>NORTHSTAR_LOG_FATAL)
        throw std::invalid_argument("Invalid logger minimum level");
    if (!state_->api.set_minimum_level(state_->context,static_cast<uint32_t>(level)))
        throw state_->module.error("Cannot change logger minimum level");
}

void Logger::flush() {
    if (!state_->api.flush(state_->context))
        throw state_->module.error("Cannot flush logger");
}

} // namespace northstarcpp
