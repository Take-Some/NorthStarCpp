// ABI 3 keyboard/IME/wheel. Included in native.cpp private namespace.
void append_change(Context& ctx,NorthStarUIHandle target,const std::string& value) {
    Event event{};
    event.kind=NORTHSTAR_UI_EVENT_CHANGE;
    event.target=target;
    event.name="change";
    event.value.kind=NORTHSTAR_UI_VALUE_STRING;
    event.value.string=value;
    ctx.events.push_back(std::move(event));
}
bool edit_enabled(const Context& ctx) {
    auto it=ctx.nodes.find(ctx.focused);
    return it!=ctx.nodes.end()&&it->second.kind==NORTHSTAR_UI_INPUT;
}
int32_t keyboard(void* ptr,uint32_t key,uint32_t down,NorthStarUIString text) noexcept {
    return protect([&]() -> int32_t {
        if(!ptr||down>1||!valid(text))
            return fail(NORTHSTAR_UI_INVALID_ARGUMENT,"Invalid key event");
        auto& ctx=*static_cast<Context*>(ptr);
        if(ctx.pumping)return fail(NORTHSTAR_UI_INVALID_ARGUMENT,"Reentrant UI keyboard");
        if(!down||!edit_enabled(ctx))return NORTHSTAR_UI_OK;
        if(ctx.events.size()>=4096)
            return fail(NORTHSTAR_UI_BACKEND_FAILURE,"UI event queue full");
        auto& input=ctx.nodes.at(ctx.focused);
        auto& value=input.properties["text"];
        if(value.kind!=NORTHSTAR_UI_VALUE_STRING) {
            value=Value{};
            value.kind=NORTHSTAR_UI_VALUE_STRING;
        }
        bool changed=false;
        if(key==8) {
            if(!value.string.empty()) {
                // Remove one UTF-8 code point, not a byte.
                size_t last=value.string.size()-1;
                while(last>0&&
                    (static_cast<unsigned char>(value.string[last])&0xc0)==0x80)
                    --last;
                value.string.resize(last);
                changed=true;
            }
        } else if(text.length) {
            // text is provided by the host IME (not inferred from key codes).
            if(value.string.size()+text.length>1048576)
                return fail(NORTHSTAR_UI_INVALID_ARGUMENT,"Input text too long");
            value.string+=copy(text);
            changed=true;
        }
        if(changed)append_change(ctx,ctx.focused,value.string);
        return NORTHSTAR_UI_OK;
    });
}
int32_t composition(void* ptr,NorthStarUIString text,uint32_t commit) noexcept {
    return protect([&]() -> int32_t {
        if(!ptr||!valid(text)||commit>1)
            return fail(NORTHSTAR_UI_INVALID_ARGUMENT,"Invalid composition");
        auto& ctx=*static_cast<Context*>(ptr);
        if(ctx.pumping)return fail(NORTHSTAR_UI_INVALID_ARGUMENT,"Reentrant composition");
        if(!edit_enabled(ctx))return NORTHSTAR_UI_OK;
        if(!commit) {
            ctx.preedit=copy(text);
            return NORTHSTAR_UI_OK;
        }
        ctx.preedit.clear();
        // Commit is identical to inserting UTF-8 text via OS character input.
        return keyboard(ptr,0,1,text);
    });
}
int32_t wheel(void* ptr,float dx,float dy) noexcept {
    return protect([&]() -> int32_t {
        if(!ptr||!std::isfinite(dx)||!std::isfinite(dy))
            return fail(NORTHSTAR_UI_INVALID_ARGUMENT,"Invalid scroll delta");
        auto& ctx=*static_cast<Context*>(ptr);
        if(ctx.pumping)return fail(NORTHSTAR_UI_INVALID_ARGUMENT,"Reentrant scroll");
        uint64_t candidate=ctx.hovered;
        // Walk up to a panel/root so widgets scroll their containing surface.
        for(size_t depth=0;candidate&&depth<ctx.nodes.size();++depth) {
            auto it=ctx.nodes.find(candidate);
            if(it==ctx.nodes.end())break;
            if(it->second.kind==NORTHSTAR_UI_PANEL||
               it->second.kind==NORTHSTAR_UI_ROOT) {
                for(const auto& entry:std::initializer_list<std::pair<const char*,float>>{
                        {"scroll_x",dx},{"scroll_y",dy}}) {
                    auto& value=it->second.properties[entry.first];
                    double current=value.kind==NORTHSTAR_UI_VALUE_NUMBER?value.number:0.;
                    value.kind=NORTHSTAR_UI_VALUE_NUMBER;
                    value.number=std::clamp(current+entry.second,-16384.,16384.);
                }
                return NORTHSTAR_UI_OK;
            }
            candidate=it->second.parent;
        }
        return NORTHSTAR_UI_OK;
    });
}
int32_t focused(void* ptr,NorthStarUIHandle* out) noexcept {
    return protect([&]() -> int32_t {
        if(!ptr||!out)return fail(NORTHSTAR_UI_INVALID_ARGUMENT,"Invalid focus query");
        auto& ctx=*static_cast<Context*>(ptr);
        *out=edit_enabled(ctx)?ctx.focused:0;
        return NORTHSTAR_UI_OK;
    });
}
const NorthStarUIInputApi input_api{
    sizeof(NorthStarUIInputApi),0,keyboard,composition,wheel,focused
};
const NorthStarModule input_module{
    sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_UI,"native","1.2.0",
    NORTHSTARCPP_UI_INPUT_ABI,sizeof(input_api),&input_api,last_ui_error
};
