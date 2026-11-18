    void configure_script_bindings(ScriptRuntime& runtime) {
        auto const script_owner=++script_context_epoch;
        configure_vehicle_bindings(runtime);
        for (auto const& entry : std::array<std::pair<char const*,LogLevel>,4>{{
                {"debug",LogLevel::debug},{"info",LogLevel::info},
                {"warn",LogLevel::warn},{"error",LogLevel::error}}}) {
            runtime.bind("engine.logger",entry.first,
                [this,level=entry.second](std::string_view json) {
                    auto args=script_args(json);
                    if (args.size()!=1 || args[0].size()>8192)
                        throw std::invalid_argument("engine.logger expects one message <= 8192 bytes");
                    if (script_logger) script_logger->write(level,"project.script",args[0]);
                    else if (script_logging_allowed)
                        std::fprintf(stderr,"[project.script] %s\n",args[0].c_str());
                    return std::string("null");
                });
        }
        // Expose the actual build's CMake PROJECT_VERSION to game scripts.
        // Do not hardcode the engine version in project-authored menus.
        runtime.bind("engine.rendering","engineVersion",
            [](std::string_view json) {
                if(!script_args(json).empty())
                    throw std::invalid_argument(
                        "engine.rendering.engineVersion() takes no arguments");
                return script_json_quote(NORTHSTARCPP_RUNTIME_VERSION);
            });
        // Generic, engine-owned quality policy. Projects own the actual
        // settings UI and persist the selected value through engine.save.
        runtime.bind("engine.rendering","setDetailLevel",
            [this](std::string_view json) {
                auto args=script_args(json);
                if(args.size()!=1 || (args[0]!="balanced" && args[0]!="high" &&
                    args[0]!="ultra" && args[0]!="cinematic"))
                    throw std::invalid_argument(
                        "engine.rendering.setDetailLevel: balanced/high/ultra/cinematic");
                graphics_detail_level=args[0];
                if(graphics) graphics->set_detail_level(graphics_detail_level);
                return script_json_quote(graphics_detail_level);
            });
        runtime.bind("engine.rendering","detailLevel",
            [this](std::string_view json) {
                auto args=script_args(json);
                if(!args.empty())
                    throw std::invalid_argument("engine.rendering.detailLevel()");
                return script_json_quote(graphics_detail_level);
            });
        // Engine Sky is engine-owned, provider-neutral simulation state.
        // Vulkan and D3D12 consume the same authored geometry/material.
        runtime.bind("engine.sky","configure",[this](std::string_view json) {
            if(json.size()>8192 || json.empty() || json.front()!='[')
                throw std::invalid_argument("engine.sky.configure expects one object");
            boost::property_tree::ptree arguments;
            std::istringstream arguments_stream{std::string(json)};
            boost::property_tree::read_json(arguments_stream,arguments);
            if(arguments.size()!=1 || !arguments.front().first.empty())
                throw std::invalid_argument("engine.sky.configure accepts one object");
            boost::property_tree::ptree tree;
            auto const& parameter=arguments.front().second;
            if(!parameter.empty()) {
                // Native engine.sky.configure({ ... }) is a direct root API.
                tree=parameter;
            } else if(parameter.get_value<std::string>().empty()) {
                if(json.find('{')==std::string_view::npos)
                    throw std::invalid_argument("Sky settings must be an object");
            } else {
                // Compatibility with the public Sky/ API.sky facade.
                auto const serialized=parameter.get_value<std::string>();
                if(serialized.size()>4096||serialized.empty()||serialized.front()!='{')
                    throw std::invalid_argument("Sky settings must be a JSON object");
                std::istringstream input(serialized);
                boost::property_tree::read_json(input,tree);
            }
            SkySettings next=sky_environment_.settings();
            auto number=[&](char const* name,double& field) {
                if(auto value=tree.get_optional<double>(name))field=*value;
            };
            auto flag=[&](char const* name,bool& field) {
                if(auto value=tree.get_optional<bool>(name))field=*value;
            };
            static constexpr std::array<char const*,19> allowed{
                "enabled","paused","sun","moon","stars","clouds",
                "timeOfDay","dayLengthSeconds","latitudeDegrees",
                "dayOfYear","moonPhase","cloudCoverage","cloudSpeed",
                "haze","sunIntensity","moonIntensity","preset","mode","drawable"};
            for(auto const& [key,node]:tree) {
                auto found=std::find_if(allowed.begin(),allowed.end(),
                    [&](char const* name){return key==name;});
                if(found==allowed.end() && key!="starDensity")
                    throw std::invalid_argument("Unknown engine.sky property: "+key);
                if(!node.empty())throw std::invalid_argument(
                    "engine.sky configuration must contain scalar properties");
            }
            auto requested_mode=tree.get_optional<std::string>("mode");
            auto requested_drawable=tree.get_optional<std::string>("drawable");
            auto mode=requested_mode?*requested_mode:sky_mode_;
            auto drawable=requested_drawable?*requested_drawable:
                sky_drawable_reference_;
            if(mode!="procedural" && mode!="drawable" &&
               mode!="hybrid" && mode!="disabled")
                throw std::invalid_argument("Unknown engine.sky mode");
            if(mode=="disabled" || mode=="procedural")drawable.clear();
            if(mode=="drawable" && drawable.empty())
                throw std::invalid_argument(
                    "engine.sky drawable mode requires a YDD/YDR reference");
            if(!drawable.empty())
                project_sky::validate_drawable_reference(drawable);
            flag("enabled",next.enabled);
            flag("paused",next.paused);
            flag("sun",next.sun);flag("moon",next.moon);
            flag("stars",next.stars);flag("clouds",next.clouds);
            number("timeOfDay",next.time_of_day);
            number("dayLengthSeconds",next.day_length_seconds);
            number("latitudeDegrees",next.latitude_degrees);
            if(auto day=tree.get_optional<int>("dayOfYear"))
                next.day_of_year=*day;
            number("moonPhase",next.moon_phase);
            number("cloudCoverage",next.cloud_coverage);
            number("cloudSpeed",next.cloud_speed);
            number("haze",next.haze);
            number("sunIntensity",next.sun_intensity);
            number("moonIntensity",next.moon_intensity);
            number("starDensity",next.star_density);
            if(auto preset=tree.get_optional<std::string>("preset"))
                next.preset=*preset;
            if(requested_mode) {
                // Modes select presentation, independent of atmospheric
                // numeric settings. Procedural is the asset-free default.
                next.enabled=mode=="procedural" || mode=="hybrid";
            }
            SkyEnvironment::validate(next);
            if(graphics && drawable!=sky_drawable_reference_) {
                try {
                    graphics->set_sky_asset_reference(drawable);
                } catch(std::exception const& error) {
                    // Bad optional art never stops the simulation or editor.
                    std::fprintf(stderr,
                        "[WARN] [engine.sky] Drawable rejected: %s\n",
                        error.what());
                    graphics->set_sky_asset_reference({});
                    drawable.clear();
                    mode="procedural";
                    next.enabled=true;
                }
            }
            sky_mode_=std::move(mode);
            sky_drawable_reference_=std::move(drawable);
            sky_environment_.configure(next);
            sky_visual_dirty_=true;
            sky_visual_elapsed_=0;
            std::fprintf(stderr,
                "[INFO] [engine.sky] configured enabled=%u time=%.2f "
                "day_length_s=%.1f clouds=%.2f sun=%u moon=%u stars=%u\\n",
                next.enabled?1u:0u,next.time_of_day,next.day_length_seconds,
                next.cloud_coverage,next.sun?1u:0u,next.moon?1u:0u,
                next.stars?1u:0u);
            return std::string("true");
        });
        runtime.bind("engine.sky","setTimeOfDay",[this](std::string_view json) {
            const auto args=script_args(json);
            if(args.size()!=1)
                throw std::invalid_argument("engine.sky.setTimeOfDay(hours)");
            sky_environment_.set_time(script_number(args[0]));
            sky_visual_dirty_=true;sky_visual_elapsed_=0;
            return std::string("true");
        });
        runtime.bind("engine.sky","advance",[this](std::string_view json) {
            const auto args=script_args(json);
            if(args.size()!=1)
                throw std::invalid_argument("engine.sky.advance(seconds)");
            sky_environment_.advance(script_number(args[0]));
            sky_visual_dirty_=true;sky_visual_elapsed_=0;
            return std::string("true");
        });
        runtime.bind("engine.sky","reset",[this](std::string_view json) {
            if(!script_args(json).empty())
                throw std::invalid_argument("engine.sky.reset()");
            sky_environment_.reset();
            sky_mode_="disabled";
            sky_drawable_reference_.clear();
            if(graphics)graphics->set_sky_asset_reference({});
            sky_visual_dirty_=true;sky_visual_elapsed_=0;
            return std::string("true");
        });
        runtime.bind("engine.sky","state",[this](std::string_view json) {
            if(!script_args(json).empty())
                throw std::invalid_argument("engine.sky.state()");
            auto const s=sky_environment_.snapshot();
            auto const& cfg=s.settings;
            auto boolean=[](bool v){return v?"true":"false";};
            auto vector=[](SkyDirection v){
                return std::string("{\"x\":")+std::to_string(v.x)+
                    ",\"y\":"+std::to_string(v.y)+
                    ",\"z\":"+std::to_string(v.z)+"}";
            };
            return std::string("{\"enabled\":")+boolean(cfg.enabled)+
                ",\"paused\":"+boolean(cfg.paused)+
                ",\"sun\":"+boolean(cfg.sun)+
                ",\"moon\":"+boolean(cfg.moon)+
                ",\"stars\":"+boolean(cfg.stars)+
                ",\"clouds\":"+boolean(cfg.clouds)+
                ",\"timeOfDay\":"+std::to_string(cfg.time_of_day)+
                ",\"dayLengthSeconds\":"+std::to_string(cfg.day_length_seconds)+
                ",\"latitudeDegrees\":"+std::to_string(cfg.latitude_degrees)+
                ",\"dayOfYear\":"+std::to_string(cfg.day_of_year)+
                ",\"moonPhase\":"+std::to_string(cfg.moon_phase)+
                ",\"cloudCoverage\":"+std::to_string(cfg.cloud_coverage)+
                ",\"cloudSpeed\":"+std::to_string(cfg.cloud_speed)+
                ",\"haze\":"+std::to_string(cfg.haze)+
                ",\"sunIntensity\":"+std::to_string(cfg.sun_intensity)+
                ",\"moonIntensity\":"+std::to_string(cfg.moon_intensity)+
                ",\"starDensity\":"+std::to_string(cfg.star_density)+
                ",\"preset\":\""+cfg.preset+"\""+
                ",\"mode\":"+script_json_quote(sky_mode_)+
                ",\"drawable\":"+script_json_quote(sky_drawable_reference_)+
                ",\"sunDirection\":"+vector(s.sun_direction)+
                ",\"moonDirection\":"+vector(s.moon_direction)+
                ",\"sunElevationDegrees\":"+std::to_string(s.sun_elevation_degrees)+
                ",\"moonElevationDegrees\":"+std::to_string(s.moon_elevation_degrees)+
                ",\"daylight\":"+std::to_string(s.daylight)+
                ",\"cloudOffset\":"+std::to_string(s.cloud_offset)+
                ",\"revision\":"+std::to_string(s.revision)+"}";
        });
        // Storage is project-scoped and does not grant arbitrary filesystem I/O.
        // The save payload is a JSON string produced by JSON.stringify(...).
        runtime.bind("engine.save","write",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=2) throw std::invalid_argument(
                "engine.save.write(slot,JSON.stringify(state))");
            project_save_store(config,file_path).write(args[0],args[1]);
            return std::string("true");
        });
        runtime.bind("engine.save","read",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=1) throw std::invalid_argument("engine.save.read(slot)");
            auto value=project_save_store(config,file_path).read(args[0]);
            return value ? script_json_quote(*value) : std::string("null");
        });
        runtime.bind("engine.save","exists",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=1) throw std::invalid_argument("engine.save.exists(slot)");
            return project_save_store(config,file_path).exists(args[0])
                ? std::string("true") : std::string("false");
        });
        runtime.bind("engine.save","remove",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=1) throw std::invalid_argument("engine.save.remove(slot)");
            return project_save_store(config,file_path).erase(args[0])
                ? std::string("true") : std::string("false");
        });
        runtime.bind("engine.perlin","sample2",
            [this](std::string_view json) {
                auto args=script_args(json);
                if (args.size()!=2) throw std::invalid_argument("sample2 expects x,y");
                if (!script_noise) script_noise=std::make_unique<PerlinNoise>();
                return std::to_string(script_noise->sample2(
                    script_number(args[0]),script_number(args[1])));
            });
        runtime.bind("engine.scene","spawn",
            [this,script_owner](std::string_view json) {
                auto args=script_args(json);
                if ((args.size()!=9 && args.size()!=10 && args.size()!=11 && args.size()!=14) ||
                    !preview || !script_ecs ||
                    !preview->supports_runtime_entities())
                    throw std::invalid_argument("scene.spawn(name,x,y,z,size,mass,color,asset,layer,parent)");
                SceneObject object;
                object.name=args[0];
                if (object.name.empty() || object.name.size()>=NORTHSTARCPP_ECS_NAME_BYTES)
                    throw std::invalid_argument("Invalid runtime entity name");
                auto finite_coordinate=[](std::string const& input) {
                    auto const v=script_number(input);
                    if (std::abs(v)>1000000.)
                        throw std::invalid_argument("Runtime position out of bounds");
                    return static_cast<float>(v);
                };
                object.position={finite_coordinate(args[1]),finite_coordinate(args[2]),
                                 finite_coordinate(args[3])};
                object.size=static_cast<float>(script_number(args[4]));
                object.mass=static_cast<float>(script_number(args[5]));
                auto const color=script_number(args[6]);
                if (color<0. || color>0xFFFFFFu || std::floor(color)!=color)
                    throw std::invalid_argument("Invalid runtime RGB color");
                object.color=static_cast<std::uint32_t>(color);
                object.node.asset=args[7];
                object.node.layer=args[8];
                if(args.size()==11 || args.size()==14) {
                    if(args[10]!="true" && args[10]!="false")
                        throw std::invalid_argument("Runtime collision must be boolean");
                    object.node.collision=args[10]=="true";
                }
                if(args.size()==14) {
                    const Vector3 visual_scale{
                        static_cast<float>(script_number(args[11])),
                        static_cast<float>(script_number(args[12])),
                        static_cast<float>(script_number(args[13]))};
                    if(!visual_scale.valid() || visual_scale.x<=0.f ||
                       visual_scale.y<=0.f || visual_scale.z<=0.f ||
                       visual_scale.x>100.f || visual_scale.y>100.f ||
                       visual_scale.z>100.f)
                        throw std::invalid_argument("Runtime visual scale must be positive <=100");
                    object.node.scale=visual_scale;
                }
                if (object.node.layer.empty() || object.node.layer.size()>96)
                    throw std::invalid_argument("Invalid runtime entity layer");
                if (args.size()>=10 && args[9]!="0") {
                    auto const parent=script_index(args[9]);
                    auto const found=script_spawn_owners.find(parent);
                    if (found==script_spawn_owners.end() || found->second!=script_owner ||
                        !preview->runtime_alive(script_entity_indices.at(parent)))
                        throw std::invalid_argument("Runtime parent must be a live entity in this script context");
                    object.node.parent=parent;
                }
                if (!object.node.asset.empty()) {
                    auto const& asset=object.node.asset;
                    if (asset.size()>256 || asset.front()=='/' ||
                        asset.find("..")!=std::string::npos ||
                        asset.find('\\')!=std::string::npos ||
                        asset.find(':')!=std::string::npos ||
                        asset.size()<4 ||
                        (asset.substr(asset.size()-4)!=".obj" &&
                         asset.substr(asset.size()-4)!=".ydd" &&
                         asset.substr(asset.size()-4)!=".ydr") ||
                        (asset.compare(0,7,"models/")!=0 &&
                         asset.compare(0,7,"assets/")!=0 &&
                         asset.compare(0,11,"characters/")!=0))
                        throw std::invalid_argument(
                            "Runtime model must be a VFS OBJ/YDD/YDR");
                    if (!script_assets)
                        throw std::runtime_error("Project VFS unavailable");
                    (void)script_assets->size(asset);
                }
                if (!next_script_spawn_id)
                    for (auto const& previous:preview->scene().objects)
                        next_script_spawn_id=std::max(next_script_spawn_id,previous.node.id);
                if (next_script_spawn_id==UINT64_MAX)
                    throw std::runtime_error("Exhausted runtime entity IDs");
                object.node.id=++next_script_spawn_id;
                auto const index=preview->spawn_runtime(object);
                if(std::getenv("NORTHSTARCPP_QA_WEAPON_TRACE") &&
                   object.node.layer=="Weapon debris")
                    std::fprintf(stderr,
                        "[WEAPON FX TRACE] casing spawned index=%zu mass=%.4f"
                        " origin=(%.4f %.4f %.4f) asset=%s visual_scale=%.1f\n",
                        index,object.mass,object.position.x,
                        object.position.y,object.position.z,
                        object.node.asset.c_str(),object.node.scale.x);
                try {
                    auto const handle=script_ecs->create_runtime(object);
                    script_entity_indices.emplace(object.node.id,index);
                    script_handle_ids.emplace(handle,object.node.id);
                    script_spawn_handles.emplace(object.node.id,handle);
                    script_spawn_owners.emplace(object.node.id,script_owner);
                } catch (...) {
                    preview->destroy_runtime(index);
                    throw;
                }
                return std::string("{\"id\":")+
                    script_json_quote(std::to_string(object.node.id))+
                    ",\"index\":"+std::to_string(index)+"}";
            });
        runtime.bind("engine.scene","destroy",
            [this,script_owner](std::string_view json) {
                auto args=script_args(json);
                if (args.size()!=1)
                    throw std::invalid_argument("scene.destroy(id)");
                destroy_script_spawn(script_index(args[0]),script_owner);
                return std::string("true");
            });
        runtime.bind("engine.scene","moveRuntime",
            [this,script_owner](std::string_view json) {
                auto args=script_args(json);
                if (args.size()!=4 || !preview || !script_ecs)
                    throw std::invalid_argument("scene.moveRuntime(id,x,y,z)");
                auto const id=script_index(args[0]);
                if (!script_spawn_owners.count(id) ||
                    script_spawn_owners.at(id)!=script_owner)
                    throw std::out_of_range("Stale runtime entity ID");
                auto const index=script_entity_indices.at(id);
                if (preview->scene().objects[index].mass!=0.f)
                    throw std::invalid_argument("Dynamic bodies must use physics");
                Vector3 p{static_cast<float>(script_number(args[1])),
                          static_cast<float>(script_number(args[2])),
                          static_cast<float>(script_number(args[3]))};
                if (std::max({std::abs(p.x),std::abs(p.y),std::abs(p.z)})>1000000.f)
                    throw std::invalid_argument("Runtime position out of bounds");
                preview->move_static(index,p);
                NsPosition position{p.x,p.y,p.z};
                script_ecs->set_component(script_spawn_handles.at(id),
                    NS_COMPONENT_POSITION,&position,sizeof(position));
                return std::string("null");
            });
        runtime.bind("engine.scene","rotateRuntime",
            [this,script_owner](std::string_view json) {
                auto args=script_args(json);
                if(args.size()!=4 || !preview)
                    throw std::invalid_argument("scene.rotateRuntime(id,x,y,z)");
                auto const id=script_index(args[0]);
                if(!script_spawn_owners.count(id) ||
                   script_spawn_owners.at(id)!=script_owner)
                    throw std::out_of_range("Stale runtime entity rotation request");
                preview->rotate_runtime(script_entity_indices.at(id),
                    Vector3{static_cast<float>(script_number(args[1])),
                            static_cast<float>(script_number(args[2])),
                            static_cast<float>(script_number(args[3]))});
                return std::string("null");
            });
        runtime.bind("engine.prefab","read",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=1 || !script_assets)
                throw std::invalid_argument("prefab.read(path)");
            auto const& path=args[0];
            if (path.size()>256 || path.size()<17 || path.front()=='/' ||
                path.compare(0,8,"prefabs/")!=0 ||
                path.substr(path.size()-8)!=".nprefab" ||
                path.find("..")!=std::string::npos ||
                path.find('\\')!=std::string::npos ||
                path.find(':')!=std::string::npos ||
                path.find("//")!=std::string::npos)
                throw std::invalid_argument("Prefab path must be under prefabs/*.nprefab");
            if (script_assets->size(path)>1024*1024u)
                throw std::length_error("Runtime prefab exceeds 1 MiB");
            auto const bytes=script_assets->read(path);
            Scene const prefab=parse_scene_data(
                std::string(bytes.begin(),bytes.end()));
            validate_scene(prefab);
            if (prefab.version!=3 || prefab.objects.empty() ||
                prefab.objects.size()>128 || !prefab.volumes.empty() ||
                !prefab.portals.empty() || !prefab.opaque_records.empty())
                throw std::invalid_argument(
                    "Runtime prefab requires 1..128 Scene V3 entities without external records");
            // An animation is authored as a generic prefab component,
            // independent of Pawn/Player and any equipped weapon.
            std::unordered_map<std::uint64_t,std::string> default_clips;
            for(auto const& component:prefab.components) {
                if(component.type!="northstar.animation.default" ||
                   component.version!=1 || component.payload.empty() ||
                   component.payload.size()>512)
                    throw std::invalid_argument(
                        "Runtime prefab supports only animation.default component v1");
                auto const& ref=component.payload;
                auto const sep=ref.rfind('@');
                if(sep==std::string::npos || sep<4 ||
                   sep+1>=ref.size() || ref.substr(sep-4,4)!=".ycd" ||
                   ref.find("..")!=std::string::npos ||
                   ref.find('\\')!=std::string::npos ||
                   ref.find(':')!=std::string::npos ||
                   ref.find('\0')!=std::string::npos ||
                   !default_clips.emplace(component.entity_id,ref).second)
                    throw std::invalid_argument("Invalid prefab animation reference");
                auto const owner=std::find_if(prefab.objects.begin(),
                    prefab.objects.end(),[&](auto const& object){
                        return object.node.id==component.entity_id;
                    });
                if(owner==prefab.objects.end() ||
                   (owner->node.asset.find(".ydd")==std::string::npos &&
                    owner->node.asset.find(".ydr")==std::string::npos))
                    throw std::invalid_argument(
                        "Prefab animation requires an authored YDD/YDR entity");
            }
            std::uint64_t root_id=0;
            std::string result="{\"objects\":[";
            for (std::size_t i=0;i<prefab.objects.size();++i) {
                auto const& obj=prefab.objects[i];
                if (scene_pawn::is_pawn(prefab,obj) || !obj.node.script.empty() ||
                    !obj.node.cell.empty() || !obj.node.room.empty() ||
                    !obj.node.visible ||
                    obj.node.rotation.x!=0.f || obj.node.rotation.y!=0.f ||
                    obj.node.rotation.z!=0.f ||
                    !obj.node.scale.valid() ||
                    obj.node.scale.x<=0.f || obj.node.scale.y<=0.f ||
                    obj.node.scale.z<=0.f || obj.node.scale.x>100.f ||
                    obj.node.scale.y>100.f ||
                    obj.node.scale.z>100.f)
                    throw std::invalid_argument(
                        "Runtime prefab requires an unrotated transform and no scene cell");
                if (!obj.node.parent) {
                    if (root_id)
                        throw std::invalid_argument("Runtime prefab has multiple roots");
                    root_id=obj.node.id;
                }
                if (i) result+=',';
                result+="{\"id\":"+script_json_quote(std::to_string(obj.node.id))+
                    ",\"parent\":"+script_json_quote(std::to_string(obj.node.parent))+
                    ",\"name\":"+script_json_quote(obj.name)+
                    ",\"position\":{\"x\":"+std::to_string(obj.position.x)+
                    ",\"y\":"+std::to_string(obj.position.y)+
                    ",\"z\":"+std::to_string(obj.position.z)+"}"+
                    ",\"size\":"+std::to_string(obj.size)+
                    ",\"mass\":"+std::to_string(obj.mass)+
                    ",\"color\":"+std::to_string(obj.color)+
                    ",\"asset\":"+script_json_quote(obj.node.asset)+
                    ",\"layer\":"+script_json_quote(obj.node.layer)+
                    ",\"scale\":{\"x\":"+std::to_string(obj.node.scale.x)+
                    ",\"y\":"+std::to_string(obj.node.scale.y)+
                    ",\"z\":"+std::to_string(obj.node.scale.z)+"}"+
                    ",\"collision\":"+(obj.node.collision?"true":"false")+"}";
            }
            if (!root_id)
                throw std::invalid_argument("Runtime prefab must have one root");
            result+="],\"animations\":[";
            bool first_animation=true;
            for(auto const& obj:prefab.objects) {
                auto it=default_clips.find(obj.node.id);
                if(it==default_clips.end())continue;
                if(!first_animation)result+=',';
                first_animation=false;
                result+="{\"entity\":"+script_json_quote(std::to_string(obj.node.id))+
                    ",\"clip\":"+script_json_quote(it->second)+"}";
            }
            return result+"]}";
        });
        runtime.bind("engine.scene","objectCount",
            [this](std::string_view json) {
                if (!script_args(json).empty())
                    throw std::invalid_argument("objectCount expects no arguments");
                return std::to_string(preview ? preview->scene().objects.size() : 0u);
            });
        runtime.bind("engine.scene","setLayerVisible",
            [this](std::string_view json) {
                auto args=script_args(json);
                if (args.size()!=2 || !scene_streaming.world() ||
                    (args[1]!="true" && args[1]!="false"))
                    throw std::invalid_argument("setLayerVisible(name,bool)");
                scene_streaming.world()->set_layer_visible(args[0],args[1]=="true");
                return std::string("null");
            });
        // Runtime VFX is independent of physics and shared across weapons.
        // Scripted weapons specify the exact world muzzle transform; the
        // physics shell uses a separate engine-side Prefab API.
        runtime.bind("engine.weaponfx","emitShot",
            [this](std::string_view json) {
                auto const args=script_args(json);
                if(args.size()!=6 || !preview || !graphics)
                    throw std::invalid_argument(
                        "weaponfx.emitShot(mx,my,mz,dx,dy,dz)");
                Vector3 const muzzle{
                    static_cast<float>(script_number(args[0])),
                    static_cast<float>(script_number(args[1])),
                    static_cast<float>(script_number(args[2]))};
                Vector3 const direction{
                    static_cast<float>(script_number(args[3])),
                    static_cast<float>(script_number(args[4])),
                    static_cast<float>(script_number(args[5]))};
                if(!muzzle.valid() || !direction.valid() ||
                   direction.magnitude()<.5f || direction.magnitude()>2.f)
                    throw std::invalid_argument("Invalid muzzle flash vectors");
                if(!script_weapon_fx)
                    script_weapon_fx=std::make_unique<FpsWeaponEffects>();
                const bool emitted=script_weapon_fx->emit_at(muzzle,direction);
                if(std::getenv("NORTHSTARCPP_QA_WEAPON_TRACE"))
                    std::fprintf(stderr,
                        "[WEAPON FX TRACE] flash emitted=%d muzzle=(%.4f %.4f %.4f)"
                        " barrel=(%.3f %.3f %.3f)\n",
                        int(emitted),muzzle.x,muzzle.y,muzzle.z,
                        direction.x,direction.y,direction.z);
                return emitted?std::string("true"):std::string("false");
            });
        // Provider-neutral physics contact stream. The provider reports
        // Begin/End with scene indices; UINT32_MAX represents native ground.
        runtime.bind("engine.physics","pollContacts",
            [this](std::string_view json) {
                auto const args=script_args(json);
                if(args.size()!=1 || !preview)
                    throw std::invalid_argument("physics.pollContacts(max=256)");
                const auto max=script_index(args[0]);
                if(max>256)throw std::out_of_range("Physics event batch exceeds 256");
                const auto contacts=preview->poll_collision_events(max);
                std::string result="[";
                bool first=true;
                for(auto const& c:contacts) {
                    if(!first)result+=',';
                    first=false;
                    const auto idx=[](std::uint32_t v) {
                        return v==UINT32_MAX ? std::string("-1"):
                            std::to_string(v);
                    };
                    result+="{\"first\":"+idx(c.first_index)+
                        ",\"second\":"+idx(c.second_index)+
                        ",\"phase\":"+std::to_string(c.phase)+
                        ",\"response\":"+std::to_string(c.response)+"}";
                }
                return result+"]";
            });
        runtime.bind("engine.physics","setBodyMaterial",
            [this](std::string_view json) {
                const auto args=script_args(json);
                if(args.size()!=3 || !preview)
                    throw std::invalid_argument(
                        "physics.setBodyMaterial(index,friction,restitution)");
                const auto index=script_index(args[0]);
                const auto friction=static_cast<float>(script_number(args[1]));
                const auto restitution=static_cast<float>(script_number(args[2]));
                return preview->set_body_material(index,friction,restitution)?
                    std::string("true"):std::string("false");
            });
        runtime.bind("engine.physics","applyImpulse",
            [this](std::string_view json) {
                auto args=script_args(json);
                if (args.size()!=4 || !preview)
                    throw std::invalid_argument("applyImpulse(index,x,y,z)");
                auto const index=script_index(args[0]);
                if (index>=preview->scene().objects.size())
                    throw std::out_of_range("Invalid scene object index");
                preview->apply_impulse(index,{
                    static_cast<float>(script_number(args[1])),
                    static_cast<float>(script_number(args[2])),
                    static_cast<float>(script_number(args[3]))});
                return std::string("null");
            });
        runtime.bind("engine.input","isDown",
            [this](std::string_view json) {
                auto args=script_args(json);
                if (args.size()!=1) throw std::invalid_argument("isDown(keyCode)");
                auto const index=script_index(args[0]);
                if (index>=script_keys.size()) throw std::out_of_range("Key code out of range");
                return script_keys[index] ? std::string("true") : std::string("false");
            });


        runtime.bind("engine.input","mouseDown",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=1) throw std::invalid_argument("input.mouseDown(index)");
            auto idx=script_index(args[0]);
            if (idx>=script_mouse_buttons.size()) throw std::out_of_range("Mouse button index");
            return script_mouse_buttons[idx] ? std::string("true") : std::string("false");
        });
        runtime.bind("engine.input","pointerDelta",[this](std::string_view json) {
            if (!script_args(json).empty()) throw std::invalid_argument("input.pointerDelta()");
            return std::string("{\"x\":")+std::to_string(script_mouse_dx)+
                ",\"y\":"+std::to_string(script_mouse_dy)+"}";
        });
        runtime.bind("engine.input","wheelDelta",[this](std::string_view json) {
            if (!script_args(json).empty()) throw std::invalid_argument("input.wheelDelta()");
            // One wheel notch is 120 input-provider units; keep precision for trackpads.
            return std::to_string(static_cast<double>(script_wheel_vertical)/120.0);
        });
        runtime.bind("engine.input","gamepad",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.size()!=1) throw std::invalid_argument("input.gamepad(index)");
            auto index=script_index(args[0]);
            if(index>=script_gamepads.size()) throw std::out_of_range("Gamepad index");
            auto const& p=script_gamepads[index];
            if(!p.connected) return std::string("null");
            return std::string("{\"buttons\":")+std::to_string(p.buttons)+
                ",\"leftX\":"+std::to_string(p.left_x)+
                ",\"leftY\":"+std::to_string(p.left_y)+
                ",\"rightX\":"+std::to_string(p.right_x)+
                ",\"rightY\":"+std::to_string(p.right_y)+
                ",\"leftTrigger\":"+std::to_string(p.left_trigger)+
                ",\"rightTrigger\":"+std::to_string(p.right_trigger)+"}";
        });
        runtime.bind("engine.camera","available",[this](std::string_view json) {
            if (!script_args(json).empty())
                throw std::invalid_argument("camera.available()");
            return fps_camera ? std::string("true") : std::string("false");
        });
        runtime.bind("engine.camera","setMode",[this](std::string_view json) {
            auto const args=script_args(json);
            if (args.size()<1 || args.size()>2 || !fps_camera)
                throw std::invalid_argument("camera.setMode(name,transitionSeconds=0.35)");
            static const std::map<std::string,CameraMode> modes={
                {"first_person",CameraMode::first_person},
                {"third_person",CameraMode::third_person},
                {"vehicle_chase",CameraMode::vehicle_chase},
                {"orbit",CameraMode::orbit},
                {"free",CameraMode::free},
                {"cinematic",CameraMode::cinematic}};
            auto const mode=modes.find(args[0]);
            if (mode==modes.end()) throw std::invalid_argument("Unknown camera mode");
            auto const seconds=args.size()==2 ? script_number(args[1]) : 0.35;
            fps_camera->set_mode(mode->second,static_cast<float>(seconds));
            return std::string("null");
        });
        runtime.bind("engine.camera","mode",[this](std::string_view json) {
            if (!script_args(json).empty() || !fps_camera)
                throw std::invalid_argument("camera.mode()");
            switch (fps_camera->mode()) {
            case CameraMode::first_person: return script_json_quote("first_person");
            case CameraMode::third_person: return script_json_quote("third_person");
            case CameraMode::vehicle_chase: return script_json_quote("vehicle_chase");
            case CameraMode::orbit: return script_json_quote("orbit");
            case CameraMode::free: return script_json_quote("free");
            case CameraMode::cinematic: return script_json_quote("cinematic");
            }
            throw std::runtime_error("Invalid camera mode");
        });
        runtime.bind("engine.camera","setFollowDistance",
            [this](std::string_view json) {
                auto const args=script_args(json);
                if (args.size()!=1 || !fps_camera)
                    throw std::invalid_argument("camera.setFollowDistance(metres)");
                auto settings=fps_camera->settings();
                settings.third_person_distance=static_cast<float>(script_number(args[0]));
                fps_camera->configure(settings);
                return std::string("null");
            });
        runtime.bind("engine.camera","followDistance",[this](std::string_view json) {
            if (!script_args(json).empty() || !fps_camera)
                throw std::invalid_argument("camera.followDistance()");
            return std::to_string(fps_camera->settings().third_person_distance);
        });
        runtime.bind("engine.camera","setAngles",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=2 || !scripted_first_person)
                throw std::invalid_argument("camera.setAngles(yaw,pitch)");
            auto yaw=script_number(args[0]),pitch=script_number(args[1]);
            if (std::abs(yaw)>1000000 || std::abs(pitch)>1.55)
                throw std::invalid_argument("Camera angles outside bounds");
            script_yaw=static_cast<float>(std::remainder(yaw,6.283185307179586));
            script_pitch=static_cast<float>(pitch);
            return std::string("null");
        });
        runtime.bind("engine.camera","angles",[this](std::string_view json) {
            if (!script_args(json).empty()) throw std::invalid_argument("camera.angles()");
            return std::string("{\"yaw\":")+std::to_string(script_yaw)+
                ",\"pitch\":"+std::to_string(script_pitch)+"}";
        });
        runtime.bind("engine.camera","cut",[this](std::string_view json) {
            if (!script_args(json).empty() || !fps_camera)
                throw std::invalid_argument("camera.cut()");
            // Explicit teleport / shot boundary. Keep active cinematic
            // track time, session state, and plugin-owned resources intact.
            fps_camera->cut();
            return std::string("null");
        });
        runtime.bind("engine.camera","pushShot",[this](std::string_view json) {
            auto const args=script_args(json);
            if(args.size()!=10 || !fps_camera)
                throw std::invalid_argument(
                    "camera.pushShot(source,x,y,z,yaw,pitch,roll,fov,priority,blend)");
            CameraShot shot;
            shot.source=args[0];
            shot.pose.position={
                static_cast<float>(script_number(args[1])),
                static_cast<float>(script_number(args[2])),
                static_cast<float>(script_number(args[3]))};
            shot.pose.yaw=static_cast<float>(script_number(args[4]));
            shot.pose.pitch=static_cast<float>(script_number(args[5]));
            shot.pose.roll=static_cast<float>(script_number(args[6]));
            shot.pose.fov_y_degrees=static_cast<float>(script_number(args[7]));
            shot.pose.near_plane=fps_camera->settings().near_plane;
            shot.pose.far_plane=fps_camera->settings().far_plane;
            auto const priority=script_number(args[8]);
            if(priority<-100. || priority>100. ||
               std::floor(priority)!=priority)
                throw std::invalid_argument("Camera shot priority must be an integer -100..100");
            shot.priority=static_cast<int>(priority);
            shot.blend_seconds=static_cast<float>(script_number(args[9]));
            fps_camera->push_shot(std::move(shot));
            return std::string("null");
        });
        runtime.bind("engine.camera","clearShot",[this](std::string_view json) {
            auto const args=script_args(json);
            if(args.size()!=1 || !fps_camera)
                throw std::invalid_argument("camera.clearShot(source)");
            fps_camera->clear_shot(args[0]);
            return std::string("null");
        });
        runtime.bind("engine.camera","clearShots",[this](std::string_view json) {
            if(!script_args(json).empty() || !fps_camera)
                throw std::invalid_argument("camera.clearShots()");
            fps_camera->clear_shots();
            return std::string("null");
        });
        runtime.bind("engine.camera","activeShot",[this](std::string_view json) {
            if(!script_args(json).empty() || !fps_camera)
                throw std::invalid_argument("camera.activeShot()");
            return script_json_quote(fps_camera->active_shot());
        });
        runtime.bind("engine.camera","applyPreset",[this](std::string_view json) {
            auto const args=script_args(json);
            if(args.size()!=1 || !fps_camera || !script_assets)
                throw std::invalid_argument("camera.applyPreset(presetId) requires project VFS");
            auto const presets=load_camera_presets(*script_assets);
            auto const it=std::find_if(presets.begin(),presets.end(),
                [&](CameraShot const& preset) {
                    return preset.source==args[0];
                });
            if(it==presets.end())
                throw std::invalid_argument("Camera preset source not found");
            fps_camera->push_shot(*it);
            return std::string("null");
        });
        runtime.bind("engine.camera","shake",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=2 || !fps_camera)
                throw std::invalid_argument("camera.shake(amplitude,seconds)");
            auto a=script_number(args[0]),t=script_number(args[1]);
            if (a<0 || a>1 || t<0 || t>10) throw std::invalid_argument("Camera shake limits");
            fps_camera->shake(static_cast<float>(a),static_cast<float>(t));
            if(std::getenv("NORTHSTARCPP_QA_WEAPON_TRACE"))
                std::fprintf(stderr,
                    "[WEAPON FX TRACE] camera recoil amplitude=%.4f duration=%.3f\n",a,t);
            return std::string("null");
        });
        runtime.bind("engine.camera","recoil",[this](std::string_view json) {
            auto const args=script_args(json);
            if(args.size()!=3 || !fps_camera)
                throw std::invalid_argument(
                    "camera.recoil(pitchRadians,yawRadians,seconds)");
            fps_camera->recoil(
                static_cast<float>(script_number(args[0])),
                static_cast<float>(script_number(args[1])),
                static_cast<float>(script_number(args[2])));
            if(std::getenv("NORTHSTARCPP_QA_WEAPON_TRACE"))
                std::fprintf(stderr,
                    "[WEAPON FX TRACE] angular recoil pitch=%s yaw=%s\n",
                    args[0].c_str(),args[1].c_str());
            return std::string("null");
        });
        runtime.bind("engine.camera","playCutscene",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.empty() || args.size()>4 || !fps_camera || !script_assets)
                throw std::invalid_argument(
                    "camera.playCutscene(vfsPath,loop=false,entry=0.55,exit=0.55)");
            const bool loop=args.size()>1 && args[1]=="true";
            if(args.size()>1 && args[1]!="true" && args[1]!="false")
                throw std::invalid_argument("Cutscene loop must be boolean");
            auto const entry=args.size()>2?script_number(args[2]):0.55;
            auto const exit=args.size()>3?script_number(args[3]):0.55;
            // VFS resolves this through the currently mounted project/provider.
            auto scene=load_camera_cutscene(*script_assets,args[0]);
            fps_camera->play_cutscene(std::move(scene),loop,
                static_cast<float>(entry),static_cast<float>(exit));
            return std::string("null");
        });
        runtime.bind("engine.camera","stopCutscene",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.size()>1 || !fps_camera)
                throw std::invalid_argument("camera.stopCutscene(exit=0.55)");
            auto const exit=args.empty()?0.55:script_number(args[0]);
            fps_camera->stop_cutscene(static_cast<float>(exit));
            return std::string("null");
        });
        runtime.bind("engine.camera","pauseCutscene",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.size()!=1 || !fps_camera ||
               (args[0]!="true" && args[0]!="false"))
                throw std::invalid_argument("camera.pauseCutscene(paused)");
            fps_camera->pause_cutscene(args[0]=="true");
            return std::string("null");
        });
        runtime.bind("engine.camera","seekCutscene",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.size()!=1 || !fps_camera)
                throw std::invalid_argument("camera.seekCutscene(seconds)");
            fps_camera->seek_cutscene(static_cast<float>(script_number(args[0])));
            return std::string("null");
        });
        runtime.bind("engine.camera","cutsceneState",[this](std::string_view json) {
            if(!script_args(json).empty() || !fps_camera)
                throw std::invalid_argument("camera.cutsceneState()");
            return std::string("{\"active\":")+
                (fps_camera->cutscene_active()?"true":"false")+
                ",\"time\":"+std::to_string(fps_camera->cutscene_time())+
                ",\"shot\":"+script_json_quote(fps_camera->cutscene_shot())+"}";
        });
        runtime.bind("engine.camera","lensState",[this](std::string_view json) {
            if(!script_args(json).empty() || !fps_camera)
                throw std::invalid_argument("camera.lensState()");
            auto const lens=fps_camera->cinematic_lens();
            return std::string("{\"active\":")+
                (fps_camera->cutscene_lens_active()?"true":"false")+
                ",\"focusMetres\":"+std::to_string(lens.focus_metres)+
                ",\"apertureF\":"+std::to_string(lens.aperture_f)+
                ",\"exposureEV\":"+std::to_string(lens.exposure_ev)+
                ",\"motionBlur\":"+std::to_string(lens.motion_blur)+
                ",\"letterbox\":"+std::to_string(lens.letterbox)+
                ",\"dofStrength\":"+std::to_string(lens.dof_strength)+"}";
        });
        runtime.bind("engine.camera","playTrack",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.empty() || args.size()>3 || !fps_camera || !script_assets)
                throw std::invalid_argument("camera.playTrack(vfsPath,loop=false,blend=0.5)");
            if(args.size()>1 && args[1]!="true" && args[1]!="false")
                throw std::invalid_argument("Track loop must be boolean");
            auto const blend=args.size()>2?script_number(args[2]):0.5;
            fps_camera->set_track(load_camera_track(*script_assets,args[0]),
                args.size()>1 && args[1]=="true");
            fps_camera->set_mode(CameraMode::cinematic,static_cast<float>(blend));
            return std::string("null");
        });
        runtime.bind("engine.camera","setZoom",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=1 || !fps_camera) throw std::invalid_argument("camera.setZoom(level)");
            auto zoom=script_number(args[0]);
            if (zoom<0 || zoom>1) throw std::invalid_argument("Zoom must be 0..1");
            fps_camera->set_zoom(static_cast<float>(zoom));
            return std::string("null");
        });
        runtime.bind("engine.character","load",[this](std::string_view json) {
            if(!script_assets || !scripted_game)
                throw std::runtime_error("character.load requires an active scripted project");
            auto spec=parse_script_character(json);
            script_character=std::move(spec);
            script_character_pose.reset();
            script_character_renderable=false;
            script_character_dirty=true;
            return std::string("null");
        });
        runtime.bind("engine.character","pose",[this](std::string_view json) {
            auto const args=script_args(json);
            if((args.size()!=3 && args.size()!=4) || !script_character || !preview)
                throw std::invalid_argument("character.pose(sceneIndex,heading,animation,arms?)");
            if(args.size()==4 && args[3]!="true" && args[3]!="false")
                throw std::invalid_argument("character.pose arms must be boolean");
            auto const index=script_index(args[0]);
            if(index>=preview->scene().objects.size())
                throw std::out_of_range("Character scene index is invalid");
            auto const yaw=script_number(args[1]);
            if(std::abs(yaw)>1000000)
                throw std::invalid_argument("Character heading out of range");
            auto const& name=args[2];
            auto const& clips=script_character->animations;
            if(std::none_of(clips.begin(),clips.end(),
               [&](auto const& clip){return clip.name==name;}))
                throw std::invalid_argument("Character animation is not defined by project");
            script_character_pose=CharacterPoseRequest{index,static_cast<float>(yaw),name,
                args.size()==4 && args[3]=="true"};
            return std::string("null");
        });
        runtime.bind("engine.character","hide",[this](std::string_view json) {
            if(!script_args(json).empty())
                throw std::invalid_argument("character.hide()");
            script_character_pose.reset();
            return std::string("null");
        });
        runtime.bind("engine.character","clear",[this](std::string_view json) {
            if(!script_args(json).empty())
                throw std::invalid_argument("character.clear()");
            script_character.reset();
            script_character_pose.reset();
            script_character_renderable=false;
            script_character_dirty=true;
            return std::string("null");
        });
        runtime.bind("engine.player","controlledEntity",[this](std::string_view json) {
            if(!script_args(json).empty() || !preview)
                throw std::invalid_argument("player.controlledEntity()");
            auto const id=preview->controlled_entity();
            return id?script_json_quote(std::to_string(id)):std::string("null");
        });
        runtime.bind("engine.player","controlledIndex",[this](std::string_view json) {
            if(!script_args(json).empty() || !preview)
                throw std::invalid_argument("player.controlledIndex()");
            auto index=preview->controlled_index();
            return index?std::to_string(*index):std::string("-1");
        });
        runtime.bind("engine.player","animationState",[this](std::string_view json) {
            if(!script_args(json).empty()||!preview)
                throw std::invalid_argument("player.animationState()");
            auto frame=preview->player_animation();
            if(!frame)return std::string("null");
            return std::string("{\"pawn\":")+std::to_string(frame->pawn_id)+
                ",\"mesh\":"+std::to_string(frame->mesh_id)+
                ",\"state\":"+script_json_quote(frame->state)+
                ",\"clip\":"+script_json_quote(frame->clip)+
                ",\"previousClip\":"+script_json_quote(frame->previous_clip)+
                ",\"blend\":"+std::to_string(frame->blend_alpha)+
                ",\"time\":"+std::to_string(frame->playback_seconds)+
                ",\"grounded\":"+(frame->grounded?"true":"false")+"}";
        });
        runtime.bind("engine.player","possess",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.size()!=1 || !preview || !scripted_game ||
               args[0].empty() ||
               !std::all_of(args[0].begin(),args[0].end(),
                   [](unsigned char ch){return ch>='0' && ch<='9';}))
                throw std::invalid_argument("player.possess(entityId)");
            auto const id=std::stoull(args[0]);
            preview->possess(id);
            auto const tune=gameplay::camera_rig(preview->scene(),id);
            script_eye_height=tune.eye_height;
            auto const fov=std::clamp(tune.fov_degrees,25.f,120.f);
            fps_camera_settings.default_fov_y=fov;
            if(fps_camera) {
                auto settings=fps_camera->settings();
                settings.default_fov_y=fov;
                fps_camera->configure(settings);
                (void)gameplay::configure_player_camera(
                    *fps_camera,preview->scene(),id);
                fps_camera->reset();
            }
            return std::string("true");
        });
        runtime.bind("engine.player","unpossess",[this](std::string_view json) {
            if(!script_args(json).empty() || !preview || !scripted_game)
                throw std::invalid_argument("player.unpossess()");
            preview->unpossess();
            script_move_x=script_move_z=0.f;
            if(fps_camera)fps_camera->reset();
            return std::string("null");
        });
        runtime.bind("engine.player","sprint",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.size()!=1 || !preview || !scripted_game ||
               (args[0]!="true" && args[0]!="false"))
                throw std::invalid_argument("player.sprint(active)");
            preview->set_sprinting(args[0]=="true");
            return std::string("null");
        });
        runtime.bind("engine.player","move",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=2 || !scripted_game)
                throw std::invalid_argument("player.move(worldX,worldZ)");
            auto x=script_number(args[0]),z=script_number(args[1]);
            if (std::abs(x)>1 || std::abs(z)>1)
                throw std::invalid_argument("Player movement components must be -1..1");
            script_move_x=static_cast<float>(x);
            script_move_z=static_cast<float>(z);
            return std::string("null");
        });
        runtime.bind("engine.player","motion",[this](std::string_view json) {
            if(!script_args(json).empty())
                throw std::invalid_argument("player.motion()");
            return std::string("{\"x\":")+std::to_string(script_move_x)+
                ",\"z\":"+std::to_string(script_move_z)+"}";
        });
        runtime.bind("engine.player","ground",[this](std::string_view json) {
            if(!script_args(json).empty() || !preview)
                throw std::invalid_argument("player.ground()");
            auto const& objects=preview->scene().objects;
            auto idx=preview->controlled_index();
            if(!idx || *idx>=objects.size())
                return std::string("{\"grounded\":false,\"surface\":\"stone\"}");
            auto const index=*idx;
            auto const& pos=objects[index].position; // Pawn feet
            auto const from=Vector3{pos.x,pos.y+0.22f,pos.z};
            auto const to=Vector3{pos.x,pos.y-0.32f,pos.z};
            auto const hit=preview->sweep_sphere(from,to,0.10f,index);
            auto const contact=hit && hit->normal.y>=0.55f;
            // Jolt/Bullet both have an implicit y=0 plane; its physics hit
            // has no scene object index. Treat that as stone unless tagged.
            auto const implicit_floor=pos.y>=-0.1f && pos.y<=0.14f;
            if(!contact && !implicit_floor)
                return std::string("{\"grounded\":false,\"surface\":\"stone\"}");
            std::string surface="stone";
            auto const hit_index=contact ? hit->object_index : UINT32_MAX;
            // A contact on a specific triangle takes precedence over
            // object names, collider tags and the implicit floor.
            if(contact && hit->material_id>physics_surface::unknown)
                surface=std::string(physics_surface::name(hit->material_id));
            else if(hit_index<objects.size()) {
                auto const& terrain=objects[hit_index];
                auto classify=[](std::string text) {
                    std::transform(text.begin(),text.end(),text.begin(),
                        [](unsigned char c){return static_cast<char>(std::tolower(c));});
                    constexpr std::array<std::string_view,8> types={{
                        "dirt","grass","metal","mud","snow","stone","water","wood"}};
                    for(auto const category:types) {
                        if(text.find("surface:"+std::string(category))!=std::string::npos ||
                           text.find("surface_"+std::string(category))!=std::string::npos)
                            return std::string(category);
                    }
                    for(auto const category:types)
                        if(text.find(std::string(category))!=std::string::npos)
                            return std::string(category);
                    return std::string("stone");
                };
                surface=classify(terrain.node.layer+" "+terrain.name+" "+terrain.node.asset);
            }
            return std::string("{\"grounded\":true,\"surface\":")+
                script_json_quote(surface)+
                ",\"materialId\":"+
                std::to_string(contact?hit->material_id:0)+
                ",\"triangleIndex\":"+
                std::to_string(contact?hit->triangle_index:UINT32_MAX)+
                ",\"objectIndex\":" + std::to_string(hit_index)+
                "}";
        });
        runtime.bind("engine.player","jump",[this](std::string_view json) {
            if (!script_args(json).empty() || !preview || !scripted_game)
                throw std::invalid_argument("player.jump()");
            preview->jump();
            return std::string("null");
        });
        runtime.bind("engine.player","eye",[this](std::string_view json) {
            if (!script_args(json).empty() || !preview)
                throw std::invalid_argument("player.eye()");
            auto const& objects=preview->scene().objects;
            auto idx=preview->controlled_index();
            if(!idx || *idx>=objects.size())
                throw std::runtime_error("No possessed Pawn in scene");
            auto const eye=gameplay::camera_anchor(
                preview->scene(),objects[*idx].node.id,script_eye_height);
            return std::string("{\"x\":")+std::to_string(eye.x)+
                ",\"y\":"+std::to_string(eye.y)+
                ",\"z\":"+std::to_string(eye.z)+"}";
        });
        runtime.bind("engine.player","setEyeHeight",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=1) throw std::invalid_argument("player.setEyeHeight(height)");
            auto height=script_number(args[0]);
            if (height<0 || height>5) throw std::invalid_argument("Eye height must be 0..5");
            script_eye_height=static_cast<float>(height);
            return std::string("null");
        });
        runtime.bind("engine.physics","sweepSphere",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=8 || !preview)
                throw std::invalid_argument("physics.sweepSphere(x,y,z,toX,toY,toZ,radius,ignore)");
            Vector3 begin{static_cast<float>(script_number(args[0])),
                          static_cast<float>(script_number(args[1])),
                          static_cast<float>(script_number(args[2]))};
            Vector3 end{static_cast<float>(script_number(args[3])),
                        static_cast<float>(script_number(args[4])),
                        static_cast<float>(script_number(args[5]))};
            auto radius=script_number(args[6]);
            if (radius<=0 || radius>100) throw std::invalid_argument("Invalid sweep radius");
            auto const ignore=args[7]=="-1"
                ? static_cast<std::size_t>(-1) : script_index(args[7]);
            auto hit=preview->sweep_sphere(begin,end,static_cast<float>(radius),
                                           ignore);
            if (!hit || hit->object_index==UINT32_MAX) return std::string("null");
            return std::string("{\"index\":")+std::to_string(hit->object_index)+
                ",\"fraction\":"+std::to_string(hit->fraction)+
                ",\"materialId\":"+std::to_string(hit->material_id)+
                ",\"triangleIndex\":"+std::to_string(hit->triangle_index)+"}";
        });
        runtime.bind("engine.scene","objects",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()>2 || !preview) throw std::invalid_argument("scene.objects(start,count)");
            auto start=args.empty()?std::size_t(0):script_index(args[0]);
            auto count=args.size()<2?std::size_t(256):script_index(args[1]);
            if (count>512) throw std::invalid_argument("Scene query page >512");
            std::string result="[";
            auto const& objects=preview->scene().objects;
            for(auto i=start;i<objects.size() && i-start<count;++i) {
                if(i!=start) result+=',';
                auto const& obj=objects[i];
                if (i>=preview->authored_object_count() && !preview->runtime_alive(i)) {
                    result+="null";
                    continue;
                }
                result+="{\"index\":"+std::to_string(i)+
                    ",\"name\":"+script_json_quote(obj.name)+
                    ",\"id\":"+script_json_quote(std::to_string(obj.node.id))+
                    ",\"parent\":"+script_json_quote(std::to_string(obj.node.parent))+
                    ",\"script\":"+script_json_quote(obj.node.script)+
                    ",\"mass\":"+std::to_string(obj.mass)+
                    ",\"size\":"+std::to_string(obj.size)+
                    ",\"player\":"+(scene_pawn::is_local(preview->scene(),obj)?"true":"false")+
                    ",\"position\":{\"x\":"+std::to_string(obj.position.x)+
                    ",\"y\":"+std::to_string(obj.position.y)+
                    ",\"z\":"+std::to_string(obj.position.z)+"}}";
            }
            return result+"]";
        });
        runtime.bind("engine.scene","moveStatic",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=4 || !preview) throw std::invalid_argument("scene.moveStatic(index,x,y,z)");
            auto i=script_index(args[0]);
            auto const& objs=preview->scene().objects;
            if (i>=objs.size() || objs[i].mass!=0.f || scene_pawn::is_pawn(preview->scene(),objs[i]))
                throw std::invalid_argument("Scene entity must be static");
            auto x=script_number(args[1]),y=script_number(args[2]),z=script_number(args[3]);
            if (std::max({std::abs(x),std::abs(y),std::abs(z)})>1000000)
                throw std::invalid_argument("Position out of bounds");
            Vector3 pos{static_cast<float>(x),static_cast<float>(y),static_cast<float>(z)};
            preview->move_static(i,pos);
            if(script_ecs && i<script_ecs->size()) {
                NsPosition updated{pos.x,pos.y,pos.z};
                script_ecs->set_component(script_ecs->at(i),NS_COMPONENT_POSITION,
                                          &updated,sizeof(updated));
            }
            return std::string("null");
        });
        runtime.bind("engine.scene","rotateStatic",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.size()!=4 || !preview)
                throw std::invalid_argument("scene.rotateStatic(index,x,y,z)");
            auto i=script_index(args[0]);
            auto const& objects=preview->scene().objects;
            if(i>=objects.size() || objects[i].mass!=0.f ||
               scene_pawn::is_pawn(preview->scene(),objects[i]))
                throw std::invalid_argument("Scene rotation requires a static non-Pawn");
            auto x=script_number(args[1]),y=script_number(args[2]),
                 z=script_number(args[3]);
            if(std::max({std::abs(x),std::abs(y),std::abs(z)})>360000.)
                throw std::invalid_argument("Rotation is outside limits");
            preview->rotate_runtime(i,Vector3{
                static_cast<float>(x),static_cast<float>(y),
                static_cast<float>(z)});
            return std::string("null");
        });
        runtime.bind("engine.scene","setColor",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.size()!=2 || !preview) throw std::invalid_argument("scene.setColor(index,rgb)");
            auto idx=script_index(args[0]),rgb=script_index(args[1]);
            if(idx>=preview->scene().objects.size() || rgb>0xffffff)
                throw std::invalid_argument("Scene object/color invalid");
            script_object_colors[idx]=static_cast<std::uint32_t>(rgb);
            return std::string("null");
        });
        runtime.bind("engine.scene","setAnimation",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.size()!=2 || !preview)
                throw std::invalid_argument("scene.setAnimation(index,clip_reference)");
            auto const index=script_index(args[0]);
            auto const& objects=preview->scene().objects;
            if(index>=objects.size() ||
               (index>=preview->authored_object_count() &&
                !preview->runtime_alive(index)))
                throw std::out_of_range("Invalid or destroyed scene entity");
            auto const& object=objects[index];
            if(!object.node.id)
                throw std::invalid_argument("Animation requires a stable scene entity ID");
            auto const& model=object.node.asset;
            auto const selector=model.find('@');
            auto const native=model.substr(0,selector);
            if(native.size()<5 ||
               (native.compare(native.size()-4,4,".ydd")!=0 &&
                native.compare(native.size()-4,4,".ydr")!=0))
                throw std::invalid_argument("Animation requires a native YDD/YDR asset");
            auto const& ref=args[1];
            if(!ref.empty()) {
                auto const sep=ref.rfind('@');
                if(ref.size()>512 || sep==std::string::npos || sep<4 ||
                   sep+1>=ref.size() || ref.substr(sep-4,4)!=".ycd" ||
                   ref.find("..")!=std::string::npos ||
                   ref.find('\\')!=std::string::npos ||
                   ref.find(':')!=std::string::npos ||
                   ref.find('\0')!=std::string::npos)
                    throw std::invalid_argument("Invalid native YCD clip reference");
                script_model_animations[object.node.id]=ref;
            } else script_model_animations.erase(object.node.id);
            return std::string("null");
        });
        // Generic presentation mode for one existing skinned scene actor.
        // No character respawn, duplicate FPS prefab, or permanent scene edit.
        runtime.bind("engine.scene","setVisualMode",
            [this,script_owner](std::string_view json) {
                auto args=script_args(json);
                if((args.size()!=5 && args.size()!=6 && args.size()!=7) || !preview)
                    throw std::invalid_argument(
                        "scene.setVisualMode(index,mode,forward,drop,pitch[,right[,ads]])");
                auto const index=script_index(args[0]);
                auto const& objects=preview->scene().objects;
                if(index>=objects.size() ||
                   (index>=preview->authored_object_count() &&
                    !preview->runtime_alive(index)))
                    throw std::out_of_range("Invalid visual actor");
                auto const& actor=objects[index];
                // A saved SkeletalMesh is a legitimate project-owned visual;
                // it must not be re-instantiated merely to enable FPS arms.
                bool const authoredVisual=index<preview->authored_object_count() &&
                    std::any_of(preview->scene().components.begin(),
                                preview->scene().components.end(),
                        [&](auto const& c) {
                            return c.entity_id==actor.node.id && c.version==1 &&
                                (c.type=="northstar.gameplay.skeletal_mesh" ||
                                 c.type=="northstar.editor.visual_prefab");
                        });
                bool const ownedRuntime=script_spawn_owners.count(actor.node.id) &&
                    script_spawn_owners.at(actor.node.id)==script_owner;
                if(!actor.node.id || actor.mass!=0.f ||
                   (!authoredVisual && !ownedRuntime))
                    throw std::invalid_argument(
                        "Visual mode requires an authored SkeletalMesh or an owned static model");
                if(args[1]=="world") {
                    script_model_visual_modes.erase(actor.node.id);
                    return std::string("null");
                }
                if(args[1]!="first_person_arms" && args[1]!="first_person_body")
                    throw std::invalid_argument("Unsupported visual mode");
                auto const model=actor.node.asset;
                if(model.size()<4 ||
                   model.substr(model.size()-4)!=".ydd")
                    throw std::invalid_argument("FPS arms require a skinned YDD");
                float const forward=static_cast<float>(script_number(args[2]));
                float const drop=static_cast<float>(script_number(args[3]));
                float const arm_pitch=static_cast<float>(script_number(args[4]));
                float const right=args.size()>=6?static_cast<float>(script_number(args[5])):0.f;
                if(!std::isfinite(forward) || forward<0.f || forward>3.f ||
                   !std::isfinite(drop) || drop<0.f || drop>3.f ||
                   !std::isfinite(arm_pitch) || arm_pitch< -180.f ||
                   arm_pitch>180.f || !std::isfinite(right) || std::abs(right)>1.f)
                    throw std::invalid_argument(
                        "FPS visual offsets outside allowed bounds");
                std::ostringstream payload;
                payload.imbue(std::locale::classic());
                payload << args[1] << ' ' << forward << ' '
                        << drop << ' ' << arm_pitch << ' ' << right;
                if(args.size()==7) {
                    if(args[6]!="true" && args[6]!="false")
                        throw std::invalid_argument("Visual ADS flag must be boolean");
                    payload << ' ' << (args[6]=="true"?"ads":"ready");
                }
                script_model_visual_modes[actor.node.id]=payload.str();
                return std::string("null");
            });
        runtime.bind("engine.scene","attachBone",[this,script_owner](std::string_view json) {
            auto args=script_args(json);
            if((args.size()!=9 && args.size()!=10) || !preview)
                throw std::invalid_argument("scene.attachBone(child,parent,bone,offset,rotation[,alignment])");
            auto const childIndex=script_index(args[0]);
            auto const parentIndex=script_index(args[1]);
            auto const& objects=preview->scene().objects;
            if(childIndex>=objects.size() || parentIndex>=objects.size() ||
               childIndex==parentIndex ||
               !preview->runtime_alive(childIndex) ||
               (parentIndex>=preview->authored_object_count() &&
                !preview->runtime_alive(parentIndex)))
                throw std::invalid_argument("Attachment entities must be live");
            auto const& child=objects[childIndex];
            auto const& parent=objects[parentIndex];
            if(child.mass!=0.f || !child.node.id || !parent.node.id ||
               !script_spawn_owners.count(child.node.id) ||
               script_spawn_owners.at(child.node.id)!=script_owner)
                throw std::invalid_argument("Only an owned static visual can attach to a bone");
            auto const& bone=args[2];
            if(bone.empty() || bone.size()>96 ||
               !std::all_of(bone.begin(),bone.end(),[](unsigned char c) {
                   return (c>='a' && c<='z') || (c>='A' && c<='Z') ||
                          (c>='0' && c<='9') || c=='_';
               }))throw std::invalid_argument("Unsafe skeleton joint name");
            std::string payload=std::to_string(parent.node.id)+" "+bone;
            for(std::size_t i=3;i<9;++i) {
                auto const value=script_number(args[i]);
                if(!std::isfinite(value) || std::abs(value)>360.)
                    throw std::invalid_argument("Unsafe socket transform");
                payload+=" "+std::to_string(value);
            }
            if(args.size()==10) {
                if(args[9]!="socket" && args[9]!="camera_forward")
                    throw std::invalid_argument("Unknown bone alignment mode");
                payload+=" "+args[9];
            }
            script_model_bone_attachments[child.node.id]=std::move(payload);
            return std::string("null");
        });
        runtime.bind("engine.scene","attachmentPoint",[this](std::string_view json) {
            auto const args=script_args(json);
            if(args.size()!=4 || !preview || !graphics)
                throw std::invalid_argument(
                    "scene.attachmentPoint(childIndex,x,y,z)");
            const auto index=script_index(args[0]);
            if(index>=preview->scene().objects.size())return std::string("null");
            auto const& child=preview->scene().objects[index];
            if(!child.node.id)return std::string("null");
            const bool attached=script_model_bone_attachments.count(
                child.node.id)!=0;
            const Vector3 local{
                static_cast<float>(script_number(args[1])),
                static_cast<float>(script_number(args[2])),
                static_cast<float>(script_number(args[3]))};
            if(!local.valid() || local.magnitude()>5.f)
                throw std::invalid_argument("Unsafe weapon local muzzle point");
            // Attached weapons use the EXACT last-published skinned pose.
            // Free weapons use their scene/physics world transform. Never
            // substitute camera/hand offsets when the attachment is missing.
            std::optional<Vector3> point;
            if(attached) {
                point=graphics->attached_local_point(
                    child.node.id,local,fps_camera ? &fps_pose : nullptr);
            } else {
                const auto scaled=Vector3{
                    local.x*child.node.scale.x*child.size,
                    local.y*child.node.scale.y*child.size,
                    local.z*child.node.scale.z*child.size};
                if(child.mass>0.f) {
                    const auto orientation=preview->orientation(index);
                    point=child.position+
                        orientation.transform_direction(scaled);
                } else {
                    constexpr float radians=.01745329251994329577f;
                    auto const& angles=child.node.rotation;
                    const auto rotation=
                        math::create_rotation_x(angles.x*radians)*
                        math::create_rotation_y(angles.y*radians)*
                        math::create_rotation_z(angles.z*radians);
                    point=child.position+rotation.transform_direction(scaled);
                }
            }
            if(!point || !point->valid())return std::string("null");
            return std::string("{\"x\":")+std::to_string(point->x)+
                ",\"y\":"+std::to_string(point->y)+
                ",\"z\":"+std::to_string(point->z)+"}";
        });
        runtime.bind("engine.scene","detachBone",[this,script_owner](std::string_view json) {
            auto args=script_args(json);
            if(args.size()!=1 || !preview)
                throw std::invalid_argument("scene.detachBone(childIndex)");
            auto const index=script_index(args[0]);
            if(index>=preview->scene().objects.size() ||
                !preview->runtime_alive(index))
                throw std::invalid_argument("Invalid detached object");
            auto id=preview->scene().objects[index].node.id;
            if(!script_spawn_owners.count(id) ||
                script_spawn_owners.at(id)!=script_owner)
                throw std::invalid_argument("Attachment owner mismatch");
            script_model_bone_attachments.erase(id);
            return std::string("null");
        });
        runtime.bind("engine.ui","setTitle",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.size()!=1 || args[0].size()>240)
                throw std::invalid_argument("ui.setTitle(text)");
            auto title=to_wide(args[0]);
            script_window_title=title;
            if(window) SetWindowTextW(window,title.c_str());
            return std::string("null");
        });
        runtime.bind("engine.ui","viewportSize",[this](std::string_view json) {
            if(!script_args(json).empty())
                throw std::invalid_argument("ui.viewportSize() takes no arguments");
            RECT area{};
            if(window)GetClientRect(window,&area);
            auto const width=std::max<LONG>(0,area.right-area.left);
            auto const height=std::max<LONG>(0,area.bottom-area.top);
            return std::string("{\"width\":")+std::to_string(width)+
                ",\"height\":"+std::to_string(height)+"}";
        });
        runtime.bind("engine.ui","hotReloadEnabled",[this](std::string_view json) {
            if(!script_args(json).empty())
                throw std::invalid_argument("ui.hotReloadEnabled() takes no arguments");
            return config.packaged_game || !config.archive_path.empty()?"false":"true";
        });
        runtime.bind("engine.ui","readAsset",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.size()!=1 || !script_assets)
                throw std::invalid_argument("ui.readAsset(relativeNuiPath)");
            auto const& path=args.front();
            if(path.size()<7 || path.size()>240 ||
               path.compare(0,3,"ui/")!=0 ||
               path.compare(path.size()-4,4,".nui")!=0 ||
               path.find("..")!=std::string::npos ||
               path.find('\\')!=std::string::npos ||
               path.find(':')!=std::string::npos ||
               path.find("//")!=std::string::npos)
                throw std::invalid_argument("UI assets must use safe ui/*.nui VFS paths");
            for(auto const& part:fs::u8path(path))
                if(part.empty() || part=="." || part=="..")
                    throw std::invalid_argument("Invalid UI asset path");
            auto const size=script_assets->size(path);
            if(size==0 || size>262144)
                throw std::length_error("UI asset size must be 1..262144 bytes");
            auto bytes=script_assets->read(path);
            if(bytes.size()!=size)
                throw std::runtime_error("UI asset was truncated");
            // Reuse the same JSON escaping helper as other host API returns.
            try {
                auto compiled=nui_xml::compile(std::string_view(
                    reinterpret_cast<char const*>(bytes.data()),bytes.size()));
                return script_json_quote(compiled);
            } catch(std::exception const& error) {
                throw std::invalid_argument(
                    "Malformed UI XML asset '"+path+"': "+error.what());
            }
        });
        runtime.bind("engine.ui","apply",[this](std::string_view json) {
            // Script presents a complete, generic UI document.
            // Rendering is deferred until the GPU viewport is ready.
            auto next=parse_script_ui_document(json);
            if(next.target=="world") {
                auto const id=next.entity_id;
                if(next.nodes.empty())script_world_ui_documents_.erase(id);
                else script_world_ui_documents_[id]=next;
            }else script_ui_document_=next;
            if(graphics)graphics->set_ui_document(std::move(next));
            return std::string("null");
        });
        runtime.bind("engine.session","quit",[this](std::string_view json) {
            if(!script_args(json).empty()) throw std::invalid_argument("session.quit()");
            if(window) PostMessageW(window,WM_CLOSE,0,0);
            return std::string("null");
        });
        runtime.bind("engine.session","pause",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.size()!=1 || (args[0]!="true" && args[0]!="false"))
                throw std::invalid_argument("session.pause(boolean)");
            script_paused=args[0]=="true";
            if(script_paused) release_fps_cursor();
            else restore_gameplay_input_focus();
            return std::string("null");
        });
        runtime.bind("engine.session","scene",[this](std::string_view json) {
            if(!script_args(json).empty())
                throw std::invalid_argument("session.scene()");
            auto path=config.archive_path.empty()
                ? fs::relative(file_path,script_project_root(file_path))
                : file_path;
            return script_json_quote(path.generic_u8string());
        });
        runtime.bind("engine.session","loadScene",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.size()!=1 || args[0].empty() || args[0].size()>240 ||
               editor || !scripted_game || !window ||
               !requested_project_scene_.empty())
                throw std::invalid_argument("session.loadScene(projectRelativeNscn)");
            auto next=fs::u8path(args[0]);
            if(next.is_absolute() || next.has_root_name() ||
               next.extension()!=L".nscn")
                throw std::invalid_argument("Scene must be a relative .nscn path");
            for(auto const& part:next)
                if(part=="." || part==".." || part.empty())
                    throw std::invalid_argument("Scene traversal is prohibited");
            requested_project_scene_=std::move(next);
            if(!PostMessageW(window,WM_APP+28,0,0)) {
                requested_project_scene_.clear();
                throw std::runtime_error("Cannot queue scene transition");
            }
            return std::string("true");
        });
        runtime.bind("engine.session","restart",[this](std::string_view json) {
            if(!script_args(json).empty()) throw std::invalid_argument("session.restart()");
            if(window) PostMessageW(window,WM_APP+27,0,0);
            return std::string("null");
        });
        runtime.bind("engine.audio","playNac",[this](std::string_view json) {
            auto const args=script_args(json);
            if((args.size()!=4 && args.size()!=7) || !script_assets)
                throw std::invalid_argument(
                    "audio.playNac(virtualNacPath,clipPrefix,variation,gain)");
            auto const& asset_path=args[0];
            auto const& prefix=args[1];
            if(asset_path.size()>256 || asset_path.rfind("audio/",0)!=0 ||
               fs::u8path(asset_path).extension()!=".nac" ||
               asset_path.find('\\')!=std::string::npos ||
               asset_path.find(':')!=std::string::npos ||
               prefix.size()>48 || prefix.empty() ||
               !std::all_of(prefix.begin(),prefix.end(),[](unsigned char c){
                   return std::isalnum(c) || c=='_';
               }))
                throw std::invalid_argument("Invalid NAC asset or clip prefix");
            for(auto const& segment:fs::u8path(asset_path))
                if(segment==".." || segment=="." || segment.empty())
                    throw std::invalid_argument("NAC path traversal is forbidden");
            auto const variation=script_index(args[2]);
            auto const gain=script_number(args[3]);
            if(variation>1000000 || gain<0 || gain>2)
                throw std::invalid_argument("Invalid NAC variation or gain");
            auto it=script_nac_cache.find(asset_path);
            if(it==script_nac_cache.end()) {
                if(script_nac_cache.size()>=32 ||
                   script_assets->size(asset_path)>16ull*1024*1024)
                    throw std::length_error("NAC cache limit exceeded");
                auto data=script_assets->read(asset_path);
                std::vector<nac::Track> tracks;
                std::string error;
                if(!nac::decode(data,tracks,error))
                    throw std::runtime_error("Cannot decode NEF8 NAC: "+error);
                it=script_nac_cache.emplace(asset_path,std::move(tracks)).first;
            }
            auto const& tracks=it->second;
            // Exact name wins for one-shot cues (fire_body/reload/equip).
            // Prefix + underscore selects variations in any NAC audio
            // collection without substring ambiguity.
            auto const exact=std::find_if(tracks.begin(),tracks.end(),
                [&](auto const& track){return track.name==prefix;});
            bool const exact_match=exact!=tracks.end();
            std::size_t matches=exact_match?1:0;
            if(!exact_match)
                for(auto const& track:tracks)
                    if(track.name.rfind(prefix+"_",0)==0) ++matches;
            if(!matches)
                throw std::runtime_error("No matching NAC audio clips: "+prefix);
            auto const wanted=variation%matches;
            std::size_t count=0;
            for(auto const& track:tracks) {
                if(exact_match ? track.name!=prefix :
                   track.name.rfind(prefix+"_",0)!=0) continue;
                if(count++ != wanted) continue;
                if(!script_voice_audio)
                    script_voice_audio=std::make_unique<AudioEngine>();
                NorthStarAudioVoiceOptions options{
                    sizeof(NorthStarAudioVoiceOptions),
                    static_cast<float>(gain),0,0,{0,0,0}};
                if(args.size()==7) {
                    const Vector3 pos{
                        static_cast<float>(script_number(args[4])),
                        static_cast<float>(script_number(args[5])),
                        static_cast<float>(script_number(args[6]))};
                    if(!pos.valid() ||
                       std::max({std::abs(pos.x),std::abs(pos.y),
                                 std::abs(pos.z)})>1000000.f)
                        throw std::invalid_argument("Invalid spatial NAC position");
                    options.spatial=1;
                    options.position={pos.x,pos.y,pos.z};
                }
                NorthStarPcm16 pcm{track.samples.data(),
                    static_cast<std::uint32_t>(track.samples.size()/track.channels),
                    track.sample_rate,track.channels};
                // ABI-1 providers support single-stream playback only.
                // Generic single-voice fallback for ABI-1 providers.
                // Per-voice gain/overlap require optional voices ABI-2.
                if(!script_voice_audio->supports_voices()) {
                    script_voice_audio->play(pcm);
                    return std::string("0");
                }
                return std::to_string(script_voice_audio->play_voice(pcm,options));
            }
            throw std::runtime_error("NAC clip selection failed");
        });
        // Optional voice controls; the audio provider remains authoritative.
        runtime.bind("engine.audio","supportsVoices",[this](std::string_view json) {
            if(!script_args(json).empty())
                throw std::invalid_argument("audio.supportsVoices()");
            if(!script_voice_audio)
                script_voice_audio=std::make_unique<AudioEngine>();
            return script_voice_audio->supports_voices() ?
                std::string("true") : std::string("false");
        });
        runtime.bind("engine.audio","stopVoice",[this](std::string_view json) {
            auto const args=script_args(json);
            if(args.size()!=1 || !script_voice_audio ||
               !script_voice_audio->supports_voices())
                throw std::invalid_argument("audio.stopVoice(voiceId) requires voices ABI-2");
            auto const id=script_index(args[0]);
            if(!id)throw std::invalid_argument("Voice ID must be nonzero");
            script_voice_audio->stop_voice(
                static_cast<NorthStarAudioVoiceId>(id));
            return std::string("true");
        });
        runtime.bind("engine.audio","setVoiceGain",[this](std::string_view json) {
            auto const args=script_args(json);
            if(args.size()!=2 || !script_voice_audio ||
               !script_voice_audio->supports_voices())
                throw std::invalid_argument("audio.setVoiceGain(id,gain) requires voices ABI-2");
            auto const id=script_index(args[0]);
            auto const gain=script_number(args[1]);
            if(!id || gain<0 || gain>2)
                throw std::invalid_argument("Invalid voice ID or gain");
            script_voice_audio->set_voice_gain(
                static_cast<NorthStarAudioVoiceId>(id),
                static_cast<float>(gain));
            return std::string("true");
        });
        runtime.bind("engine.audio","setVoicePosition",[this](std::string_view json) {
            auto const args=script_args(json);
            if(args.size()!=4 || !script_voice_audio ||
               !script_voice_audio->supports_voices())
                throw std::invalid_argument("audio.setVoicePosition(id,x,y,z) requires voices ABI-2");
            auto const id=script_index(args[0]);
            const Vector3 pos{static_cast<float>(script_number(args[1])),
                              static_cast<float>(script_number(args[2])),
                              static_cast<float>(script_number(args[3]))};
            if(!id || !pos.valid() ||
               std::max({std::abs(pos.x),std::abs(pos.y),std::abs(pos.z)})>1000000.f)
                throw std::invalid_argument("Invalid voice ID or spatial position");
            script_voice_audio->set_voice_position(
                static_cast<NorthStarAudioVoiceId>(id),
                {pos.x,pos.y,pos.z});
            return std::string("true");
        });
        runtime.bind("engine.audio","play",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.size()!=1 || !fps_assets)
                throw std::invalid_argument("audio.play(virtualWavAsset)");
            auto path=fs::u8path(args[0]);
            if(args[0].rfind("audio/",0)!=0 || path.extension()!=".wav" ||
                args[0].size()>256 || args[0].find("..")!=std::string::npos ||
                args[0].find('\\')!=std::string::npos)
                throw std::invalid_argument("Audio asset path must be audio/*.wav");
            auto it=script_audio_cache.find(args[0]);
            if(it==script_audio_cache.end()) {
                auto request=fps_assets->request_audio_clip(args[0]);
                it=script_audio_cache.emplace(args[0],*request.get()).first;
            }
            auto const& sound=it->second;
            if(!audio) audio=std::make_unique<AudioOutput>();
            NorthStarPcm16 pcm{sound.samples.data(),sound.frames(),
                              sound.sample_rate,sound.channels};
            audio->play(pcm);
            return std::string("null");
        });
        runtime.bind("engine.animation","play",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()<2 || args.size()>4 || !preview ||
                !script_assets || !script_animations)
                throw std::invalid_argument(
                    "animation.play(entityId,virtualPath,loop=false,speed=1)");
            auto const id=static_cast<std::uint64_t>(script_index(args[0]));
            auto const& path=args[1];
            if (path.rfind("animations/",0)!=0 || path.size()<=16 ||
                path.compare(path.size()-10,10,".anim.json")!=0 ||
                path.find('\\')!=std::string::npos ||
                path.size()>256)
                throw std::invalid_argument("Animation must be animations/*.anim.json");
            auto rel=fs::u8path(path);
            for (auto const& part:rel)
                if (part.empty() || part=="." || part==".." || part.is_absolute())
                    throw std::invalid_argument("Animation path traversal is forbidden");
            if (!script_source_root.empty()) {
                // Source-project files must not redirect through intermediate
                // directory links, including after hot reload.
                auto cursor=script_source_root;
                for (auto const& part:rel) {
                    cursor/=part;
                    if (fs::is_symlink(cursor))
                        throw std::invalid_argument("Redirected animation asset");
                }
                if (!fs::is_regular_file(cursor))
                    throw std::invalid_argument("Missing animation asset");
            }
            bool loop=false;
            if (args.size()>=3) {
                if (args[2]!="true" && args[2]!="false")
                    throw std::invalid_argument("Animation loop must be boolean");
                loop=args[2]=="true";
            }
            float speed=1.f;
            if (args.size()>=4) speed=static_cast<float>(script_number(args[3]));
            auto clip=load_scene_animation_clip(*script_assets,path);
            script_animations->play(preview->scene(),id,std::move(clip),loop,speed);
            return std::string("null");
        });
        runtime.bind("engine.animation","stop",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=1 || !script_animations)
                throw std::invalid_argument("animation.stop(entityId)");
            auto const id=static_cast<std::uint64_t>(script_index(args[0]));
            for (auto const& sample:script_animation_samples)
                if (sample.entity_id==id)
                    script_animation_visuals.erase(sample.scene_index);
            script_animations->stop(id);
            return std::string("null");
        });
        runtime.bind("engine.animation","pause",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=2 || !script_animations ||
                (args[1]!="true" && args[1]!="false"))
                throw std::invalid_argument("animation.pause(entityId,boolean)");
            script_animations->pause(script_index(args[0]),args[1]=="true");
            return std::string("null");
        });
        runtime.bind("engine.animation","seek",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=2 || !script_animations)
                throw std::invalid_argument("animation.seek(entityId,time)");
            script_animations->seek(script_index(args[0]),
                                    static_cast<float>(script_number(args[1])));
            return std::string("null");
        });
        runtime.bind("engine.animation","state",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=1 || !script_animations)
                throw std::invalid_argument("animation.state(entityId)");
            auto id=static_cast<std::uint64_t>(script_index(args[0]));
            if (!script_animations->active(id)) return std::string("null");
            auto state=script_animations->status(id);
            return std::string("{\"time\":")+std::to_string(state.time)+
                ",\"duration\":"+std::to_string(state.duration)+
                ",\"speed\":"+std::to_string(state.speed)+
                ",\"loop\":"+(state.loop?"true":"false")+
                ",\"paused\":"+(state.paused?"true":"false")+
                ",\"finished\":"+(state.finished?"true":"false")+"}";
        });
        runtime.bind("engine.ecs","entities",[this](std::string_view json) {
            if (!script_args(json).empty()) throw std::invalid_argument("entities() expects no arguments");
            std::string result="[";
            bool first=true;
            if (preview) for (auto const& object:preview->scene().objects) {
                if (!object.node.id) continue;
                if (!first) result+=',';
                first=false;
                result+=script_json_quote(std::to_string(object.node.id));
            }
            return result+"]";
        });
        // Native FLECS set-intersection in one script/native boundary crossing.
        // Accept only known engine-owned component names and bounded arity.
        runtime.bind("engine.ecs","queryMany",[this](std::string_view json) {
            auto const args=script_args(json);
            if(args.empty() || args.size()>32 || !script_ecs)
                throw std::invalid_argument("engine.ecs.queryMany(componentNames...)");
            static const std::set<std::string> allowed={
                NS_COMPONENT_NAME,NS_COMPONENT_POSITION,NS_COMPONENT_VISUAL,
                NS_COMPONENT_BODY,NS_COMPONENT_CONTROLLER,NS_COMPONENT_MOTION,
                NS_COMPONENT_SCRIPT};
            for(auto const& name:args)
                if(!allowed.count(name))
                    throw std::invalid_argument("Unknown ECS component: "+name);
            std::string result="[";
            bool first=true;
            for(auto handle:script_ecs->query(args)) {
                auto it=script_handle_ids.find(handle);
                if(it==script_handle_ids.end() || !it->second) continue;
                if(!first) result+=',';
                first=false;
                result+=script_json_quote(std::to_string(it->second));
            }
            return result+"]";
        });
        runtime.bind("engine.ecs","query",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=1 || !script_ecs)
                throw std::invalid_argument("engine.ecs.query(componentName)");
            static const std::set<std::string> allowed={
                NS_COMPONENT_NAME,NS_COMPONENT_POSITION,NS_COMPONENT_VISUAL,
                NS_COMPONENT_BODY,NS_COMPONENT_CONTROLLER,NS_COMPONENT_MOTION,
                NS_COMPONENT_SCRIPT};
            if (!allowed.count(args[0])) throw std::invalid_argument("Unknown ECS component");
            std::string result="[";
            bool first=true;
            for (auto handle:script_ecs->query({args[0]})) {
                auto it=script_handle_ids.find(handle);
                if (it==script_handle_ids.end() || !it->second) continue;
                if (!first) result+=',';
                first=false;
                result+=script_json_quote(std::to_string(it->second));
            }
            return result+"]";
        });
        runtime.bind("engine.ecs","has",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=2 || !script_ecs)
                throw std::invalid_argument("engine.ecs.has(id,componentName)");
            static const std::set<std::string> allowed={
                NS_COMPONENT_NAME,NS_COMPONENT_POSITION,NS_COMPONENT_VISUAL,
                NS_COMPONENT_BODY,NS_COMPONENT_CONTROLLER,NS_COMPONENT_MOTION,
                NS_COMPONENT_SCRIPT};
            if (!allowed.count(args[1])) throw std::invalid_argument("Unknown ECS component");
            auto it=script_entity_indices.find(script_index(args[0]));
            if (it==script_entity_indices.end()) return std::string("false");
            return script_ecs->has_component(script_ecs->at(it->second),args[1])
                ? std::string("true") : std::string("false");
        });
        runtime.bind("engine.ecs","get",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=2 || !preview || !script_ecs)
                throw std::invalid_argument("engine.ecs.get(id,componentName)");
            auto found=script_entity_indices.find(script_index(args[0]));
            if (found==script_entity_indices.end())
                throw std::out_of_range("Unknown stable scene entity ID");
            auto const& obj=preview->scene().objects.at(found->second);
            if (args[1]==NS_COMPONENT_NAME)
                return script_json_quote(obj.name);
            if (args[1]==NS_COMPONENT_POSITION)
                return std::string("{\"x\":")+std::to_string(obj.position.x)+
                    ",\"y\":"+std::to_string(obj.position.y)+
                    ",\"z\":"+std::to_string(obj.position.z)+"}";
            if (args[1]==NS_COMPONENT_VISUAL)
                return std::string("{\"size\":")+std::to_string(obj.size)+
                    ",\"color\":"+std::to_string(obj.color)+"}";
            if (args[1]==NS_COMPONENT_BODY)
                return std::string("{\"mass\":")+std::to_string(obj.mass)+"}";
            if (args[1]==NS_COMPONENT_CONTROLLER)
                return preview->controlled_entity()==obj.node.id ? std::string("true") : std::string("false");
            if (args[1]==NS_COMPONENT_SCRIPT)
                return obj.node.script.empty() ? std::string("null") :
                    script_json_quote(obj.node.script);
            if (args[1]==NS_COMPONENT_MOTION) {
                NsMotion motion{};
                script_ecs->get_component(script_ecs->at(found->second),
                    NS_COMPONENT_MOTION,&motion,sizeof(motion));
                return std::string("{\"x\":")+std::to_string(motion.x)+
                    ",\"z\":"+std::to_string(motion.z)+"}";
            }
            throw std::invalid_argument("Unsupported ECS component value type");
        });
        runtime.bind("engine.ecs","getScript",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=1 || !preview)
                throw std::invalid_argument("engine.ecs.getScript(id)");
            auto it=script_entity_indices.find(script_index(args[0]));
            if (it==script_entity_indices.end())
                throw std::out_of_range("Unknown stable scene entity ID");
            return script_json_quote(preview->scene().objects.at(it->second).node.script);
        });
        runtime.bind("engine.ecs","getPosition",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=1 || !preview)
                throw std::invalid_argument("engine.ecs.getPosition(id)");
            auto it=script_entity_indices.find(script_index(args[0]));
            if (it==script_entity_indices.end())
                throw std::out_of_range("Unknown stable scene entity ID");
            auto const position=preview->scene().objects.at(it->second).position;
            return std::string("{\"x\":")+std::to_string(position.x)+
                ",\"y\":"+std::to_string(position.y)+
                ",\"z\":"+std::to_string(position.z)+"}";
        });
        runtime.bind("engine.ecs","setPosition",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=4 || !preview || !script_ecs)
                throw std::invalid_argument("engine.ecs.setPosition(id,x,y,z)");
            auto it=script_entity_indices.find(script_index(args[0]));
            if (it==script_entity_indices.end())
                throw std::out_of_range("Unknown stable scene entity ID");
            auto const idx=it->second;
            auto const& body=preview->scene().objects.at(idx);
            if (body.mass!=0.f || scene_pawn::is_pawn(preview->scene(),body))
                throw std::invalid_argument("Only static ECS entities may be repositioned");
            auto finite_float=[](std::string const& str) {
                auto number=script_number(str);
                if (std::abs(number)>1000000.0)
                    throw std::invalid_argument("Position exceeds world bounds");
                return static_cast<float>(number);
            };
            Vector3 position{finite_float(args[1]),finite_float(args[2]),finite_float(args[3])};
            preview->move_static(idx,position);
            NsPosition next{position.x,position.y,position.z};
            script_ecs->set_component(script_ecs->at(idx),NS_COMPONENT_POSITION,
                                      &next,sizeof(next));
            return std::string("null");
        });

    }

    // Load project-relative JavaScript through engine.assets, which also
    // works with the exported game/ VFS layout. The scene controls exactly
    // which entity script assets may execute.
    // Engine API modules are compiled into the runtime, never read from
    // a writable project or sharedAssets mount. Project gameplay scripts
    // live under assets/scripts in the project VFS and have their own lifecycle.
