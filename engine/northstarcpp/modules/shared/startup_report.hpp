// NorthStarCpp bootstrap diagnostics, independent of engine.logger provider ABI.
// Owns no engine resources and can report startup faults before logger selection.
#pragma once

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <cstdio>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace northstarcpp::diagnostics {

class StartupReport final {
public:
    using Clock=std::chrono::steady_clock;
    enum class Severity {info, warning, error};
    struct Event {
        std::string phase;
        std::string status;
        std::string detail;
        double offset_ms=0.0;
        double duration_ms=0.0;
    };
    using Sink=std::function<void(Severity,std::string const&)>;

    StartupReport()
        : started_(Clock::now()), started_utc_(utc_now()) {}

    // Replay events from before engine.logger was ready, then send new
    // records through the same configured .log/.ulog provider.
    void set_sink(Sink sink) noexcept
    {
        try {
            sink_=std::move(sink);
            if(!sink_)return;
            for(auto const& event:events_)
                deliver(event);
        }catch(...) {
            sink_={};
            std::fputs("[WARN] [engine.startup] Logger replay unavailable\n",stderr);
        }
    }

    void set_report_path(std::filesystem::path path) noexcept
    {
        try {report_path_=std::move(path);}
        catch(...) {}
    }

    template<class F>
    decltype(auto) measure(std::string_view phase, F&& work)
    {
        std::string const label=clean(phase);
        auto const start=Clock::now();
        append_event(label,"BEGIN",{},0.,start);
        try {
            if constexpr(std::is_void_v<std::invoke_result_t<F>>) {
                std::forward<F>(work)();
                append_event(label,"PASS",{},milliseconds(start,Clock::now()),Clock::now());
                return;
            } else {
                decltype(auto) result=std::forward<F>(work)();
                append_event(label,"PASS",{},milliseconds(start,Clock::now()),Clock::now());
                return result;
            }
        } catch(std::exception const& error) {
            append_event(label,"FAIL",error.what(),milliseconds(start,Clock::now()),Clock::now());
            throw;
        } catch(...) {
            append_event(label,"FAIL","non-standard exception",milliseconds(start,Clock::now()),Clock::now());
            throw;
        }
    }

    void info(std::string_view phase, std::string_view detail) noexcept
    {
        try {append_event(clean(phase),"INFO",clean(detail),0.,Clock::now());}
        catch(...) {}
    }
    void warning(std::string_view phase, std::string_view detail) noexcept
    {
        try {append_event(clean(phase),"WARN",clean(detail),0.,Clock::now());}
        catch(...) {}
    }
    void failure(std::string_view phase, std::string_view detail) noexcept
    {
        try {append_event(clean(phase),"FAIL",clean(detail),0.,Clock::now());}
        catch(...) {}
    }

    void complete(std::string_view status) noexcept
    {
        if(completed_)return;
        completed_=true;
        try {
            auto const total=milliseconds(started_,Clock::now());
            std::ostringstream details;
            details<<"total_ms="<<format(total)<<" passed="<<passed_
                   <<" warnings="<<warnings_<<" failures="<<failures_
                   <<" phases="<<events_.size();
            // A failed subsystem cannot be silently reported as READY.
            auto const outcome=(failures_>0 &&
                (status=="READY" || status=="READY_WITH_WARNINGS"))
                ?std::string("FAILED"):std::string(status);
            append_event("summary",outcome,details.str(),total,Clock::now());
            persist();
        }catch(...) {
            std::fputs("[ERROR] [engine.startup] Cannot complete startup report\n",stderr);
        }
    }

    bool completed() const noexcept {return completed_;}
    std::size_t warning_count() const noexcept {return warnings_;}
    std::size_t failure_count() const noexcept {return failures_;}
    std::vector<Event> const& events() const noexcept {return events_;}
    std::filesystem::path const& report_path() const noexcept {return report_path_;}

private:
    static double milliseconds(Clock::time_point from,Clock::time_point to) {
        return std::chrono::duration<double,std::milli>(to-from).count();
    }
    static std::string format(double value) {
        std::ostringstream out;
        out<<std::fixed<<std::setprecision(3)<<value;
        return out.str();
    }
    static std::string clean(std::string_view data) {
        std::string out;
        out.reserve(std::min(data.size(),std::size_t(2048)));
        for(unsigned char ch:data) {
            if(out.size()>=2048)break;
            out.push_back(ch<32 || ch==127 ? ' ' : static_cast<char>(ch));
        }
        return out;
    }
    static std::string quote(std::string_view data) {
        std::ostringstream out;
        out<<'"';
        for(unsigned char ch:data) {
            if(ch=='"' || ch=='\\') {
                out<<'\\'<<static_cast<char>(ch);
            }else if(ch<32) {
                out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')
                   <<static_cast<unsigned>(ch)<<std::dec<<std::setfill(' ');
            }else {
                out<<static_cast<char>(ch);
            }
        }
        out<<'"';
        return out.str();
    }
    static std::string utc_now() {
        std::time_t stamp=std::time(nullptr);
        std::tm utc{};
#ifdef _WIN32
        gmtime_s(&utc,&stamp);
#else
        gmtime_r(&stamp,&utc);
#endif
        std::ostringstream out;
        out<<std::put_time(&utc,"%Y-%m-%dT%H:%M:%SZ");
        return out.str();
    }
    void append_event(std::string phase,std::string status,std::string detail,
              double duration_ms,Clock::time_point now)
    {
        if(status=="PASS")++passed_;
        if(status=="WARN")++warnings_;
        if(status=="FAIL")++failures_;
        events_.push_back({std::move(phase),std::move(status),
                           std::move(detail),milliseconds(started_,now),
                           duration_ms});
        deliver(events_.back());
    }
    void deliver(Event const& event) noexcept
    {
        try {
            std::string message="phase="+event.phase+" status="+event.status+
                " elapsed_ms="+format(event.offset_ms);
            if(event.status=="PASS" || event.status=="FAIL" ||
               event.phase=="summary")
                message+=" duration_ms="+format(event.duration_ms);
            if(!event.detail.empty())
                message+=" detail=\""+event.detail+"\"";
            Severity severity=event.status=="WARN"?Severity::warning:
                event.status=="FAIL" || event.status=="FAILED"?Severity::error:
                Severity::info;
            if(sink_) {
                try {
                    sink_(severity,message);
                    return;
                }catch(...) {
                    // Keep original error visible if the logger itself fails.
                }
            }
            std::fprintf(stderr,"[%s] [engine.startup] %s\n",
                severity==Severity::error?"ERROR":
                severity==Severity::warning?"WARN":"INFO",message.c_str());
        }catch(...) {}
    }
    void persist() const noexcept
    {
        if(report_path_.empty())return;
        try {
            auto const parent=report_path_.parent_path();
            if(!parent.empty())std::filesystem::create_directories(parent);
            std::ofstream out(report_path_,std::ios::binary|std::ios::trunc);
            if(!out)throw std::runtime_error("Cannot open startup report file");
            out<<"{\n  \"schema\": \"northstar.startup.v1\",\n"
               <<"  \"started_utc\": "<<quote(started_utc_)<<",\n"
               <<"  \"outcome\": "<<quote(events_.back().status)<<",\n"
               <<"  \"passed\": "<<passed_<<", \"warnings\": "<<warnings_
               <<", \"failures\": "<<failures_<<",\n"
               <<"  \"events\": [\n";
            for(std::size_t i=0;i<events_.size();++i) {
                auto const& e=events_[i];
                out<<"    {\"phase\": "<<quote(e.phase)
                   <<", \"status\": "<<quote(e.status)
                   <<", \"offset_ms\": "<<format(e.offset_ms)
                   <<", \"duration_ms\": "<<format(e.duration_ms)
                   <<", \"detail\": "<<quote(e.detail)<<"}"
                   <<(i+1<events_.size()?",":"")<<"\n";
            }
            out<<"  ]\n}\n";
            out.flush();
            if(!out)throw std::runtime_error("Cannot write startup report");
        }catch(std::exception const& error) {
            std::fprintf(stderr,"[WARN] [engine.startup] Cannot persist report: %s\n",
                error.what());
        }
    }

    Clock::time_point started_;
    std::string started_utc_;
    Sink sink_;
    std::filesystem::path report_path_;
    std::vector<Event> events_;
    std::size_t passed_=0;
    std::size_t warnings_=0;
    std::size_t failures_=0;
    bool completed_=false;
};

} // namespace northstarcpp::diagnostics
