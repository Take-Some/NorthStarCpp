#include <northstarcpp/module.h>
#include <northstarcpp/render_plugin.h>
#include <northstarcpp/render_rigid_instances_api.h>
#include <northstarcpp/render_rigid_residency_api.h>
#include <windows.h>
#include <cstdio>
#include <stdexcept>
static void check(bool x,const char* msg){if(!x)throw std::runtime_error(msg);}
int main(int argc,char** argv){
 if(argc!=2)return 2;
 HMODULE dll=LoadLibraryA(argv[1]);HWND hwnd=nullptr;void* context=nullptr;
 NorthStarRenderApi const* renderer=nullptr;
 try {
  check(dll!=nullptr,"DLL");
  auto query=(NorthStarModuleQuery)GetProcAddress(dll,"northstar_module_query");
  check(query!=nullptr,"query");
  auto base=query(NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_RENDER,NORTHSTARCPP_RENDER_ABI);
  auto ext=query(NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_RENDER,NORTHSTARCPP_RENDER_RIGID_INSTANCES_MODULE_ABI);
  auto retirement=query(NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_RENDER,
      NORTHSTARCPP_RENDER_RIGID_RESIDENCY_MODULE_ABI);
  check(base&&ext&&retirement&&base->api&&ext->api&&retirement->api,"ABI");
  renderer=(NorthStarRenderApi const*)base->api;
  auto api=(NorthStarRenderRigidInstancesApi const*)ext->api;
  auto residency=(NorthStarRenderRigidResidencyApi const*)retirement->api;
  check(residency->abi_version==NORTHSTARCPP_RENDER_RIGID_RESIDENCY_ABI &&
        residency->release_meshes!=nullptr,"residency ABI");
  WNDCLASSA cls{};cls.lpfnWndProc=DefWindowProcA;
  cls.hInstance=GetModuleHandleA(nullptr);cls.lpszClassName="RigidWarpNS";
  check(RegisterClassA(&cls)!=0,"class");
  hwnd=CreateWindowExA(0,cls.lpszClassName,"Test",WS_OVERLAPPEDWINDOW,
      0,0,320,240,nullptr,nullptr,cls.hInstance,nullptr);
  check(hwnd!=nullptr,"window");
  context=renderer->create(hwnd,1);
  if(!context && base->last_error)
    std::fprintf(stderr,"D3D12 create: %s\n",base->last_error());
  check(context!=nullptr,"renderer");
  NorthStarMaterialVertex verts[]{
    {-1,-1,0,0,0,-1,0,0},{1,-1,0,0,0,-1,1,0},
    {1,1,0,0,0,-1,1,1},{-1,1,0,0,0,-1,0,1}};
  uint32_t idx[]{0,1,2,0,2,3};
  NorthStarRigidMesh mesh{1001,verts,4,idx,6};
  check(api->upload_mesh(context,&mesh)!=0,"mesh upload");
  NorthStarRigidInstanceStats stats{};
  stats.struct_size=sizeof(stats);
  check(api->read_stats(context,&stats)!=0,"read GPU residency stats");
  check(stats.resident_meshes==1 &&
        stats.resident_geometry_bytes==sizeof(verts)+sizeof(idx) &&
        stats.lifetime_geometry_uploaded_bytes==sizeof(verts)+sizeof(idx),
        "immutable mesh GPU bytes inaccurate");
  verts[0].x=123456.f;idx[0]=1000;
  check(api->upload_mesh(context,&mesh)!=0,"cached immutable mesh");
  NorthStarRigidInstance v{};v.mesh_key=1001;v.entity_id=7;v.index_count=6;
  v.world[0]=v.world[5]=v.world[10]=v.world[15]=1;v.world[14]=5;
  v.material.base_color[0]=.7f;v.material.base_color[1]=.2f;
  v.material.base_color[2]=.1f;v.material.base_color[3]=1;
  v.material.roughness=.5f;
  NorthStarRenderFrame frame{};frame.first_person=1;
  for(int k=0;k<11;++k) {
    v.world[12]=k*.03f;
    check(api->set_instances(context,&v,1)!=0,"update matrix");
    check(renderer->draw(context,&frame)!=0,"indexed render");
    stats.struct_size=sizeof(stats);
    check(api->read_stats(context,&stats)!=0 && stats.active_draws==1 &&
          stats.resident_meshes==1 && stats.last_transform_bytes==64 &&
          stats.last_drawn_indices==6,"transform-only GPU telemetry wrong");
  }
  v.first_index=7;
  check(api->set_instances(context,&v,1)==0,"reject bad range");
  // Destruction is forbidden while the previous frame still references it.
  uint64_t key=1001;
  check(residency->release_meshes(context,&key,1)==0,
        "active GPU mesh was illegally freed");
  check(api->set_instances(context,nullptr,0)!=0,"clear active draws");
  check(residency->release_meshes(context,&key,1)!=0,
        "fence-safe per-mesh retirement failed");
  stats.struct_size=sizeof(stats);
  check(api->read_stats(context,&stats)!=0 && stats.resident_meshes==0 &&
        stats.resident_geometry_bytes==0,"actual D3D12 buffers not released");
  check(api->set_instances(context,&v,1)==0,"retired mesh still drawable");
  check(api->clear_meshes(context)!=0,"clear");
  check(api->set_instances(context,&v,1)==0,"reject stale mesh");
  std::puts("D3D12 WARP immutable indexed mesh + matrix updates PASS");
  renderer->destroy(context);DestroyWindow(hwnd);FreeLibrary(dll);
  return 0;
 } catch(std::exception const& e){
   std::fprintf(stderr,"RIGID WARP: %s\n",e.what());
   if(context&&renderer)renderer->destroy(context);
   if(hwnd)DestroyWindow(hwnd);if(dll)FreeLibrary(dll);return 1;
 }
}
