#!/usr/bin/env python3
"""Replace mission-specific C++ script bindings with one generic UI.document gateway."""
from pathlib import Path
R=Path(__file__).resolve().parents[1]
p=R/"engine/northstarcpp/modules/editor/window.cpp"
s=p.read_text(encoding="utf-8-sig")
marker='std::size_t script_index(std::string const& value) {'
assert s.count(marker)==1
parser=r'''// A single game-agnostic UI entrypoint. All node IDs, text, colors, and
// behavior are supplied by the script; native code only validates structure.
UiDocument parse_script_ui_document(std::string_view args_json) {
    if(args_json.empty() || args_json.size()>262144 ||
       args_json.front()!='[')
        throw std::invalid_argument("ui.apply expects one bounded document");
    boost::property_tree::ptree args;
    std::istringstream stream{std::string(args_json)};
    boost::property_tree::read_json(stream,args);
    if(args.size()!=1 || !args.begin()->first.empty())
        throw std::invalid_argument("ui.apply(document) expects one argument");
    auto const& document=args.begin()->second;
    auto const target=document.get<std::string>("target","screen");
    if(target!="screen")
        throw std::runtime_error(
            "World-space UI requires a render material/texture target provider");
    auto nodes=document.get_child_optional("nodes");
    if(!nodes)throw std::invalid_argument("UI document needs nodes array");
    if(nodes->size()>256)
        throw std::length_error("UI document exceeds 256 elements");
    UiDocument result;
    result.nodes.reserve(nodes->size());
    std::unordered_set<std::string> declared{""};
    auto identifier=[](std::string const& key) {
        return !key.empty() && key.size()<=96 &&
            std::all_of(key.begin(),key.end(),[](unsigned char c){
                return (c>='A'&&c<='Z') || (c>='a'&&c<='z') ||
                    (c>='0'&&c<='9') || c=='_' || c=='-' || c=='.';
            });
    };
    for(auto const& entry:*nodes) {
        if(!entry.first.empty())
            throw std::invalid_argument("UI nodes must be an array");
        auto const& item=entry.second;
        UiDocumentNode node;
        node.id=item.get<std::string>("id");
        node.parent=item.get<std::string>("parent","");
        auto kind=item.get<std::string>("type");
        if(!identifier(node.id) || !declared.insert(node.id).second ||
           !declared.count(node.parent))
            throw std::invalid_argument("UI node ID or parent is invalid");
        if(kind=="panel")node.kind=NORTHSTAR_UI_PANEL;
        else if(kind=="text")node.kind=NORTHSTAR_UI_TEXT;
        else if(kind=="button")node.kind=NORTHSTAR_UI_BUTTON;
        else if(kind=="image")node.kind=NORTHSTAR_UI_IMAGE;
        else if(kind=="input")node.kind=NORTHSTAR_UI_INPUT;
        else throw std::invalid_argument("Unsupported UI node type");
        if(auto props=item.get_child_optional("props")) {
            if(props->size()>48)
                throw std::length_error("UI node has too many properties");
            for(auto const& prop:*props) {
                if(!identifier(prop.first) || prop.first.size()>64)
                    throw std::invalid_argument("Invalid UI property name");
                UiPropertyValue value;
                auto const& typed=prop.second;
                auto const type=typed.get<std::string>("type");
                auto const raw=typed.get<std::string>("value");
                if(type=="text") {
                    if(raw.size()>2048)
                        throw std::length_error("UI text exceeds 2048 bytes");
                    value.kind=NORTHSTAR_UI_VALUE_STRING;
                    value.text=raw;
                } else if(type=="number") {
                    std::size_t pos=0;
                    value.number=std::stod(raw,&pos);
                    if(pos!=raw.size() || !std::isfinite(value.number) ||
                       std::abs(value.number)>1e6)
                        throw std::invalid_argument("Invalid UI numeric property");
                    value.kind=NORTHSTAR_UI_VALUE_NUMBER;
                } else if(type=="bool") {
                    if(raw!="true" && raw!="false")
                        throw std::invalid_argument("Invalid UI boolean property");
                    value.kind=NORTHSTAR_UI_VALUE_BOOL;
                    value.boolean=raw=="true";
                } else if(type=="color") {
                    if(raw.size()!=9 || raw[0]!='#' ||
                       !std::all_of(raw.begin()+1,raw.end(),
                           [](unsigned char c){return std::isxdigit(c)!=0;}))
                        throw std::invalid_argument("UI color needs #RRGGBBAA");
                    value.kind=NORTHSTAR_UI_VALUE_COLOR_RGBA8;
                    value.rgba=static_cast<std::uint32_t>(
                        std::stoul(raw.substr(1),nullptr,16));
                } else throw std::invalid_argument("Invalid UI property type");
                node.properties.emplace_back(prop.first,std::move(value));
            }
        }
        result.nodes.push_back(std::move(node));
    }
    return result;
}
'''
s=s.replace(marker,parser+marker,1)
a=s.index('        runtime.bind("engine.ui","setMission"')
b=s.index('        runtime.bind("engine.session","quit"',a)
s=s[:a]+'''        runtime.bind("engine.ui","apply",[this](std::string_view json) {
            // Script presents a complete, generic UI document.
            // Rendering is deferred until the GPU viewport is ready.
            auto next=parse_script_ui_document(json);
            script_ui_document_=std::move(next);
            if(graphics)graphics->set_ui_document(script_ui_document_);
            return std::string("null");
        });
'''+s[b:]
s=s.replace('''                if(script_mission_hud.visible)
                    graphics->set_mission_hud(script_mission_hud);''',
            '''                if(!script_ui_document_.nodes.empty())
                    graphics->set_ui_document(script_ui_document_);''')
s=s.replace('    ViewportMissionHud script_mission_hud{};',
            '    UiDocument script_ui_document_{};')
assert "script_mission_hud" not in s and "ViewportMissionHud" not in s
p.write_text(s,encoding="utf-8",newline="")
print("Win32 Player now accepts only engine.ui.apply(document).")
