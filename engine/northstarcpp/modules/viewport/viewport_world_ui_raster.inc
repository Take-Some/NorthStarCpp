    // Bounded cache of script-authored world UI textures (one per Entity).
    struct WorldUiLayer {
        UiDocument document;
        std::vector<std::uint8_t> pixels;
        std::array<NorthStarMaterialVertex,12> vertices{};
        bool dirty=true;
    };
    void prepare_world_ui(WorldUiLayer& layer) {
        if(!layer.dirty)return;
        if(!ui_module) {
            ui_module=std::make_unique<Module>(NORTHSTARCPP_CAPABILITY_UI);
            ui_api=ui_module->query<NorthStarUIApi>(NORTHSTARCPP_UI_ABI);
            ui_raster=ui_module->query_extension<NorthStarUIRasterApi>(
                NORTHSTARCPP_UI_RASTER_ABI);
            ui_input_api=ui_module->query_extension<NorthStarUIInputApi>(
                NORTHSTARCPP_UI_INPUT_ABI);
            if(!ui_raster)
                throw std::runtime_error("World UI requires raster UI provider");
        }
        NorthStarUIConfig cfg{sizeof(NorthStarUIConfig),0,nullptr};
        void* context=nullptr;
        if(ui_api->create_context(&cfg,&context)!=NORTHSTAR_UI_OK)
            throw ui_module->error("World UI context failed");
        struct Guard {
            NorthStarUIApi const* api;
            void* context;
            ~Guard(){if(context)api->destroy_context(context);}
        } guard{ui_api,context};
        auto root=ui::element(NORTHSTAR_UI_ROOT,0,"world");
        NorthStarUIHandle root_handle=0;
        if(ui_api->create_element(context,&root,&root_handle)!=NORTHSTAR_UI_OK)
            throw ui_module->error("World UI root failed");
        std::unordered_map<std::string,NorthStarUIHandle> handles{{"",root_handle}};
        for(const auto& node:layer.document.nodes) {
            auto it=handles.find(node.parent);
            if(it==handles.end())
                throw std::invalid_argument("World UI parent must precede child");
            auto desc=ui::element(node.kind,it->second,node.id);
            NorthStarUIHandle handle=0;
            if(ui_api->create_element(context,&desc,&handle)!=NORTHSTAR_UI_OK)
                throw ui_module->error("World UI element failed");
            handles.emplace(node.id,handle);
            for(const auto& [key,input]:node.properties) {
                NorthStarUIValue value{};
                switch(input.kind) {
                case NORTHSTAR_UI_VALUE_NUMBER:value=ui::number(input.number);break;
                case NORTHSTAR_UI_VALUE_BOOL:value=ui::boolean(input.boolean);break;
                case NORTHSTAR_UI_VALUE_STRING:value=ui::text(input.text);break;
                case NORTHSTAR_UI_VALUE_COLOR_RGBA8:
                    value={sizeof(NorthStarUIValue),NORTHSTAR_UI_VALUE_COLOR_RGBA8,
                           0,input.rgba,0,{}};break;
                default:throw std::invalid_argument("Invalid world UI property");
                }
                if(ui_api->set_property(context,handle,ui::string(key),&value)
                   !=NORTHSTAR_UI_OK)
                    throw ui_module->error("World UI property failed");
            }
        }
        auto const w=layer.document.width,h=layer.document.height;
        if(ui_api->resize(context,w,h,1.f)!=NORTHSTAR_UI_OK)
            throw ui_module->error("World UI resize failed");
        std::vector<std::uint8_t> rgba(static_cast<size_t>(w)*h*4);
        if(ui_raster->draw_rgba(context,rgba.data(),w*4,w,h)!=NORTHSTAR_UI_OK)
            throw ui_module->error("World UI raster failed");
        layer.pixels.swap(rgba);
        layer.dirty=false;
    }
