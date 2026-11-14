// NorthStarCpp - OBJ resource through the active VFS
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#include <northstarcpp/mesh_asset.hpp>
#include <northstarcpp/assets.hpp>
#include <stdexcept>

namespace northstarcpp {
MeshAsset load_obj_mesh(AssetStore const& store, std::string const& path) {
    if (path.size()<18 || path.size()>256 ||
        path.rfind("assets/models/",0)!=0 ||
        path.compare(path.size()-4,4,".obj")!=0 ||
        path.find('\\')!=std::string::npos ||
        path.find("..")!=std::string::npos ||
        path.find("//")!=std::string::npos)
        throw std::invalid_argument("OBJ path must be assets/models/*.obj");
    if (store.size(path)>8*1024*1024)
        throw std::invalid_argument("OBJ asset exceeds 8 MiB");
    auto bytes=store.read(path);
    return parse_obj_mesh(std::string(bytes.begin(),bytes.end()));
}
} // namespace northstarcpp
