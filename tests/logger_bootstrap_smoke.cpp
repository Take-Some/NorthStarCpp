// Application-level bootstrap.json logger configuration smoke.
#include <northstarcpp/logger.hpp>
#include <northstarcpp/logger_plugin.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <cstdint>

namespace fs=std::filesystem;
using namespace northstarcpp;

void check(bool value,char const* why) {
    if (!value) throw std::runtime_error(why);
}

int main(int argc,char** argv) {
    if(argc!=3)return 2;
    try {
        auto const mode=std::string(argv[1]);
        if(mode=="bad") {
            try {
                (void)application_logger_configuration("BootstrapSmoke");
                std::cerr<<"Invalid logger config accepted\n";
                return 3;
            } catch(std::exception const& e) {
                check(std::string(e.what()).find("logging")!=std::string::npos,
                      "Expected a logger config validation error");
                return 0;
            }
        }
        auto const cfg=application_logger_configuration("BootstrapSmoke");
        check(cfg.options.session=="BootstrapSmoke","Lost logger session");
        if(mode=="default") {
            check(cfg.enabled && !cfg.console_visible,"Hidden console must be default");
            check(cfg.options.minimum_level==LogLevel::info,"Default level must be INFO");
            check((cfg.options.sinks&NORTHSTAR_LOG_SINK_CONSOLE)==0,
                  "Console sink enabled without opt-in");
            check((cfg.options.sinks&NORTHSTAR_LOG_SINK_TEXT)!=0 &&
                  (cfg.options.sinks&NORTHSTAR_LOG_SINK_STRUCTURED)!=0,
                  "Default file sinks missing");
            check(cfg.options.rotate_bytes==16777216 && cfg.options.backup_count==3,
                  "Incorrect default rotation limits");
        } else if(mode=="custom") {
            check(cfg.enabled && cfg.console_visible,"Visible console not configured");
            check(cfg.options.minimum_level==LogLevel::warn,"WARN level not parsed");
            check(cfg.options.rotate_bytes==1048576 && cfg.options.backup_count==2,
                  "Custom rotation ignored");
            check((cfg.options.sinks & NORTHSTAR_LOG_SINK_CONSOLE)!=0,
                  "Console sink not implicitly enabled by visible console");
            check((cfg.options.sinks & NORTHSTAR_LOG_SINK_DEBUGGER)==0,
                  "Explicit debugger sink disable ignored");
            check(cfg.options.directory==fs::u8path(argv[2]),
                  "Relative logger directory must resolve from EXE, not cwd");
        } else if(mode=="explicit_off") {
            check(cfg.console_visible &&
                  !(cfg.options.sinks&NORTHSTAR_LOG_SINK_CONSOLE),
                  "Explicit console sink disable ignored");
        } else if(mode=="disabled") {
            check(!cfg.enabled,"logging.enabled=false ignored");
        } else if(mode=="override") {
            check(cfg.options.directory==fs::u8path(argv[2]),
                  "NORTHSTARCPP_LOG_DIRECTORY override not applied");
        } else {
            throw std::runtime_error("Unknown smoke variant");
        }
        std::cout<<"bootstrap logging configuration: "<<mode<<" passed\n";
        return 0;
    } catch(std::exception const& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
