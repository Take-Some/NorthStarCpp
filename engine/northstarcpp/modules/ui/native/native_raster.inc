// Included in native.cpp, inside its private namespace.
// ABI 2 software surface. This deterministic reference backend is a CPU
// fallback and test oracle; renderer upload is deliberately owned by host.
struct Rect { int x=0,y=0,w=0,h=0; };
bool inside(Rect r,int x,int y) {
    return x>=r.x && y>=r.y && x<r.x+r.w && y<r.y+r.h;
}
Rect intersect(Rect a,Rect b) {
    int x=std::max(a.x,b.x),y=std::max(a.y,b.y);
    return {x,y,std::max(0,std::min(a.x+a.w,b.x+b.w)-x),
            std::max(0,std::min(a.y+a.h,b.y+b.h)-y)};
}
double prop(const Node& n,const char* key,double fallback) {
    auto it=n.properties.find(key);
    if(it==n.properties.end()||it->second.kind!=NORTHSTAR_UI_VALUE_NUMBER)
        return fallback;
    return it->second.number;
}
uint32_t color(const Node& n,const char* key,uint32_t fallback) {
    auto it=n.properties.find(key);
    if(it==n.properties.end()||it->second.kind!=NORTHSTAR_UI_VALUE_COLOR_RGBA8)
        return fallback;
    return it->second.rgba8;
}
std::string label(const Node& n) {
    auto it=n.properties.find("text");
    if(it!=n.properties.end()&&it->second.kind==NORTHSTAR_UI_VALUE_STRING)
        return it->second.string;
    return n.kind==NORTHSTAR_UI_TEXT?n.name:std::string{};
}
int dimension(double d,int fallback) {
    if(!std::isfinite(d))return fallback;
    return std::clamp(static_cast<int>(std::round(d)),0,16384);
}
using RectMap=std::unordered_map<uint64_t,Rect>;
// Layout V2: deterministic row/column/grid flow, per-parent scroll offsets,
// anchors, percentage dimensions, min/max constraints, nested clipping.
RectMap geometry(Context& context) {
    RectMap boxes;
    std::vector<uint64_t> order;
    order.reserve(context.nodes.size());
    std::unordered_map<uint64_t,std::vector<uint64_t>> children;
    for(const auto& pair:context.nodes) {
        order.push_back(pair.first);
        children[pair.second.parent].push_back(pair.first);
    }
    std::sort(order.begin(),order.end());
    for(auto& item:children)std::sort(item.second.begin(),item.second.end());
    const Rect screen{0,0,static_cast<int>(context.width),
                         static_cast<int>(context.height)};
    // Node IDs are monotonically allocated; parents necessarily precede children.
    for(uint64_t id:order) {
        const Node& node=context.nodes.at(id);
        if(node.kind==NORTHSTAR_UI_ROOT) {
            boxes[id]=screen;
            continue;
        }
        auto parentIt=boxes.find(node.parent);
        if(parentIt==boxes.end())continue;
        const Rect parent=parentIt->second;
        const Node& container=context.nodes.at(node.parent);
        int padding=dimension(prop(container,"padding",8),8);
        int gap=dimension(prop(container,"gap",6),6);
        int innerW=std::max(0,parent.w-padding*2);
        int innerH=std::max(0,parent.h-padding*2);
        const auto& siblings=children.at(node.parent);
        auto sibling=std::lower_bound(siblings.begin(),siblings.end(),id);
        int index=static_cast<int>(sibling-siblings.begin());
        auto layoutProperty=container.properties.find("layout");
        const std::string layout=layoutProperty!=container.properties.end()&&
            layoutProperty->second.kind==NORTHSTAR_UI_VALUE_STRING?
            layoutProperty->second.string:"column";
        int columns=std::max(1,dimension(prop(container,"columns",2),2));
        int col=layout=="grid"?index%columns:index;
        int row=layout=="grid"?index/columns:index;
        int gridWidth=std::max(0,(innerW-gap*(columns-1))/columns);
        int baseW=layout=="grid"?gridWidth:innerW;
        int naturalH=node.kind==NORTHSTAR_UI_PANEL?130:34;
        int width=dimension(prop(node,"width",baseW),baseW);
        int height=dimension(prop(node,"height",naturalH),naturalH);
        if(double percent=prop(node,"width_percent",-1);percent>=0)
            width=dimension(baseW*std::min(percent,100.0)/100.0,width);
        if(double percent=prop(node,"height_percent",-1);percent>=0)
            height=dimension(innerH*std::min(percent,100.0)/100.0,height);
        width=std::clamp(width,dimension(prop(node,"min_width",0),0),
                        std::max(dimension(prop(node,"min_width",0),0),
                                 dimension(prop(node,"max_width",16384),16384)));
        height=std::clamp(height,dimension(prop(node,"min_height",0),0),
                         std::max(dimension(prop(node,"min_height",0),0),
                                  dimension(prop(node,"max_height",16384),16384)));
        int x=parent.x+padding,y=parent.y+padding;
        if(layout=="row") {
            for(int i=0;i<index;++i) {
                const Node& previous=context.nodes.at(siblings[i]);
                x+=dimension(prop(previous,"width",80),80)+gap;
            }
        } else if(layout=="grid") {
            x+=col*(gridWidth+gap);
            // Deterministic row height, overridable by parent grid_row_height.
            int rowHeight=dimension(prop(container,"grid_row_height",naturalH),naturalH);
            y+=row*(rowHeight+gap);
        } else {
            for(int i=0;i<index;++i) {
                const Node& previous=context.nodes.at(siblings[i]);
                auto const position=previous.properties.find("position");
                if(position!=previous.properties.end() &&
                   position->second.kind==NORTHSTAR_UI_VALUE_STRING &&
                   position->second.string=="absolute")continue;
                int previousDefault=previous.kind==NORTHSTAR_UI_PANEL?130:34;
                y+=dimension(prop(previous,"height",previousDefault),previousDefault)+gap;
            }
        }
        auto booleanProperty=[&](const char* key)->bool {
            auto it=node.properties.find(key);
            return it!=node.properties.end() && it->second.kind==NORTHSTAR_UI_VALUE_BOOL
                && it->second.boolean;
        };
        if(booleanProperty("anchor_right"))x=parent.x+parent.w-padding-width;
        if(booleanProperty("anchor_bottom"))y=parent.y+parent.h-padding-height;
        // Resolution-independent UI/2 design: center anchors and percentage
        // placement are computed from the actual parent rectangle.
        if(booleanProperty("anchor_center_x"))
            x=parent.x+(parent.w-width)/2;
        if(booleanProperty("anchor_center_y"))
            y=parent.y+(parent.h-height)/2;
        if(double percentage=prop(node,"x_percent",-1.);percentage>=0.)
            x=parent.x+dimension(parent.w*std::min(percentage,100.)/100.,0);
        if(double percentage=prop(node,"y_percent",-1.);percentage>=0.)
            y=parent.y+dimension(parent.h*std::min(percentage,100.)/100.,0);
        x+=static_cast<int>(prop(node,"x",0));
        y+=static_cast<int>(prop(node,"y",0));
        x-=static_cast<int>(prop(container,"scroll_x",0));
        y-=static_cast<int>(prop(container,"scroll_y",0));
        boxes[id]=intersect({x,y,width,height},parent);
    }
    return boxes;
}
void paint_rect(uint8_t* pixels,uint32_t stride,Rect bounds,Rect rect,uint32_t rgba) {
    rect=intersect(bounds,rect);
    if(!rect.w||!rect.h)return;
    int r=(rgba>>24)&255,g=(rgba>>16)&255,b=(rgba>>8)&255,a=rgba&255;
    if(!a)return;
    for(int y=rect.y;y<rect.y+rect.h;++y) {
        uint8_t* row=pixels+static_cast<size_t>(y)*stride+static_cast<size_t>(rect.x)*4;
        for(int x=0;x<rect.w;++x,row+=4) {
            row[0]=static_cast<uint8_t>((r*a+row[0]*(255-a)+127)/255);
            row[1]=static_cast<uint8_t>((g*a+row[1]*(255-a)+127)/255);
            row[2]=static_cast<uint8_t>((b*a+row[2]*(255-a)+127)/255);
            row[3]=static_cast<uint8_t>(a+(row[3]*(255-a)+127)/255);
        }
    }
}
// Project-selectable procedural button finish. The CPU reference raster
// produces the UI texture consumed by the D3D12/Vulkan GPU compositor.
// No external image IO, allocations, or per-frame shader compilation.
bool ui_flag(const Node& node,const char* key) {
    auto it=node.properties.find(key);
    return it!=node.properties.end() &&
        it->second.kind==NORTHSTAR_UI_VALUE_BOOL && it->second.boolean;
}
bool ui_neon_style(const Node& node) {
    auto it=node.properties.find("style");
    return it!=node.properties.end() &&
        it->second.kind==NORTHSTAR_UI_VALUE_STRING &&
        (it->second.string=="neon" || it->second.string=="cyberpunk");
}
uint32_t ui_tint(uint32_t rgba,int amount) {
    auto channel=[&](int shift) {
        return static_cast<uint32_t>(std::clamp(
            int((rgba>>shift)&255)+amount,0,255));
    };
    return (channel(24)<<24)|(channel(16)<<16)|
           (channel(8)<<8)|(rgba&255);
}
// Cyberpunk-style chamfered polygon with a recessed face and extruded base.
// Deliberately opt-in: the legacy "neon" material alias remains supported.
void paint_neon_button(uint8_t* pixels,uint32_t stride,Rect screen,
                       Rect rect,uint32_t fill,bool hovered,bool pressed,
                       bool primary) {
    if(rect.w<=0||rect.h<=0)return;
    const int cut=std::clamp(std::min(rect.w/15,rect.h/4),2,15);
    const int lowerCut=std::max(2,cut-3);
    const uint32_t light=primary?0xffcf55ff:0x61e0f7ff;
    const uint32_t faceEdge=primary?0xd6aa4aff:0x519eb8ff;
    const uint32_t topLine=primary?0xffe49aff:0xb0f7ffff;
    // One polygon is used for every row, including the beveled corners.
    auto shape=[&](int y,int inset=0) {
        const int topRight=std::max(0,cut-y);
        const int bottomLeft=std::max(0,y-(rect.h-lowerCut-1));
        const int left=std::min(rect.w,std::max(0,bottomLeft+inset));
        const int right=std::max(left,rect.w-topRight-inset);
        return std::pair<int,int>{left,right};
    };
    // An offset, dark metal backing gives the button visible thickness.
    // It stays inside the authored button bounds, without spilling into
    // adjacent focus targets or clipping against parent containers.
    for(int y=2;y<rect.h;++y) {
        auto [left,right]=shape(y-2,1);
        if(right>left)
            paint_rect(pixels,stride,screen,
                {rect.x+left,rect.y+y,right-left,1},0x040912e8);
    }
    for(int y=0;y<rect.h-3;++y) {
        auto [left,right]=shape(y);
        if(right<=left)continue;
        const int grad=20-y*30/std::max(1,rect.h);
        const int grain=(y%4==0?-4:(y%3==0?3:0));
        const int lightBoost=(hovered?12:0)-(pressed?24:0);
        const auto alloy=ui_tint(fill,grad+grain+lightBoost);
        const int yy=rect.y+y+(pressed?1:0);
        // Intrinsic top and bottom steel bevels, raised inset surface.
        paint_rect(pixels,stride,screen,{rect.x+left,yy,right-left,1},
                   y<=1?faceEdge:(y>=rect.h-7?0x06101dfc:alloy));
        if(y>=2 && y<rect.h-8 && right-left>8) {
            paint_rect(pixels,stride,screen,
                {rect.x+left+2,yy,right-left-4,1},
                ui_tint(alloy,y<7?8:-1));
        }
        // Accurate sloped left/right edges; highlights follow the chamfers.
        paint_rect(pixels,stride,screen,{rect.x+left,yy,1,1},
                   (y>=rect.h-lowerCut-1)?topLine:faceEdge);
        paint_rect(pixels,stride,screen,{rect.x+right-1,yy,1,1},
                   (y<cut)?topLine:faceEdge);
        if(y==3 && right-left>12)
            paint_rect(pixels,stride,screen,
                {rect.x+left+3,yy,right-left-6,1},topLine);
        if(y==rect.h-9 && right-left>12)
            paint_rect(pixels,stride,screen,
                {rect.x+left+3,yy,right-left-6,1},0x020b18ff);
        // Micro-etched periodic trace lines inside the visible polygon.
        if(y>6 && y<rect.h-12 && y%5==0 && right-left>55)
            paint_rect(pixels,stride,screen,
                {rect.x+left+23,yy,right-left-45,1},
                primary?0xf3c75e0c:0x8cddf011);
    }
    // Functional cyan/gold side electrode and reference notches.
    if(rect.w>68 && rect.h>26) {
        paint_rect(pixels,stride,screen,
            {rect.x+9,rect.y+10,3,std::max(1,rect.h-23)},light);
        paint_rect(pixels,stride,screen,
            {rect.x+16,rect.y+9,16,2},primary?0xfbd279a4:0x7aeefab8);
        paint_rect(pixels,stride,screen,
            {rect.x+rect.w-48,rect.y+rect.h-14,25,2},
            primary?0xfbd279b8:0x72deefc8);
        paint_rect(pixels,stride,screen,
            {rect.x+rect.w-22,rect.y+rect.h-15,4,4},light);
        if(hovered)
            paint_rect(pixels,stride,screen,
                {rect.x+16,rect.y+12,9,2},light);
    }
}
// An opt-in brushed-steel finish for menu containers.
bool ui_alloy_style(const Node& n) {
    auto it=n.properties.find("style");
    return it!=n.properties.end() &&
        it->second.kind==NORTHSTAR_UI_VALUE_STRING &&
        it->second.string=="alloy";
}
void paint_alloy_panel(uint8_t* pixels,uint32_t stride,Rect screen,
                       Rect rect,uint32_t base) {
    if(rect.w<=0||rect.h<=0)return;
    for(int y=0;y<rect.h;++y)
        paint_rect(pixels,stride,screen,{rect.x,rect.y+y,rect.w,1},
                   ui_tint(base,8-y*22/std::max(rect.h,1)+(y%7==0?2:0)));
    paint_rect(pixels,stride,screen,{rect.x,rect.y,rect.w,2},0xf1c46bff);
    paint_rect(pixels,stride,screen,{rect.x,rect.y,2,rect.h},0x36556aaa);
    paint_rect(pixels,stride,screen,
        {rect.x+rect.w-2,rect.y,2,rect.h},0x274356aa);
}
// Builtin compact 5x7 Latin glyphs. Other UTF-8 glyphs render a visible
// replacement box until a font shaping/rasterization provider is introduced.
const uint8_t* glyph(unsigned char c) {
    static const uint8_t font[][7]={
      {14,17,17,31,17,17,17}, // A
      {30,17,17,30,17,17,30}, // B
      {14,17,16,16,16,17,14}, // C
      {30,17,17,17,17,17,30}, // D
      {31,16,16,30,16,16,31}, // E
      {31,16,16,30,16,16,16}, // F
      {14,17,16,23,17,17,14}, // G
      {17,17,17,31,17,17,17}, // H
      {31,4,4,4,4,4,31}, // I
      {7,2,2,2,18,18,12}, // J
      {17,18,20,24,20,18,17}, // K
      {16,16,16,16,16,16,31}, // L
      {17,27,21,21,17,17,17}, // M
      {17,25,21,19,17,17,17}, // N
      {14,17,17,17,17,17,14}, // O
      {30,17,17,30,16,16,16}, // P
      {14,17,17,17,21,18,13}, // Q
      {30,17,17,30,20,18,17}, // R
      {15,16,16,14,1,1,30}, // S
      {31,4,4,4,4,4,4}, // T
      {17,17,17,17,17,17,14}, // U
      {17,17,17,17,17,10,4}, // V
      {17,17,17,21,21,21,10}, // W
      {17,17,10,4,10,17,17}, // X
      {17,17,10,4,4,4,4}, // Y
      {31,1,2,4,8,16,31}, // Z
      {14,17,19,21,25,17,14}, // 0
      {4,12,4,4,4,4,14}, // 1
      {14,17,1,2,4,8,31}, // 2
      {30,1,1,14,1,1,30}, // 3
      {2,6,10,18,31,2,2}, // 4
      {31,16,30,1,1,17,14}, // 5
      {6,8,16,30,17,17,14}, // 6
      {31,1,2,4,8,8,8}, // 7
      {14,17,17,14,17,17,14}, // 8
      {14,17,17,15,1,2,12}  // 9
    };
    static const uint8_t space[7]={0,0,0,0,0,0,0};
    static const uint8_t dot[7]={0,0,0,0,0,4,4};
    static const uint8_t minus[7]={0,0,0,31,0,0,0};
    static const uint8_t question[7]={14,17,1,2,4,0,4};
    if(c>='a'&&c<='z')c=static_cast<unsigned char>(c-32);
    if(c>='A'&&c<='Z')return font[c-'A'];
    if(c>='0'&&c<='9')return font[26+c-'0'];
    if(c==' ')return space;
    if(c=='.')return dot;
    if(c=='-')return minus;
    return question;
}
#include "native_font.inc"
// Decode a Unicode scalar, once per code point (not once per UTF-8 byte).
// Invalid sequences consume one byte and return U+FFFD.
uint32_t decode_utf8(const std::string& value,size_t& offset) {
    const auto first=static_cast<unsigned char>(value[offset++]);
    if(first<0x80)return first;
    int extra=first>=0xc2&&first<=0xdf?1:
              first>=0xe0&&first<=0xef?2:
              first>=0xf0&&first<=0xf4?3:0;
    if(!extra || offset+extra>value.size())return 0xfffd;
    uint32_t scalar=first & (extra==1?0x1f:extra==2?0x0f:0x07);
    for(int n=0;n<extra;++n) {
        const auto c=static_cast<unsigned char>(value[offset+n]);
        if((c&0xc0)!=0x80)return 0xfffd;
        scalar=(scalar<<6)|(c&0x3f);
    }
    if((extra==1 && scalar<0x80) || (extra==2 && scalar<0x800) ||
       (extra==3 && scalar<0x10000) || (scalar>=0xd800&&scalar<=0xdfff) ||
       scalar>0x10ffff)return 0xfffd;
    offset+=extra;
    return scalar;
}
void paint_text(uint8_t* pixels,uint32_t stride,Rect screen,Rect bounds,
                const std::string& text,uint32_t rgba,int scale) {
    scale=std::clamp(scale,1,5);
    if(paint_shaped_text(pixels,stride,screen,bounds,text,rgba,scale))
        return;
    int x=bounds.x+8,y=bounds.y+std::max(0,(bounds.h-7*scale)/2);
    for(size_t offset=0;offset<text.size();) {
        uint32_t codepoint=decode_utf8(text,offset);
        if(x+6*scale>bounds.x+bounds.w)break;
        // A full Unicode font requires FreeType/HarfBuzz (future text adapter).
        const uint8_t* rows=glyph(codepoint<0x80?
            static_cast<unsigned char>(codepoint):static_cast<unsigned char>('?'));
        for(int gy=0;gy<7;++gy)
            for(int gx=0;gx<5;++gx)
                if(rows[gy]&(1<<(4-gx)))
                    paint_rect(pixels,stride,screen,
                        intersect({x+gx*scale,y+gy*scale,scale,scale},bounds),rgba);
        x+=6*scale;
    }
}
int32_t draw_rgba_filtered(void* ptr,uint8_t* pixels,uint32_t stride,
                  uint32_t width,uint32_t height,
                  NorthStarUIHandle target,uint32_t mode) noexcept {
    return protect([&]() -> int32_t {
        if(!ptr||!pixels||!width||!height||width>8192||height>8192||
           stride<static_cast<uint64_t>(width)*4||
           static_cast<uint64_t>(stride)*height>512ull*1024ull*1024ull)
            return fail(NORTHSTAR_UI_INVALID_ARGUMENT,"Invalid RGBA surface");
        auto& ctx=*static_cast<Context*>(ptr);
        if(ctx.pumping||ctx.width!=width||ctx.height!=height)
            return fail(NORTHSTAR_UI_INVALID_ARGUMENT,"UI surface/context mismatch");
        if(mode>2 || (mode!=0 && (!target || !ctx.nodes.count(target))))
            return fail(NORTHSTAR_UI_INVALID_ARGUMENT,"Invalid UI raster filter");
        for(uint32_t y=0;y<height;++y)
            std::memset(pixels+static_cast<size_t>(stride)*y,0,width*4);
        const Rect screen{0,0,static_cast<int>(width),static_cast<int>(height)};
        auto boxes=geometry(ctx);
        std::vector<uint64_t> ids;
        for(auto const& kv:boxes)ids.push_back(kv.first);
        std::sort(ids.begin(),ids.end());
        for(uint64_t id:ids) {
            if((mode==1 && id==target)||(mode==2 && id!=target))
                continue;
            auto const& node=ctx.nodes.at(id);
            Rect rect=boxes.at(id);
            uint32_t background=0;
            if(node.kind==NORTHSTAR_UI_PANEL)background=0x222936ee;
            if(node.kind==NORTHSTAR_UI_BUTTON)background=0x3b5369ff;
            if(node.kind==NORTHSTAR_UI_INPUT)background=0x1e2631ff;
            background=color(node,"background",background);
            if(node.kind==NORTHSTAR_UI_BUTTON && ui_neon_style(node))
                paint_neon_button(pixels,stride,screen,rect,background,
                    ctx.hovered==id,ctx.pressed==id,ui_flag(node,"primary"));
            else if(node.kind==NORTHSTAR_UI_PANEL && ui_alloy_style(node))
                paint_alloy_panel(pixels,stride,screen,rect,background);
            else
                paint_rect(pixels,stride,screen,rect,background);
            const auto text=label(node);
            if(!text.empty()) {
                const uint32_t ink=color(node,"color",0xeef3ffff);
                const int scale=dimension(prop(node,"font_scale",2),2);
                Rect textBounds=rect;
                if(node.kind==NORTHSTAR_UI_BUTTON && ui_neon_style(node) && rect.w>32) {
                    textBounds.x+=14;
                    textBounds.w-=14;
                }
                paint_text(pixels,stride,screen,textBounds,text,ink,scale);
            }
        }
        return NORTHSTAR_UI_OK;
    });
}
int32_t draw_rgba(void* ptr,uint8_t* pixels,uint32_t stride,
                  uint32_t width,uint32_t height) noexcept {
    return draw_rgba_filtered(ptr,pixels,stride,width,height,0,0);
}
int32_t get_rect(void* ptr,NorthStarUIHandle handle,
                 NorthStarUIRect* out) noexcept {
    return protect([&]() -> int32_t {
        if(!ptr||!out||!handle)
            return fail(NORTHSTAR_UI_INVALID_ARGUMENT,"Invalid geometry query");
        auto& ctx=*static_cast<Context*>(ptr);
        if(ctx.pumping)return fail(NORTHSTAR_UI_INVALID_ARGUMENT,"Reentrant UI call");
        auto boxes=geometry(ctx);
        auto it=boxes.find(handle);
        if(it==boxes.end())return fail(NORTHSTAR_UI_NOT_FOUND,"Element not found");
        Rect b=it->second;
        *out={static_cast<float>(b.x),static_cast<float>(b.y),
              static_cast<float>(b.w),static_cast<float>(b.h)};
        return NORTHSTAR_UI_OK;
    });
}
int32_t pointer(void* ptr,float x,float y,uint32_t phase) noexcept {
    return protect([&]() -> int32_t {
        if(!ptr||!std::isfinite(x)||!std::isfinite(y)||phase>2)
            return fail(NORTHSTAR_UI_INVALID_ARGUMENT,"Invalid pointer event");
        auto& ctx=*static_cast<Context*>(ptr);
        if(ctx.pumping)return fail(NORTHSTAR_UI_INVALID_ARGUMENT,"Reentrant UI call");
        auto boxes=geometry(ctx);
        uint64_t hit=0;
        std::vector<uint64_t> ids;
        for(auto const& kv:boxes)ids.push_back(kv.first);
        std::sort(ids.begin(),ids.end());
        for(auto id:ids) {
            auto const& node=ctx.nodes.at(id);
            if((node.kind==NORTHSTAR_UI_BUTTON||node.kind==NORTHSTAR_UI_INPUT)
                &&inside(boxes.at(id),static_cast<int>(std::floor(x)),
                                       static_cast<int>(std::floor(y))))
                hit=id;
        }
        // Dispatch *transitions*, not one event for every mouse-move sample.
        // Pointer enter/leave values are booleans: project sounds can play
        // once upon entering a control without audio flooding every frame.
        if(ctx.hovered!=hit) {
            auto const previous=ctx.hovered;
            const std::size_t transitions=
                (previous?1u:0u)+(hit?1u:0u);
            if(ctx.events.size()+transitions>4096)
                return fail(NORTHSTAR_UI_BACKEND_FAILURE,
                            "UI pointer event queue full");
            auto transition=[&](std::uint64_t target,bool entered) {
                if(!target || ctx.nodes.at(target).kind!=NORTHSTAR_UI_BUTTON)
                    return;
                Event e{};
                e.kind=NORTHSTAR_UI_EVENT_POINTER;
                e.target=target;
                e.name="pointer";
                e.value.kind=NORTHSTAR_UI_VALUE_BOOL;
                e.value.boolean=entered;
                ctx.events.push_back(std::move(e));
            };
            transition(previous,false);
            ctx.hovered=hit;
            transition(hit,true);
        }
        if(phase==1) {
            if(hit && ctx.nodes.at(hit).kind==NORTHSTAR_UI_BUTTON) {
                if(ctx.events.size()>=4096)
                    return fail(NORTHSTAR_UI_BACKEND_FAILURE,
                                "UI press event queue full");
                Event e{};
                e.kind=NORTHSTAR_UI_EVENT_POINTER;
                e.target=hit;
                e.name="press";
                e.value.kind=NORTHSTAR_UI_VALUE_BOOL;
                e.value.boolean=true;
                ctx.events.push_back(std::move(e));
            }
            ctx.pressed=hit;
            if(hit && ctx.nodes.at(hit).kind==NORTHSTAR_UI_INPUT)
                ctx.focused=hit;
            else if(hit)ctx.focused=0;
        }
        if(phase==0) {
            if(hit&&hit==ctx.pressed) {
                if(ctx.events.size()>=4096)
                    return fail(NORTHSTAR_UI_BACKEND_FAILURE,"UI event queue full");
                Event event{};
                event.kind=NORTHSTAR_UI_EVENT_CLICK;
                event.target=hit;
                event.name="click";
                event.value.kind=NORTHSTAR_UI_VALUE_BOOL;
                event.value.boolean=true;
                ctx.events.push_back(std::move(event));
            }
            ctx.pressed=0;
        }
        return NORTHSTAR_UI_OK;
    });
}
const NorthStarUIRasterApi raster_api{sizeof(NorthStarUIRasterApi),0,
                                      draw_rgba,get_rect,pointer};
const NorthStarUIRasterLayersApi raster_layers_api{
    sizeof(NorthStarUIRasterLayersApi),0,draw_rgba_filtered};
const NorthStarModule raster_layers_module{
    sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_UI,"native","1.1.0",
    NORTHSTARCPP_UI_RASTER_LAYERS_MODULE_ABI,sizeof(raster_layers_api),
    &raster_layers_api,last_ui_error};
const NorthStarModule raster_module{sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_UI,"native","1.1.0",
    NORTHSTARCPP_UI_RASTER_ABI,sizeof(raster_api),&raster_api,last_ui_error};
