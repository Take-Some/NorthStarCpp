#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <northstarcpp/logger.hpp>
#include <northstarcpp/logger_plugin.h>
#include <northstarcpp/module.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs=std::filesystem;
using northstarcpp::Logger;
using northstarcpp::LogLevel;
using northstarcpp::LoggerOptions;

static void check(bool okay, char const* why) {
    if(!okay) throw std::runtime_error(why);
}
static std::string read_all(fs::path const& path) {
    std::ifstream in(path,std::ios::binary);
    check(bool(in),"Cannot read logger test file");
    return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
}
static std::size_t count_occurrences(std::string const& content,std::string const& needle) {
    std::size_t count=0,pos=0;
    while((pos=content.find(needle,pos))!=std::string::npos) {
        ++count;
        pos+=needle.size();
    }
    return count;
}
static void default_provider(fs::path folder) {
    fs::remove_all(folder);
    fs::create_directories(folder);
    LoggerOptions options;
    options.directory=folder;
    options.session="logger-smoke";
    options.minimum_level=LogLevel::info;
    options.sinks=NORTHSTAR_LOG_SINK_TEXT|NORTHSTAR_LOG_SINK_STRUCTURED;
    options.rotate_bytes=0;
    Logger logger(options);
    check(logger.provider_name()=="MultiSink","Wrong default logger provider");
    check(logger.provider_version()=="1.0.0","Wrong logger provider version");
    logger.debug("graphics","filtered-debug");
    logger.info("renderer","frame \"one\"\nsecond line");
    logger.warn("assets",u8"Файлы загружены");
    logger.error("physics","collision");
    logger.set_minimum_level(LogLevel::trace);

    std::vector<std::thread> workers;
    for(int i=0;i<5;++i)
        workers.emplace_back([&,i] {
            for(int n=0;n<50;++n)
                logger.trace("worker",std::to_string(i)+"/"+std::to_string(n));
        });
    for(auto& worker:workers)worker.join();
    logger.flush();

    std::string const prefix="logger-smoke-"+std::to_string(GetCurrentProcessId());
    auto log=read_all(folder/(prefix+".log"));
    auto ulog=read_all(folder/(prefix+".ulog"));
    check(log.find("filtered-debug")==std::string::npos,"Level filter ignored");
    check(log.find("[INFO] [renderer]")!=std::string::npos,"Plain log missing context");
    check(log.find("[WARN] [assets]")!=std::string::npos,"Missing warn log");
    check(log.find("second line")!=std::string::npos,"Multiline text log lost line");
    check(log.find("\033[")==std::string::npos,"ANSI terminal codes leaked to .log");
    check(ulog.find("\"message\":\"frame \\\"one\\\"\\nsecond line\"")!=std::string::npos,
          "Structured JSON escaped content missing");
    check(ulog.find("\"timestamp_utc\":\"20")!=std::string::npos,
          "Structured UTC timestamp absent");
    check(ulog.find("\"level\":\"WARN\"")!=std::string::npos,
          "Structured warn severity absent");
    check(count_occurrences(ulog,"\"channel\":\"worker\"")==250,"Concurrent structured log lost records");
    check(count_occurrences(ulog,"\"channel\":")==253,"Expected one structured record per call");

    // Rotations retain backups and operate independently on .log and .ulog.
    LoggerOptions rotating=options;
    rotating.session="rotate-smoke";
    rotating.rotate_bytes=512;
    rotating.backup_count=3;
    {
        Logger writer(rotating);
        for(int i=0;i<120;++i)writer.warn("rotation",
            "record-"+std::to_string(i)+"-abcdefghijklmnopqrstuvwxyz");
        writer.flush();
    }
    auto const base="rotate-smoke-"+std::to_string(GetCurrentProcessId());
    for(auto const& ext:{".log",".ulog"}) {
        check(fs::exists(folder/(base+ext)),"Log rotation output missing");
        check(fs::exists(folder/(base+ext+".1")),"Log rotation first backup missing");
        check(fs::exists(folder/(base+ext+".2")),"Log rotation second backup missing");
        check(fs::exists(folder/(base+ext+".3")),"Log rotation third backup missing");
    }

    LoggerOptions bad=options;
    bad.session="../traversal";
    try { Logger invalid(bad); throw std::runtime_error("Unsafe logger session accepted"); }
    catch(std::runtime_error const& e) {
        check(std::string(e.what()).find("Unsafe logger session accepted")==std::string::npos,
              "Unsafe logger session accepted");
    }
    try {
        logger.info("message",std::string(70u*1024u,'!'));
        throw std::runtime_error("Oversized logger entry accepted");
    } catch(std::invalid_argument const&) {}
    std::cout<<"MultiSink logging/rotation/concurrency passed\n";
}
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    try {
        std::string const mode=argv[1];
        if(mode=="default")default_provider(fs::u8path(argv[2]));
        else if(mode=="colors") {
            LoggerOptions options;
            options.directory=fs::u8path(argv[2]);
            options.session="color-smoke";
            options.sinks=NORTHSTAR_LOG_SINK_CONSOLE |
                          NORTHSTAR_LOG_SINK_TEXT |
                          NORTHSTAR_LOG_SINK_STRUCTURED;
            options.minimum_level=LogLevel::trace;
            Logger logger(options);
            logger.trace("core","trace sample");
            logger.debug("assets","debug sample");
            logger.info("engine","info sample");
            logger.warn("physics","warn sample");
            logger.error("render","error sample");
            logger.fatal("engine","fatal sample");
            logger.info("console","text with \x1b[31m escape");
            logger.info("console","first line\nsecond line");
            logger.flush();
        }
        else if(mode=="fixture") {
            LoggerOptions options;options.sinks=NORTHSTAR_LOG_SINK_CONSOLE;
            Logger logger(options);
            check(logger.provider_name()=="Test Logger","Logger provider override ignored");
            logger.info("test","selected");
            logger.flush();
        } else if(mode=="reject") {
            try {
                Logger logger;
                throw std::runtime_error("Incompatible logger provider accepted");
            } catch(std::runtime_error const& e) {
                check(std::string(e.what()).find("engine.logger") !=
                    std::string::npos,"Wrong incompatible provider error");
            }
        } else return 2;
        return 0;
    } catch(std::exception const& e) {
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
