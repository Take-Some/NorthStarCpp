// NEF8 ListFile virtual directory indexing for the editor.
// Reuses the validated envelope/decompression logic; never extracts files.
#include <northstarcpp/shared_assets.hpp>
#include <northstarcpp/assets.hpp>
#include "asset_list_file.hpp"
#include <set>
#include <stdexcept>

namespace northstarcpp::shared_assets {
std::vector<DictionaryEntry> list_dictionary_entries(
    std::vector<std::uint8_t> const& source,std::string const& path) {
    using namespace detail;
    auto const ext=extension(path);
    require(ext==".ytd" || ext==".ydd",
            "Only NEF8 YTD/YDD ListFiles are virtual dictionaries");
    auto file=parse_list_file(source); // size, deflate and hash checks
    std::vector<DictionaryEntry> output;
    output.reserve(file.entries);
    std::set<std::string> unique;
    if(ext==".ydd") {
        require(file.kind==2,"Expected YDD drawable dictionary");
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
