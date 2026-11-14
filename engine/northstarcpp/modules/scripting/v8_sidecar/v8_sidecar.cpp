// NorthStarCpp engine.scripting ABI 2: V8 d8 developer worker (NOT release approved).
// Official Google V8 15.8.62 executes in a separate process, avoiding Qt/CRT collisions.
#include <northstarcpp/scripting_plugin.h>
#include "module_provider.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <limits>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace {
using northstarcpp::module_provider::guarded;
using northstarcpp::module_provider::last_error;
namespace fs=std::filesystem;
void require(bool x,char const* why) {if(!x)throw std::runtime_error(why);}
struct Handle {
    HANDLE h=nullptr;
    Handle()=default;explicit Handle(HANDLE p):h(p){}
    Handle(Handle const&)=delete;Handle& operator=(Handle const&)=delete;
    Handle(Handle&& other) noexcept:h(std::exchange(other.h,nullptr)){}
    Handle& operator=(Handle&& other) noexcept {
        if(this!=&other){close();h=std::exchange(other.h,nullptr);}
        return *this;
    }
    void close() noexcept {
        if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);
        h=nullptr;
    }
    ~Handle(){close();}
};
std::wstring env(wchar_t const* key) {
    DWORD n=GetEnvironmentVariableW(key,nullptr,0);
    if(!n)return {};
    require(n<32760,"V8 environment path too long");
    std::wstring s(n,L'\0');
    auto m=GetEnvironmentVariableW(key,s.data(),n);
    require(m+1==n,"V8 environment changed during read");
    s.resize(m);
    return s;
}
fs::path bundled_runtime()
{
    static int dll_anchor;
    HMODULE module = nullptr;
    require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&dll_anchor), &module) != FALSE,
            "Cannot locate the V8 provider DLL");
    std::wstring filename(32768, L'\0');
    auto const length = GetModuleFileNameW(module, filename.data(),
                                           static_cast<DWORD>(filename.size()));
    require(length && length < filename.size(), "Cannot resolve the V8 provider path");
    filename.resize(length);
    return fs::path(filename).parent_path() / L"scripting" / L"v8";
}
std::string to_hex(std::string_view s) {
    static char const* digits="0123456789abcdef";
    std::string out;out.reserve(s.size()*2);
    for(unsigned char v:s){out.push_back(digits[v>>4]);out.push_back(digits[v&15]);}
    return out;
}
int nibble(char v) {
    if(v>='0'&&v<='9')return v-'0';
    if(v>='a'&&v<='f')return v-'a'+10;
    if(v>='A'&&v<='F')return v-'A'+10;
    return -1;
}
std::string from_hex(std::string_view s) {
    require(s.size()<=8u*1024u*1024u&&s.size()%2==0,"Invalid V8 IPC payload size");
    std::string out;out.reserve(s.size()/2);
    for(std::size_t i=0;i<s.size();i+=2) {
        int a=nibble(s[i]),b=nibble(s[i+1]);
        require(a>=0&&b>=0,"Malformed V8 IPC hex");
        out.push_back(char((a<<4)|b));
    }
    return out;
}
std::vector<std::string_view> split(std::string const& s) {
    std::vector<std::string_view> out;
    for(std::size_t start=0;;) {
        auto at=s.find('|',start);
        out.push_back(std::string_view(s).substr(start,
            at==std::string::npos?at:at-start));
        if(at==std::string::npos)break;
        start=at+1;
    }
    return out;
}
// Anonymous Win32 pipes expose synchronous writes which can block forever
// when d8 does not consume stdin. Overlapped named pipes provide a finite,
// cancellable wait for BOTH directions of the IPC transport.
struct PipePair {
    Handle parent,child;
};
PipePair pipe_pair(bool parent_reads) {
    static std::atomic<std::uint64_t> serial{0};
    SECURITY_ATTRIBUTES attrs{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
    auto name=L"\\\\.\\pipe\\NorthStarCpp-V8-"+std::to_wstring(GetCurrentProcessId())+
        L"-"+std::to_wstring(GetTickCount64())+L"-"+
        std::to_wstring(++serial);
    auto server_access=(parent_reads?PIPE_ACCESS_INBOUND:PIPE_ACCESS_OUTBOUND) |
        FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE;
    Handle parent(CreateNamedPipeW(name.c_str(),server_access,
        PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,
        1,65536,65536,0,&attrs));
    require(parent.h && parent.h!=INVALID_HANDLE_VALUE,
            "Cannot create overlapped V8 IPC pipe");
    Handle child(CreateFileW(name.c_str(),parent_reads?GENERIC_WRITE:GENERIC_READ,
        0,&attrs,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    require(child.h && child.h!=INVALID_HANDLE_VALUE,
            "Cannot open child V8 IPC endpoint");
    // CreateFileW establishes the connection before ConnectNamedPipe.
    BOOL connected=ConnectNamedPipe(parent.h,nullptr);
    require(connected || GetLastError()==ERROR_PIPE_CONNECTED,
            "V8 named pipe connection failed");
    require(SetHandleInformation(parent.h,HANDLE_FLAG_INHERIT,0)!=FALSE,
            "Cannot restrict parent V8 pipe inheritance");
    return {std::move(parent),std::move(child)};
}
struct Worker {
    Handle input,output,process,thread;
    HANDLE cancel_event=nullptr; // owned by Context, kept alive past Worker
    fs::path binary,script_path;
    unsigned heap_size=0;
    std::string pending;
    explicit Worker(HANDLE cancel=nullptr):cancel_event(cancel){}
    Worker(Worker const&)=delete;
    Worker& operator=(Worker const&)=delete;
    bool alive() const noexcept {
        return process.h&&WaitForSingleObject(process.h,0)==WAIT_TIMEOUT;
    }
    void stop() noexcept {
        input.close();output.close();
        if(process.h && alive()){
            TerminateProcess(process.h,22);
            WaitForSingleObject(process.h,1000);
        }
        process.close();thread.close();pending.clear();
    }
    ~Worker(){stop();}
    static DWORD remaining(std::uint64_t deadline) noexcept {
        auto now=GetTickCount64();
        return now>=deadline?0u:static_cast<DWORD>(
            std::min<std::uint64_t>(deadline-now,INFINITE-1u));
    }
    // The operation is started nonblocking. The caller waits on completion,
    // child exit, or explicit cancellation without spinning or busy polling.
    DWORD transfer(HANDLE endpoint,char* bytes,DWORD amount,bool write,
                   std::uint64_t deadline) {
        require(endpoint && amount>0,"Invalid V8 IPC transfer");
        if(cancel_event && WaitForSingleObject(cancel_event,0)==WAIT_OBJECT_0)
            throw std::runtime_error("JavaScript evaluation cancelled");
        if(remaining(deadline)==0u)
            throw std::runtime_error("JavaScript execution timed out");
        Handle completed(CreateEventW(nullptr,TRUE,FALSE,nullptr));
        require(completed.h,"Cannot allocate V8 overlapped I/O event");
        OVERLAPPED io{};io.hEvent=completed.h;
        DWORD done=0;
        BOOL ok=write?WriteFile(endpoint,bytes,amount,&done,&io):
                      ReadFile(endpoint,bytes,amount,&done,&io);
        if(!ok) {
            auto code=GetLastError();
            require(code==ERROR_IO_PENDING,"V8 IPC pipe read/write failed");
            HANDLE waits[]={completed.h,process.h,cancel_event};
            const DWORD count=cancel_event?3u:2u;
            auto state=WaitForMultipleObjects(count,waits,FALSE,remaining(deadline));
            if(state!=WAIT_OBJECT_0) {
                // A pending I/O request must finish cancellation before the
                // OVERLAPPED structure and event go out of scope.
                CancelIoEx(endpoint,&io);
                DWORD ignored=0;
                GetOverlappedResult(endpoint,&io,&ignored,TRUE);
                if(state==WAIT_TIMEOUT)
                    throw std::runtime_error("JavaScript execution timed out");
                if(cancel_event && state==WAIT_OBJECT_0+2)
                    throw std::runtime_error("JavaScript evaluation cancelled");
                throw std::runtime_error("V8 worker exited during IPC");
            }
            require(GetOverlappedResult(endpoint,&io,&done,FALSE)!=FALSE,
                    "V8 overlapped IPC completion failed");
        }
        require(done>0,"V8 IPC endpoint disconnected");
        return done;
    }
    void send(std::string const& s,std::uint64_t deadline) {
        require(input.h&&s.size()<=10u*1024u*1024u,"Invalid V8 input frame");
        auto line=s+"\n";
        std::size_t i=0;
        while(i<line.size()) {
            auto take=static_cast<DWORD>(std::min<std::size_t>(65536,line.size()-i));
            i+=transfer(input.h,line.data()+i,take,true,deadline);
        }
    }
    std::string receive(std::uint64_t deadline) {
        for(;;) {
            auto pos=pending.find('\n');
            if(pos!=std::string::npos) {
                auto result=pending.substr(0,pos);
                pending.erase(0,pos+1);
                if(!result.empty()&&result.back()=='\r')result.pop_back();
                return result;
            }
            require(pending.size()<=10u*1024u*1024u,
                    "V8 output frame exceeds limit");
            char bytes[65536];
            auto got=transfer(output.h,bytes,sizeof(bytes),false,deadline);
            pending.append(bytes,got);
        }
    }
    void start(fs::path const& d8,fs::path const& script,unsigned heap) {
        stop();
        require(fs::is_regular_file(d8)&&fs::is_regular_file(script),
                "V8 15.8.62 d8.exe / worker.js files missing");
        auto in=pipe_pair(false);
        auto out=pipe_pair(true);
        SECURITY_ATTRIBUTES attrs{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
        Handle child_stderr(CreateFileW(L"NUL",GENERIC_WRITE,
            FILE_SHARE_READ|FILE_SHARE_WRITE,&attrs,OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,nullptr));
        require(child_stderr.h!=INVALID_HANDLE_VALUE,
                "Cannot isolate V8 worker stderr");
        STARTUPINFOW startup{};startup.cb=sizeof(startup);
        startup.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;
        startup.wShowWindow=SW_HIDE;
        startup.hStdInput=in.child.h;
        startup.hStdOutput=out.child.h;
        startup.hStdError=child_stderr.h;
        PROCESS_INFORMATION info{};
        auto exe=fs::absolute(d8).wstring();
        auto worker=fs::absolute(script).wstring();
        require(exe.find(L'"')==std::wstring::npos &&
                worker.find(L'"')==std::wstring::npos,
                "Invalid V8 path quoting");
        auto args=L"\""+exe+L"\" --max-old-space-size="+
            std::to_wstring(heap)+L" \""+worker+L"\"";
        std::vector<wchar_t> buffer(args.begin(),args.end());buffer.push_back(0);
        auto cwd=d8.parent_path().wstring();
        require(CreateProcessW(exe.c_str(),buffer.data(),nullptr,nullptr,
                TRUE,CREATE_NO_WINDOW,nullptr,cwd.c_str(),&startup,&info)!=FALSE,
                "CreateProcessW failed to launch official V8 d8");
        process=Handle(info.hProcess);thread=Handle(info.hThread);
        in.child.close();out.child.close();child_stderr.close();
        input=std::move(in.parent);
        output=std::move(out.parent);
        require(receive(GetTickCount64()+15000)=="READY|15.8.62",
                "Official V8 sidecar version probe failed");
        binary=d8;
        script_path=script;
        heap_size=heap;
    }
};
// An idle pool amortizes process startup when editors/tests create many
// short-lived script contexts. Active contexts remain independent: a hung
// script cannot stall another active context's pipes or event loop.
struct WorkerPool {
    std::mutex lock;
    std::vector<std::unique_ptr<Worker>> idle;
    constexpr static std::size_t capacity=3;
    std::unique_ptr<Worker> acquire(fs::path const& binary,
            fs::path const& script,unsigned heap,HANDLE cancel) {
        std::lock_guard guard(lock);
        for(auto it=idle.begin();it!=idle.end();) {
            if(!(*it)->alive()) {
                it=idle.erase(it);
                continue;
            }
            if((*it)->binary==binary && (*it)->script_path==script &&
               (*it)->heap_size==heap) {
                auto lease=std::move(*it);
                idle.erase(it);
                lease->cancel_event=cancel;
                return lease;
            }
            ++it;
        }
        return std::make_unique<Worker>(cancel);
    }
    void release(std::unique_ptr<Worker> lease) {
        if(!lease || !lease->alive())return;
        lease->cancel_event=nullptr;
        std::lock_guard guard(lock);
        if(idle.size()<capacity)idle.push_back(std::move(lease));
        // If the idle pool is full, the extra process is terminated by RAII.
    }
};
WorkerPool& worker_pool() {
    static WorkerPool pool;
    return pool;
}
struct Context {
    std::mutex mutex;
    Handle cancellation{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    std::unique_ptr<Worker> worker;
    fs::path d8,script;
    NorthStarScriptingConfig config{};
    NorthStarScriptHostCall callback=nullptr;
    void* callback_data=nullptr;
    std::string json="null";
    explicit Context(NorthStarScriptingConfig const& values):config(values) {
        auto executable=env(L"NORTHSTARCPP_V8_D8_EXECUTABLE");
        auto worker_script=env(L"NORTHSTARCPP_V8_SIDECAR_WORKER");
        require(executable.empty() == worker_script.empty(),
                "Set both V8 sidecar path overrides or leave both unset");
        require(cancellation.h,"Cannot allocate script cancellation event");
        if (executable.empty()) {
            auto const runtime = bundled_runtime();
            d8 = runtime / L"d8.exe";
            script = runtime / L"v8_worker.js";
        } else {
            d8 = fs::absolute(fs::path(executable));
            script = fs::absolute(fs::path(worker_script));
        }
        if (!fs::is_regular_file(d8) || !fs::is_regular_file(script))
            throw std::runtime_error("V8 sidecar files missing at " +
                                     d8.parent_path().string());
        // Defer process startup until first use. Unused contexts do not
        // consume a V8 process, and used contexts may lease a warm process.
    }
    ~Context() {
        if(!worker)return;
        try {
            if(worker->alive()) {
                ResetEvent(cancellation.h);
                auto deadline=GetTickCount64()+1000;
                worker->send("RESET",deadline);
                if(worker->receive(deadline)=="OK|6e756c6c") {
                    worker_pool().release(std::move(worker));
                    return;
                }
            }
        } catch(...) {
            // A timed-out or broken worker must never re-enter the idle pool.
        }
        worker.reset();
    }
    std::string eval(std::string_view source,std::string_view origin) {
        ResetEvent(cancellation.h);
        bool script_exception=false;
        try {
            if(!worker)
                worker=worker_pool().acquire(d8,script,config.heap_limit_mb,
                                              cancellation.h);
            // A cold d8 process has its own bounded READY handshake (15 s).
            // Do not consume the per-JS-evaluation 1 s budget during
            // process creation/startup; otherwise first scripts time out
            // before they are submitted, even though the worker is ready.
            if(!worker->alive())worker->start(d8,script,config.heap_limit_mb);
            auto const deadline=GetTickCount64()+config.timeout_ms;
            worker->send("E|"+to_hex(source)+"|"+to_hex(origin),deadline);
            for(;;){
                auto line=worker->receive(deadline);
                auto parts=split(line);
                if(parts.size()==4 && parts[0]=="CALL"){
                    auto cap=from_hex(parts[1]);
                    auto method=from_hex(parts[2]);
                    auto args=from_hex(parts[3]);
                    require(args.size()<=NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES,
                            "V8 native callback args too large");
                    char const* response=nullptr;
                    std::uint32_t length=0;
                    int ok=0;
                    if(callback) ok=callback(callback_data,cap.c_str(),
                        method.c_str(),args.data(),
                        static_cast<std::uint32_t>(args.size()),&response,&length);
                    std::string message;
                    if(!callback) message="Engine API dispatcher is not installed";
                    else if(!response ||
                            length>NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES*4u)
                        message="Invalid engine API response";
                    else message.assign(response,length);
                    worker->send("ANS|"+std::to_string(ok?1:0)+"|"+to_hex(message),deadline);
                    continue;
                }
                if(parts.size()==2&&parts[0]=="OK") {
                    auto body=from_hex(parts[1]);
                    require(body.size()<=NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES*4u,
                            "JavaScript JSON output too large");
                    return body;
                }
                if(parts.size()==2&&parts[0]=="ERR") {
                    // An ordinary JS exception does not invalidate the Realm.
                    // Preserve global variables, event handlers and engine
                    // callbacks for the next evaluation.
                    auto message=from_hex(parts[1]);
                    script_exception=true;
                    throw std::runtime_error(message);
                }
                if(parts.size()==2&&parts[0]=="FATAL")
                    throw std::runtime_error(from_hex(parts[1]));
                throw std::runtime_error("Unexpected V8 subprocess response");
            }
        } catch(...) {
            // On IPC corruption, timeout, or worker failure, discard it.
            // On a normal JavaScript error, retain the valid live context.
            if(!script_exception)worker->stop();
            throw;
        }
    }
    void reset() {
        if(!worker)return;
        if(!worker->alive()) {
            worker->start(d8,script,config.heap_limit_mb);
            return;
        }
        ResetEvent(cancellation.h);
        auto deadline=GetTickCount64()+10000;
        try {
            worker->send("RESET",deadline);
            require(worker->receive(deadline)=="OK|6e756c6c",
                    "V8 reset failed");
        } catch(...) {worker->stop();throw;}
    }
};
void* create(NorthStarScriptingConfig const* cfg) {
    Context* context=nullptr;
    guarded([&]{
        require(cfg && cfg->size>=sizeof(*cfg),"Invalid V8 config");
        require(cfg->heap_limit_mb>=16&&cfg->heap_limit_mb<=512 &&
                cfg->timeout_ms>=1&&cfg->timeout_ms<=60000 &&
                cfg->max_source_bytes>=1 &&
                cfg->max_source_bytes<=NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES,
                "Invalid V8 limits");
        context=new Context(*cfg);
    });
    return context;
}
void destroy(void* raw) {
    try {delete static_cast<Context*>(raw);}catch(...){}
}
int evaluate(void* raw,char const* source,std::uint32_t len,
             char const* origin,NorthStarScriptResult* output) {
    return guarded([&] {
        require(raw && output && output->size>=sizeof(*output),
                "V8 context/result unavailable");
        auto& ctx=*static_cast<Context*>(raw);
        require((source||!len)&&len<=ctx.config.max_source_bytes,
                "Invalid source size");
        if(len&&std::memchr(source,0,len))
            throw std::invalid_argument("Embedded NUL in JS source");
        std::string_view filename=origin?std::string_view(origin):
                                  std::string_view("<script>");
        require(filename.size()<=1024,"JavaScript origin too long");
        std::lock_guard guard(ctx.mutex);
        ctx.json=ctx.eval({source?source:"",len},filename);
        output->json_utf8=ctx.json.data();
        output->json_bytes=static_cast<std::uint32_t>(ctx.json.size());
    });
}
int reset(void* raw) {
    return guarded([&]{
        require(raw,"Missing V8 context");
        auto& ctx=*static_cast<Context*>(raw);
        std::lock_guard guard(ctx.mutex);ctx.reset();
    });
}
int set_host_call(void* raw,NorthStarScriptHostCall cb,void* user) {
    return guarded([&]{
        require(raw,"Missing V8 context");
        auto& ctx=*static_cast<Context*>(raw);
        std::lock_guard guard(ctx.mutex);
        ctx.callback=cb;ctx.callback_data=user;
    });
}
// Optional provider-specific cancel hook for a concurrent host thread.
// Unlike evaluate/reset, this does not acquire the context mutex.
int cancel(void* raw) noexcept {
    if(!raw)return 0;
    auto& context=*static_cast<Context*>(raw);
    return context.cancellation.h && SetEvent(context.cancellation.h) ? 1:0;
}
NorthStarScriptingApi const api{create,destroy,evaluate,reset,set_host_call};
NorthStarModule const descriptor{
 sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
 NORTHSTARCPP_CAPABILITY_SCRIPTING,"V8","15.8.62-sidecar-dev",
 NORTHSTARCPP_SCRIPTING_ABI,sizeof(api),&api,last_error
};
}
extern "C" NORTHSTARCPP_MODULE_EXPORT NorthStarModule const*
northstar_module_query(std::uint32_t host,char const* cap,std::uint32_t abi) {
    return northstarcpp::module_provider::match(descriptor,host,cap,abi);
}

// ABI 2 remains intact. Host may discover this opt-in cancellation function
// with GetProcAddress; caller guarantees context is not being destroyed.
extern "C" __declspec(dllexport) int northstar_v8_sidecar_cancel(void* context) {
    return cancel(context);
}
// Diagnostic only; returns 0 for a lazily created, not yet evaluated context.
extern "C" __declspec(dllexport) unsigned long
northstar_v8_sidecar_worker_pid(void* raw) {
    if(!raw)return 0;
    auto& context=*static_cast<Context*>(raw);
    std::lock_guard guard(context.mutex);
    return context.worker && context.worker->alive() ?
        GetProcessId(context.worker->process.h):0;
}

