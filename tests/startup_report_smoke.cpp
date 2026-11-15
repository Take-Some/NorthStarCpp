#include "../engine/northstarcpp/modules/shared/startup_report.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdio>

using northstarcpp::diagnostics::StartupReport;

int main(int argc,char** argv) {
    if(argc!=2)return 2;
    StartupReport report;
    std::vector<std::string> delivered;
    report.info("process","NorthStar bootstrap");
    int const value=report.measure("one.valid",[]{return 42;});
    assert(value==42);
    try {
        report.measure("one.invalid",[]{throw std::runtime_error("invalid asset");});
        return 3;
    }catch(std::runtime_error const&) {}
    report.warning("asset.decode","bad model\\nnot loaded");
    report.set_sink([&](StartupReport::Severity,std::string const& text) {
        delivered.push_back(text);
    });
    report.measure("two.valid",[]{});
    report.set_report_path(std::filesystem::u8path(argv[1]));
    report.complete("READY_WITH_WARNINGS");
    report.complete("FAILED"); // must not overwrite a completed session
    if(report.warning_count()!=1 || report.failure_count()!=1 ||
       delivered.size()!=report.events().size())return 4;
    auto const& events=report.events();
    if(events.back().status!="FAILED")return 5;
    std::ifstream in(std::filesystem::u8path(argv[1]));
    std::string json((std::istreambuf_iterator<char>(in)),{});
    if(json.find("\"outcome\": \"FAILED\"")==std::string::npos ||
       json.find("one.invalid")==std::string::npos ||
       json.find("bad model")==std::string::npos ||
       json.find("schema")==std::string::npos)
        return 6;
    std::puts("Startup report: stage timing, failure, warning, replay, JSON PASS");
    return 0;
}
