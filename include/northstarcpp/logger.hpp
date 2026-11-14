// NorthStarCpp engine.logger C++ SDK.
#pragma once
#include <northstarcpp/logger_plugin.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace northstarcpp {

enum class LogLevel : std::uint32_t {
    trace = NORTHSTAR_LOG_TRACE, debug = NORTHSTAR_LOG_DEBUG,
    info = NORTHSTAR_LOG_INFO, warn = NORTHSTAR_LOG_WARN,
    error = NORTHSTAR_LOG_ERROR, fatal = NORTHSTAR_LOG_FATAL
};

struct LoggerOptions {
    std::filesystem::path directory{}; // empty -> local application logs
    std::string session{"NorthStarCpp"};
    LogLevel minimum_level{LogLevel::info};
    std::uint32_t sinks{
        NORTHSTAR_LOG_SINK_CONSOLE |
        NORTHSTAR_LOG_SINK_TEXT |
        NORTHSTAR_LOG_SINK_STRUCTURED |
        NORTHSTAR_LOG_SINK_DEBUGGER
    };
    std::uint64_t rotate_bytes{16ull * 1024 * 1024};
    std::uint32_t backup_count{3};
};

// Parsed application-level logging from bootstrap.json beside the executable.
// The logger ABI and direct Logger(LoggerOptions) usage remain unchanged.
struct ApplicationLoggerConfig {
    LoggerOptions options{};
    bool enabled = true;
    bool console_visible = false; // Never allocate or show a console by default.
};

// Settings are read on startup (not cached); invalid values raise an exception
// so callers can report configuration errors through their fallback channel.
ApplicationLoggerConfig application_logger_configuration(std::string session);

// Only explicitly requested consoles are attached/allocated. An already
// attached parent terminal is never hidden or detached.
void prepare_application_log_console(ApplicationLoggerConfig const& config);

// Windows engine hosts may explicitly redirect all application logs with
// NORTHSTARCPP_LOG_DIRECTORY. The override also enables logging in smoke tests.
// This helper only configures the directory and session; it does not open files.
LoggerOptions application_logger_options(std::string session);
bool application_log_directory_overridden();

// Owns a logger context and retains the DLL for as long as it is needed.
class Logger final {
public:
    explicit Logger(LoggerOptions options = {});
    ~Logger();
    Logger(Logger const&) = delete;
    Logger& operator=(Logger const&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    std::string provider_name() const;
    std::string provider_version() const;
    void write(LogLevel level, std::string_view channel, std::string_view message);
    void trace(std::string_view channel, std::string_view message) {write(LogLevel::trace,channel,message);}
    void debug(std::string_view channel, std::string_view message) {write(LogLevel::debug,channel,message);}
    void info(std::string_view channel, std::string_view message) {write(LogLevel::info,channel,message);}
    void warn(std::string_view channel, std::string_view message) {write(LogLevel::warn,channel,message);}
    void error(std::string_view channel, std::string_view message) {write(LogLevel::error,channel,message);}
    void fatal(std::string_view channel, std::string_view message) {write(LogLevel::fatal,channel,message);}
    void set_minimum_level(LogLevel level);
    void flush();

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace northstarcpp
