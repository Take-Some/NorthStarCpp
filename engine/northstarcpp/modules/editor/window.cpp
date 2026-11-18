#include "window_app_internal.hpp"

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int)
{
    northstarcpp::diagnostics::install_crash_handler();
    bool smoke = false;
    bool arguments_parsed = false;
    std::unique_ptr<northstarcpp::Logger> process_logger;
    bool startup_sink_bound=false;
    northstarcpp::diagnostics::StartupReport startup;
    startup.info("process.begin",
        std::string("host=")+(NORTHSTARCPP_EDITOR?"editor":"player")+
        " pid="+std::to_string(GetCurrentProcessId())+" platform=Windows");
    try {
        auto settings = startup.measure("arguments.parse", [] {
            return northstarcpp::options();
        });
        arguments_parsed = true;
        smoke = settings.smoke || settings.check_scene || settings.runtime_init_smoke ||
                settings.fps_ui_smoke || settings.character_smoke || settings.check_shared_assets ||
                settings.inspect_providers;
        startup.info("arguments.summary",
            std::string("scene=")+settings.scene_path.u8string()+
            " archive="+settings.archive_path.u8string()+
            " fps="+(settings.fps?"true":"false")+
            " warp="+(settings.warp?"true":"false")+
            " smoke="+(smoke?"true":"false")+
            " packaged="+(settings.packaged_game?"true":"false"));
        // Test/validation runs do not create log files unless explicitly
        // redirected. The bootstrap report still goes to stderr in that case.
        if (!smoke || northstarcpp::application_log_directory_overridden()) {
            try {
                startup.measure("logger.initialize", [&] {
                    auto const logging=northstarcpp::application_logger_configuration(
                        NORTHSTARCPP_EDITOR?"NorthStarCpp-Editor":"NorthStarCpp-Player");
                    if(!logging.enabled) {
                        startup.warning("logger.initialize",
                            "engine.logger is disabled by bootstrap.json");
                        return;
                    }
                    northstarcpp::prepare_application_log_console(logging);
                    process_logger=std::make_unique<northstarcpp::Logger>(logging.options);
                    auto folder=logging.options.directory;
                    if(folder.empty()) {
                        std::array<wchar_t,32768> env{};
                        auto length=GetEnvironmentVariableW(L"LOCALAPPDATA",
                            env.data(),static_cast<DWORD>(env.size()));
                        if(length==0 || length>=env.size()) {
                            length=GetTempPathW(static_cast<DWORD>(env.size()),env.data());
                            if(length==0 || length>=env.size())
                                throw std::runtime_error("Cannot locate startup report directory");
                        }
                        folder=std::filesystem::path(env.data())/
                            L"KaylasSystems"/L"NorthStarCpp"/L"Logs";
                    }
                    auto const stem=logging.options.session+"-"+
                        std::to_string(GetCurrentProcessId());
                    startup.set_report_path(folder/(stem+"-startup.json"));
                });
                if(process_logger) {
                    startup.set_sink([&](auto severity,std::string const& text) {
                        using Severity=northstarcpp::diagnostics::StartupReport::Severity;
                        process_logger->write(severity==Severity::error?
                            northstarcpp::LogLevel::error:
                            severity==Severity::warning?northstarcpp::LogLevel::warn:
                            northstarcpp::LogLevel::info,"engine.startup",text);
                    });
                    startup_sink_bound=true;
                    startup.info("logger.provider",
                        "selected="+process_logger->provider_name()+
                        " version="+process_logger->provider_version()+
                        " report="+startup.report_path().u8string());
                }
            } catch (std::exception const& error) {
                startup.warning("logger.initialize",
                    std::string("Fallback to stderr: ")+error.what());
                OutputDebugStringA(error.what());
            }
        } else {
            startup.info("logger.initialize","smoke mode; stderr only");
        }
        if (settings.inspect_providers) {
            startup.info("providers.inspect","read-only ABI negotiation");
            // Same ABI negotiation and provider precedence as the Qt Editor
            // inspector. This mode never opens a window or creates a GPU device.
            std::puts("Capability\tRequested\tProvider\tVersion\tABI\tSource\tStatus");
            for (auto const& provider:northstarcpp::inspect_engine_providers()) {
                std::printf("%s\t%s\t%s\t%s\t%u\t%s\t%s\n",
                    provider.capability.c_str(),provider.requested_provider.c_str(),
                    provider.provider.c_str(),provider.version.c_str(),
                    provider.api_abi,provider.source.c_str(),
                    provider.available?"OK":provider.error.c_str());
            }
            startup.complete("READY");
            if(process_logger)process_logger->flush();
            return 0;
        }
        if (settings.check_shared_assets) {
            auto const assets = northstarcpp::shared_assets::inspect_directory(settings.shared_asset_root);
            std::printf("Shared assets: %zu materials, %zu drawables, %zu textures, %zu scripts, %zu shaders\n",
                assets.materials, assets.drawables, assets.textures, assets.scripts, assets.shaders);
            std::printf("Dependencies: %zu model materials, %zu material textures resolved; %zu skeleton files unsupported\n",
                assets.resolved_materials, assets.resolved_textures, assets.unsupported_skeletons);
            for (auto const& reference : assets.missing_references)
                std::printf("Missing asset: %s\n", reference.c_str());
            startup.info("assets.inventory",
                "materials="+std::to_string(assets.materials)+
                " drawables="+std::to_string(assets.drawables)+
                " textures="+std::to_string(assets.textures)+
                " missing="+std::to_string(assets.missing_references.size()));
            startup.complete(assets.missing_references.empty()?"READY":"READY_WITH_WARNINGS");
            if(process_logger)process_logger->flush();
            return 0;
        }
#if NORTHSTARCPP_EDITOR
        // Unreal-style entry point: the native Project Manager opens before
        // the editor unless a scene was explicitly requested.
        if (!smoke && settings.scene_path.empty()) {
            std::array<wchar_t, 32768> executable{};
            auto const count = GetModuleFileNameW(nullptr, executable.data(),
                static_cast<DWORD>(executable.size()));
            if (!count || count >= executable.size())
                throw std::runtime_error("Cannot locate editor installation");
            auto manager = std::filesystem::path(executable.data()).parent_path() /
                L"northstarcpp_project_manager.exe";
            if (!std::filesystem::is_regular_file(manager))
                manager=std::filesystem::path(executable.data()).parent_path()/
                    L"qt_tools"/L"northstarcpp_project_manager.exe";
            if (!std::filesystem::is_regular_file(manager))
                throw std::runtime_error("Native Project Manager is missing from editor directory");
            auto const started = reinterpret_cast<INT_PTR>(
                ShellExecuteW(nullptr, L"open", manager.c_str(), nullptr,
                              manager.parent_path().c_str(), SW_SHOWNORMAL));
            if (started <= 32)
                throw std::runtime_error("Unable to start native Project Manager");
            startup.info("editor.manager","launched="+manager.u8string());
            startup.complete("READY");
            if(process_logger)process_logger->flush();
            return 0;
        }
#endif
        if(settings.embed_handshake_smoke) {
            // Test the actual cross-process HWND parenting and Escape
            // notification without requiring a GPU or loading a city.
            WNDCLASSW type{};
            type.hInstance=instance;
            type.lpszClassName=L"NorthStarCppWindow";
            type.lpfnWndProc=DefWindowProcW;
            if(!RegisterClassW(&type))
                throw std::runtime_error("Cannot register embedded smoke window");
            RECT area{};
            GetClientRect(settings.embed_parent,&area);
            HWND const embedded=CreateWindowExW(0,type.lpszClassName,
                L"NorthStar embedded handshake",WS_CHILD|WS_VISIBLE,
                0,0,std::max(1L,area.right),std::max(1L,area.bottom),
                settings.embed_parent,nullptr,instance,nullptr);
            if(!embedded)
                throw std::runtime_error("Cannot create embedded Player HWND");
            DWORD parent_pid=0,child_pid=0;
            GetWindowThreadProcessId(settings.embed_parent,&parent_pid);
            GetWindowThreadProcessId(embedded,&child_pid);
            if(parent_pid==child_pid || child_pid!=GetCurrentProcessId())
                throw std::runtime_error("Embedded HWND is not cross-process");
            Sleep(120); // parent Qt timer must observe the embedded child
            UINT const notify=RegisterWindowMessageW(
                ::northstarcpp::editor::pie_protocol::exit_request);
            if(!notify || !PostMessageW(settings.embed_parent,notify,0,0)) {
                DestroyWindow(embedded);
                throw std::runtime_error("Embedded Player Escape notification failed");
            }
            Sleep(300); // allow Qt to discover the real child HWND
            DestroyWindow(embedded);
            startup.complete("READY");
            return 0;
        }
        if(settings.runtime_init_smoke) {
            // Exercise the REAL Player startup pipeline: physics, scene-v3
            // streaming mounts, zones and scripts. Unlike --check-scene,
            // this runs the App constructor but opens no window.
            startup.measure("application.construct", [&] {
                northstarcpp::App initialized(std::move(settings),&startup);
                initialized.verify_procedural_terrain_startup();
            });
            startup.complete(startup.warning_count()?"READY_WITH_WARNINGS":"READY");
            if(process_logger)process_logger->flush();
            std::puts("Player runtime asset initialization: PASS");
            return 0;
        }
        auto app=startup.measure("application.construct", [&] {
            return std::make_unique<northstarcpp::App>(std::move(settings),&startup);
        });
        int const result=app->run(instance);
        if(!startup.completed())
            startup.complete(result?"FAILED":
                (startup.warning_count()?"READY_WITH_WARNINGS":"READY"));
        if (process_logger) {
            try {
                if (result)
                    process_logger->error("engine", "Application exited with code " +
                        std::to_string(result));
                else
                    process_logger->info("engine", "Application exited with code 0");
                process_logger->flush();
            } catch (std::exception const&) {}
        }
        return result;
    } catch (std::exception const& error) {
        // Parsing options can fail before normal logger startup. Attempt an
        // emergency logger, but never let its own failure replace the real error.
        if (!process_logger && (!smoke ||
                northstarcpp::application_log_directory_overridden())) {
            try {
                auto const logging=northstarcpp::application_logger_configuration(
                    NORTHSTARCPP_EDITOR ? "NorthStarCpp-Editor" : "NorthStarCpp-Player");
                if (logging.enabled) {
                    northstarcpp::prepare_application_log_console(logging);
                    process_logger=std::make_unique<northstarcpp::Logger>(logging.options);
                    auto folder=logging.options.directory;
                    if(folder.empty()) {
                        std::array<wchar_t,32768> env{};
                        auto length=GetEnvironmentVariableW(L"LOCALAPPDATA",
                            env.data(),static_cast<DWORD>(env.size()));
                        if(!length || length>=env.size()) {
                            length=GetTempPathW(static_cast<DWORD>(env.size()),env.data());
                            if(!length || length>=env.size())
                                throw std::runtime_error("No bootstrap report directory");
                        }
                        folder=std::filesystem::path(env.data())/
                            L"KaylasSystems"/L"NorthStarCpp"/L"Logs";
                    }
                    startup.set_report_path(folder/
                        (logging.options.session+"-"+
                            std::to_string(GetCurrentProcessId())+"-startup.json"));
                }
            } catch(std::exception const& logging_error) {
                std::fprintf(stderr,"Logger emergency initialization: %s\n",
                             logging_error.what());
            }
        }
        if(process_logger && !startup_sink_bound) {
            startup.set_sink([&](auto severity,std::string const& text) {
                using Severity=northstarcpp::diagnostics::StartupReport::Severity;
                process_logger->write(severity==Severity::error?
                    northstarcpp::LogLevel::error:
                    severity==Severity::warning?northstarcpp::LogLevel::warn:
                    northstarcpp::LogLevel::info,"engine.startup",text);
            });
            startup_sink_bound=true;
        }
        startup.failure("bootstrap",error.what());
        startup.complete("FAILED");
        if (process_logger) {
            try {
                process_logger->fatal("engine", error.what());
                process_logger->flush();
            } catch (std::exception const&) {}
        }
        OutputDebugStringA(error.what());
        std::fprintf(stderr, "%s\n", error.what());
        // Invalid command lines fail immediately in CI/terminal contexts.
        // A modal dialog here would deadlock unattended release verification.
        if (!smoke && arguments_parsed)
            MessageBoxA(nullptr, error.what(), "NorthStarCpp", MB_OK | MB_ICONERROR);
        return 1;
    }
}
