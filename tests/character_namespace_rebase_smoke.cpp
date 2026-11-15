// Pure character namespace rebasing: imported binary references remain intact.
#include "character_asset_namespace.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

int main() {
    using northstarcpp::detail::rebase_embedded_character_material;
    using northstarcpp::detail::rebase_embedded_character_texture;
    auto require=[](bool ok,char const* text){
        if(!ok)throw std::runtime_error(text);
    };
    try {
        require(rebase_embedded_character_material(
            "characters/abby/models/abby.ydd",
            "materials/character_abby.ymat@m00")==
            "characters/abby/materials/abby.ymat@m00",
            "Legacy YDD material was not rebound");
        require(rebase_embedded_character_texture(
            "characters/abby/materials/abby.ymat@m00",
            "textures/characters/abby.ytd@body_color")==
            "characters/abby/textures/abby.ytd@body_color",
            "Legacy YMAT texture was not rebound");
        require(rebase_embedded_character_material(
            "characters/abby/models/abby.ydd",
            "materials/character_v.ymat@m00")==
            "materials/character_v.ymat@m00",
            "Unrelated YDD material was silently changed");
        require(rebase_embedded_character_texture(
            "characters/abby/materials/abby.ymat@m00",
            "textures/characters/other.ytd@a")==
            "textures/characters/other.ytd@a",
            "Unrelated YMAT texture was silently changed");
        require(rebase_embedded_character_material(
            "characters/../models/abby.ydd",
            "materials/character_abby.ymat@m00")==
            "materials/character_abby.ymat@m00",
            "Unsafe character path was accepted");
        require(rebase_embedded_character_material(
            "models/abby/abby.ydd",
            "materials/character_abby.ymat@m00")==
            "materials/character_abby.ymat@m00",
            "Existing non-namespaced sources were rewritten");
        require(rebase_embedded_character_material(
            "characters/abby/models/abby.ymt",
            "materials/character_abby.ymat@m00")==
            "materials/character_abby.ymat@m00",
            "Skeleton source treated as YDD");
        std::cout<<"Character asset namespace mapping: PASS\n";
        return 0;
    }catch(std::exception const& error) {
        std::cerr<<"Character asset namespace mapping: FAIL: "
                 <<error.what()<<"\n";
        return 1;
    }
}
