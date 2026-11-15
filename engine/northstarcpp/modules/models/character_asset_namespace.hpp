// Imported character resource namespace rebasing.
// Imported YDD and YMAT bytes may embed old VFS references. Their contents
// remain immutable; a resource under characters/<id>/... is resolved to its
// namespaced source with a strictly matched importer alias.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace northstarcpp::detail {
inline bool valid_character_id(std::string_view id) noexcept {
    if(id.empty() || id.size()>72)return false;
    for(char c:id) {
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
             (c>='0'&&c<='9')||c=='-'||c=='_'))
            return false;
    }
    return true;
}

// Valid only for the canonical bundle's own model or material file.
inline std::optional<std::string> character_id_from_source(
    std::string const& reference,std::string_view role,
    std::string_view extension) {
    constexpr std::string_view prefix="characters/";
    if(reference.compare(0,prefix.size(),prefix)!=0)return {};
    auto const separator=reference.find('/',prefix.size());
    if(separator==std::string::npos)return {};
    auto const id=reference.substr(prefix.size(),separator-prefix.size());
    if(!valid_character_id(id))return {};
    std::string expected{role};
    expected.push_back('/');
    expected+=id;
    expected+=extension;
    if(reference.substr(separator+1)!=expected)return {};
    return id;
}

inline std::string rebase_embedded_character_material(
    std::string const& model_reference,std::string const& embedded_reference) {
    auto const id=character_id_from_source(model_reference,"models",".ydd");
    if(!id)return embedded_reference;
    auto const separator=embedded_reference.find('@');
    if(separator==std::string::npos || separator==embedded_reference.size()-1)
        return embedded_reference;
    auto const old="materials/character_"+*id+".ymat";
    if(embedded_reference.compare(0,separator,old)!=0)
        return embedded_reference;
    return "characters/"+*id+"/materials/"+*id+".ymat"+
           embedded_reference.substr(separator);
}

inline std::string rebase_embedded_character_texture(
    std::string const& material_reference,std::string const& embedded_reference) {
    auto source=material_reference.substr(0,material_reference.find('@'));
    auto const id=character_id_from_source(source,"materials",".ymat");
    if(!id)return embedded_reference;
    auto const separator=embedded_reference.find('@');
    std::string const old="textures/characters/"+*id+".ytd";
    auto const path=embedded_reference.substr(0,separator);
    if(path!=old)return embedded_reference;
    return "characters/"+*id+"/textures/"+*id+".ytd"+
           (separator==std::string::npos?
             std::string{}:embedded_reference.substr(separator));
}
} // namespace northstarcpp::detail
