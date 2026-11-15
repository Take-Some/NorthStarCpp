// NEF8 ListFile virtual directory indexing for the editor.
// Reuses the validated envelope/decompression logic; never extracts files.
#include <northstarcpp/shared_assets.hpp>
#include <northstarcpp/assets.hpp>
#include <northstarcpp/skeleton.hpp>
#include "asset_list_file.hpp"
#include <set>
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace northstarcpp::shared_assets {
std::vector<AnimationClipInfo> inspect_animation_clip_dictionary(
    std::vector<std::uint8_t> const& source) {
    using namespace detail;
    auto file=parse_list_file(source);
    require(file.kind==11 && file.entries>0 && file.entries<=4096,
            "Expected a native YCD animation dictionary");
    Bytes const bytes(file.body);
    auto const version=bytes.integer(0,4);
    auto const count=bytes.integer(4,4);
    auto const table=bytes.integer(8,8);
    auto const strings=bytes.integer(16,8);
    auto const payload=bytes.integer(32,8);
    require((version==2 || version==3) && count==file.entries &&
            table==48 && strings>=table &&
            file.entries<=(strings-table)/64 &&
            strings<=payload && payload<=bytes.size(),
            "Invalid native YCD table offsets");
    std::vector<AnimationClipInfo> clips;
    clips.reserve(file.entries);
    std::set<std::string> names;
    for(std::size_t i=0;i<file.entries;++i) {
        auto const record=table+64*i;
        auto const offset=bytes.integer(record+8,4);
        require(offset<payload-strings,"YCD name offset outside string table");
        auto const* begin=bytes.at(strings+offset,1);
        auto const remaining=std::min<std::uint64_t>(payload-strings-offset,257);
        auto const* end=std::find(begin,bytes.at(strings+offset,remaining)+remaining,0);
        require(end!=begin+remaining,"Unterminated YCD clip name");
        std::string name(begin,end);
        validate_name(name);
        require(names.insert(name).second,"Duplicate YCD clip name");
        AnimationClipInfo clip;
        clip.name=std::move(name);
        clip.tracks=static_cast<std::uint32_t>(bytes.integer(record+16,4));
        clip.frames=static_cast<std::uint32_t>(bytes.integer(record+20,4));
        clip.duration=bytes.real(record+24);
        clip.sample_rate=bytes.real(record+28);
        auto const looping=bytes.integer(record+32,4);
        clip.loop=looping==1;
        auto const start=bytes.integer(record+40,8);
        auto const length=bytes.integer(record+48,8);
        require(clip.tracks>0 && clip.tracks<=4096 &&
                clip.frames>0 && clip.frames<=12000 &&
                static_cast<std::uint64_t>(clip.frames)*clip.tracks<=1500000 &&
                std::isfinite(clip.sample_rate) && clip.sample_rate>=1.f &&
                clip.sample_rate<=240.f && std::isfinite(clip.duration) &&
                looping<=1 &&
                std::abs(clip.duration-
                    ((clip.frames==1?1.f:(clip.frames-1.f))/
                     clip.sample_rate))<.02f &&
                start>=payload && start<=bytes.size() &&
                length==static_cast<std::uint64_t>(clip.tracks)*
                    (4ull+40ull*clip.frames) &&
                length<=bytes.size()-start,
                "Invalid YCD clip frame/track payload");
        clips.push_back(std::move(clip));
    }
    return clips;
}
std::vector<DictionaryEntry> list_dictionary_entries(
    std::vector<std::uint8_t> const& source,std::string const& path) {
    using namespace detail;
    auto const ext=extension(path);
    require(ext==".ytd" || ext==".ydd" || ext==".ydr" ||
            ext==".ymat" || ext==".ymt" || ext==".ycd",
            "Expected NEF8 YTD/YDD/YMAT/YMT/YCD dictionary");
    auto file=parse_list_file(source); // size, deflate and hash checks
    std::vector<DictionaryEntry> output;
    output.reserve(file.entries);
    std::set<std::string> unique;
    if(ext==".ymt") {
        auto skeleton=northstarcpp::load_skeleton_from_bytes(source);
        require(!skeleton.joints.empty(),"Empty native YMT skeleton");
        output.reserve(skeleton.joints.size());
        for(auto const& joint:skeleton.joints) {
            validate_name(joint.name);
            require(unique.insert(joint.name).second,"Duplicate YMT joint");
            output.push_back({joint.name,0});
        }
        return output; // YMT has one skeleton record, multiple joints.
    } else if(ext==".ycd") {
        auto clips=inspect_animation_clip_dictionary(source);
        require(clips.size()==file.entries,"YCD clip count mismatch");
        for(auto const& clip:clips)
            output.push_back({clip.name,0});
        return output;
    } else if(ext==".ymat") {
        auto materials=detail::parse_materials(file,path);
        require(materials.size()==file.entries,"YMAT ListFile entry count mismatch");
        for(auto const& material:materials) {
            validate_name(material.name);
            require(unique.insert(material.name).second,"Duplicate YMAT material");
            output.push_back({material.name,0});
        }
    } else if(ext==".ydd" || ext==".ydr") {
        require(file.kind==2,"Expected native drawable dictionary");
        if (!file.body.empty() && file.body.front()!='{') {
            auto const parsed=load_drawables_from_bytes(source,path);
            require(parsed.size()==file.entries,
                "Native binary drawable entry count differs from NEF8 header");
            for (auto const& drawable:parsed) {
                validate_name(drawable.name);
                require(unique.insert(drawable.name).second,
                    "Duplicate binary drawable entry");
                output.push_back({drawable.name,0});
            }
            return output;
        }
        auto const body=parse_json(std::string(file.body.begin(),file.body.end()));
        require(body.get<std::string>("schema","")==
                    "newengine.ydd.drawable_dictionary.v1" &&
                body.get<std::string>("mesh_encoding","")==
                    "newengine.ydd.runtime_mesh_parts.v1",
                "Unsupported YDD ListFile schema");
        auto const& entries=body.get_child("entries");
        auto const& names=body.get_child("common_entries");
        require(entries.size()==file.entries && names.size()==file.entries,
                "YDD ListFile entry count mismatch");
        for(auto const& item:names) {
            auto name=item.second.get<std::string>("name");
            validate_name(name);
            require(unique.insert(name).second,"Duplicate YDD drawable entry");
            output.push_back({std::move(name),0});
        }
    } else {
        require(file.kind==1,"Expected YTD texture dictionary");
        Bytes const bytes(file.body);
        require(bytes.size()>=64 && bytes.magic(0,"NETD") &&
                    bytes.integer(4,4)==2 && bytes.integer(8,4)==64 &&
                    bytes.integer(12,4)==file.entries,
                "Unsupported YTD NETD ListFile");
        auto table=bytes.integer(16,8);
        auto table_size=bytes.integer(24,8);
        auto payload=bytes.integer(32,8);
        auto payload_size=bytes.integer(40,8);
        require(table>=64 && table<=payload &&
                table_size<=payload-table && table_size>=40 &&
                payload<=bytes.size() && payload_size==bytes.size()-payload,
                "Invalid YTD texture table");
        require(bytes.magic(table,"NTDX") &&
                bytes.integer(table+4,2)==1 &&
                bytes.integer(table+6,2)==64 &&
                bytes.integer(table+8,4)==32 &&
                bytes.integer(table+12,4)==file.entries,
                "Unsupported YTD texture index");
        auto const entry_base=bytes.integer(table+20,4);
        auto const mip_base=bytes.integer(table+24,4);
        auto const name_base=bytes.integer(table+28,4);
        auto const names_size=bytes.integer(table+32,4);
        auto const mip_count=bytes.integer(table+16,4);
        require(entry_base>=40 && entry_base<=mip_base &&
                file.entries<=(mip_base-entry_base)/64 &&
                mip_base<=name_base &&
                mip_count<=(name_base-mip_base)/32 &&
                name_base<=table_size && names_size<=table_size-name_base,
                "YTD ListFile index ranges invalid");
        for(std::size_t i=0;i<file.entries;++i) {
            auto entry=table+entry_base+i*64;
            auto offset=bytes.integer(entry+8,8);
            auto size=bytes.integer(entry+16,8);
            auto name_off=bytes.integer(entry+24,4);
            auto name_length=bytes.integer(entry+28,2);
            require(name_off<=names_size &&
                    name_length<=names_size-name_off &&
                    offset<=payload_size && size<=payload_size-offset,
                    "Invalid YTD texture entry bounds");
            auto name=bytes.string(table+name_base+name_off,name_length);
            validate_name(name);
            require(unique.insert(name).second,"Duplicate YTD texture entry");
            output.push_back({std::move(name),size});
        }
    }
    require(output.size()==file.entries,"NEF8 ListFile enumeration incomplete");
    return output;
}
std::vector<DictionaryEntry> list_dictionary_entries(
    AssetStore const& assets,std::string const& path) {
    auto bytes=assets.read(path);
    return list_dictionary_entries(bytes,path);
}
} // namespace northstarcpp::shared_assets
