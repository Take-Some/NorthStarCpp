// Optional FreeType + HarfBuzz text path. Included by the native raster backend
// only when CMake discovers both libraries. Raster fallback remains available.
#ifdef NORTHSTARCPP_UI_HAS_FONT_SHAPING
struct CachedGlyph {
    int width=0,height=0,left=0,top=0;
    std::vector<uint8_t> bitmap;
};
struct FontRasterizer {
    FT_Library library=nullptr;
    FT_Face face=nullptr;
    hb_font_t* hbfont=nullptr;
    unsigned pixels=0;
    std::unordered_map<uint32_t,CachedGlyph> cache;
    FontRasterizer() {
        if(FT_Init_FreeType(&library))return;
        std::vector<std::string> choices;
        if(const char* path=std::getenv("NORTHSTARCPP_UI_FONT"))
            if(*path)choices.emplace_back(path);
#ifdef _WIN32
        choices.emplace_back("C:/Windows/Fonts/segoeui.ttf");
        choices.emplace_back("C:/Windows/Fonts/arial.ttf");
#else
        choices.emplace_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
        choices.emplace_back("/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf");
#endif
        for(const auto& path:choices) {
            if(!std::filesystem::exists(std::filesystem::u8path(path)))continue;
            if(FT_New_Face(library,path.c_str(),0,&face)==0)break;
        }
        if(face)hbfont=hb_ft_font_create_referenced(face);
    }
    ~FontRasterizer() {
        if(hbfont)hb_font_destroy(hbfont);
        if(face)FT_Done_Face(face);
        if(library)FT_Done_FreeType(library);
    }
    bool size(unsigned desired) {
        if(!face||!hbfont)return false;
        if(pixels==desired)return true;
        if(FT_Set_Pixel_Sizes(face,0,desired))return false;
        pixels=desired;
        hb_ft_font_changed(hbfont);
        cache.clear();
        return true;
    }
    const CachedGlyph& glyph(uint32_t index) {
        auto found=cache.find(index);
        if(found!=cache.end())return found->second;
        if(cache.size()>=2048)cache.clear();
        CachedGlyph output;
        if(FT_Load_Glyph(face,index,FT_LOAD_DEFAULT)==0 &&
           FT_Render_Glyph(face->glyph,FT_RENDER_MODE_NORMAL)==0) {
            const auto& bitmap=face->glyph->bitmap;
            output.width=static_cast<int>(bitmap.width);
            output.height=static_cast<int>(bitmap.rows);
            output.left=face->glyph->bitmap_left;
            output.top=face->glyph->bitmap_top;
            if(bitmap.pixel_mode==FT_PIXEL_MODE_GRAY &&
               output.width>0&&output.height>0&&bitmap.buffer) {
                output.bitmap.resize(static_cast<size_t>(output.width)*output.height);
                for(int y=0;y<output.height;++y) {
                    const uint8_t* row=bitmap.pitch>=0?
                        bitmap.buffer+static_cast<size_t>(y)*bitmap.pitch:
                        bitmap.buffer+static_cast<size_t>(output.height-1-y)*(-bitmap.pitch);
                    std::memcpy(output.bitmap.data()+static_cast<size_t>(y)*output.width,
                                row,static_cast<size_t>(output.width));
                }
            }
        }
        return cache.emplace(index,std::move(output)).first->second;
    }
};
bool paint_shaped_text(uint8_t* dst,uint32_t stride,Rect screen,Rect bounds,
                       const std::string& value,uint32_t rgba,int scale) {
    // Thread-local: the UI API serializes calls on its UI thread.
    static thread_local FontRasterizer font;
    const unsigned pixels=static_cast<unsigned>(std::clamp(scale,1,5)*9);
    if(!font.size(pixels))return false;
    hb_buffer_t* buffer=hb_buffer_create();
    if(!buffer)return false;
    hb_buffer_add_utf8(buffer,value.c_str(),static_cast<int>(value.size()),0,-1);
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(font.hbfont,buffer,nullptr,0);
    unsigned count=0;
    auto* infos=hb_buffer_get_glyph_infos(buffer,&count);
    auto* positions=hb_buffer_get_glyph_positions(buffer,&count);
    const int red=(rgba>>24)&255,green=(rgba>>16)&255,blue=(rgba>>8)&255;
    const int alpha=rgba&255;
    // 26.6 fixed-point pen, matches HarfBuzz FT font units.
    int64_t penx=static_cast<int64_t>(bounds.x+8)*64;
    int64_t peny=static_cast<int64_t>(bounds.y+std::max(0,(bounds.h-static_cast<int>(pixels))/2)
                                     +static_cast<int>(pixels))*64;
    const Rect clip=intersect(screen,bounds);
    for(unsigned i=0;i<count;++i) {
        const CachedGlyph& glyph=font.glyph(infos[i].codepoint);
        int ox=static_cast<int>((penx+positions[i].x_offset)/64)+glyph.left;
        int oy=static_cast<int>((peny-positions[i].y_offset)/64)-glyph.top;
        for(int gy=0;gy<glyph.height;++gy) {
            const int y=oy+gy;
            if(y<clip.y||y>=clip.y+clip.h)continue;
            for(int gx=0;gx<glyph.width;++gx) {
                const int x=ox+gx;
                if(x<clip.x||x>=clip.x+clip.w)continue;
                const unsigned coverage=glyph.bitmap[
                    static_cast<size_t>(gy)*glyph.width+gx];
                const unsigned a=(coverage*alpha+127)/255;
                if(a==0)continue;
                uint8_t* pixel=dst+static_cast<size_t>(y)*stride+static_cast<size_t>(x)*4;
                pixel[0]=static_cast<uint8_t>((red*a+pixel[0]*(255-a)+127)/255);
                pixel[1]=static_cast<uint8_t>((green*a+pixel[1]*(255-a)+127)/255);
                pixel[2]=static_cast<uint8_t>((blue*a+pixel[2]*(255-a)+127)/255);
                pixel[3]=static_cast<uint8_t>(a+(pixel[3]*(255-a)+127)/255);
            }
        }
        penx+=positions[i].x_advance;
        peny-=positions[i].y_advance;
        if(penx>static_cast<int64_t>(clip.x+clip.w+1024)*64)break;
    }
    hb_buffer_destroy(buffer);
    return true;
}
#else
bool paint_shaped_text(uint8_t*,uint32_t,Rect,Rect,const std::string&,uint32_t,int) {
    return false;
}
#endif
