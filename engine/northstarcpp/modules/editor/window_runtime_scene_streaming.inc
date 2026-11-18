// NorthStarCpp window_runtime_scene_streaming.inc -- included in class App; do not compile separately.
    void initialize_asset_zones()
    {
        auto manifest = config.asset_zones_path;
        if (manifest.empty() && config.archive_path.empty() && !file_path.empty()) {
            auto const candidate = fs::absolute(file_path).parent_path() / "assets/streaming/zones.json";
            if (fs::is_regular_file(candidate))
                manifest = candidate;
        }
        if (manifest.empty())
            return;

        manifest = fs::absolute(manifest);
        zone_assets = std::make_unique<AssetManager>();
        // Reuse the exact same provider VFS hierarchy as Player/Viewport.
        // The streaming manifest is a separate namespace only for reading
        // the explicitly selected manifest; gameplay assets are not remounted.
        if(config.archive_path.empty() && !file_path.empty()) {
            auto const base=fs::absolute(file_path).parent_path();
            zone_assets->mount_project_assets(base,
                runtime_options::discover_shared_assets(config,base));
        } else {
            zone_assets->mount_runtime_archives();
            auto const common=runtime_options::discover_shared_assets(config,{});
            if(!common.empty())zone_assets->mount_shared_assets(common);
            if(!config.archive_path.empty())
                zone_assets->mount_archive("",config.archive_path,
                    config.archive_data.empty()?config.archive_path:config.archive_data);
        }
        zone_assets->mount_directory("streaming-manifest", manifest.parent_path());
        auto const source = zone_assets->request_bytes(
            "streaming-manifest/" + manifest.filename().u8string(), AssetPriority::critical).get();
        asset_zones = std::make_unique<AssetZones>(*zone_assets,
            parse_asset_zones(std::string(source->begin(), source->end())));
    }

    void rebuild_scene_world()
    {
        scene_streaming.rebuild(document.snapshot(),
            {file_path,config.archive_path,config.archive_data,config.shared_asset_root},
            !editor || !dirty);
        update_scene_world(0.f);
    }

    void update_scene_world(Scene const& scene,float seconds)
    {
        if(!scene_streaming.world())return;
        // Mobile cities cannot be evicted by *authored* static-cell
        // coordinates. Sea of Rust already streams its terrain separately;
        // keep its 347 game objects resident until dynamic cell ownership
        // is supported by SceneWorld.
        if(preview && terrain_config_enabled &&
           terrain_follow_entity=="CITYROOT Bastion")return;
        Vector3 position{0.f,0.f,0.f};
        std::string room;
        for(auto const& object:scene.objects)
            if(scene_pawn::is_local(scene,object)) {
                position=object.position;
                room=object.node.room;
                break;
            }
        if(fps_camera) {
            position=fps_pose.position;
        } else if(editor && !preview && valid_selection()) {
            auto const& object=document.object(selected);
            position=object.position;
            room=object.node.room;
        }
        // FPS camera projection is the authoritative gameplay frustum.
        // Never derive portal clipping from the actor's position alone.
        RECT viewport_bounds{};
        if(fps_camera && surface && GetClientRect(surface,&viewport_bounds) &&
           viewport_bounds.right>viewport_bounds.left &&
           viewport_bounds.bottom>viewport_bounds.top) {
            float const aspect=float(viewport_bounds.right-viewport_bounds.left)/
                               float(viewport_bounds.bottom-viewport_bounds.top);
            try {
                scene_streaming.world()->set_view_frustum(
                    make_scene_view_frustum(fps_pose,aspect));
            } catch(std::exception const& error) {
                scene_streaming.world()->clear_view_frustum();
                static bool warned_camera_projection=false;
                if(!warned_camera_projection) {
                    std::fprintf(stderr,"[WARN] [scene.portal] camera frustum: %s\n",
                                 error.what());
                    warned_camera_projection=true;
                }
            }
        } else {
            // Non-FPS paths preserve reachability until their editor/cinematic
            // viewport provides a fully specified camera projection.
            scene_streaming.world()->clear_view_frustum();
        }
        scene_streaming.update(scene,position,room,seconds,preview.get());
    }

    void update_scene_world(float seconds)
    {
        if(!scene_streaming.world())return;
        if(preview)update_scene_world(preview->authored_scene(),seconds);
        else update_scene_world(document.snapshot(),seconds);
    }

    void inspect_scene_streaming()
    {
        if (!scene_streaming.world()) {
            MessageBoxW(window,L"Enable Scene V3 to use World Streaming diagnostics.",
                        L"NorthStarCpp Streaming Inspector",MB_OK|MB_ICONINFORMATION);
            return;
        }
        auto const& data=scene_streaming.world()->streaming_diagnostics();
        const auto stream_stats=scene_streaming.stats();
        std::wstring report =
            L"World Partition / Scene V3\n"
            L"---------------------------\n"
            L"Cells loaded: " + std::to_wstring(data.loaded_cells) + L"/" +
                std::to_wstring(scene_streaming.world()->source().cells.size()) +
            L"\nPending loads: " + std::to_wstring(data.pending_loads) +
            L"\nPending unloads: " + std::to_wstring(data.pending_unloads) +
            L"\nResident entities: " + std::to_wstring(data.resident_entities) +
            L"\nVisible entities: " + std::to_wstring(data.visible_entities) +
            L"\nUpdate generation: " + std::to_wstring(data.generation) +
            L"\nAsync scene blocks pending: " +
                 std::to_wstring(stream_stats.pending_chunks) +
            L"\nDecoded scene blocks: " +
                 std::to_wstring(stream_stats.decoded_chunks) +
            L"\nAsset requests: " + std::to_wstring(stream_stats.requests) +
            L"\nNavigation cells (coarse): " +
                std::to_wstring(stream_stats.navigation_cells) +
            L"\nResolved assets: " + std::to_wstring(stream_stats.resolved) +
            L"\nFailed assets: " + std::to_wstring(stream_stats.failed) +
            L"\n\nLast cell transitions:\n";
        auto const& transitions=scene_streaming.world()->cell_transitions();
        if (transitions.empty())
            report+=L"  (none)\n";
        else {
            auto const count=std::min<std::size_t>(transitions.size(),24);
            for (std::size_t i=0;i<count;++i) {
                auto const& transition=transitions[i];
                report+=L"  " + std::wstring(transition.loaded?L"[IN] ":L"[OUT] ") +
                    to_wide(transition.name) +
                    (transition.forced?L" (pinned)\n":L"\n");
            }
            if (transitions.size()>count)
                report+=L"  ... and " + std::to_wstring(transitions.size()-count) +
                    L" more\n";
        }
        auto const frame_stats=scene_streaming.world()->cost_profiler().summary();
        report+=L"\nSceneWorld CPU (rolling "+std::to_wstring(frame_stats.cpu_us.samples)+
            L" frames): p50="+std::to_wstring(frame_stats.cpu_us.p50)+
            L"us, p95="+std::to_wstring(frame_stats.cpu_us.p95)+
            L"us, p99="+std::to_wstring(frame_stats.cpu_us.p99)+L"us\n";
        report+=L"GPU p50/p95/p99: ";
        if(frame_stats.gpu_us.samples)
            report+=std::to_wstring(frame_stats.gpu_us.p50)+L"/"+
                std::to_wstring(frame_stats.gpu_us.p95)+L"/"+
                std::to_wstring(frame_stats.gpu_us.p99)+L"us\n";
        else report+=L"unavailable (provider frame-ID query required)\n";
        report+=L"\nSector CPU-side inventory (not measured VRAM):\n";
        auto const& sector_costs=scene_streaming.world()->cell_telemetry();
        for(std::size_t i=0;i<std::min<std::size_t>(sector_costs.size(),16);++i) {
            auto const& cell=sector_costs[i];
            report+=L"  "+to_wide(cell.name)+L": entities="+
                std::to_wstring(cell.assigned_entities)+L", visible="+
                std::to_wstring(cell.visible_entities)+L", assets="+
                std::to_wstring(cell.unique_authored_assets)+L", HLOD="+
                std::to_wstring(cell.active_hlod_proxies)+L"\n";
        }
        if(sector_costs.size()>16)report+=L"  ... additional sectors omitted\n";
        report+=L"\nHLOD clusters: " +
            std::to_wstring(scene_streaming.world()->hlod_cluster_count()) +
            L"\nActive HLOD proxies: " +
            std::to_wstring(scene_streaming.world()->hlod_proxies().size()) + L"\n";
        if (preview)
            report+=L"Physics residency extension: " +
                std::wstring(preview->supports_scene_residency() ? L"supported" :
                                                           L"provider fallback") + L"\n";
        report+=L"\nThis diagnostic reports logical residency and render visibility. "
                L"It does not assert GPU or physics resource eviction.";
        MessageBoxW(window,report.c_str(),L"NorthStarCpp Streaming Inspector",
                    MB_OK|MB_ICONINFORMATION);
    }

    void update_asset_zones()
    {
        if (!asset_zones)
            return;
        Vector3 position{0.f, 0.f, 0.f};
        if (fps_camera) {
            position = fps_pose.position;
        } else {
            for (auto const& object : preview->scene().objects)
                if (scene_pawn::is_local(preview->scene(),object)) {
                    position = object.position;
                    break;
                }
        }
        try {
            asset_zones->update({position.x, position.y, position.z});
        } catch(std::exception const& e) {
            std::fprintf(stderr,
                "[WARN] [engine.assets] Disabling invalid streaming zone: %s\n",
                e.what());
            asset_zones.reset(); // Prevent a warning storm on later frames.
        }
    }
