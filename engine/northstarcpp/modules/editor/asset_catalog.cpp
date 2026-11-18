// Cross-platform asset catalog; source of truth is project Content directory.
#include <northstarcpp/editor_asset_catalog.hpp>
#include <northstarcpp/embedded_script_api.hpp>
#include <northstarcpp/nac.hpp>
#include "asset_catalog_nef8_cache.hpp"
#include <northstarcpp/shared_assets.hpp>
#include <northstarcpp/scene.hpp>
#include <algorithm>
#include <unordered_set>
#include <chrono>
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
void index_time(AssetRecord& record,fs::directory_entry const& entry) noexcept {
    std::error_code ec;
    auto stamp=entry.last_write_time(ec);
    if(ec)return;
    using namespace std::chrono;
    // C++17 has no standardized file_clock -> system_clock conversion.
    auto converted=time_point_cast<milliseconds>(
        system_clock::now()+(stamp-fs::file_time_type::clock::now()));
    record.modified_epoch_ms=converted.time_since_epoch().count();
}
// Parse bounded prefab metadata during asynchronous catalog indexing only.
void index_prefab(AssetRecord& record,fs::path const& source) noexcept {
    if(record.size==0 || record.size>4u*1024u*1024u)return;
    try {
        std::ifstream input(source,std::ios::binary);
        if(!input)throw std::runtime_error("Unreadable prefab");
        std::string bytes(static_cast<std::size_t>(record.size),'\0');
        if(!input.read(bytes.data(),
                       static_cast<std::streamsize>(bytes.size())))
            throw std::runtime_error("Truncated prefab");
        auto scene=parse_scene_v3(bytes);
        validate_scene(scene);
        if(scene.version!=3 || scene.objects.empty())
            throw std::invalid_argument("Invalid or empty prefab");
        record.prefab_actor_count=scene.objects.size();
        record.prefab_valid=true;
    }catch(std::exception const&) {
        // Keep corrupt assets visible but never classify them as valid.
    }
}
}
char const* kind_name(AssetKind k) noexcept {
    switch(k) {
    case AssetKind::ui_document:return "UI Document";
    case AssetKind::folder:return "Folder";
    case AssetKind::mesh:return "Mesh";
    case AssetKind::collision:return "Collision";
    case AssetKind::material:return "Material";
    case AssetKind::texture:return "Texture";
    case AssetKind::audio:return "Audio";
    case AssetKind::particle:return "Particle";
    case AssetKind::scene:return "Scene";
    case AssetKind::prefab:return "Prefab";
    case AssetKind::character:return "Character";
    case AssetKind::material_function:return "Material Function";
    case AssetKind::script:return "Script";
    case AssetKind::animation:return "Animation";
    case AssetKind::shader:return "Shader";
    case AssetKind::skeleton:return "Skeleton";
    default:return "Other";
    }
}
AssetKind kind_from_extension(fs::path const& p) noexcept {
    auto e=lowercase(p.extension().u8string());
    if(e==".ybn" || e==".ncm")return AssetKind::collision;
    if(e==".ydd" || e==".ydr" || e==".mesh" || e==".obj" || e==".fbx" ||
       e==".gltf" || e==".glb")return AssetKind::mesh;
    auto const filename=lowercase(p.filename().u8string());
    if(filename.size()>=12 &&
       filename.compare(filename.size()-12,12,".nagraph.xml")==0)
        return AssetKind::animation;
    static constexpr char character_suffix[]=".character.json";
    constexpr auto character_suffix_length=sizeof(character_suffix)-1;
    if(filename.size()>character_suffix_length &&
       filename.compare(filename.size()-character_suffix_length,
                        character_suffix_length,character_suffix)==0)
        return AssetKind::character;
    if(e==".json" && filename.size()>23 &&
       filename.compare(filename.size()-23,23,
                        ".material-function.json")==0)
        return AssetKind::material_function;
    if(e==".ymat" ||
       (e==".json" && filename.size()>23 &&
        filename.compare(filename.size()-23,23,".material-instance.json")==0))
        return AssetKind::material;
    if(e==".ytd" || e==".png" || e==".jpg" || e==".jpeg" ||
       e==".dds" || e==".tga" || e==".webp" || e==".bmp" ||
       e==".ktx" || e==".ktx2")return AssetKind::texture;
    if(e==".wav" || e==".ogg" || e==".mp3" || e==".flac" || e==".nac")return AssetKind::audio;
    if(e==".nui")return AssetKind::ui_document;
    if(e==".nprefab")return AssetKind::prefab;
    if(e==".nscn" || e==".nscene" || e==".ymap" || e==".world")return AssetKind::scene;
    if(e==".js" || e==".mjs" || e==".cjs" || e==".jsx" ||
       e==".ts" || e==".tsx" || e==".ysc" || e==".lua" ||
       e==".py")return AssetKind::script;
    if(e==".ypt")return AssetKind::particle;
    if(e==".ymt")return AssetKind::skeleton;
    if(e==".anim" || e==".ycd" || e==".gltfanim")return AssetKind::animation;
    if(e==".vert" || e==".frag" || e==".hlsl" || e==".hlsli" || e==".shader" || e==".vs" ||
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
void AssetCatalog::scan(fs::path root,std::size_t max_records,
                        bool include_embedded_engine_api) {
    entries_.clear();
    entry_index_.clear();
    cache_hits_=0;
    cache_misses_=0;
    truncated_=false;
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
        // Engine shared on-disk scripts are obsolete. Never expose a writable
        // filesystem copy in the engine branch or let it shadow compiled API.
        if(include_embedded_engine_api &&
           (relative=="scripts" || relative.compare(0,8,"scripts/")==0)) {
            if(folder)it.disable_recursion_pending();
            continue;
        }
        if(!safe_virtual_path(relative)) {
            if(folder)it.disable_recursion_pending();
            continue;
        }
        AssetRecord rec;
        rec.virtual_path=relative;
        rec.filename=it->path().filename().u8string();
        rec.directory=folder;
        rec.kind=folder?AssetKind::folder:kind_from_extension(it->path());
        index_time(rec,*it);
        if(!folder) {
            rec.size=it->file_size(entry_error);
            if(entry_error)rec.size=0;
            if(rec.kind==AssetKind::prefab)
                index_prefab(rec,it->path());
            if(rec.kind==AssetKind::audio &&
               lowercase(it->path().extension().u8string())==".nac" &&
               rec.size>=40) {
                std::vector<nac::TrackInfo> tracks;
                std::string error;
                if(nac::inspect_file(it->path(),tracks,error)) {
                    if(tracks.size()>max_records-entries_.size()-1) {
                        truncated_=true;
                        break;
                    }
                    rec.directory=true;
                    rec.kind=AssetKind::folder;
                    rec.virtual_child_count=tracks.size();
                    rec.container_path=relative;
                    entries_.push_back(rec);
                    for(auto const& track:tracks) {
                        AssetRecord child;
                        child.entry_name="track-"+std::to_string(track.id);
                        child.virtual_path=relative+"/"+child.entry_name;
                        child.filename=track.name;
                        child.kind=AssetKind::audio;
                        child.container_path=relative;
                        child.size=track.payload_bytes;
                        child.audio_track_id=track.id;
                        child.audio_sample_rate=track.sample_rate;
                        child.audio_channels=track.channels;
                        child.audio_frames=track.frames;
                        child.modified_epoch_ms=rec.modified_epoch_ms;
                        entries_.push_back(std::move(child));
                    }
                    continue;
                }
                // A damaged container stays visible as an ordinary audio file.
            }
            auto ext=lowercase(it->path().extension().u8string());
            if((ext==".ytd" || ext==".ydd" || ext==".ydr" ||
                 ext==".ymat" || ext==".ymt" || ext==".ycd" ||
                  ext==".ypt") && rec.size>=128) {
                // Only valid NEF8 ListFiles are virtual directories.
                // Invalid/legacy files stay visible as ordinary assets.
                try {
                    if(rec.size>64u*1024u*1024u)
                        throw std::runtime_error("Oversized NEF8 dictionary");
                    auto const indexed=detail::inspect_dictionary(
                        it->path(),relative,rec.size);
                    auto const& contents=*indexed.entries;
                    if(indexed.cache_hit)++cache_hits_;
                    else ++cache_misses_;
                    if(contents.size()>max_records-entries_.size()-1) {
                        truncated_=true;
                        break;
                    }
                    rec.directory=true;
                    rec.kind=AssetKind::folder;
                    rec.virtual_child_count=contents.size();
                    rec.container_path=relative;
                    entries_.push_back(rec);
                    for(auto const& item:contents) {
                        AssetRecord child;
                        child.virtual_path=relative+"/"+item.name;
                        child.filename=item.name;
                        child.directory=false;
                        child.kind=(ext==".ydd"||ext==".ydr")?AssetKind::mesh:
                                   ext==".ymat"?AssetKind::material:
                                   ext==".ycd"?AssetKind::animation:
                                   ext==".ymt"?AssetKind::skeleton:
                                   ext==".ypt"?AssetKind::particle:
                                   AssetKind::texture;
                        child.container_path=relative;
                        child.entry_name=item.name;
                        child.size=item.size;
                        child.modified_epoch_ms=rec.modified_epoch_ms;
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
    // Prefab templates belong to project/prefabs, alongside project/assets.
    // Add them to Content as virtual prefabs/... paths without rewriting
    // existing mesh/material VFS references or following junctions.
    auto const prefab_root=root_.parent_path()/"prefabs";
    std::error_code prefab_ec;
    if(fs::is_directory(prefab_root,prefab_ec) &&
       !fs::is_symlink(fs::symlink_status(prefab_root,prefab_ec))) {
        if(entries_.size()<max_records) {
            AssetRecord folder_record;
            folder_record.virtual_path="prefabs";
            folder_record.filename="prefabs";
            folder_record.kind=AssetKind::folder;
            folder_record.directory=true;
            folder_record.external_prefab=true;
            entries_.push_back(std::move(folder_record));
        }
        fs::recursive_directory_iterator prefabs(prefab_root,
            fs::directory_options::skip_permission_denied,prefab_ec),end_prefabs;
        if(!prefab_ec)for(;prefabs!=end_prefabs;prefabs.increment(prefab_ec)) {
            if(prefab_ec){prefab_ec.clear();continue;}
            if(entries_.size()>=max_records){truncated_=true;break;}
            if(prefabs->is_symlink(prefab_ec)) {
                prefabs.disable_recursion_pending();
                continue;
            }
            bool const dir=prefabs->is_directory(prefab_ec);
            if(prefab_ec)continue;
            if(!dir && lowercase(prefabs->path().extension().u8string())!=".nprefab")
                continue;
            auto relative=prefabs->path().lexically_relative(prefab_root)
                .generic_u8string();
            auto virtual_path=std::string("prefabs/")+relative;
            if(!safe_virtual_path(virtual_path)) {
                if(dir)prefabs.disable_recursion_pending();
                continue;
            }
            AssetRecord record;
            record.virtual_path=virtual_path;
            record.filename=prefabs->path().filename().u8string();
            record.directory=dir;
            record.kind=dir?AssetKind::folder:AssetKind::prefab;
            record.external_prefab=true;
            index_time(record,*prefabs);
            if(!dir) {
                record.size=prefabs->file_size(prefab_ec);
                if(prefab_ec)record.size=0;
                index_prefab(record,prefabs->path());
            }
            entries_.push_back(std::move(record));
        }
    }
    // The engine mount includes a read-only virtual directory whose sources
    // live in the executable, not the filesystem or the project VFS.
    if(include_embedded_engine_api) {
        // The physical scripts directory was skipped by the engine scan.
        // Dedupe only the few virtual script directories, not every asset
        // in the potentially 100k-entry catalog (memory and CPU reduction).
        std::unordered_set<std::string> virtual_paths;
        virtual_paths.reserve(embedded_api::count()*3+1);
        auto add=[&](std::string path,bool folder,std::size_t bytes) {
            if(!virtual_paths.emplace(path).second)return;
            if(entries_.size()>=max_records){truncated_=true;return;}
            AssetRecord rec;
            rec.virtual_path=std::move(path);
            rec.filename=fs::u8path(rec.virtual_path).filename().u8string();
            rec.kind=folder?AssetKind::folder:AssetKind::script;
            rec.directory=folder;
            rec.embedded_engine_api=true;
            rec.size=bytes;
            entries_.push_back(std::move(rec));
        };
        add("scripts",true,0);
        auto const* const scripts=embedded_api::entries();
        auto const count=embedded_api::count();
        for(std::size_t i=0;i<count;++i) {
            auto const& script=scripts[i];
            auto const path=std::string(script.virtual_path);
            if(!safe_virtual_path(path))continue;
            auto slash=path.find('/',8);
            while(slash!=std::string::npos) {
                add(path.substr(0,slash),true,0);
                slash=path.find('/',slash+1);
            }
            add(path,false,script.source.size());
        }
    }
    // Previous comparator allocated two lowercase strings for every
    // comparison (O(N log N) heap churn). Compare ASCII-folded bytes
    // directly; preserve deterministic order for case-only differences.
    std::sort(entries_.begin(),entries_.end(),
        [](AssetRecord const& a,AssetRecord const& b){
            if(a.directory!=b.directory)return a.directory>b.directory;
            auto const& left=a.virtual_path;
            auto const& right=b.virtual_path;
            const auto count=std::min(left.size(),right.size());
            for(std::size_t i=0;i<count;++i) {
                auto x=static_cast<unsigned char>(left[i]);
                auto y=static_cast<unsigned char>(right[i]);
                x=static_cast<unsigned char>(std::tolower(x));
                y=static_cast<unsigned char>(std::tolower(y));
                if(x!=y)return x<y;
            }
            if(left.size()!=right.size())return left.size()<right.size();
            return left<right;
        });
    entry_index_.reserve(entries_.size());
    for(std::size_t index=0;index<entries_.size();++index)
        entry_index_.emplace(entries_[index].virtual_path,index);
}
AssetRecord const* AssetCatalog::find(std::string const& virtual_path) const noexcept {
    const auto it=entry_index_.find(virtual_path);
    return it==entry_index_.end()?nullptr:&entries_[it->second];
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
    auto const* record=find(virtual_path);
    if(record && record->embedded_engine_api)
        throw std::invalid_argument("Engine API is embedded, not a physical file");
    if(record && record->is_virtual_entry())
        throw std::invalid_argument("ListFile child is not a physical file");
    if(root_.empty() || !safe_virtual_path(virtual_path))
        throw std::invalid_argument("Unsafe asset reference");
    fs::path requested=fs::u8path(virtual_path);
    if(requested.is_absolute() || requested.has_root_name())
        throw std::invalid_argument("Absolute asset reference");
    bool const prefab=record && record->external_prefab;
    auto const base=prefab?root_.parent_path():root_;
    auto full=(base/requested).lexically_normal();
    if(!belongs_to(full,prefab?root_.parent_path()/"prefabs":root_))
        throw std::invalid_argument("Asset escaped project root");
    // A changed on-disk symlink must never allow resolving outside the
    // project even if an earlier scan did not follow symlinks.
    std::error_code error;
    auto actual=fs::weakly_canonical(full,error);
    if(!error) {
        auto root_actual=fs::weakly_canonical(
            prefab?root_.parent_path()/"prefabs":root_,error);
        if(!error && !belongs_to(actual,root_actual))
            throw std::invalid_argument("Asset symlink escaped project root");
    }
    return full;
}
} // namespace northstarcpp::editor
