    // World UI is deliberately NOT a screen overlay. Its cached RGBA8
    // document becomes an unlit UV material on a quad in the 3D scene.
    // The authored Entity provides position, local yaw and bounding size.
    if(!state->world_ui.empty()) {
        if(!state->material_api_v2)
            throw std::runtime_error("World UI requires render materials ABI v2");
        for(auto& [id,layer]:state->world_ui) {
            auto found=std::find_if(scene.objects.begin(),scene.objects.end(),
                [&](auto const& obj){return std::to_string(obj.node.id)==id;});
            if(found==scene.objects.end())continue;
            if(gpu_materials_v2.size()>=NORTHSTARCPP_RENDER_MATERIAL_MAX_BATCHES)
                throw std::length_error("World UI material batch limit");
            state->prepare_world_ui(layer);
            auto const& obj=*found;
            const float yaw=obj.node.rotation.y*0.017453292519943295f;
            const float sy=std::sin(yaw),cy=std::cos(yaw);
            const Vector3 right{cy,0.f,-sy};
            const Vector3 outward{-sy,0.f,-cy};
            const Vector3 center{
                obj.position.x+outward.x*(obj.size*.5f+.025f),
                obj.position.y+obj.size*.5f,
                obj.position.z+outward.z*(obj.size*.5f+.025f)
            };
            auto vertex=[&](float u,float v,bool back) {
                return NorthStarMaterialVertex{
                    center.x+right.x*(u-.5f)*layer.document.world_width,
                    center.y+(.5f-v)*layer.document.world_height,
                    center.z+right.z*(u-.5f)*layer.document.world_width,
                    back?-outward.x:outward.x,0.f,back?-outward.z:outward.z,
                    u,v
                };
            };
            layer.vertices={
                vertex(0,1,false),vertex(0,0,false),vertex(1,1,false),
                vertex(1,1,false),vertex(0,0,false),vertex(1,0,false),
                vertex(1,1,true),vertex(0,0,true),vertex(0,1,true),
                vertex(1,0,true),vertex(0,0,true),vertex(1,1,true)
            };
            NorthStarMaterialBatch batch{};
            batch.vertices=layer.vertices.data();
            batch.vertex_count=static_cast<std::uint32_t>(layer.vertices.size());
            batch.base_color[0]=batch.base_color[1]=
                batch.base_color[2]=batch.base_color[3]=1.f;
            batch.roughness=1.f;
            batch.unlit=1;
            batch.textures[0]={
                layer.pixels.data(),layer.document.width,layer.document.height,
                static_cast<std::uint32_t>(layer.pixels.size())
            };
            NorthStarMaterialBatchV2 variant{};
            variant.material=batch;
            variant.blend_mode=2;
            variant.opacity=1.f;
            gpu_materials.push_back(batch);
            gpu_materials_v2.push_back(variant);
            ++state->asset_stats.material_batches;
            ++state->asset_stats.textured_material_batches;
            material_vertex_total+=layer.vertices.size();
            if(material_vertex_total>NORTHSTARCPP_RENDER_MATERIAL_MAX_VERTICES)
                throw std::length_error("World UI vertex limit");
            needs_material_v2=true;
            pick_complete=false; // translucent geometry needs dedicated ID pass
        }
    }
