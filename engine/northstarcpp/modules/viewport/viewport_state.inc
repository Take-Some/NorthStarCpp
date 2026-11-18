struct Viewport::State {
    State(HWND window, bool software)
        : window_handle(window), software_renderer(software)
    {
        // A project-selected provider must never silently fall back to
        // another backend: unsupported ABI/features must remain visible.
        std::array<char,128> selected{};
        auto const selection_length=GetEnvironmentVariableA(
            "NORTHSTARCPP_RENDER_PROVIDER",selected.data(),
            static_cast<DWORD>(selected.size()));
        if(selection_length>=selected.size())
            throw std::runtime_error("Selected renderer provider ID is too long");
        auto const required=renderer_requirements_from_environment();
        std::vector<std::string> providers;
        if(selection_length)providers.emplace_back(selected.data());
        else providers={"d3d12","d3d11"}; // Existing automatic fallback.
        for(std::size_t i=0;i<providers.size();++i) {
            try {
                auto candidate=std::make_unique<Module>(
                    NORTHSTARCPP_CAPABILITY_RENDER,providers[i]);
                auto const* candidate_api=
                    candidate->query<NorthStarRenderApi>(NORTHSTARCPP_RENDER_ABI);
                require_provider_render_features(*candidate,required);
                void* instance=candidate_api->create(window, software?1u:0u);
                if(!instance)throw candidate->error("Cannot create viewport");
                module=std::move(candidate);
                api=candidate_api;
                renderer=instance;
                break;
            }catch(std::exception const&) {
                if(i+1==providers.size())throw;
            }
        }
        triangle_api = module->query_extension<NorthStarRenderTrianglesApi>(
            NORTHSTARCPP_RENDER_TRIANGLES_MODULE_ABI);
        if (!triangle_api) {
            auto const query = module->symbol<NorthStarRenderGetTrianglesApi>(
                "northstar_render_get_triangles_api");
            auto const* candidate = query ? query(NORTHSTARCPP_RENDER_TRIANGLES_ABI) : nullptr;
            if (candidate && valid_api(*candidate))
                triangle_api = candidate;
        }
        surface_api=module->query_extension<NorthStarRenderSurfacesApi>(
            NORTHSTARCPP_RENDER_SURFACES_MODULE_ABI);
        material_api=module->query_extension<NorthStarRenderMaterialsApi>(
            NORTHSTARCPP_RENDER_MATERIALS_MODULE_ABI);
        material_api_v2=module->query_extension<NorthStarRenderMaterialsApiV2>(
            NORTHSTARCPP_RENDER_MATERIALS_V2_MODULE_ABI);
        rigid_api=module->query_extension<NorthStarRenderRigidInstancesApi>(
            NORTHSTARCPP_RENDER_RIGID_INSTANCES_MODULE_ABI);
        if(rigid_api && (rigid_api->abi_version!=
            NORTHSTARCPP_RENDER_RIGID_INSTANCES_ABI ||
            rigid_api->struct_size<sizeof(NorthStarRenderRigidInstancesApi) ||
            !rigid_api->upload_mesh || !rigid_api->clear_meshes ||
            !rigid_api->set_instances || !rigid_api->read_stats))rigid_api=nullptr;
        rigid_residency_api=rigid_api?
            module->query_extension<NorthStarRenderRigidResidencyApi>(
                NORTHSTARCPP_RENDER_RIGID_RESIDENCY_MODULE_ABI):nullptr;
        // Explicit opt-out makes an identical-scene legacy-vs-GPU A/B run
        // possible without modifying authoring, textures, camera or scene.
        if(auto const* disabled=std::getenv("NORTHSTARCPP_DISABLE_RIGID_GPU");
           disabled && disabled[0]=='1')rigid_api=nullptr;
        shader_reload_api=module->query_extension<NorthStarRenderShaderReloadApi>(
            NORTHSTARCPP_RENDER_SHADER_RELOAD_MODULE_ABI);
        shader_reload_v2=module->query_extension<NorthStarRenderShaderReloadApiV2>(
            NORTHSTARCPP_RENDER_SHADER_RELOAD_V2_MODULE_ABI);
        postfx_api=module->query_extension<NorthStarRenderPostFxApi>(
            NORTHSTARCPP_RENDER_POSTFX_MODULE_ABI);
        timing_api=module->query_extension<NorthStarRenderTimingApi>(
            NORTHSTARCPP_RENDER_TIMING_MODULE_ABI);
        skinned_material_api=module->query_extension<NorthStarRenderSkinnedMaterialsApi>(
            NORTHSTARCPP_RENDER_SKINNED_MATERIALS_MODULE_ABI);
        lights_api=module->query_extension<NorthStarRenderLightsApi>(
            NORTHSTARCPP_RENDER_LIGHTS_MODULE_ABI);
        editor_grid_api=module->query_extension<NorthStarRenderEditorGridApi>(
            NORTHSTARCPP_RENDER_EDITOR_GRID_MODULE_ABI);
        pick_api=module->query_extension<NorthStarRenderPickApi>(
            NORTHSTARCPP_RENDER_PICK_MODULE_ABI);
        pick_v2_api=module->query_extension<NorthStarRenderPickApiV2>(
            NORTHSTARCPP_RENDER_PICK_V2_MODULE_ABI);
        pick_async_api=module->query_extension<NorthStarRenderPickAsyncApi>(
            NORTHSTARCPP_RENDER_PICK_ASYNC_MODULE_ABI);
        capture_api=module->query_extension<NorthStarRenderCaptureApi>(
            NORTHSTARCPP_RENDER_CAPTURE_MODULE_ABI);
        sky_gpu_api=module->query_extension<NorthStarRenderSkyApi>(
            NORTHSTARCPP_RENDER_SKY_MODULE_ABI);
        try {
            cubes.reserve(max_scene_objects);
            surface_ids.reserve(max_scene_objects);
        } catch (...) {
            api->destroy(renderer);
            throw;
        }
    }

    ~State() {
        if(ui_context&&ui_api)ui_api->destroy_context(ui_context);
        if(renderer)api->destroy(renderer);
    }
    void prepare_ui_overlay(bool gameplay) {
        // A generic UI document authored by scripts, never a mission-specific
        // structure. The optional demo uses the same element-tree contract.
        UiDocument demo;
        auto const* document=&ui_document;
        const char* enabled=std::getenv("NORTHSTARCPP_UI_DEMO");
        if(ui_document.nodes.empty() && gameplay && enabled &&
           std::strcmp(enabled,"1")==0) {
            UiDocumentNode panel;
            panel.id="demoPanel";
            panel.kind=NORTHSTAR_UI_PANEL;
            panel.properties={
                {"width",{NORTHSTAR_UI_VALUE_NUMBER,320}},
                {"height",{NORTHSTAR_UI_VALUE_NUMBER,65}},
                {"background",{NORTHSTAR_UI_VALUE_COLOR_RGBA8,0,0x0b1423d9}}
            };
            UiDocumentNode label;
            label.id="demoText";
            label.parent="demoPanel";
            label.kind=NORTHSTAR_UI_TEXT;
            UiPropertyValue text;
            text.kind=NORTHSTAR_UI_VALUE_STRING;
            text.text="NORTHSTAR UI NATIVE";
            label.properties={{"text",std::move(text)}};
            demo.nodes={std::move(panel),std::move(label)};
            document=&demo;
        }
        if(document->nodes.empty()) {
            ui_fx_target_id.clear();
            ui_fx_id.clear();
            ui_fx_hover=false;
            ui_fx_pressed=false;
            ui_fx_press_burst=false;
            if(ui_effects_api && ui_effects_api->set_hover_fx)
                ui_effects_api->set_hover_fx(renderer,nullptr);
            if(ui_render_api && ui_active &&
               !ui_render_api->set_rgba(renderer,nullptr,0,0,0))
                throw module->error("Cannot clear UI composition");
            ui_active=false;
            if(ui_context&&ui_api) {
                ui_api->destroy_context(ui_context);
                ui_context=nullptr;
                ui_handles.clear();
                ui_names_by_handle.clear();
                ui_events.clear();
                ui_applied_document={};
            }
            return;
        }
        if(!ui_render_api) {
            ui_render_api=module->query_extension<NorthStarRenderUiApi>(
                NORTHSTARCPP_RENDER_UI_MODULE_ABI);
            ui_effects_api=module->query_extension<NorthStarRenderUiEffectsApi>(
                NORTHSTARCPP_RENDER_UI_EFFECTS_MODULE_ABI);
            ui_shaders_api=module->query_extension<NorthStarRenderUiShadersApi>(
                NORTHSTARCPP_RENDER_UI_SHADERS_MODULE_ABI);
        }
        if(!ui_render_api)
            throw std::runtime_error(
                "Active engine.render provider has no GPU UI compositor");
        if(!ui_project_shaders_loaded && project_assets && ui_shaders_api) {
            // Renderer selection is independent of project styling. VFS can
            // read loose files and packed archives using the same asset IDs.
            ui_project_shaders_loaded=true;
            // Shader asset IDs are authored by the project UI document.
            auto shaderPath=[&](char const* key)->std::string {
                for(auto const& node:document->nodes)
                    for(auto const& [property,value]:node.properties)
                        if(property==key && value.kind==NORTHSTAR_UI_VALUE_STRING)
                            return value.text;
                return {};
            };
            auto loadShader=[&](char const* path)->std::vector<std::uint8_t> {
                try {
                    if(project_assets->size(path)==0)return {};
                    return project_assets->read(path);
                } catch(std::exception const& error) {
                    warn_asset("Project UI shader",path,error);
                    return {};
                }
            };
            if(ui_shaders_api->set_hlsl) {
                auto path=shaderPath("ui_shader_hlsl");
                auto source=path.empty()?std::vector<std::uint8_t>{}:loadShader(path.c_str());
                if(!source.empty()) {
                    if(source.size()>262144 ||
                       !ui_shaders_api->set_hlsl(renderer,
                         reinterpret_cast<char const*>(source.data()),
                         static_cast<std::uint32_t>(source.size()))) {
                        std::fprintf(stderr,
                            "[NorthStarCpp] Warning: invalid project UI HLSL %s: %s\n",
                            path.c_str(),module->error("Project shader error").what());
                    } else {
                        std::fprintf(stderr,
                            "[NorthStarCpp] Project UI shader loaded: %s\n",path.c_str());
                    }
                }
            }
            if(ui_shaders_api->set_spirv) {
                auto path=shaderPath("ui_shader_spirv");
                auto bytes=path.empty()?std::vector<std::uint8_t>{}:loadShader(path.c_str());
                if(!bytes.empty()) {
                    if(bytes.size()>1048576 || bytes.size()%4!=0) {
                        std::fprintf(stderr,
                            "[NorthStarCpp] Warning: invalid project UI SPIR-V %s\n",path.c_str());
                    } else {
                        std::vector<std::uint32_t> words(bytes.size()/4);
                        std::memcpy(words.data(),bytes.data(),bytes.size());
                        if(!ui_shaders_api->set_spirv(renderer,words.data(),
                                static_cast<std::uint32_t>(words.size()))) {
                            std::fprintf(stderr,
                                "[NorthStarCpp] Warning: project UI SPIR-V rejected: %s\n",
                                path.c_str());
                        } else {
                            std::fprintf(stderr,
                                "[NorthStarCpp] Project UI shader loaded: %s\n",path.c_str());
                        }
                    }
                }
            }
        }
        if(!ui_module) {
            ui_module=std::make_unique<Module>(NORTHSTARCPP_CAPABILITY_UI);
            ui_api=ui_module->query<NorthStarUIApi>(NORTHSTARCPP_UI_ABI);
            ui_raster=ui_module->query_extension<NorthStarUIRasterApi>(
                NORTHSTARCPP_UI_RASTER_ABI);
            ui_raster_layers=ui_module->query_extension<NorthStarUIRasterLayersApi>(
                NORTHSTARCPP_UI_RASTER_LAYERS_MODULE_ABI);
            ui_input_api=ui_module->query_extension<NorthStarUIInputApi>(
                NORTHSTARCPP_UI_INPUT_ABI);
            if(!ui_raster)
                throw std::runtime_error("Active engine.ui cannot rasterize");
        }
        auto set=[&](NorthStarUIHandle handle,
                     std::pair<std::string,UiPropertyValue> const& property) {
            auto const& input=property.second;
            NorthStarUIValue value{};
            switch(input.kind) {
            case NORTHSTAR_UI_VALUE_NUMBER: value=ui::number(input.number);break;
            case NORTHSTAR_UI_VALUE_BOOL: value=ui::boolean(input.boolean);break;
            case NORTHSTAR_UI_VALUE_STRING: value=ui::text(input.text);break;
            case NORTHSTAR_UI_VALUE_COLOR_RGBA8:
                value={sizeof(NorthStarUIValue),NORTHSTAR_UI_VALUE_COLOR_RGBA8,
                    0,input.rgba,0,{}};break;
            default:throw std::invalid_argument("Invalid UI property value type");
            }
            if(ui_api->set_property(ui_context,handle,ui::string(property.first),
                &value)!=NORTHSTAR_UI_OK)
                throw ui_module->error("Cannot apply UI property");
        };
        bool same_structure=ui_context &&
            document->nodes.size()==ui_applied_document.nodes.size();
        if(same_structure) {
            for(std::size_t i=0;i<document->nodes.size();++i) {
                const auto& current=document->nodes[i];
                const auto& previous=ui_applied_document.nodes[i];
                if(current.id!=previous.id || current.parent!=previous.parent ||
                   current.kind!=previous.kind ||
                   current.properties.size()!=previous.properties.size()) {
                    same_structure=false;break;
                }
                for(std::size_t j=0;j<current.properties.size();++j)
                    if(current.properties[j].first!=previous.properties[j].first ||
                       current.properties[j].second.kind!=
                       previous.properties[j].second.kind) {
                        same_structure=false;break;
                    }
                if(!same_structure)break;
            }
        }
        if(!same_structure) {
            if(ui_context) {
                ui_api->destroy_context(ui_context);
                ui_context=nullptr;
                ui_handles.clear();
                ui_names_by_handle.clear();
                ui_events.clear();
            }
            NorthStarUIConfig cfg{sizeof(NorthStarUIConfig),0,nullptr};
            if(ui_api->create_context(&cfg,&ui_context)!=NORTHSTAR_UI_OK)
                throw ui_module->error("Cannot create UI context");
            // Provider callbacks are synchronous during pump_events().
            // Buffer the event; script callbacks run later, never reentrantly
            // inside the C ABI UI provider.
            if(ui_api->set_event_callback(ui_context,
                    [](void* owner,const NorthStarUIEvent* event) {
                        if(!owner||!event)return;
                        auto& state=*static_cast<State*>(owner);
                        try {
                            if(state.ui_events.size()>=1024)return;
                            auto it=state.ui_names_by_handle.find(event->target);
                            if(it==state.ui_names_by_handle.end())return;
                            UiInteractionEvent next;
                            next.id=it->second;
                            next.kind=event->kind;
                            next.value_kind=event->value.kind;
                            next.number=event->value.number;
                            next.boolean=event->value.boolean!=0;
                            if(event->name.data && event->name.length<=64)
                                next.name.assign(event->name.data,
                                                 static_cast<size_t>(event->name.length));
                            if(event->value.kind==NORTHSTAR_UI_VALUE_STRING &&
                               event->value.string.data &&
                               event->value.string.length<=4096)
                                next.text.assign(event->value.string.data,
                                                 static_cast<size_t>(event->value.string.length));
                            state.ui_events.push_back(std::move(next));
                        } catch(...) {
                            // Never throw through an external provider callback.
                        }
                    },this)!=NORTHSTAR_UI_OK)
                throw ui_module->error("Cannot register UI event sink");
            auto root=ui::element(NORTHSTAR_UI_ROOT,0,"screen");
            NorthStarUIHandle root_handle=0;
            if(ui_api->create_element(ui_context,&root,&root_handle)!=NORTHSTAR_UI_OK)
                throw ui_module->error("Cannot create UI root");
            ui_handles.emplace("",root_handle);
            for(auto const& node:document->nodes) {
                auto parent=ui_handles.find(node.parent);
                if(parent==ui_handles.end())
                    throw std::invalid_argument("UI node parent must precede child");
                auto descriptor=ui::element(node.kind,parent->second,node.id);
                NorthStarUIHandle handle=0;
                if(ui_api->create_element(ui_context,&descriptor,&handle)!=NORTHSTAR_UI_OK)
                    throw ui_module->error("Cannot create UI node");
                ui_handles.emplace(node.id,handle);
                ui_names_by_handle.emplace(handle,node.id);
            }
            ui_last_width=ui_last_height=0;
        }
        if(!same_structure || !(*document==ui_applied_document)) {
            for(std::size_t i=0;i<document->nodes.size();++i) {
                const auto& node=document->nodes[i];
                const auto& previous=same_structure?
                    ui_applied_document.nodes[i]:node;
                const auto handle=ui_handles.at(node.id);
                for(std::size_t j=0;j<node.properties.size();++j)
                    if(!same_structure ||
                       !(node.properties[j].second==previous.properties[j].second))
                        set(handle,node.properties[j]);
            }
            ui_applied_document=*document;
            ui_dirty=true;
        }
        RECT rect{};
        if(!GetClientRect(window_handle,&rect) ||
           rect.right<=rect.left||rect.bottom<=rect.top)return;
        const auto width=static_cast<std::uint32_t>(rect.right-rect.left);
        const auto height=static_cast<std::uint32_t>(rect.bottom-rect.top);
        if(width>4096||height>4096 ||
           static_cast<std::uint64_t>(width)*height*4>64ull*1024*1024)return;
        // Keep the last isolated layer during the short hover fade-out.
        const float fxElapsed=std::chrono::duration<float>(
            std::chrono::steady_clock::now()-ui_fx_changed).count();
        if(!ui_fx_hover && !ui_fx_target_id.empty() && fxElapsed>=0.22f) {
            ui_fx_target_id.clear();
            ui_dirty=true;
        }
        auto layerHandle=ui_handles.find(ui_fx_target_id);
        const bool layered=ui_raster_layers && ui_effects_api &&
            layerHandle!=ui_handles.end() &&
            static_cast<std::uint64_t>(width)*height*8<=64ull*1024*1024 &&
            height<=4096;
        // GPU hover animation advances without rerasterizing the UI texture.
        if(ui_effects_api && ui_effects_api->set_hover_fx) {
            const auto elapsed=std::chrono::duration<float>(
                std::chrono::steady_clock::now()-ui_fx_changed).count();
            const float fade=std::clamp(elapsed/0.22f,0.f,1.f);
            const float ease=fade*fade*(3.f-2.f*fade);
            const float strength=ui_fx_hover?ease:1.f-ease;
            NorthStarRenderUiHoverFx fx{};
            // 150-ms release pulse remains visible even if the click completes
            // between two rendered frames.
            const float pressAge=std::chrono::duration<float>(
                std::chrono::steady_clock::now()-ui_fx_press_changed).count();
            const float releaseFade=std::clamp(1.f-pressAge/0.150f,0.f,1.f);
            const float pressBurst=ui_fx_pressed?1.f:
                (ui_fx_press_burst?releaseFade*releaseFade:0.f);
            fx.layered=layered?1.f:0.f;
            // Time must advance even without hover so the project can animate
            // a very faint resting shimmer without CPU re-rasterization.
            fx.seconds=std::chrono::duration<float>(
                std::chrono::steady_clock::now()-ui_fx_clock_start).count();
            // Initialize position even on the first hover frame (fade=0).
            if(layered && ui_fx_rect.width>0 && ui_fx_rect.height>0) {
                fx.x=ui_fx_rect.x/static_cast<float>(width);
                fx.y=ui_fx_rect.y/static_cast<float>(height);
                fx.width=ui_fx_rect.width/static_cast<float>(width);
                fx.height=ui_fx_rect.height/static_cast<float>(height);
                fx.strength=strength;
                fx.pressed=pressBurst;
                fx.primary=ui_fx_primary?1.f:0.f;
                fx.origin_slot=ui_fx_origin_slot;
                fx.seed=ui_fx_seed;
                fx.layered=layered?1.f:0.f;
                // Hover age also synchronizes the short entry flicker
                // with the pointer-enter event that starts the NAC cue.
                fx.reserved=elapsed;
            }
            if(!layered)fx.strength=0.f;
            if(!ui_effects_api->set_hover_fx(renderer,&fx))
                throw module->error("Cannot update UI hover shader parameters");
        }
        if(ui_active && ui_last_width==width && ui_last_height==height &&
           !ui_dirty)return;
        if(ui_api->resize(ui_context,width,height,1.f)!=NORTHSTAR_UI_OK)
            throw ui_module->error("Cannot resize UI context");
        const auto screenBytes=static_cast<std::size_t>(width)*height*4;
        ui_pixels.resize(screenBytes*(layered?2u:1u));
        if(layered) {
            // Base layer omits ONLY the target; the second layer contains the
            // button surface, bevels, glyphs and alpha with transparent space.
            if(ui_raster_layers->draw_filtered_rgba(ui_context,
                ui_pixels.data(),width*4,width,height,layerHandle->second,1)
                !=NORTHSTAR_UI_OK)
                throw ui_module->error("Cannot rasterize UI base without button");
            if(ui_raster_layers->draw_filtered_rgba(ui_context,
                ui_pixels.data()+screenBytes,width*4,width,height,
                layerHandle->second,2)!=NORTHSTAR_UI_OK)
                throw ui_module->error("Cannot rasterize isolated UI button");
        } else if(ui_raster->draw_rgba(ui_context,ui_pixels.data(),
                     width*4,width,height)!=NORTHSTAR_UI_OK)
            throw ui_module->error("Cannot rasterize UI document");
        // Project-authored UI images are separate asset-backed layers.
        // Backgrounds composite UNDER the existing premultiplied UI;
        // logos/foreground images composite OVER it with their own alpha.
        // The second isolated-button atlas layer remains untouched.
        for(auto const& node:ui_applied_document.nodes) {
            if(node.kind!=NORTHSTAR_UI_IMAGE || !project_assets)continue;
            auto getString=[&](char const* key)->std::string {
                auto found=std::find_if(node.properties.begin(),node.properties.end(),
                    [&](auto const& item){return item.first==key &&
                        item.second.kind==NORTHSTAR_UI_VALUE_STRING;});
                return found!=node.properties.end()?found->second.text:std::string{};
            };
            auto const source=getString("source");
            if(source.empty())continue;
            auto found=ui_image_cache.find(source);
            if(found==ui_image_cache.end()) {
                UiCachedImage asset;
                try {
                    auto at=source.rfind('@');
                    if(at==std::string::npos || at<4 || at+1>=source.size() ||
                       source.find("..")!=std::string::npos || source[0]=='/' ||
                       source.find(':')!=std::string::npos ||
                       source.substr(at-4,4)!=".ytd")
                        throw std::invalid_argument(
                            "UI source must be project-relative .ytd@entry");
                    auto textures=shared_assets::load_textures(
                        *project_assets,source.substr(0,at));
                    auto chosen=std::find_if(textures.begin(),textures.end(),
                        [&](auto const& texture){
                            return texture.name==source.substr(at+1);
                        });
                    if(chosen==textures.end() || chosen->format_id!=1 ||
                       chosen->mips.empty())
                        throw std::runtime_error("YTD RGBA8 UI entry missing");
                    auto const& mip=chosen->mips.front();
                    if(!mip.width || !mip.height || mip.width>4096 ||
                       mip.height>4096 ||
                       mip.size!=static_cast<std::size_t>(mip.width)*mip.height*4 ||
                       mip.offset>chosen->data.size() ||
                       mip.size>chosen->data.size()-mip.offset)
                        throw std::runtime_error("Invalid YTD UI image mip");
                    asset.width=mip.width;
                    asset.height=mip.height;
                    asset.rgba.assign(
                        chosen->data.begin()+static_cast<std::ptrdiff_t>(mip.offset),
                        chosen->data.begin()+static_cast<std::ptrdiff_t>(mip.offset+mip.size));
                    std::fprintf(stderr,
                        "[NorthStarCpp] UI image loaded: %s (%ux%u)\n",
                        source.c_str(),mip.width,mip.height);
                } catch(std::exception const& error) {
                    warn_asset("UI image",source,error);
                }
                found=ui_image_cache.emplace(source,std::move(asset)).first;
            }
            auto const& asset=found->second;
            if(asset.rgba.empty() || !asset.width || !asset.height)continue;
            auto handle=ui_handles.find(node.id);
            if(handle==ui_handles.end())continue;
            NorthStarUIRect rect{};
            if(ui_raster->get_rect(ui_context,handle->second,&rect)!=NORTHSTAR_UI_OK)
                continue;
            const int srcW=static_cast<int>(asset.width);
            const int srcH=static_cast<int>(asset.height);
            int bx=static_cast<int>(rect.x),by=static_cast<int>(rect.y);
            int bw=std::max(1,static_cast<int>(rect.width));
            int bh=std::max(1,static_cast<int>(rect.height));
            int cropW=srcW,cropH=srcH,offX=0,offY=0;
            if(getString("fit")=="contain") {
                // Preserve aspect ratio and transparency, especially logos.
                if(static_cast<std::int64_t>(bw)*srcH <
                   static_cast<std::int64_t>(bh)*srcW)
                    bh=std::max(1,bw*srcH/srcW);
                else bw=std::max(1,bh*srcW/srcH);
                bx+= (static_cast<int>(rect.width)-bw)/2;
                by+= (static_cast<int>(rect.height)-bh)/2;
            } else {
                // CSS cover: crop from the centre, never resample on disk.
                if(static_cast<std::int64_t>(bw)*srcH >
                   static_cast<std::int64_t>(bh)*srcW) {
                    cropH=std::max(1,srcW*bh/bw);
                    offY=(srcH-cropH)/2;
                } else {
                    cropW=std::max(1,srcH*bw/bh);
                    offX=(srcW-cropW)/2;
                }
            }
            const bool underlay=getString("layer")!="foreground";
            for(int y=std::max(0,by);
                y<std::min(static_cast<int>(height),by+bh);++y) {
                auto sy=std::clamp(offY+(y-by)*cropH/bh,0,srcH-1);
                for(int x=std::max(0,bx);
                    x<std::min(static_cast<int>(width),bx+bw);++x) {
                    auto sx=std::clamp(offX+(x-bx)*cropW/bw,0,srcW-1);
                    auto src=(static_cast<std::size_t>(sy)*srcW+sx)*4;
                    auto dst=(static_cast<std::size_t>(y)*width+x)*4;
                    unsigned alpha=asset.rgba[src+3];
                    if(!alpha)continue;
                    unsigned dstAlpha=ui_pixels[dst+3];
                    unsigned factor=underlay?((255u-dstAlpha)*alpha+127)/255:
                                               alpha;
                    for(unsigned c=0;c<3;++c) {
                        unsigned contribution=(unsigned(asset.rgba[src+c])*factor+127)/255;
                        unsigned existing=underlay?ui_pixels[dst+c]:
                            (unsigned(ui_pixels[dst+c])*(255u-alpha)+127)/255;
                        ui_pixels[dst+c]=static_cast<std::uint8_t>(
                            std::min(255u,existing+contribution));
                    }
                    ui_pixels[dst+3]=underlay?
                        static_cast<std::uint8_t>(std::min(255u,dstAlpha+factor)):
                        static_cast<std::uint8_t>(std::min(255u,
                            alpha+(dstAlpha*(255u-alpha)+127)/255));
                }
            }
        }
        if(!ui_render_api->set_rgba(renderer,ui_pixels.data(),
                width,layered?height*2:height,width*4))
            throw module->error("Cannot upload GPU UI document");
        ui_last_width=width;
        ui_last_height=height;
        ui_active=true;
        ui_dirty=false;
    }
    #include "viewport_world_ui_raster.inc"
    void fallback_to_d3d11() {
        if (module && module->provider()!="d3d12") return;
        std::array<char,8> selected{};
        if(GetEnvironmentVariableA("NORTHSTARCPP_RENDER_PROVIDER",
             selected.data(),static_cast<DWORD>(selected.size()))>0)
            throw std::runtime_error(
                "Selected renderer lacks character support; explicit provider fallback disabled");
        auto next_module=std::make_unique<Module>(NORTHSTARCPP_CAPABILITY_RENDER,"d3d11");
        require_provider_render_features(*next_module,
            renderer_requirements_from_environment());
        auto next_api=next_module->query<NorthStarRenderApi>(NORTHSTARCPP_RENDER_ABI);
        // DXGI only permits one active flip-model swapchain per HWND.
        // Release the D3D12 swapchain before creating the replacement.
        if(renderer)api->destroy(renderer);
        renderer=nullptr;
        auto next_renderer=next_api->create(window_handle,software_renderer?1u:0u);
        if(!next_renderer)throw next_module->error("Cannot initialize D3D11 fallback");
        renderer=next_renderer;api=next_api;module=std::move(next_module);
        ui_render_api=nullptr;
        ui_effects_api=nullptr;
        ui_shaders_api=nullptr;
        ui_project_shaders_loaded=false;
        triangle_api=module->query_extension<NorthStarRenderTrianglesApi>(
            NORTHSTARCPP_RENDER_TRIANGLES_MODULE_ABI);
        if(!triangle_api) {
            auto query=module->symbol<NorthStarRenderGetTrianglesApi>(
                "northstar_render_get_triangles_api");
            auto candidate=query?query(NORTHSTARCPP_RENDER_TRIANGLES_ABI):nullptr;
            if(candidate&&valid_api(*candidate))triangle_api=candidate;
        }
        surface_api=module->query_extension<NorthStarRenderSurfacesApi>(
            NORTHSTARCPP_RENDER_SURFACES_MODULE_ABI);
        material_api=module->query_extension<NorthStarRenderMaterialsApi>(
            NORTHSTARCPP_RENDER_MATERIALS_MODULE_ABI);
        material_api_v2=module->query_extension<NorthStarRenderMaterialsApiV2>(
            NORTHSTARCPP_RENDER_MATERIALS_V2_MODULE_ABI);
        rigid_api=nullptr;
        rigid_residency_api=nullptr;
        uploaded_rigid_meshes.clear();
        rigid_inactive_frames.clear();
        rejected_rigid_meshes.clear();
        rigid_opaque_materials.clear();
        preview_material_cache.clear();
        preview_cache_vertices=0;
        shader_reload_api=module->query_extension<NorthStarRenderShaderReloadApi>(
            NORTHSTARCPP_RENDER_SHADER_RELOAD_MODULE_ABI);
        shader_reload_v2=module->query_extension<NorthStarRenderShaderReloadApiV2>(
            NORTHSTARCPP_RENDER_SHADER_RELOAD_V2_MODULE_ABI);
        postfx_api=module->query_extension<NorthStarRenderPostFxApi>(
            NORTHSTARCPP_RENDER_POSTFX_MODULE_ABI);
        timing_api=module->query_extension<NorthStarRenderTimingApi>(
            NORTHSTARCPP_RENDER_TIMING_MODULE_ABI);
        skinned_material_api=module->query_extension<NorthStarRenderSkinnedMaterialsApi>(
            NORTHSTARCPP_RENDER_SKINNED_MATERIALS_MODULE_ABI);
        lights_api=module->query_extension<NorthStarRenderLightsApi>(
            NORTHSTARCPP_RENDER_LIGHTS_MODULE_ABI);
        editor_grid_api=module->query_extension<NorthStarRenderEditorGridApi>(
            NORTHSTARCPP_RENDER_EDITOR_GRID_MODULE_ABI);
        pick_api=module->query_extension<NorthStarRenderPickApi>(
            NORTHSTARCPP_RENDER_PICK_MODULE_ABI);
        pick_v2_api=module->query_extension<NorthStarRenderPickApiV2>(
            NORTHSTARCPP_RENDER_PICK_V2_MODULE_ABI);
        pick_async_api=module->query_extension<NorthStarRenderPickAsyncApi>(
            NORTHSTARCPP_RENDER_PICK_ASYNC_MODULE_ABI);
        capture_api=module->query_extension<NorthStarRenderCaptureApi>(
            NORTHSTARCPP_RENDER_CAPTURE_MODULE_ABI);
        sky_gpu_api=module->query_extension<NorthStarRenderSkyApi>(
            NORTHSTARCPP_RENDER_SKY_MODULE_ABI);
    }
    HWND window_handle=nullptr;
    bool software_renderer=false;
    std::unique_ptr<Module> module;
    NorthStarRenderApi const* api = nullptr;
    NorthStarRenderUiApi const* ui_render_api = nullptr;
    NorthStarRenderUiEffectsApi const* ui_effects_api = nullptr;
    NorthStarRenderUiShadersApi const* ui_shaders_api = nullptr;
    bool ui_project_shaders_loaded=false;
    std::unique_ptr<Module> ui_module;
    NorthStarUIApi const* ui_api = nullptr;
    NorthStarUIRasterApi const* ui_raster = nullptr;
    NorthStarUIRasterLayersApi const* ui_raster_layers = nullptr;
    NorthStarUIInputApi const* ui_input_api = nullptr;
    void* ui_context = nullptr;
    std::vector<std::uint8_t> ui_pixels;
    struct UiCachedImage {
        std::vector<std::uint8_t> rgba;
        std::uint32_t width=0,height=0;
    };
    std::unordered_map<std::string,UiCachedImage> ui_image_cache;
    UiDocument ui_document{};
    UiDocument ui_applied_document{};
    std::unordered_map<std::string,WorldUiLayer> world_ui;
    std::unordered_map<std::string,NorthStarUIHandle> ui_handles;
    std::unordered_map<NorthStarUIHandle,std::string> ui_names_by_handle;
    std::vector<UiInteractionEvent> ui_events;
    std::uint32_t ui_last_width=0,ui_last_height=0;
    bool ui_dirty=true,ui_active=false,ui_was_hovering=false;
    std::string ui_fx_id;
    std::string ui_fx_target_id;
    NorthStarUIRect ui_fx_rect{};
    bool ui_fx_hover=false,ui_fx_primary=false,ui_fx_pressed=false;
    bool ui_fx_press_burst=false;
    std::chrono::steady_clock::time_point ui_fx_press_changed=
        std::chrono::steady_clock::now();
    float ui_fx_origin_slot=0.f,ui_fx_seed=0.f;
    float ui_fx_pointer_x=0.5f,ui_fx_pointer_y=0.5f;
    std::string ui_raster_hover_id;
    std::chrono::steady_clock::time_point ui_fx_changed=
        std::chrono::steady_clock::now();
    // Universal GPU-UI timebase. Only the project shader decides its style;
    // the default engine compositor remains entirely neutral.
    std::chrono::steady_clock::time_point ui_fx_clock_start=
        std::chrono::steady_clock::now();

    std::vector<NorthStarRenderCube> cubes;
    viewport_detail::ViewportFrameScratch frame_scratch;
    viewport_detail::ViewportFrameBuilder frame_builder;
    ViewportAssetStats asset_stats;
    std::string detail_level="cinematic";
    std::vector<std::uint32_t> surface_ids;
    NorthStarRenderSurfacesApi const* surface_api = nullptr;
    NorthStarRenderShaderReloadApi const* shader_reload_api=nullptr;
    NorthStarRenderShaderReloadApiV2 const* shader_reload_v2=nullptr;
    NorthStarRenderPostFxApi const* postfx_api=nullptr;
    NorthStarRenderTimingApi const* timing_api=nullptr;
    std::unordered_map<std::string,std::uint64_t> reload_pending;
    NorthStarRenderMaterialsApi const* material_api = nullptr;
    NorthStarRenderMaterialsApiV2 const* material_api_v2=nullptr;
    NorthStarRenderRigidInstancesApi const* rigid_api=nullptr;
    NorthStarRenderRigidResidencyApi const* rigid_residency_api=nullptr;
    std::unordered_set<std::string> uploaded_rigid_meshes;
    std::unordered_map<std::string,std::uint32_t> rigid_inactive_frames;
    std::unordered_set<std::string> rejected_rigid_meshes;
    // Cached per material reference; alpha inspection is done once per VFS mount.
    std::unordered_map<std::string,bool> rigid_opaque_materials;
    struct PreviewMaterialCacheEntry {
        std::array<float,13> transform{};
        MeshAsset const* source=nullptr;
        std::size_t first_index=0,index_count=0;
        std::vector<NorthStarMaterialVertex> vertices;
    };
    bool preview_static_cache_enabled=false;
    std::unordered_map<std::string,PreviewMaterialCacheEntry> preview_material_cache;
    std::size_t preview_cache_vertices=0;
    std::size_t preview_frame_hits=0,preview_frame_vertices=0;
    std::vector<NorthStarRigidInstance> rigid_draw_scratch;
    std::vector<NorthStarMaterialVertex> rigid_upload_vertices;
    NorthStarRenderSkinnedMaterialsApi const* skinned_material_api=nullptr;
    NorthStarRenderLightsApi const* lights_api=nullptr;
    NorthStarRenderEditorGridApi const* editor_grid_api=nullptr;
    NorthStarRenderPickApi const* pick_api=nullptr;
    NorthStarRenderPickApiV2 const* pick_v2_api=nullptr;
    NorthStarRenderPickAsyncApi const* pick_async_api=nullptr;
    NorthStarRenderCaptureApi const* capture_api=nullptr;
    NorthStarRenderSkyApi const* sky_gpu_api=nullptr;
    std::uint64_t pick_generation=0, next_pick_request=1;
    std::unordered_map<std::uint64_t,int> pick_entity_actor;
    bool pick_scene_complete=false;
    std::vector<int> cube_actor_map;
    bool warned_missing_light_provider=false;
    std::unordered_map<std::string,MaterialDefinition> cached_materials;
    // Failed resources are quarantined for this VFS mount, not retried in
    // every frame. Changing mounted asset sources clears the quarantine.
    std::unordered_set<std::string> invalid_materials;
    std::unordered_set<std::string> invalid_meshes;
    std::unordered_set<std::uint64_t> warned_unbound_players;
    void warn_asset(char const* type,std::string const& path,
                    std::exception const& error) const {
        std::fprintf(stderr,"[WARN] [engine.assets] Skipping invalid %s '%s': %s\n",
            type,path.c_str(),error.what());
    }
    std::optional<shared_assets::DrawableResources> retained_sky;
    SkyEnvironment sky_environment;
    bool sky_gpu_uploaded=false;
    std::optional<SkyTextureLayers> sky_source_layers;
    std::string sky_source_preset;
    bool sky_source_checked=false;
    std::vector<NorthStarSkyVertex> procedural_sky_vertices;
    std::vector<std::uint32_t> procedural_sky_indices;
    bool world_surfaces_ready = false;
    NorthStarRenderCharacterApi const* character_api = nullptr;
    std::unique_ptr<Character> character;
    bool character_arm_mode=false;
    std::unique_ptr<AssetStore> project_assets;
    std::unique_ptr<viewport_detail::NativeModelPreloader> native_model_preloader;
    bool vehicle_models_prewarm_queued=false;
    std::filesystem::path source_project_root;
    std::unordered_map<std::string,MeshAsset> cached_meshes;
    std::unordered_map<std::string,MeshAsset> cached_hlod_meshes;
    std::unordered_set<std::string> hlod_unbakeable_assets;
    viewport_detail::ActiveMeshRefs active_mesh_residency;
    // Reuse original YDD eight-weight influences, YMT bind skeleton and
    // decoded YCD clips. GPU providers receive a CPU-deformed PBR mesh.
    struct NativeRig {
        Skeleton skeleton;
        std::vector<SkinWeights> weights;
        math::float4x4 mesh_to_skeleton{};
        math::float4x4 presentation_basis{}; // YDD -> canonical rig forward.
        std::optional<viewport_detail::WeaponGripLibrary> weapon_grips;
        // Bounded one-time mask for optional arm-only native viewmodels.
        std::vector<std::uint8_t> first_person_arm_vertices;
        // True first-person shows the existing body, excluding only the
        // viewer's own head/face surfaces so the camera cannot look inside it.
        std::vector<std::uint8_t> first_person_body_vertices;
        std::unordered_map<std::string,SkeletalClip> clips;
        std::unique_ptr<AsyncYcdClipLoader> graph_loader;
        std::unordered_set<std::string> invalid_clips;
        std::optional<viewport_detail::PreparedSkinSections> gpu_sections;
        std::optional<viewport_detail::PreparedSkinSections> gpu_arm_sections;
        std::optional<viewport_detail::PreparedSkinSections> gpu_body_sections;
    };
    std::unordered_map<std::string,NativeRig> cached_native_skins;
    struct NativePlayback {
        std::string clip_reference;
        std::string graph_path;
        std::vector<std::string> graph_references;
        std::unordered_set<std::string> graph_bound_refs;
        std::unique_ptr<AnimationGraphInstance> graph;
        std::unique_ptr<ProceduralIkController> procedural_ik;
        std::unique_ptr<GroundedLocomotionController> grounded;
        std::unique_ptr<DistributedLookAt> lookat;
        std::vector<std::string> lookat_cache;
        std::vector<std::string> grounded_foot_cache;
        std::string grounded_body_cache;
        PhysicsScene const* grounded_physics_source=nullptr;
        std::size_t grounded_ignore_index=static_cast<std::size_t>(-1);
        math::float3 previous_visual_root{};
        bool visual_root_valid=false;
        std::vector<std::string> procedural_ik_definitions;
        std::string mesh_source;
        float seconds=0.f;
        // Opt-in stable procedural pose for NPC rigs without native YCD.
        math::float3 npc_previous_position{};
        float npc_gait_phase=0.f;
        float npc_smoothed_speed=0.f;
        bool npc_position_valid=false;
        bool npc_pose_reported=false;
        std::string npc_last_native_clip;
        float transition_seconds=0.f;
        bool pose_ready=false; // last good GPU/CPU skin; never draw bind fallback
        std::vector<JointTransform> base_pose; // previous pre-IK sample
        std::vector<JointTransform> transition_pose; // outgoing keyed joints
        std::unique_ptr<SkeletonPose> pose;
        MeshAsset cpu_mesh; // Persistent, per-actor scratch with cached topology.
        // Reusable basis-correct skin palette for both CPU and GPU renderers.
        std::vector<math::float4x4> mesh_space_palette;
    };
    std::unordered_map<std::uint64_t,NativePlayback> native_playback;
    struct ScriptAnimationOverride {
        std::string clip_reference;
        float playback_rate = 1.f;
    };
    std::unordered_map<std::uint64_t,ScriptAnimationOverride> script_animation_overrides;
    std::unordered_set<std::uint64_t> reported_socket_attachments;
    std::unordered_set<std::uint64_t> reported_grip_attachments;
    std::chrono::steady_clock::time_point last_native_frame{};
    std::unordered_map<std::string,GeometryData> cached_geometry;
    std::unique_ptr<GeometryService> geometry_service;
    std::vector<NorthStarRenderTriangleVertex> mesh_vertices;
    viewport_detail::SkinFrameBatches skin_batches;
    viewport_detail::SkeletalSceneFrame skeletal_frame;
    // Per-scene scratch indexed once; no ABI/layout changes to frame
    // service used by independently compiled character tests.
    std::unordered_map<std::uint64_t,std::string_view> grip_presets;
    std::filesystem::path skin_cache_dir=viewport_detail::skin_cache_directory();
    NorthStarRenderTrianglesApi const* triangle_api = nullptr;
    void* renderer = nullptr;
};
