# NorthStar UI XML v3 — format and implementation

Current optional components, declarative actions, responsive breakpoints and
animation timeline extensions are documented in
`docs/NUI_COMPONENTS_ACTIONS_RESPONSIVE_ANIMATION_2026-11-17.md`.

Date: 17 November 2026. Status: source migration completed; release/GPU QA pending.

## The single authored UI format

Every persisted .nui file is UTF-8 XML. Legacy JSON-formatted .nui assets
are no longer accepted by Player or the Qt Designer.

    <?xml version="1.0" encoding="UTF-8"?>
    <NorthStarUI version="3" target="screen" layer="mainmenu">
      <Tokens>
        <Token name="accent" type="color" value="#ffcb64ff"/>
      </Tokens>
      <Styles>
        <Style name="action">
          <Property name="width" type="number" value="320"/>
          <Property name="height" type="number" value="56"/>
          <Property name="color" token="accent"/>
        </Style>
      </Styles>
      <Elements>
        <Panel id="menu">
          <Property name="width" type="number" value="420"/>
          <Property name="anchor_center_x" type="bool" value="true"/>
          <Property name="anchor_center_y" type="bool" value="true"/>
        </Panel>
        <Button id="start" parent="menu" style="action">
          <Property name="text" type="string" value="START GAME"/>
        </Button>
      </Elements>
    </NorthStarUI>

All properties carry explicit types: string, bool, number, color (#RRGGBBAA),
or a typed token reference. Strings "0012" and "true" never silently
convert to numbers/booleans. Named styles apply first, local properties
override them. UI elements have stable IDs and parent relationships.

The asset does not execute code. Project scripts bind actions and data
to the named elements with UIRoot.onClick/onPress/setText.

## Structure and limits

Root: NorthStarUI version=3, target=screen/world, optional layer/entityId,
width/height/worldWidth/worldHeight.
Sections: Tokens (0..128), Styles (0..128), Elements (0..256).
Widgets: Panel, Text, Button, Image, Input.
Properties: up to 48 per style or widget; text values up to 2048 UTF-8
bytes; finite numeric magnitude at most 1,000,000.
Document byte limit: 262,144; identifier pattern:
[A-Za-z][A-Za-z0-9_.-]{0,63}.

Duplicate identifiers, unknown styles/tokens, invalid parent order,
unsupported XML attributes, DTD/entity declarations, invalid property
types and unsafe VFS paths are rejected.

Full schema for IDE completion / external validation:
schemas/northstar-ui-v3.xsd.

## Implementation contract

Disk/UI authoring format: XML v3.
C++ runtime reads and validates XML via modules/shared/ui_xml_codec.hpp.
The V8-facing bridge delivers an in-memory normalized descriptor to the
existing JavaScript UI builder. It is not a JSON file, not an XML
sidecar, and not a second persisted asset representation.

Standalone FPS uses window_fps_nui.hpp to preserve the original
explicit XML types when creating plugin-neutral UiDocument values.
All rendering and events continue through the existing engine.ui
provider and the D3D12/Vulkan UI compositor.

The Qt UI Designer uses qt_nui_xml_codec.hpp for loading, serialization
and round-trip validation. Undo/redo, native preview, dragging, snapping,
properties and tokens/styles work on an in-memory editable model.
The Design System text editor now displays XML, not JSON.
New UI Document in the Qt editor and Project Manager both produce XML v3.

Project VFS: project/assets/ui overrides sharedAssets/ui, and NSP
packages retain the XML files. The project manifest is still JSON
(project metadata, not a .nui file) with ui.default and
ui.format = northstar.ui/xml/3.

Live development UI hot reload keeps stable event handlers and runtime
text overrides, and retains last-good UI on parse failures.

## World-space UI

A shared, editable world-surface sample is provided at
sharedAssets/ui/world_terminal.nui. It declares target=world, texture
resolution, physical dimensions, typed style tokens and a button. Gameplay
attaches it to an Entity using UI.loadAsset with options.entityId. It uses
the same schema and provider as screen-based menus.

## Migration

36 UI assets across sharedAssets, the embedded resource mirror,
three projects and the Abby demo template were migrated to XML v3.

For additional legacy assets run:
    python tools/dev/nui_xml.py migrate path/to/screen.nui

To verify a file:
    python tools/dev/nui_xml.py check sharedAssets/ui/main_menu.nui

To validate the XSD schema on Windows:
    powershell -NoProfile -ExecutionPolicy Bypass -File tools/dev/nui_schema_smoke.ps1

Registered / standalone test coverage includes native XML compiler,
FPS native XML, Qt XML roundtrip, XML schema, UI startup and runtime
hot reload, UI event routing, Player/Camera, Abby and First Person Protocol.

## Remaining work

No installed production Player/editor was replaced. Full clean Release
build, GPU screenshots in D3D12/Vulkan, interactive window testing, font
and DPI acceptance and installed NSP verification remain required.
The canvas currently supports dragging, not resizing, animation timelines
or responsive breakpoint simulation. The low-level engine.ui provider
ABI remains stable and internal rather than an authored UI format.
