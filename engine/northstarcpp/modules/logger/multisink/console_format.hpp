// NorthStarCpp MultiSink console presentation (private implementation).
// Copyright (c) 2026 Kayla's Systems - Kayla Verner.
// Color is terminal-only: .log/.ulog and debugger records stay unformatted.
#pragma once
#include <northstarcpp/logger_plugin.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace northstarcpp::logger_console {

enum class Style : std::uint8_t {
    timestamp, brackets, severity, channel, metadata, message
};
struct Segment {
    Style style;
    std::string text;
};

inline char const* severity_name(std::uint32_t level) {
    static constexpr char const* names[]={"TRACE","DEBUG","INFO","WARN","ERROR","FATAL"};
    return level<=NORTHSTAR_LOG_FATAL ? names[level] : "UNKNOWN";
}

inline std::string safe_text(std::string_view input) {
    std::string result;
    result.reserve(input.size());
    for(unsigned char byte : input) {
        if(byte == '\0') result+="\\0";
        else if(byte == '\t' || byte >= 0x20) result.push_back(static_cast<char>(byte));
        else result.push_back('?'); // Do not allow terminal control injection.
    }
    return result;
}

// One semantically colored prefix per physical line, matching the .log format.
// Split CR, LF and CRLF without emitting control characters in any segment.
inline std::vector<Segment> segments(std::string const& timestamp,
    std::uint32_t level,std::uint32_t pid,std::uint32_t tid,
    std::string_view channel,std::string_view message) {
    std::vector<Segment> result;
    std::size_t begin=0;
    while(true) {
        auto const end=message.find_first_of("\r\n",begin);
        result.push_back({Style::timestamp,timestamp});
        result.push_back({Style::brackets," ["});
        result.push_back({Style::severity,severity_name(level)});
        result.push_back({Style::brackets,"] ["});
        result.push_back({Style::channel,safe_text(channel)});
        result.push_back({Style::brackets,"] ["});
        result.push_back({Style::metadata,"pid:"+std::to_string(pid)+
            " tid:"+std::to_string(tid)});
        result.push_back({Style::brackets,"] "});
        auto const line=message.substr(begin,
            end==std::string_view::npos ? message.size()-begin : end-begin);
        result.push_back({Style::message,safe_text(line)});
        result.push_back({Style::brackets,"\n"});
        if(end==std::string_view::npos)break;
        begin=end+1;
        if(message[end]=='\r' && begin<message.size() && message[begin]=='\n')
            ++begin;
        if(begin>=message.size())break;
    }
    return result;
}

inline std::string flatten(std::vector<Segment> const& parts) {
    std::string result;
    for(auto const& item:parts)result+=item.text;
    return result;
}

inline char const* color(Style style,std::uint32_t level) {
    switch(style) {
    case Style::timestamp: return "\x1b[38;2;96;200;230m";       // cool cyan
    case Style::brackets:  return "\x1b[38;2;122;136;155m";      // slate
    case Style::channel:   return "\x1b[38;2;190;164;250m";      // violet
    case Style::metadata:  return "\x1b[38;2;145;158;177m";      // subdued
    case Style::message:   return "\x1b[38;2;226;232;242m";      // ivory
    case Style::severity:
        switch(level) {
        case NORTHSTAR_LOG_TRACE: return "\x1b[1;38;2;155;168;184m";
        case NORTHSTAR_LOG_DEBUG: return "\x1b[1;38;2;111;171;255m";
        case NORTHSTAR_LOG_INFO:  return "\x1b[1;38;2;104;218;155m";
        case NORTHSTAR_LOG_WARN:  return "\x1b[1;38;2;255;193;86m";
        case NORTHSTAR_LOG_ERROR: return "\x1b[1;38;2;255;110;110m";
        case NORTHSTAR_LOG_FATAL: return "\x1b[1;37;41m";
        default: return "\x1b[1;37m";
        }
    }
    return "\x1b[0m";
}

inline std::string ansi(std::vector<Segment> const& parts,std::uint32_t level) {
    std::string result;
    for(auto const& item:parts) {
        if(item.text=="\n") {
            result+="\x1b[0m\n";
        } else {
            result+=color(item.style,level);
            result+=item.text;
            result+="\x1b[0m";
        }
    }
    return result;
}

} // namespace northstarcpp::logger_console
