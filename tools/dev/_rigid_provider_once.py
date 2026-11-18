from pathlib import Path
p=Path("engine/northstarcpp/modules/render/d3d12/d3d12.cpp")
s=p.read_text("utf-8")
needle="int set_temporal_instances(void* instance,"
assert s.count(needle)==1
code="""int rigid_upload(void* instance,NorthStarRigidMesh const* mesh) {
    return guarded([&]{
        if(!instance||!mesh)throw std::invalid_argument("Rigid mesh missing");
        static_cast<Renderer*>(instance)->upload_rigid_mesh(*mesh);
    });
}
int rigid_clear(void* instance) {
    return guarded([&]{
        if(!instance)throw std::invalid_argument("Rigid renderer missing");
        static_cast<Renderer*>(instance)->clear_rigid_meshes();
    });
}
int rigid_instances(void* instance,
                    NorthStarRigidInstance const* draws,std::uint32_t count) {
    return guarded([&]{
        if(!instance)throw std::invalid_argument("Rigid renderer missing");
        static_cast<Renderer*>(instance)->set_rigid_instances(draws,count);
    });
}
const NorthStarRenderRigidInstancesApi rigid_api{
    NORTHSTARCPP_RENDER_RIGID_INSTANCES_ABI,
    sizeof(NorthStarRenderRigidInstancesApi),
    rigid_upload,rigid_clear,rigid_instances};
"""
s=s.replace(needle,code+needle,1)
needle="    if(version==NORTHSTARCPP_RENDER_MATERIALS_V2_MODULE_ABI) {"
assert s.count(needle)==1
s=s.replace(needle,'''    if(version==NORTHSTARCPP_RENDER_RIGID_INSTANCES_MODULE_ABI) {
        static const NorthStarModule extension{sizeof(NorthStarModule),
            NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_RENDER,
            "d3d12","1.8.0",NORTHSTARCPP_RENDER_RIGID_INSTANCES_MODULE_ABI,
            sizeof(rigid_api),&rigid_api,last_error};
        return match(extension,host,capability,version);
    }
'''+needle,1)
p.write_text(s,"utf-8")
print("D3D12 module advertises optional rigid instances ABI")
