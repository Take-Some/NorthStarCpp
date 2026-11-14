// NorthStarCpp MultiSink logger provider.
// Copyright (c) 2026 Kayla's Systems - Kayla Verner.
// Thread-safe, synchronous UTF-8 .log and JSON Lines .ulog files, rotating by size.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <northstarcpp/logger_plugin.h>
#include "module_provider.hpp"
#include "console_format.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace {

namespace fs = std::filesystem;
using namespace northstarcpp::module_provider;

constexpr std::uint32_t all_sinks =
    NORTHSTAR_LOG_SINK_CONSOLE | NORTHSTAR_LOG_SINK_TEXT |
    NORTHSTAR_LOG_SINK_STRUCTURED | NORTHSTAR_LOG_SINK_DEBUGGER;
constexpr std::uint64_t max_rotation = 1024ull*1024ull*1024ull;

void require(bool ok, char const* message) {
    if (!ok) throw std::invalid_argument(message);
}

bool utf8_valid(std::string_view s) {
    if (s.empty()) return true;
    return s.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
        MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),
                           static_cast<int>(s.size()),nullptr,0)>0;
}

std::wstring wide(std::string_view utf8) {
    if (utf8.empty()) return {};
    int const len=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,
        utf8.data(),static_cast<int>(utf8.size()),nullptr,0);
    if (len<=0) throw std::invalid_argument("Invalid UTF-8 logger data");
    std::wstring result(static_cast<std::size_t>(len),L'\0');
    if (MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.data(),
        static_cast<int>(utf8.size()),result.data(),len)!=len)
        throw std::runtime_error("Cannot convert logger UTF-8");
    return result;
}

fs::path default_directory() {
    std::array<wchar_t,32768> buffer{};
    DWORD count=GetEnvironmentVariableW(L"LOCALAPPDATA",buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if(!count || count>=buffer.size()) {
        count=GetTempPathW(static_cast<DWORD>(buffer.size()),buffer.data());
        if(!count || count>=buffer.size())
            throw std::runtime_error("Cannot locate local log directory");
    }
    return fs::path(buffer.data())/L"KaylasSystems"/L"NorthStarCpp"/L"Logs";
}

bool valid_session(std::string_view session) {
    if(session.empty() || session.size()>64 || session=="." || session=="..")
        return false;
    for(unsigned char ch:session)
        if(!((ch>='a' && ch<='z') || (ch>='A' && ch<='Z') ||
             (ch>='0' && ch<='9') || ch=='_' || ch=='-' || ch=='.'))
            return false;
    return true;
}

char const* level_name(std::uint32_t level) {
    static constexpr char const* names[] = {"TRACE","DEBUG","INFO","WARN","ERROR","FATAL"};
    return level<=NORTHSTAR_LOG_FATAL ? names[level] : "UNKNOWN";
}

std::string utc_timestamp() {
    SYSTEMTIME utc{};
    GetSystemTime(&utc);
    char result[40]{};
    std::snprintf(result,sizeof(result),"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        utc.wYear,utc.wMonth,utc.wDay,utc.wHour,utc.wMinute,
        utc.wSecond,utc.wMilliseconds);
    return result;
}

std::string escape_json(std::string_view value) {
    static constexpr char hex[]="0123456789abcdef";
    std::string escaped;
    escaped.reserve(value.size()+16);
    for(unsigned char ch:value) {
        switch(ch) {
        case '"': escaped+="\\\""; break;
        case '\\': escaped+="\\\\"; break;
        case '\b': escaped+="\\b"; break;
        case '\f': escaped+="\\f"; break;
        case '\n': escaped+="\\n"; break;
        case '\r': escaped+="\\r"; break;
        case '\t': escaped+="\\t"; break;
        default:
            if(ch<0x20) {
                escaped+="\\u00";
                escaped.push_back(hex[ch>>4]);
                escaped.push_back(hex[ch&15]);
            } else escaped.push_back(static_cast<char>(ch));
        }
    }
    return escaped;
}

std::string text_record(std::string const& stamp, std::uint32_t level,
                        std::uint32_t pid,std::uint32_t tid,
                        std::string_view channel,std::string_view message) {
    std::string prefix=stamp+" ["+level_name(level)+"] ["+
       std::string(channel)+"] [pid:"+std::to_string(pid)+
       " tid:"+std::to_string(tid)+"] ";
    // Every physical line keeps timestamp, severity and channel. No ANSI
    // formatting or embedded NUL/CR control characters in the .log file.
    std::string result;
    result.reserve(message.size()+prefix.size()*2);
    std::size_t position=0;
    while(true) {
        auto const end=message.find_first_of("\r\n",position);
        result+=prefix;
        for(auto i=position;i<(end==std::string_view::npos ? message.size():end);++i) {
            auto const ch=message[i];
            if(ch=='\0')result+="\\0";
            else if(static_cast<unsigned char>(ch)<0x20 && ch!='\t')
                result+='?';
            else result+=ch;
        }
        result+="\n";
        if(end==std::string_view::npos) break;
        position=end+1;
        if(message[end]=='\r' && position<message.size() && message[position]=='\n')
            ++position;
        if(position==message.size())break;
    }
    return result;
}

std::string structured_record(std::string const& stamp, std::uint32_t level,
    std::uint32_t pid,std::uint32_t tid,
    std::string_view channel,std::string_view message) {
    return "{\"timestamp_utc\":\""+stamp+"\",\"level\":\""+
        level_name(level)+"\",\"channel\":\""+escape_json(channel)+
        "\",\"message\":\""+escape_json(message)+"\",\"pid\":"+
        std::to_string(pid)+",\"tid\":"+std::to_string(tid)+"}\n";
}

// Detect actual console capabilities. Redirected stderr is plain UTF-8
// unless FORCE_COLOR=1 / CLICOLOR_FORCE=1 is deliberately enabled.
// NO_COLOR and CLICOLOR=0 always win, including over forced color.
bool env_present(wchar_t const* name) noexcept {
    wchar_t value[2]{};
    DWORD const count=GetEnvironmentVariableW(name,value,2);
    return count>0;
}
bool env_nonzero(wchar_t const* name) noexcept {
    wchar_t value[64]{};
    DWORD const count=GetEnvironmentVariableW(name,value,64);
    return count>0 && count<64 && !(count==1 && value[0]==L'0');
}
bool disable_color() noexcept {
    if(env_present(L"NO_COLOR"))return true;
    wchar_t value[4]{};
    DWORD const count=GetEnvironmentVariableW(L"CLICOLOR",value,4);
    return count==1 && value[0]==L'0';
}

enum class ConsoleColorMode { plain, ansi, legacy };

ConsoleColorMode console_color_mode(HANDLE handle) noexcept {
    if(disable_color())return ConsoleColorMode::plain;
    bool const force=env_nonzero(L"FORCE_COLOR")||
                     env_nonzero(L"CLICOLOR_FORCE");
    DWORD mode{};
    if(!GetConsoleMode(handle,&mode))
        return force ? ConsoleColorMode::ansi : ConsoleColorMode::plain;
    if(SetConsoleMode(handle,mode|ENABLE_VIRTUAL_TERMINAL_PROCESSING))
        return ConsoleColorMode::ansi;
    return ConsoleColorMode::legacy; // Win32 console attributes (pre-VT hosts)
}

WORD attribute_for(northstarcpp::logger_console::Style style,
                   std::uint32_t level,WORD original) {
    constexpr WORD intense=FOREGROUND_INTENSITY;
    constexpr WORD red=FOREGROUND_RED;
    constexpr WORD green=FOREGROUND_GREEN;
    constexpr WORD blue=FOREGROUND_BLUE;
    constexpr WORD base=red|green|blue;
    switch(style) {
    case northstarcpp::logger_console::Style::timestamp: return green|blue|intense;
    case northstarcpp::logger_console::Style::brackets:  return base;
    case northstarcpp::logger_console::Style::channel:   return red|blue|intense;
    case northstarcpp::logger_console::Style::metadata:  return blue|green;
    case northstarcpp::logger_console::Style::message:   return base|intense;
    case northstarcpp::logger_console::Style::severity:
        switch(level) {
        case NORTHSTAR_LOG_TRACE: return base;
        case NORTHSTAR_LOG_DEBUG: return blue|intense;
        case NORTHSTAR_LOG_INFO: return green|intense;
        case NORTHSTAR_LOG_WARN: return red|green|intense;
        case NORTHSTAR_LOG_ERROR: return red|intense;
        case NORTHSTAR_LOG_FATAL: return BACKGROUND_RED|base|intense;
        }
    }
    return original;
}

void output_legacy(HANDLE handle,
    std::vector<northstarcpp::logger_console::Segment> const& parts,
    std::uint32_t level) noexcept {
    CONSOLE_SCREEN_BUFFER_INFO screen{};
    if(!GetConsoleScreenBufferInfo(handle,&screen)) {
        return;
    }
    WORD const initial=screen.wAttributes;
    for(auto const& part:parts) {
        if(!SetConsoleTextAttribute(handle,attribute_for(part.style,level,initial)))
            break;
        try {
            auto const w=wide(part.text);
            DWORD done=0;
            if(!WriteConsoleW(handle,w.data(),
                              static_cast<DWORD>(w.size()),&done,nullptr))
                break;
        } catch(...) {break;}
    }
    SetConsoleTextAttribute(handle,initial);
}

void output_utf8(std::string const& value, bool debugger) noexcept {
    try {
        auto const w=wide(value);
        if(debugger) {
            OutputDebugStringW(w.c_str());
            return;
        }
        HANDLE const handle=GetStdHandle(STD_ERROR_HANDLE);
        if(handle==nullptr || handle==INVALID_HANDLE_VALUE) return;
        DWORD console_mode=0,written=0;
        if(GetConsoleMode(handle,&console_mode))
            WriteConsoleW(handle,w.data(),static_cast<DWORD>(w.size()),&written,nullptr);
        else
            WriteFile(handle,value.data(),static_cast<DWORD>(value.size()),&written,nullptr);
    } catch (...) {
        // No console/debugger errors may prevent file logging.
    }
}

void output_console(std::string const& text,
    std::string const& timestamp,std::uint32_t level,
    std::uint32_t pid,std::uint32_t tid,
    std::string_view channel,std::string_view message) noexcept {
    HANDLE const handle=GetStdHandle(STD_ERROR_HANDLE);
    if(handle==nullptr || handle==INVALID_HANDLE_VALUE)return;
    auto const mode=console_color_mode(handle);
    if(mode==ConsoleColorMode::plain) {
        output_utf8(text,false);
        return;
    }
    try {
        auto const parts=northstarcpp::logger_console::segments(
            timestamp,level,pid,tid,channel,message);
        if(mode==ConsoleColorMode::legacy) {
            output_legacy(handle,parts,level);
        } else {
            output_utf8(northstarcpp::logger_console::ansi(parts,level),false);
        }
    } catch(...) {
        output_utf8(text,false);
    }
}

struct LogFile {
    fs::path path;
    std::FILE* file=nullptr;
    std::uint64_t bytes=0;

    ~LogFile() noexcept {
        if(file) {
            std::fflush(file);
            std::fclose(file);
        }
    }

    void open(fs::path value) {
        path=std::move(value);
        file=_wfopen(path.c_str(),L"ab");
        if(!file) throw std::runtime_error("Cannot open logger file");
        std::error_code ec;
        bytes=fs::file_size(path,ec);
        if(ec) throw std::runtime_error("Cannot read logger file size: "+ec.message());
    }

    void flush() {
        if(file && std::fflush(file)!=0)
            throw std::runtime_error("Cannot flush logger file");
    }

    void rotate(std::uint32_t backups) {
        flush();
        if(std::fclose(file)!=0) {
            file=nullptr;
            throw std::runtime_error("Cannot close logger file for rotation");
        }
        file=nullptr;
        std::error_code ec;
        for(int i=static_cast<int>(backups);i>=1;--i) {
            fs::path const from=i==1 ? path : fs::path(path.wstring()+L"."+std::to_wstring(i-1));
            fs::path const to=fs::path(path.wstring()+L"."+std::to_wstring(i));
            if(fs::exists(from,ec) && !ec) {
                fs::remove(to,ec);
                ec.clear();
                fs::rename(from,to,ec);
                if(ec) throw std::runtime_error("Cannot rotate logger file: "+ec.message());
            }
        }
        if(backups==0) {
            fs::remove(path,ec);
            if(ec) throw std::runtime_error("Cannot truncate logger file: "+ec.message());
        }
        open(path);
    }

    void append(std::string const& value,std::uint64_t threshold,std::uint32_t backups,
                bool force_flush) {
        if(!file) return;
        if(threshold && bytes && bytes+value.size()>threshold)rotate(backups);
        if(!value.empty() && std::fwrite(value.data(),1,value.size(),file)!=value.size())
            throw std::runtime_error("Cannot write logger file");
        bytes+=value.size();
        if(force_flush) flush();
    }
};

class MultiSink {
public:
    explicit MultiSink(NorthStarLoggerConfig const& config)
        : sinks_(config.sinks), min_level_(config.minimum_level),
          rotate_bytes_(config.rotate_bytes), backups_(config.backup_count),
          pid_(GetCurrentProcessId())
    {
        require(config.size>=sizeof(NorthStarLoggerConfig),"Incomplete logger configuration");
        require(sinks_ && !(sinks_&~all_sinks),"Invalid logger sink combination");
        require(min_level_<=NORTHSTAR_LOG_FATAL,"Invalid log severity");
        require(rotate_bytes_<=max_rotation,"Logger rotation limit exceeds 1 GiB");
        require(backups_<=16,"Too many log backups");
        std::string const name=config.session_utf8 && *config.session_utf8
            ? config.session_utf8 : "NorthStarCpp";
        require(valid_session(name),"Invalid logger session basename");

        if(sinks_ & (NORTHSTAR_LOG_SINK_TEXT|NORTHSTAR_LOG_SINK_STRUCTURED)) {
            fs::path folder=config.directory_utf8 && *config.directory_utf8
                ? fs::path(wide(config.directory_utf8))
                : default_directory();
            std::error_code ec;
            fs::create_directories(folder,ec);
            if(ec || !fs::is_directory(folder))
                throw std::runtime_error("Cannot create logger directory");
            std::wstring const stem=wide(name)+L"-"+std::to_wstring(pid_);
            if(sinks_ & NORTHSTAR_LOG_SINK_TEXT)
                text_.open(folder/(stem+L".log"));
            if(sinks_ & NORTHSTAR_LOG_SINK_STRUCTURED)
                structured_.open(folder/(stem+L".ulog"));
        }
    }

    void set_level(std::uint32_t level) {
        require(level<=NORTHSTAR_LOG_FATAL,"Invalid log minimum level");
        std::lock_guard<std::mutex> lock(mutex_);
        min_level_=level;
    }

    void log(NorthStarLogRecord const& rec) {
        require(rec.size>=sizeof(NorthStarLogRecord) &&
                rec.level<=NORTHSTAR_LOG_FATAL &&
                rec.channel_bytes<=128u && rec.message_bytes<=65536u &&
                (rec.channel_bytes==0 || rec.channel_utf8) &&
                (rec.message_bytes==0 || rec.message_utf8),
                "Invalid logger record");
        std::string_view const channel(rec.channel_utf8 ? rec.channel_utf8 : "",
                                       rec.channel_bytes);
        std::string_view const message(rec.message_utf8 ? rec.message_utf8 : "",
                                       rec.message_bytes);
        require(utf8_valid(channel)&&utf8_valid(message),
                "Logger expects UTF-8 channel and message");
        require(channel.find_first_of("\r\n\0",0,3)==std::string_view::npos,
                "Logger channel cannot contain control separators");

        std::lock_guard<std::mutex> lock(mutex_);
        if(rec.level<min_level_)return;
        auto const stamp=utc_timestamp();
        auto const tid=GetCurrentThreadId();
        bool const important=rec.level>=NORTHSTAR_LOG_ERROR;
        auto const text=text_record(stamp,rec.level,pid_,tid,channel,message);
        if(sinks_ & NORTHSTAR_LOG_SINK_TEXT)
            text_.append(text,rotate_bytes_,backups_,important);
        if(sinks_ & NORTHSTAR_LOG_SINK_STRUCTURED)
            structured_.append(structured_record(stamp,rec.level,pid_,tid,channel,message),
                               rotate_bytes_,backups_,important);
        if(sinks_ & NORTHSTAR_LOG_SINK_CONSOLE)
            output_console(text,stamp,rec.level,pid_,tid,channel,message);
        if(sinks_ & NORTHSTAR_LOG_SINK_DEBUGGER)
            output_utf8(text,true);
    }

    void flush() {
        std::lock_guard<std::mutex> lock(mutex_);
        text_.flush();
        structured_.flush();
    }

private:
    std::mutex mutex_;
    std::uint32_t sinks_,min_level_;
    std::uint64_t rotate_bytes_;
    std::uint32_t backups_,pid_;
    LogFile text_,structured_;
};

void* create(NorthStarLoggerConfig const* config) {
    MultiSink* result=nullptr;
    guarded([&] {
        if(!config || config->size<sizeof(NorthStarLoggerConfig))
            throw std::invalid_argument("Missing logger configuration");
        result=new MultiSink(*config);
    });
    return result;
}
void destroy(void* state) {delete static_cast<MultiSink*>(state);}
int write(void* state,NorthStarLogRecord const* record) {
    return guarded([&] {
        if(!state || !record) throw std::invalid_argument("Missing logger context/record");
        static_cast<MultiSink*>(state)->log(*record);
    });
}
int flush(void* state) {
    return guarded([&] {
        if(!state) throw std::invalid_argument("Missing logger context");
        static_cast<MultiSink*>(state)->flush();
    });
}
int set_minimum_level(void* state,std::uint32_t level) {
    return guarded([&] {
        if(!state) throw std::invalid_argument("Missing logger context");
        static_cast<MultiSink*>(state)->set_level(level);
    });
}

NorthStarLoggerApi const api{create,destroy,write,flush,set_minimum_level};
NorthStarModule const module{sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_LOGGER,"MultiSink","1.0.0",NORTHSTARCPP_LOGGER_ABI,
    sizeof(api),&api,last_error};

} // namespace

extern "C" const NorthStarModule* northstar_module_query(
    std::uint32_t host,char const* capability,std::uint32_t version)
{
    return match(module,host,capability,version);
}
