#include "window_app_internal.hpp"
#include "window_script_bindings.hpp"
#include "window_script_domain_contracts.hpp"
#include <northstarcpp/render_shader_reload_api.h>
#include <cctype>

namespace {
// Source is always a VFS-relative shader asset. GPU providers never receive
// project file-system paths or arbitrary user-controlled source names.
bool render_shader_path_valid(std::string const& path,bool compiled=false) {
    if(path.size()<12 || path.size()>256 ||
       path.rfind("shaders/",0)!=0 ||
       path.find("..")!=std::string::npos ||
       path.find("//")!=std::string::npos ||
       path.find('\\')!=std::string::npos ||
       path.find(':')!=std::string::npos)return false;
    for(unsigned char c:path)
        if(!(std::isalnum(c)||c=='_'||c=='.'||c=='-'||c=='/'))
            return false;
    auto const ext=std::filesystem::u8path(path).extension().string();
    if(compiled)return ext==".spv"||ext==".dxil";
    return ext==".hlsl"||ext==".glsl"||ext==".vert"||
           ext==".frag"||ext==".comp"||ext==".geom"||
           ext==".tesc"||ext==".tese";
}
}
namespace northstarcpp::window_app_internal {

void App::configure_script_bindings(ScriptRuntime& runtime) {
    ScriptDomainBindings domains;
        auto const script_owner=++script_context_epoch;
        auto lease=std::make_shared<ScriptBindingLease>();
        script_binding_leases.emplace(script_owner,lease);
        domains.set_lifetime(lease);
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
                    // Script start failures must be observable even when the
                    // structured log sink writes to a separate file.
                    if (level==LogLevel::warn || level==LogLevel::error)
                        std::fprintf(stderr,"[project.script][%s] %s\n",
                            level==LogLevel::error?"error":"warn",args[0].c_str());
                    else if (script_logging_allowed)
                        std::fprintf(stderr,"[project.script] %s\n",args[0].c_str());
                    return std::string("null");
                });
        }
        // Expose the actual build's CMake PROJECT_VERSION to game scripts.
        // Do not hardcode the engine version in project-authored menus.
        runtime.bind("engine.render.provider","engineVersion",
            [](std::string_view json) {
                if(!script_args(json).empty())
                    throw std::invalid_argument(
                        "engine.rendering.engineVersion() takes no arguments");
                return script_json_quote(NORTHSTARCPP_RUNTIME_VERSION);
            });
        // Generic, engine-owned quality policy. Projects own the actual
        // settings UI and persist the selected value through engine.save.
        runtime.bind("engine.render.provider","setDetailLevel",
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
        runtime.bind("engine.render.provider","detailLevel",
            [this](std::string_view json) {
                auto args=script_args(json);
                if(!args.empty())
                    throw std::invalid_argument("engine.rendering.detailLevel()");
                return script_json_quote(graphics_detail_level);
            });
        runtime.bind("engine.render.provider","name",[this](std::string_view json) {
            if(!script_args(json).empty())
                throw std::invalid_argument("engine.render.provider.name()");
            return graphics?script_json_quote(graphics->renderer_provider_name()):
                std::string("null");
        });
        runtime.bind("engine.render.provider","version",[this](std::string_view json) {
            if(!script_args(json).empty())
                throw std::invalid_argument("engine.render.provider.version()");
            return graphics?script_json_quote(graphics->renderer_provider_version()):
                std::string("null");
        });
        // Canonical root engine.render API: project and built-in shaders
        // are read through the mounted AssetStore, not direct provider IO.
        runtime.bind("engine.render.shaders","catalog",
            [this](std::string_view json) {
                if(!script_args(json).empty())
                    throw std::invalid_argument("render.shaders.catalog()");
                const std::array<std::pair<char const*,char const*>,4> builtins{{
                    {"shaders/sky/atmosphere.hlsl","hlsl"},
                    {"shaders/sky/atmosphere.glsl","glsl"},
                    {"shaders/sky/sun_optics.hlsl","hlsl"},
                    {"shaders/sky/sun_optics.glsl","glsl"}
                }};
                auto result=std::string("{\"provider\":")+
                    (graphics?script_json_quote(graphics->renderer_provider_name()):
                         "null")+
                    ",\"hotReload\":"+
                    (graphics&&graphics->supports_shader_hot_reload()?"true":"false")+
                    ",\"builtin\":[";
                for(auto const& [path,language]:builtins) {
                    if(result.back()!='[')result+=',';
                    result+="{\"path\":"+script_json_quote(path)+
                            ",\"language\":"+script_json_quote(language)+
                            ",\"system\":\"sky\"}";
                }
                return result+"]}";
            });
        runtime.bind("engine.render.shaders","source",
            [this](std::string_view json) {
                auto const args=script_args(json);
                if(args.size()!=1 || !render_shader_path_valid(args[0]))
                    throw std::invalid_argument(
                        "render.shaders.source requires shaders/*.{hlsl,glsl,frag,vert,...}");
                if(!script_assets)
                    throw std::runtime_error("Shader VFS not mounted");
                auto const size=script_assets->size(args[0]);
                if(!size || size>256u*1024u)
                    throw std::length_error("Shader source size must be 1..262144 bytes");
                auto bytes=script_assets->read(args[0]);
                if(bytes.size()!=size ||
                   std::find(bytes.begin(),bytes.end(),0)!=bytes.end())
                    throw std::invalid_argument("Shader source is truncated or binary");
                return script_json_quote(std::string(bytes.begin(),bytes.end()));
            });
        runtime.bind("engine.render.shaders","queueCompiled",
            [this](std::string_view json) {
                auto const args=script_args(json);
                if(args.size()!=2 ||
                   !render_shader_path_valid(args[0]) ||
                   !render_shader_path_valid(args[1],true))
                    throw std::invalid_argument(
                        "render.shaders.queueCompiled(source,compiledBinary)");
                bool const hlsl=fs::u8path(args[0]).extension()==".hlsl";
                if((hlsl && fs::u8path(args[1]).extension()!=".dxil") ||
                   (!hlsl && fs::u8path(args[1]).extension()!=".spv"))
                    throw std::invalid_argument("Shader binary language mismatch");
                if(!graphics || !graphics->supports_shader_hot_reload() ||
                   !script_assets || !config.archive_path.empty())
                    throw std::runtime_error(
                        "Compiled shader hot reload requires unpacked project and active provider");
                auto const base=script_project_root(file_path)/"assets";
                auto const source=base/fs::u8path(args[0]);
                auto walk=base;
                for(auto const& segment:fs::u8path(args[0])) {
                    walk/=segment;
                    if(fs::is_symlink(walk))
                        throw std::invalid_argument("Linked shader sources prohibited");
                }
                if(!fs::is_regular_file(source))
                    throw std::invalid_argument("Shader source is not a project asset");
                auto const size=script_assets->size(args[1]);
                if(size<24 || size>NORTHSTARCPP_SHADER_RELOAD_MAX_BINARY_BYTES)
                    throw std::length_error("Compiled shader outside allowed binary size");
                auto bytes=script_assets->read(args[1]);
                if(bytes.size()!=size)
                    throw std::runtime_error("Compiled shader binary was truncated");
                auto const matched=graphics->queue_fragment_shader_reload(
                    source,bytes);
                if(!matched)
                    throw std::invalid_argument(
                        "No active material references this shader source");
                return std::to_string(matched);
            });
        runtime.bind("engine.render.shaders","poll",
            [this](std::string_view json) {
                if(!script_args(json).empty())
                    throw std::invalid_argument("render.shaders.poll()");
                auto report=graphics?graphics->poll_shader_reload():
                    Viewport::ShaderReloadProgress{};
                auto output=std::string("{\"pending\":")+
                    (report.pending?"true":"false")+",\"completed\":[";
                for(auto const& event:report.completed) {
                    if(output.back()!='[')output+=',';
                    output+=script_json_quote(event);
                }
                return output+"]}";
            });
        // The nested shadow namespace exposes startup configuration. A
        // render-provider runtime feature query is not yet part of the ABI:
        // report requested settings, never mistake them for GPU validation.
        auto shadow_startup_settings=[this]() {
            auto provider=graphics?graphics->renderer_provider_name():std::string{};
            auto lower=provider;
            std::transform(lower.begin(),lower.end(),lower.begin(),
                [](unsigned char c){return char(std::tolower(c));});
            bool const d3d12=lower.find("d3d12")!=std::string::npos;
            bool const vulkan=lower.find("vulkan")!=std::string::npos;
            auto flag=[](char const* name,bool fallback) {
                char value[8]{};
                DWORD size=GetEnvironmentVariableA(name,value,sizeof(value));
                if(size==0)return fallback;
                return size==1 && value[0]=='1';
            };
            // Explicit 0 overrides defaults. Vulkan CSM requires deferred.
            bool const requested=d3d12?
                flag("NORTHSTARCPP_ENABLE_SHADOWS",true):
                vulkan && flag("NORTHSTARCPP_ENABLE_VULKAN_DEFERRED",true) &&
                    flag("NORTHSTARCPP_ENABLE_VULKAN_CSM",true);
            bool const pcss=requested && d3d12 &&
                flag("NORTHSTARCPP_ENABLE_PCSS",true);
            auto out=std::string("{\"provider\":")+
                (graphics?script_json_quote(provider):"null")+
                ",\"requested\":"+(requested?"true":"false")+
                ",\"filter\":"+script_json_quote(
                    !requested?"off":pcss?"pcss":"pcf")+
                ",\"cascades\":"+std::to_string(
                    (requested && (d3d12||vulkan))?3:0)+
                ",\"verifiedOnGpu\":false,\"requiresProviderRestart\":true}";
            return out;
        };
        runtime.bind("engine.render.shadows","state",
            [shadow_startup_settings](std::string_view json) {
                if(!script_args(json).empty())
                    throw std::invalid_argument("engine.render.shadows.state()");
                return shadow_startup_settings();
            });
        runtime.bind("engine.render.shadows","capabilities",
            [this](std::string_view json) {
                if(!script_args(json).empty())
                    throw std::invalid_argument("engine.render.shadows.capabilities()");
                auto provider=graphics?graphics->renderer_provider_name():std::string{};
                std::transform(provider.begin(),provider.end(),provider.begin(),
                    [](unsigned char c){return char(std::tolower(c));});
                bool const d3d12=provider.find("d3d12")!=std::string::npos;
                bool const vulkan=provider.find("vulkan")!=std::string::npos;
                return std::string("{\"csm\":")+
                    ((d3d12||vulkan)?"true":"false")+
                    ",\"pcf\":"+((d3d12||vulkan)?"true":"false")+
                    ",\"pcss\":"+(d3d12?"true":"false")+
                    ",\"runtimeToggle\":false}";
            });
        // Engine Sky is engine-owned, provider-neutral simulation state.
        // Vulkan and D3D12 consume the same authored geometry/material.
        runtime.bind("engine.render.sky","configure",[this](std::string_view json) {
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
            static constexpr std::array<char const*,20> allowed{
                "enabled","paused","sun","moon","stars","clouds",
                "timeOfDay","dayLengthSeconds","latitudeDegrees",
                "dayOfYear","moonPhase","cloudCoverage","cloudSpeed",
                "haze","fogDensity","sunIntensity","moonIntensity","preset","mode","drawable"};
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
            if(sky_scene_disabled_)mode="disabled";
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
            number("fogDensity",next.fog_density);
            number("sunIntensity",next.sun_intensity);
            number("moonIntensity",next.moon_intensity);
            number("starDensity",next.star_density);
            if(auto preset=tree.get_optional<std::string>("preset"))
                next.preset=*preset;
            if(sky_scene_disabled_ || requested_mode || mode=="drawable") {
                next.enabled=mode=="procedural" || mode=="hybrid";
            } else if(mode=="disabled" && next.enabled) {
                // Existing scripts that opt in through enabled alone request
                // the default procedural presentation.
                mode="procedural";
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
                "day_length_s=%.1f clouds=%.2f sun=%u moon=%u stars=%u\n",
                next.enabled?1u:0u,next.time_of_day,next.day_length_seconds,
                next.cloud_coverage,next.sun?1u:0u,next.moon?1u:0u,
                next.stars?1u:0u);
            return std::string("true");
        });
        runtime.bind("engine.render.sky","setTimeOfDay",[this](std::string_view json) {
            const auto args=script_args(json);
            if(args.size()!=1)
                throw std::invalid_argument("engine.sky.setTimeOfDay(hours)");
            sky_environment_.set_time(script_number(args[0]));
            sky_visual_dirty_=true;sky_visual_elapsed_=0;
            return std::string("true");
        });
        runtime.bind("engine.render.sky","advance",[this](std::string_view json) {
            const auto args=script_args(json);
            if(args.size()!=1)
                throw std::invalid_argument("engine.sky.advance(seconds)");
            sky_environment_.advance(script_number(args[0]));
            sky_visual_dirty_=true;sky_visual_elapsed_=0;
            return std::string("true");
        });
        runtime.bind("engine.render.sky","reset",[this](std::string_view json) {
            if(!script_args(json).empty())
                throw std::invalid_argument("engine.sky.reset()");
            sky_environment_.reset();
            sky_mode_="disabled";
            sky_drawable_reference_.clear();
            if(graphics)graphics->set_sky_asset_reference({});
            sky_visual_dirty_=true;sky_visual_elapsed_=0;
            return std::string("true");
        });
        runtime.bind("engine.render.sky","state",[this](std::string_view json) {
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
            auto output=std::string("{\"enabled\":")+boolean(cfg.enabled)+
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
                ",\"fogDensity\":"+std::to_string(cfg.fog_density)+
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
                ",\"moonIllumination\":"+std::to_string(s.moon_illumination)+
                ",\"daylight\":"+std::to_string(s.daylight)+
                ",\"cloudOffset\":"+std::to_string(s.cloud_offset)+
                ",\"revision\":"+std::to_string(s.revision)+"}";
            return output;
        });
        // App exposes only the current project store capability to this
        // stateless ScriptRuntime registrar; save-as changes remain observable.
        bind_project_save_api(runtime, ScriptSaveBindingsContext{
            [this] { return project_save_store(config,file_path); }
        });
        runtime.bind("engine.math.noise","sample2",
            [this](std::string_view json) {
                auto args=script_args(json);
                if (args.size()!=2) throw std::invalid_argument("sample2 expects x,y");
                if (!script_noise) script_noise=std::make_unique<PerlinNoise>();
                return std::to_string(script_noise->sample2(
                    script_number(args[0]),script_number(args[1])));
            });
        collect_scene_runtime_script_bindings(domains.scene,
            SceneDomainCapabilities{
                &preview,&script_ecs,&script_assets,&graphics,&fps_camera,
                &fps_pose,&script_entity_indices,&script_handle_ids,
                &script_spawn_handles,&script_spawn_owners,&next_script_spawn_id,
                &script_object_colors,&script_model_animations,
                 &script_model_animation_rates,
                &script_model_visual_modes,&script_model_bone_attachments,
                script_owner,&script_scene_generation,script_scene_generation,
                [this](std::uint64_t id,std::uint64_t owner) {
                    destroy_script_spawn(id,owner);
                },
                [this] {return scene_streaming.world()!=nullptr;},
                [this](std::string const& name,bool visible) {
                    if(auto* world=scene_streaming.world())
                        world->set_layer_visible(name,visible);
                    else
                        throw std::out_of_range("Script world is unavailable");
                }
            });
        runtime.bind("engine.scene.prefabs","read",[this](std::string_view json) {
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
        collect_scene_count_script_binding(domains.scene, SceneCountStateView{
            [this] { return preview ? preview->scene().objects.size() : std::size_t{0}; }
        });
        // Runtime VFX is independent of physics and shared across weapons.
        // Scripted weapons specify the exact world muzzle transform; the
        // physics shell uses a separate engine-side Prefab API.
        runtime.bind("engine.weaponfx.effects","emitShot",
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
        runtime.bind("engine.physics.contacts","pollContacts",
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
        runtime.bind("engine.physics.bodies","setBodyMaterial",
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
        runtime.bind("engine.physics.bodies","applyImpulse",
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
        collect_input_script_bindings(domains.input, InputBindingStateView{
            &script_input_actions_,[this]{save_project_input_actions();},
            &script_paused,&fps_menu_visible,
            script_keys.data(), script_keys.size(),
            script_mouse_buttons.data(), script_mouse_buttons.size(),
            &script_mouse_dx, &script_mouse_dy, &script_wheel_vertical,
            script_gamepads.data(), script_gamepads.size()
        });
        collect_camera_available_script_binding(domains.camera, CameraAvailableStateView{
            [this] { return fps_camera!=nullptr; }
        });
        collect_camera_runtime_script_bindings(domains.camera,
            CameraDomainCapabilities{&fps_camera,&script_assets,
                &scripted_first_person,&script_yaw,&script_pitch});
        runtime.bind("engine.character.visual","load",[this](std::string_view json) {
            if(!script_assets || !scripted_game)
                throw std::runtime_error("character.load requires an active scripted project");
            auto spec=parse_script_character(json);
            script_character=std::move(spec);
            script_character_pose.reset();
            script_character_renderable=false;
            script_character_dirty=true;
            return std::string("null");
        });
        runtime.bind("engine.character.visual","pose",[this](std::string_view json) {
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
        runtime.bind("engine.character.visual","hide",[this](std::string_view json) {
            if(!script_args(json).empty())
                throw std::invalid_argument("character.hide()");
            script_character_pose.reset();
            return std::string("null");
        });
        runtime.bind("engine.character.visual","clear",[this](std::string_view json) {
            if(!script_args(json).empty())
                throw std::invalid_argument("character.clear()");
            script_character.reset();
            script_character_pose.reset();
            script_character_renderable=false;
            script_character_dirty=true;
            return std::string("null");
        });
        collect_player_runtime_script_bindings(domains.player,
            PlayerDomainCapabilities{&preview,&fps_camera,&scripted_game,
                &fps_camera_settings,&script_eye_height,&script_move_x,
                &script_move_z,&player_action_tracker_,
                &script_scene_generation,script_scene_generation});
        runtime.bind("engine.ai.navigation","moveTo",[this](std::string_view json) {
            auto const args=script_args(json);
            if(args.size()!=4 || !preview || !preview->supports_ai_motor() ||
               !scripted_game || args[0].empty() ||
               !std::all_of(args[0].begin(),args[0].end(),
                 [](unsigned char c){return c>='0' && c<='9';}))
                throw std::invalid_argument("ai.moveTo(pawnId,x,y,z) requires Jolt AI motor");
            const Vector3 desired{
                static_cast<float>(script_number(args[1])),
                static_cast<float>(script_number(args[2])),
                static_cast<float>(script_number(args[3]))};
            if(!std::isfinite(desired.x) || !std::isfinite(desired.y) ||
               !std::isfinite(desired.z))
                throw std::invalid_argument("AI destination must be finite");
            const auto pawn=std::stoull(args[0]);
            if(pawn==preview->controlled_entity() ||
               !gameplay::valid_controller_pawn(preview->scene(),pawn))
                throw std::invalid_argument("AI target must be an unpossessed Pawn");
            if(script_ai_controllers.size()>=256 &&
               !script_ai_controllers.count(pawn))
                throw std::length_error("Too many active AI controllers");
            auto [it,inserted]=script_ai_controllers.try_emplace(pawn);
            auto& npc=it->second;
            if(inserted) {
                npc.controller.possess(preview->scene(),pawn);
                if(auto const layout=player_blueprint::layout(preview->scene(),pawn);
                   layout && layout->mesh) {
                    npc.mesh_id=layout->mesh;
                    npc.animation.emplace(player_blueprint::animation(
                        preview->scene(),layout->mesh));
                }
            }
            // Identical moveTo commands do not discard an in-flight NavMesh
            // build. Only an actual goal change invalidates the cached route.
            if(npc.controller.move_to(desired)) {
                npc.navmesh.reset();
                npc.path.clear();
                npc.path_cursor=0;
                npc.route_failed=false;
                npc.replanning_delay=0.f;
                npc.stall_seconds=0.f;
            }
            return std::string("true");
        });
        runtime.bind("engine.ai.navigation","stop",[this](std::string_view json) {
            auto const args=script_args(json);
            if(args.size()!=1 || !preview || args[0].empty() ||
               !std::all_of(args[0].begin(),args[0].end(),
                 [](unsigned char c){return c>='0' && c<='9';}))
                throw std::invalid_argument("ai.stop(pawnId)");
            auto const pawn=std::stoull(args[0]);
            auto it=script_ai_controllers.find(pawn);
            if(it==script_ai_controllers.end())return std::string("false");
            if(it->second.motor_active)preview->clear_ai_move_intent(pawn);
            it->second.controller.stop();
            it->second.motor_active=false;
            return std::string("true");
        });
        runtime.bind("engine.ai.navigation","isMoving",[this](std::string_view json) {
            auto const args=script_args(json);
            if(args.size()!=1 || args[0].empty() ||
               !std::all_of(args[0].begin(),args[0].end(),
                 [](unsigned char c){return c>='0' && c<='9';}))
                throw std::invalid_argument("ai.isMoving(pawnId)");
            auto const pawn=std::stoull(args[0]);
            auto const it=script_ai_controllers.find(pawn);
            return it!=script_ai_controllers.end() &&
                   it->second.controller.has_goal()?
                   std::string("true"):std::string("false");
        });
        runtime.bind("engine.physics.queries","sweepSphere",[this](std::string_view json) {
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
        runtime.bind("engine.ui.viewport","setTitle",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.size()!=1 || args[0].size()>240)
                throw std::invalid_argument("ui.setTitle(text)");
            auto title=to_wide(args[0]);
            script_window_title=title;
            if(window) SetWindowTextW(window,title.c_str());
            return std::string("null");
        });
        runtime.bind("engine.ui.viewport","viewportSize",[this](std::string_view json) {
            if(!script_args(json).empty())
                throw std::invalid_argument("ui.viewportSize() takes no arguments");
            RECT area{};
            if(window)GetClientRect(window,&area);
            auto const width=std::max<LONG>(0,area.right-area.left);
            auto const height=std::max<LONG>(0,area.bottom-area.top);
            return std::string("{\"width\":")+std::to_string(width)+
                ",\"height\":"+std::to_string(height)+"}";
        });
        runtime.bind("engine.ui.documents","hotReloadEnabled",[this](std::string_view json) {
            if(!script_args(json).empty())
                throw std::invalid_argument("ui.hotReloadEnabled() takes no arguments");
            return config.packaged_game || !config.archive_path.empty()?"false":"true";
        });
        runtime.bind("engine.ui.documents","readAsset",[this](std::string_view json) {
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
        runtime.bind("engine.ui.documents","apply",[this](std::string_view json) {
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
        runtime.bind("engine.session.lifecycle","quit",[this](std::string_view json) {
            if(!script_args(json).empty()) throw std::invalid_argument("session.quit()");
            if(window) PostMessageW(window,WM_CLOSE,0,0);
            return std::string("null");
        });
        runtime.bind("engine.session.lifecycle","pause",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.size()!=1 || (args[0]!="true" && args[0]!="false"))
                throw std::invalid_argument("session.pause(boolean)");
            script_paused=args[0]=="true";
            if(script_paused) release_fps_cursor();
            else restore_gameplay_input_focus();
            return std::string("null");
        });
        runtime.bind("engine.session.scenes","scene",[this](std::string_view json) {
            if(!script_args(json).empty())
                throw std::invalid_argument("session.scene()");
            auto path=config.archive_path.empty()
                ? fs::relative(file_path,script_project_root(file_path))
                : file_path;
            return script_json_quote(path.generic_u8string());
        });
        runtime.bind("engine.session.scenes","loadScene",[this](std::string_view json) {
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
        runtime.bind("engine.session.lifecycle","restart",[this](std::string_view json) {
            if(!script_args(json).empty()) throw std::invalid_argument("session.restart()");
            if(window) PostMessageW(window,WM_APP+27,0,0);
            return std::string("null");
        });
        collect_audio_runtime_script_bindings(domains.audio,
            AudioDomainCapabilities{&script_assets,&fps_assets,
                &script_nac_cache,&script_audio_cache,&script_voice_audio,
                &audio,&script_scene_generation,script_scene_generation});
        // Optional voice controls; the audio provider remains authoritative.
        collect_audio_voices_script_binding(domains.audio, AudioVoicesStateView{
            &script_voice_audio
        });
        runtime.bind("engine.animation.clips","play",[this](std::string_view json) {
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
        runtime.bind("engine.animation.clips","stop",[this](std::string_view json) {
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
        runtime.bind("engine.animation.clips","pause",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=2 || !script_animations ||
                (args[1]!="true" && args[1]!="false"))
                throw std::invalid_argument("animation.pause(entityId,boolean)");
            script_animations->pause(script_index(args[0]),args[1]=="true");
            return std::string("null");
        });
        runtime.bind("engine.animation.clips","seek",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=2 || !script_animations)
                throw std::invalid_argument("animation.seek(entityId,time)");
            script_animations->seek(script_index(args[0]),
                                    static_cast<float>(script_number(args[1])));
            return std::string("null");
        });
        runtime.bind("engine.animation.clips","state",[this](std::string_view json) {
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
        collect_ecs_entities_script_binding(domains.ecs, EcsEntitiesStateView{
            [this]() -> Scene const* { return preview ? &preview->scene() : nullptr; },
            [this](std::size_t index) {
                return preview && index<preview->scene().objects.size() &&
                    (index<preview->authored_object_count() ||
                     preview->runtime_alive(index));
            }
        });
        collect_ecs_runtime_script_bindings(domains.ecs,
            EcsDomainCapabilities{&preview,&script_ecs,
                &script_entity_indices,&script_handle_ids,
                &script_scene_generation,script_scene_generation});
        // Bind scoped capabilities after their App-owned callbacks exist.
        // The registry and domain TUs do not include the App definition.
        bind_script_domains(runtime, std::move(domains));
    }

} // namespace northstarcpp::window_app_internal
