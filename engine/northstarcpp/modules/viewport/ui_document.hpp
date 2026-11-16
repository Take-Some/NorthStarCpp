// NorthStar engine.ui — game-agnostic declarative UI document.
// Content and semantics are fully owned by project scripts. No mission,
// health, advertising, quest, inventory or other gameplay concepts belong here.
#pragma once
#include <northstarcpp/ui_plugin.h>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace northstarcpp {
struct UiPropertyValue {
    std::uint32_t kind=NORTHSTAR_UI_VALUE_STRING;
    double number=0;
    std::uint32_t rgba=0;
    bool boolean=false;
    std::string text;
    bool operator==(UiPropertyValue const& other) const {
        return kind==other.kind && number==other.number &&
            rgba==other.rgba && boolean==other.boolean && text==other.text;
    }
};
struct UiDocumentNode {
    std::string id;
    std::string parent; // empty means the document root
    std::uint32_t kind=NORTHSTAR_UI_TEXT;
    std::vector<std::pair<std::string,UiPropertyValue>> properties;
    bool operator==(UiDocumentNode const& other) const {
        return id==other.id && parent==other.parent &&
            kind==other.kind && properties==other.properties;
    }
};
// UI events are provider-neutral: they reference script element IDs, not
// native pointer addresses or gameplay-specific callbacks.
struct UiInteractionEvent {
    std::string id;
    std::string name;
    std::uint32_t kind=0;
    std::uint32_t value_kind=0;
    double number=0;
    bool boolean=false;
    std::string text;
};
struct UiDocument {
    // "screen" uses the post-tonemap compositor. "world" is rendered to
    // RGBA8 and submitted as a UV-textured quad on the front of an entity.
    // Gameplay remains responsible for the content of every node.
    std::string target="screen";
    std::string entity_id;
    std::uint32_t width=512, height=256;
    float world_width=1.8f, world_height=1.f;
    std::vector<UiDocumentNode> nodes;
    bool operator==(UiDocument const& other) const {
        return target==other.target && entity_id==other.entity_id &&
            width==other.width && height==other.height &&
            world_width==other.world_width &&
            world_height==other.world_height && nodes==other.nodes;
    }
};
} // namespace northstarcpp
