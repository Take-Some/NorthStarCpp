// NorthStarCpp window_runtime_bootstrap.inc -- included in class App; do not compile separately.
    explicit App(Options settings, diagnostics::StartupReport* startup_report=nullptr)
        : AppRuntimeState(std::move(settings),startup_report)
    {
        auto const profile_frames = diagnostics::FrameProfiler::requested_frames();
        if (profile_frames)
            frame_profiler = std::make_unique<diagnostics::FrameProfiler>(profile_frames);
        file_path = config.scene_path;
        bootstrap_stage("scene.load", [&] {
            if (config.new_document && fs::exists(file_path))
                config.new_document = false;
            if (!file_path.empty() && !config.new_document)
                document.load(load_startup_scene());
            else if (!config.smoke || !editor)
                document.load(make_startup_scene());
        });
        if(!editor && !config.new_document && !file_path.empty())
            bootstrap_stage("scene.visual_prefabs",[&] {
                auto resolved=runtime_services_.resolve_visual_prefabs(
                    document.snapshot(),[this](std::string const& error) {
                        bootstrap_warning("scene.visual_prefabs",error);
                    });
                if(resolved)document.load(std::move(*resolved));
            });
        dirty = config.new_document;
        if(startup_report_) {
            auto const snapshot=document.snapshot();
            startup_report_->info("scene.summary",
                "version="+std::to_string(snapshot.version)+
                " objects="+std::to_string(snapshot.objects.size())+
                " components="+std::to_string(snapshot.components.size())+
                " cells="+std::to_string(snapshot.cells.size())+
                " layers="+std::to_string(snapshot.layers.size()));
        }
        if(!file_path.empty() && !config.new_document)
            bootstrap_stage("sky.project", [&] {load_scene_sky();});
        bootstrap_stage("script.discovery", [&] {
            auto const mode=runtime_services_.discover_script_mode(document.snapshot());
            scripted_game=mode.scripted_game;
            scripted_first_person=mode.first_person;
        });
        if(startup_report_)
            startup_report_->info("script.mode",
                std::string("scripted=")+(scripted_game?"true":"false")+
                " first_person="+(scripted_first_person?"true":"false"));
        if (!editor) {
            auto initial_scene = document.snapshot();
            if (config.fps) bootstrap_stage("fps.assets", [&] {
                // A FPS project can supply its data through engine.assets.
                // Both source projects and exported games contain <scene>/assets/.
                // Data is never read directly from disk by the gameplay subsystem.
                if (config.archive_path.empty() && !file_path.empty()) {
                    fs::path const asset_root = fs::absolute(file_path).parent_path() / "assets";
                    if (fs::exists(asset_root)) {
                        if (!fs::is_directory(asset_root))
                            throw std::runtime_error("FPS asset root is not a directory");
                        fps_assets = std::make_unique<AssetManager>();
                        auto const base=script_project_root(file_path);
                        fps_assets->mount_project_assets(base,
                            discover_shared_assets(config,base));
                        if (!scripted_game && !config.explore) {
                            auto tuning = fps_assets->request_fps_tuning("gameplay/fps.json");
                            fps_shot = fps_assets->request_audio_clip("audio/shot.wav", AssetPriority::background);
                            fps_reload = fps_assets->request_audio_clip("audio/reload.wav", AssetPriority::background);
                            try { fps_tuning = *tuning.get(); }
                            catch(std::exception const& e) {
                                bootstrap_warning("fps.assets",
                                    std::string("Skipping invalid gameplay/fps.json: ")+e.what());
                            }
                        }
                        // Optional authoring camera presets do not prevent
                        // otherwise valid FPS scenes from loading.
                        bool has_camera_asset = false;
                        try { (void)fps_assets->size("gameplay/camera.json"); has_camera_asset = true; }
                        catch (std::exception const&) {}
                        if (has_camera_asset) {
                            try { fps_camera_settings=load_camera_settings(*fps_assets); }
                            catch(std::exception const& e) {
                                bootstrap_warning("fps.assets",
                                    std::string("Skipping invalid gameplay/camera.json: ")+e.what());
                            }
                        }
                    }
                }
                if (!scripted_first_person)
                    fps = std::make_unique<FpsArena>(initial_scene,
                        fps_tuning, config.explore);
            });
            bootstrap_stage("physics.provider", [&] {
                preview=std::make_unique<PhysicsScene>(initial_scene);
                if(scripted_first_person && preview->controlled_entity()) {
                    auto const rig=gameplay::camera_rig(preview->scene(),
                        preview->controlled_entity());
                    script_eye_height=rig.eye_height;
                    fps_camera_settings.default_fov_y=std::clamp(rig.fov_degrees,25.f,120.f);
                }
            });
            if(startup_report_)
                startup_report_->info("physics.provider",
                    "selected="+preview->provider_name()+
                    " version="+preview->provider_version());
            bootstrap_stage("streaming.zones", [&] {
                try {
                    initialize_asset_zones();
                } catch(std::exception const& e) {
                    bootstrap_warning("streaming.zones",
                        std::string("Skipping invalid streaming zone manifest: ")+e.what());
                    asset_zones.reset();
                    zone_assets.reset();
                }
            });
            if(startup_report_)
                startup_report_->info("streaming.zones",
                    std::string("enabled=")+(asset_zones?"true":"false"));
            if (fps || scripted_first_person) bootstrap_stage("camera.initialize", [&] {
                fps_camera = std::make_unique<CameraDirector>(fps_camera_settings);
                if (fps) update_fps_camera(0.f, false);
                else initialize_authored_player_camera();
            });
            bootstrap_stage("streaming.update_initial", [&] {update_asset_zones();});
        }
        bootstrap_stage("scene.partition", [&] {rebuild_scene_world();});
        if(startup_report_)
            startup_report_->info("scene.partition",
                std::string("enabled=")+(scene_streaming.world()?"true":"false")+
                " asset_store="+(scene_streaming.assets()?scene_streaming.assets()->provider_name():"none"));
        if (!editor && !config.smoke && !config.check_scene && !config.fps_ui_smoke &&
            !file_path.empty())
            bootstrap_stage("scripts.initialize", [&] {initialize_project_scripts();});
        if(startup_report_) {
            startup_report_->info("scripts.provider",
                std::string("state=")+(script_runtime?"running":"inactive")+
                (script_runtime?
                    " selected="+script_runtime->provider_name()+
                    " version="+script_runtime->provider_version():""));
            startup_report_->info("ecs.provider",
                std::string("state=")+(script_ecs?"running":"inactive")+
                (script_ecs?" selected="+script_ecs->provider_name()+
                    " entities="+std::to_string(script_ecs->size()):""));
            startup_report_->info("assets.provider",
                std::string("fps=")+(fps_assets?fps_assets->provider_name():"none")+
                " zones="+(zone_assets?zone_assets->provider_name():"none")+
                " scripts="+(script_assets?script_assets->provider_name():"none"));
            startup_report_->info("audio.provider",
                "initialization=deferred-until-first-play");
        }
    }

    ~App() {
        stop_project_scripts();
    }
