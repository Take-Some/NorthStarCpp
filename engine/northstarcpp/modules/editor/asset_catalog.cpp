// Cross-platform asset catalog; source of truth is project Content directory.
#include <northstarcpp/editor_asset_catalog.hpp>
#include <northstarcpp/shared_assets.hpp>
#include <algorithm>
#include <fstream>
#include <cctype>
#include <stdexcept>
#include <string>
#include <system_error>

namespace northstarcpp::editor {
namespace fs=std::filesystem;
namespace {
std::string lowercase(std::string name) {
    for(auto& c:name)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return name;
}
bool belongs_to(fs::path const& candidate, fs::path const& root) {
    auto a=candidate.begin(),b=root.begin();
    for(;b!=root.end();++a,++b)
        if(a==candidate.end() || *a!=*b) return false;
    return true;
}
}
char const* kind_name(AssetKind k) noexcept {
    switch(k) {
    case AssetKind::folder:return "Folder";
    case AssetKind::mesh:return "Mesh";
    case AssetKind::material:return "Material";
    case AssetKind::texture:return "Texture";
    case AssetKind::audio:return "Audio";
    case AssetKind::scene:return "Scene";
    case AssetKind::script:return "Script";
    case AssetKind::animation:return "Animation";
    case AssetKind::shader:return "Shader";
    default:return "Other";
    }
}
AssetKind kind_from_extension(fs::path const& p) noexcept {
    auto e=lowercase(p.extension().u8string());
    if(e==".ydd" || e==".mesh" || e==".obj" || e==".fbx" ||
       e==".gltf" || e==".glb")return AssetKind::mesh;
    if(e==".ymat" || e==".material" || e==".mtl")return AssetKind::material;
    if(e==".ytd" || e==".png" || e==".jpg" || e==".jpeg" ||
       e==".dds" || e==".tga" || e==".webp" || e==".bmp" ||
       e==".ktx" || e==".ktx2")return AssetKind::texture;
    if(e==".wav" || e==".ogg" || e==".mp3" || e==".flac")return AssetKind::audio;
    if(e==".nscn" || e==".nscene" || e==".nprefab" || e==".ymap" || e==".world")return AssetKind::scene;
    if(e==".js" || e==".mjs" || e==".ysc" || e==".lua" ||
       e==".ts" || e==".py")return AssetKind::script;
    if(e==".anim" || e==".ycd" || e==".gltfanim")return AssetKind::animation;
    if(e==".hlsl" || e==".hlsli" || e==".shader" || e==".vs" ||
       e==".ps" || e==".cs" || e==".glsl" || e==".spv")
       return AssetKind::shader;
    return AssetKind::other;
}
bool safe_virtual_path(std::string const& name) noexcept {
    if(name.empty() || name[0]=='/' || name.find('\\')!=std::string::npos ||
       name.find(':')!=std::string::npos || name.find('\0')!=std::string::npos)
        return false;
    std::size_t start=0;
    while(start<name.size()) {
        auto end=name.find('/',start);
        if(end==std::string::npos)end=name.size();
        auto part=name.substr(start,end-start);
        if(part.empty() || part==".." || part=="." || part.size()>255)return false;
        start=end+1;
    }
    return name.back()!='/';
}
void AssetCatalog::scan(fs::path root,std::size_t max_records) {
    entries_.clear();truncated_=false;
    root_=fs::absolute(root).lexically_normal();
    std::error_code ec;
    if(!fs::is_directory(root_,ec))
        throw std::invalid_argument("Asset root must be a directory");
    if(max_records==0 || max_records>1000000)
        throw std::invalid_argument("Asset record limit must be 1..1000000");
    fs::recursive_directory_iterator it(root_,
        fs::directory_options::skip_permission_denied,ec), end;
    if(ec)throw std::runtime_error("Cannot iterate asset root");
    for(;it!=end;it.increment(ec)) {
        if(ec) {ec.clear();continue;}
        std::error_code entry_error;
        if(it->is_symlink(entry_error)) {
            it.disable_recursion_pending();
            continue;
        }
        bool folder=it->is_directory(entry_error);
        if(entry_error)continue;
        if(entries_.size()>=max_records) {truncated_=true;break;}
        auto relative=it->path().lexically_relative(root_).generic_u8string();
        if(!safe_virtual_path(relative)) {
            if(folder)it.disable_recursion_pending();
            continue;
        }
        AssetRecord rec;
        rec.virtual_path=relative;
        rec.filename=it->path().filename().u8string();
        rec.directory=folder;
        rec.kind=folder?AssetKind::folder:kind_from_extension(it->path());
        if(!folder) {
            rec.size=it->file_size(entry_error);
            if(entry_error)rec.size=0;
            auto ext=lowercase(it->path().extension().u8string());
            if((ext==".ytd" || ext==".ydd" || ext==".ymat") && rec.size>=128) {
                // Only valid NEF8 ListFiles are virtual directories.
                // Invalid/legacy files stay visible as ordinary assets.
                try {
                    if(rec.size>64u*1024u*1024u)
                        throw std::runtime_error("Oversized NEF8 dictionary");
                    std::ifstream input(it->path(),std::ios::binary);
                    if(!input)throw std::runtime_error("Cannot read ListFile");
                    std::vector<std::uint8_t> data(static_cast<std::size_t>(rec.size));
                    if(!input.read(reinterpret_cast<char*>(data.data()),
                                   static_cast<std::streamsize>(data.size())))
                        throw std::runtime_error("Incomplete ListFile");
                    auto const contents=shared_assets::list_dictionary_entries(
                        data,relative);
                    if(contents.size()>max_records-entries_.size()-1) {
                        truncated_=true;
                        break;
                    }
                    rec.directory=true;
                    rec.kind=AssetKind::folder;
                    rec.container_path=relative;
                    entries_.push_back(rec);
                    for(auto const& item:contents) {
                        AssetRecord child;
                        child.virtual_path=relative+"/"+item.name;
                        child.filename=item.name;
                        child.directory=false;
                        child.kind=ext==".ydd"?AssetKind::mesh:
                                   ext==".ymat"?AssetKind::material:AssetKind::texture;
                        child.container_path=relative;
                        child.entry_name=item.name;
                        child.size=item.size;
                        entries_.push_back(std::move(child));
                    }
                    continue;
                } catch(std::exception const&) {
                    // Ignore malformed containers, not the rest of the catalog.
                }
            }
        }
        entries_.push_back(std::move(rec));
    }
    std::sort(entries_.begin(),entries_.end(),
        [](AssetRecord const& a,AssetRecord const& b){
            if(a.directory!=b.directory)return a.directory>b.directory;
            return lowercase(a.virtual_path)<lowercase(b.virtual_path);
        });
}
AssetRecord const* AssetCatalog::find(std::string const& virtual_path) const noexcept {
    auto it=std::find_if(entries_.begin(),entries_.end(),
        [&](AssetRecord const& value){return value.virtual_path==virtual_path;});
    return it==entries_.end()?nullptr:&*it;
}
fs::path AssetCatalog::backing_file(std::string const& virtual_path) const {
    auto rec=find(virtual_path);
    if(!rec)throw std::invalid_argument("Asset not found in index");
    return resolve(rec->container_path.empty()?virtual_path:rec->container_path);
}
std::string AssetCatalog::asset_reference(std::string const& virtual_path) const {
    auto rec=find(virtual_path);
    if(!rec)throw std::invalid_argument("Asset not found in index");
    return rec->asset_reference();
}
fs::path AssetCatalog::resolve(std::string const& virtual_path) const {
    if(auto rec=find(virtual_path);rec && rec->is_virtual_entry())
        throw std::invalid_argument("ListFile child is not a physical file");
    if(root_.empty() || !safe_virtual_path(virtual_path))
        throw std::invalid_argument("Unsafe asset reference");
    fs::path requested=fs::u8path(virtual_path);
    if(requested.is_absolute() || requested.has_root_name())
        throw std::invalid_argument("Absolute asset reference");
    auto full=(root_/requested).lexically_normal();
    if(!belongs_to(full,root_))
        throw std::invalid_argument("Asset escaped project root");
    // A changed on-disk symlink must never allow resolving outside the
    // project even if an earlier scan did not follow symlinks.
    std::error_code error;
    auto actual=fs::weakly_canonical(full,error);
    if(!error) {
        auto root_actual=fs::weakly_canonical(root_,error);
        if(!error && !belongs_to(actual,root_actual))
            throw std::invalid_argument("Asset symlink escaped project root");
    }
    return full;
}
} // namespace northstarcpp::editor
