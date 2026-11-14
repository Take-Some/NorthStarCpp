// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026
//
// Native C++ / Win32 project browser. Light NorthStarCpp theme and a
// template-first, Unreal-style workflow; no Python or web renderer.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <northstarcpp/project_catalog.hpp>
#include <northstarcpp/logger.hpp>
#include "project_dialog.hpp"
#include "resource_ids.h"
#include "crash_handler.hpp"

#include <algorithm>
#include <cstdio>
#include <array>
#include <cwctype>
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace gp = Gdiplus;
namespace ns = northstarcpp::projects;

#ifndef NORTHSTARCPP_ENGINE_VERSION
#define NORTHSTARCPP_ENGINE_VERSION "0.1.0"
#endif

namespace {

constexpr wchar_t window_class[] = L"NorthStarCppProjectBrowserLight";
constexpr int header_h = 78;
constexpr int tabs_h = 65;
constexpr int sidebar_w = 216;
constexpr int detail_w = 324;
constexpr int card_gap = 18;
constexpr int card_h = 247;

enum ControlId {
    id_search = 110,
    id_physics = 111,
    id_new_name = 112,
    id_new_location = 113,
    id_new_description = 114,
    id_browse = 115,
    id_create = 116,
    id_import = 117,
    id_scan = 118,
    id_refresh = 119,
    id_open = 120,
    id_play = 121,
    id_favorite = 122,
    id_duplicate = 123,
    id_edit = 124,
    id_folder = 125,
    id_forget = 126,
};

gp::Color color(unsigned char red, unsigned char green, unsigned char blue)
{
    return gp::Color(255, red, green, blue);
}
const gp::Color white = color(255,255,255);
const gp::Color bg = color(247,249,252);
const gp::Color rail = color(240,243,248);
const gp::Color ink = color(34,47,64);
const gp::Color secondary = color(108,123,143);
const gp::Color blue = color(36,113,188);
const gp::Color blue_tint = color(232,242,253);
const gp::Color edge = color(218,226,235);
const gp::Color green = color(42,145,103);
const gp::Color orange = color(202,114,48);
const gp::Color gray_field = color(248,250,253);

std::wstring wide(char const* s)
{
    try { return ns::to_wide(s ? std::string(s) : std::string{}); }
    catch (...) { return L"Invalid project metadata"; }
}

std::wstring read_text(HWND control)
{
    if (!control)
        return {};
    int count = GetWindowTextLengthW(control);
    std::wstring result(static_cast<size_t>(count+1), L'\0');
    GetWindowTextW(control, result.data(), count+1);
    result.resize(static_cast<size_t>(count));
    return result;
}

std::wstring lower(std::wstring s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t x) { return std::towlower(x); });
    return s;
}

bool contains(std::wstring const& value, std::wstring const& query)
{
    return lower(value).find(lower(query)) != std::wstring::npos;
}

void error(HWND owner, std::exception const& e)
{
    auto const value = wide(e.what());
    MessageBoxW(owner, value.c_str(), L"NorthStarCpp Project Browser", MB_ICONERROR | MB_OK);
}

void rounded(gp::Graphics& g, gp::RectF r, float radius,
             gp::Color fill, gp::Color outline = gp::Color(0,0,0,0), float stroke = 1.f)
{
    gp::GraphicsPath p;
    auto const d = std::min(radius*2.f, std::min(r.Width, r.Height));
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.GetRight()-d, r.Y, d, d, 270, 90);
    p.AddArc(r.GetRight()-d, r.GetBottom()-d, d, d, 0, 90);
    p.AddArc(r.X, r.GetBottom()-d, d, d, 90, 90);
    p.CloseFigure();
    gp::SolidBrush brush(fill);
    g.FillPath(&brush, &p);
    if (outline.GetA() > 0) {
        gp::Pen pen(outline, stroke);
        g.DrawPath(&pen, &p);
    }
}

void label(gp::Graphics& g, std::wstring const& value, float x, float y,
           float width, float height, float size = 15.f,
           gp::Color fg = ink, bool strong = false,
           gp::StringAlignment align = gp::StringAlignmentNear)
{
    gp::Font font(L"Segoe UI", size, strong ? gp::FontStyleBold : gp::FontStyleRegular,
                  gp::UnitPixel);
    gp::SolidBrush brush(fg);
    gp::StringFormat format;
    format.SetAlignment(align);
    format.SetLineAlignment(gp::StringAlignmentCenter);
    format.SetTrimming(gp::StringTrimmingEllipsisCharacter);
    format.SetFormatFlags(gp::StringFormatFlagsNoWrap);
    g.DrawString(value.c_str(), -1, &font, gp::RectF(x,y,width,height), &format, &brush);
}

RECT rect(int x, int y, int w, int h)
{
    return RECT{x,y,x+w,y+h};
}

bool inside(RECT const& r, int x, int y)
{
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

void line(gp::Graphics& g, float x, float y, float w, gp::Color shade = edge)
{
    gp::Pen pen(shade, 1);
    g.DrawLine(&pen,x,y,x+w,y);
}

HWND add_control(HWND parent, wchar_t const* cls, wchar_t const* title,
                 DWORD style, int id, HFONT font)
{
    auto result = CreateWindowExW(0,cls,title,WS_VISIBLE | WS_CHILD | style,
        0,0,100,25,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr),nullptr);
    if (!result)
        throw std::runtime_error("Native project browser control creation failed");
    SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return result;
}

void place(HWND parent, int id, int x,int y,int w,int h)
{
    HWND child = GetDlgItem(parent,id);
    if (child)
        MoveWindow(child,x,y,std::max(1,w),std::max(1,h),TRUE);
}

void visibility(HWND parent, int id, bool show)
{
    if (HWND child=GetDlgItem(parent,id))
        ShowWindow(child,show ? SW_SHOW : SW_HIDE);
}

fs::path pick_folder(HWND window)
{
    IFileDialog* picker = nullptr;
    HRESULT result = CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&picker));
    if (FAILED(result))
        throw std::runtime_error("Windows folder chooser unavailable");
    fs::path path;
    DWORD options = 0;
    if (SUCCEEDED(picker->GetOptions(&options))) {
        picker->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        picker->SetTitle(L"Select project directory");
        if (picker->Show(window) == S_OK) {
            IShellItem* item=nullptr;
            if (SUCCEEDED(picker->GetResult(&item))) {
                PWSTR value=nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&value))) {
                    path=fs::path(value);
                    CoTaskMemFree(value);
                }
                item->Release();
            }
        }
    }
    picker->Release();
    return path;
}

std::wstring quoted(fs::path const& path)
{
    auto const value=path.wstring();
    if (value.find(L'"') != std::wstring::npos)
        throw std::runtime_error("Invalid path for process launch");
    return L"\"" + value + L"\"";
}

std::vector<wchar_t> launch_environment(std::wstring provider)
{
    auto* env = GetEnvironmentStringsW();
    if (!env)
        throw std::runtime_error("Unable to read launch environment");
    std::vector<std::wstring> variables;
    static constexpr wchar_t key[] = L"NORTHSTARCPP_PHYSICS_PROVIDER=";
    for (auto p=env; *p; p += wcslen(p)+1)
        if (_wcsnicmp(p,key,wcslen(key)) != 0)
            variables.emplace_back(p);
    FreeEnvironmentStringsW(env);
    provider = lower(provider);
    variables.emplace_back(std::wstring(key)+provider);
    std::sort(variables.begin(),variables.end(),[](auto const& a,auto const& b) {
        return _wcsicmp(a.c_str(),b.c_str()) < 0;
    });
    std::vector<wchar_t> block;
    for (auto const& v : variables) {
        block.insert(block.end(),v.begin(),v.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

void start_project(ns::Project const& p, bool editor, std::wstring const& physics)
{
    if (p.status != ns::Project::Status::ready)
        throw std::runtime_error("Selected project is not ready");
    auto exe = ns::executable_directory() /
        (editor ? L"northstarcpp_editor_qt.exe" : L"northstarcpp_player.exe");
    if (!fs::is_regular_file(exe))
        exe=ns::executable_directory().parent_path()/
            (editor ? L"northstarcpp_editor_qt.exe" : L"northstarcpp_player.exe");
    if (!fs::is_regular_file(exe))
        throw std::runtime_error("Build the NorthStarCpp editor/player first");
    auto command = quoted(exe) + L" --scene " + quoted(p.startup_scene);
    auto environment = launch_environment(physics);
    STARTUPINFOW startup{};
    startup.cb=sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,
        CREATE_UNICODE_ENVIRONMENT,environment.data(),p.directory.c_str(),
        &startup,&process))
        throw std::runtime_error("Cannot start the editor or game player");
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}

struct Tile {
    RECT bounds{};
    size_t model_index=0;
};

enum class Page { recent, create };

class Browser {
public:
    Browser()
      : catalog(ns::default_projects_directory(), ns::default_catalog_file()),
        templates(ns::templates()) {}

    int run(HINSTANCE app, int show, bool smoke)
    {
        smoke_ = smoke;
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&controls);
        gp::GdiplusStartupInput input{};
        if (gp::GdiplusStartup(&token_, &input,nullptr) != gp::Ok)
            throw std::runtime_error("GDI+ initialization failed");
        WNDCLASSW wc{};
        wc.hInstance=app;
        wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        wc.hIcon=LoadIconW(app,MAKEINTRESOURCEW(IDI_NORTHSTARCPP_APP));
        wc.lpfnWndProc=&dispatch;
        wc.lpszClassName=window_class;
        RegisterClassW(&wc);
        hwnd_=CreateWindowExW(0,window_class,L"NorthStarCpp — Project Browser",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,CW_USEDEFAULT,
            1470,880,nullptr,nullptr,app,this);
        if (!hwnd_)
            throw std::runtime_error("Unable to initialize the Project Browser (Win32 " +
                std::to_string(GetLastError()) + ")");
        SendMessageW(hwnd_,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(
            LoadImageW(app,MAKEINTRESOURCEW(IDI_NORTHSTARCPP_APP),
                IMAGE_ICON,16,16,LR_SHARED)));
        if (smoke_ && !FindResourceW(app,MAKEINTRESOURCEW(IDI_NORTHSTARCPP_APP),RT_GROUP_ICON))
            throw std::runtime_error("NorthStarCpp EXE is missing its embedded logoStar.ico resource");
        ShowWindow(hwnd_,smoke ? SW_HIDE : show);
        UpdateWindow(hwnd_);
        MSG msg{};
        while (GetMessageW(&msg,nullptr,0,0)>0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        thumbnails_.clear(); // Bitmaps must be destroyed before GDI+ shutdown.
        if (token_) gp::GdiplusShutdown(token_);
        token_ = 0;
        return static_cast<int>(msg.wParam);
    }

private:
    static LRESULT CALLBACK dispatch(HWND w,UINT m,WPARAM wp,LPARAM lp)
    {
        Browser* self=reinterpret_cast<Browser*>(GetWindowLongPtrW(w,GWLP_USERDATA));
        if (m==WM_NCCREATE) {
            self=static_cast<Browser*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            self->hwnd_ = w;
            SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if (!self)
            return DefWindowProcW(w,m,wp,lp);
        try { return self->message(m,wp,lp); }
        catch (std::exception const& e) {
            error(w,e);
            return 0;
        }
    }

    int w() const { RECT r{};GetClientRect(hwnd_,&r);return r.right; }
    int h() const { RECT r{};GetClientRect(hwnd_,&r);return r.bottom; }
    int content_left() const { return sidebar_w+27; }
    int right_left() const { return w()-detail_w-20; }
    int content_width() const { return std::max(120,right_left()-content_left()-25); }
    int card_columns() const { return std::max(1,content_width()/265); }
    int card_width() const {
        int count=card_columns();
        return (content_width()-card_gap*(count-1))/count;
    }

    void init()
    {
        font_=CreateFontW(-18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
                          DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        edit_brush_=CreateSolidBrush(RGB(248,250,253));
        auto add=[&](wchar_t const* cls,wchar_t const* text,DWORD style,int id){
            return add_control(hwnd_,cls,text,style,id,font_);
        };
        add(L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,id_search);
        add(L"BUTTON",L"Import",BS_OWNERDRAW,id_import);
        add(L"BUTTON",L"Scan Folder",BS_OWNERDRAW,id_scan);
        add(L"BUTTON",L"Refresh",BS_OWNERDRAW,id_refresh);
        add(L"BUTTON",L"Open Project",BS_OWNERDRAW,id_open);
        add(L"BUTTON",L"Play",BS_OWNERDRAW,id_play);
        add(L"BUTTON",L"★ Favorite",BS_OWNERDRAW,id_favorite);
        add(L"BUTTON",L"Duplicate",BS_OWNERDRAW,id_duplicate);
        add(L"BUTTON",L"Edit Details",BS_OWNERDRAW,id_edit);
        add(L"BUTTON",L"Open Folder",BS_OWNERDRAW,id_folder);
        add(L"BUTTON",L"Remove from Library",BS_OWNERDRAW,id_forget);
        auto combo=add(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,id_physics);
        SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Bullet"));
        SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Jolt"));
        SendMessageW(combo,CB_SETCURSEL,0,0);

        add(L"EDIT",L"MyNewProject",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,id_new_name);
        add(L"EDIT",catalog.projects_directory().c_str(),
            WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,id_new_location);
        add(L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL,id_new_description);
        add(L"BUTTON",L"Browse...",BS_OWNERDRAW,id_browse);
        add(L"BUTTON",L"Create Project",BS_OWNERDRAW,id_create);
        RegisterHotKey(hwnd_,1,MOD_CONTROL,'N');
        RegisterHotKey(hwnd_,2,MOD_CONTROL,'O');
        RegisterHotKey(hwnd_,3,0,VK_F5);
        refresh();
        layout();
        if (smoke_) PostMessageW(hwnd_,WM_APP+11,0,0);
    }

    void layout()
    {
        if (!hwnd_ || !font_) return;
        int x=content_left(), cw=content_width(), rx=right_left();
        auto put=[&](int id,int a,int b,int ww,int hh){
            if (HWND ctrl=GetDlgItem(hwnd_,id))
                MoveWindow(ctrl,a,b,std::max(1,ww),std::max(1,hh),TRUE);
        };
        put(id_search,x,169,cw,39);
        put(id_import,w()-424,22,116,38);
        put(id_scan,w()-300,22,128,38);
        put(id_refresh,w()-164,22,141,38);
        put(id_physics,rx+20,422,detail_w-62,36);
        put(id_open,rx+20,477,detail_w-62,46);
        put(id_play,rx+20,531,detail_w-62,40);
        put(id_favorite,rx+20,587,detail_w-62,39);
        put(id_duplicate,rx+20,633,(detail_w-72)/2,38);
        put(id_edit,rx+28+(detail_w-72)/2,633,(detail_w-72)/2,38);
        put(id_folder,rx+20,680,detail_w-62,38);
        put(id_forget,rx+20,725,detail_w-62,38);

        put(id_new_name,rx+20,346,detail_w-62,39);
        put(id_new_location,rx+20,429,detail_w-152,39);
        put(id_browse,w()-132,429,90,39);
        put(id_new_description,rx+20,521,detail_w-62,91);
        put(id_create,rx+20,h()-103,detail_w-62,48);

        bool const recent=page_==Page::recent;
        for (auto id : {id_search,id_physics,id_open,id_play,id_favorite,id_duplicate,
                        id_edit,id_folder,id_forget,id_import,id_scan,id_refresh})
            ShowWindow(GetDlgItem(hwnd_,id),recent?SW_SHOW:SW_HIDE);
        for (auto id : {id_new_name,id_new_location,id_browse,id_new_description,id_create})
            ShowWindow(GetDlgItem(hwnd_,id),recent?SW_HIDE:SW_SHOW);
        bool const ready=selected() && selected()->status==ns::Project::Status::ready;
        for (auto id : {id_open,id_play,id_duplicate,id_edit})
            EnableWindow(GetDlgItem(hwnd_,id),ready);
        EnableWindow(GetDlgItem(hwnd_,id_folder),selected()&&fs::is_directory(selected()->directory));
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

    void refresh()
    {
        projects_=catalog.list();
        thumbnails_.clear();
        auto before=selected_path_;
        bool found=false;
        for (auto const& p : projects_)
            if (p.directory == before) {found=true;break;}
        if (!found) selected_path_=projects_.empty()?fs::path{}:projects_.front().directory;
        rebuild_filter();
    }

    void rebuild_filter()
    {
        filtered_.clear();
        auto search=lower(read_text(GetDlgItem(hwnd_,id_search)));
        for (size_t i=0;i<projects_.size();++i) {
            auto const& p=projects_[i];
            bool match=true;
            switch (section_) {
                case 1: match=!p.last_opened.empty();break;
                case 2: match=p.favorite;break;
                case 3: match=!p.external;break;
                case 4: match=p.external;break;
                case 5: match=p.status!=ns::Project::Status::ready;break;
                default: break;
            }
            if (match&&!search.empty())
                match=contains(p.name,search)||contains(p.category,search)||
                    contains(p.description,search)||contains(p.directory.wstring(),search);
            if (match) filtered_.push_back(i);
        }
        scroll_=0;
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

    ns::Project const* selected() const
    {
        for (auto const& p : projects_)
            if (!selected_path_.empty()&&p.directory==selected_path_) return &p;
        return nullptr;
    }

    void page(Page next)
    {
        if (page_==next) return;
        page_=next;scroll_=0;
        layout();
    }

    std::vector<size_t> filtered_templates() const
    {
        std::vector<size_t> indexes;
        for (size_t i=0;i<templates.size();++i) {
            if (section_==0 || templates[i].category==category_names()[static_cast<size_t>(section_)])
                indexes.push_back(i);
        }
        return indexes;
    }

    static std::vector<std::wstring> const& category_names()
    {
        static std::vector<std::wstring> categories{
            L"All Categories",L"General",L"Games",L"Simulation",L"Worlds",L"Visualization"
        };
        return categories;
    }

    void painted_card(gp::Graphics& g,RECT box,std::wstring title,std::wstring category,
                      std::wstring sub,bool chosen,std::string const& type,fs::path thumb={})
    {
        float x=static_cast<float>(box.left),y=static_cast<float>(box.top);
        float width=static_cast<float>(box.right-box.left);
        rounded(g,gp::RectF(x+2,y+4,width,static_cast<float>(card_h)),10,
            color(231,235,242));
        rounded(g,gp::RectF(x,y,width,static_cast<float>(card_h)),10,white,
                chosen?blue:edge,chosen?2.f:1.f);
        gp::RectF view(x+10,y+10,width-20,154);
        rounded(g,view,7,color(238,245,253));

        bool loaded=false;
        if (!thumb.empty()&&fs::is_regular_file(thumb)) {
            auto key=thumb.wstring();
            auto it=thumbnails_.find(key);
            if (it==thumbnails_.end()) {
                auto bmp=std::make_unique<gp::Bitmap>(key.c_str());
                it=thumbnails_.emplace(key,std::move(bmp)).first;
            }
            auto* bitmap=it->second.get();
            if (bitmap && bitmap->GetLastStatus()==gp::Ok && bitmap->GetWidth()>0
                && bitmap->GetHeight()>0) {
                auto const scale=std::max(view.Width/static_cast<float>(bitmap->GetWidth()),
                                          view.Height/static_cast<float>(bitmap->GetHeight()));
                g.SetClip(view);
                g.DrawImage(bitmap,view.X+(view.Width-bitmap->GetWidth()*scale)/2.f,
                           view.Y+(view.Height-bitmap->GetHeight()*scale)/2.f,
                           bitmap->GetWidth()*scale,bitmap->GetHeight()*scale);
                g.ResetClip();
                loaded=true;
            }
        }
        if (!loaded) {
            gp::SolidBrush horizon(color(219,234,249));
            g.FillRectangle(&horizon,view.X,view.Y,view.Width,view.Height);
            gp::Pen grid(color(205,224,243),1);
            for (int i=0;i<8;++i) {
                float xx=view.X+view.Width*i/7.f;
                g.DrawLine(&grid,xx,view.Y+view.Height,view.X+view.Width/2.f,
                           view.Y+view.Height*.51f);
            }
            gp::SolidBrush block(color(117,168,221));
            gp::SolidBrush darker(color(69,122,182));
            gp::Pen outline(color(48,102,170),1);
            auto cube=[&](float cx,float cy,float size) {
                gp::PointF roof[4]{{cx,cy-size},{cx+size,cy-size*.45f},
                    {cx,cy},{cx-size,cy-size*.45f}};
                gp::PointF left[4]{{cx-size,cy-size*.45f},{cx,cy},
                    {cx,cy+size},{cx-size,cy+size*.5f}};
                gp::PointF right[4]{{cx,cy},{cx+size,cy-size*.45f},
                    {cx+size,cy+size*.5f},{cx,cy+size}};
                g.FillPolygon(&block,roof,4);
                g.FillPolygon(&darker,left,4);
                g.FillPolygon(&block,right,4);
                g.DrawPolygon(&outline,roof,4);
            };
            auto cx=view.X+view.Width*.5f,cy=view.Y+view.Height*.55f;
            if (type=="blank") {
                gp::Pen plus(blue,4);
                g.DrawLine(&plus,cx-17,cy,cx+17,cy);
                g.DrawLine(&plus,cx,cy-17,cx,cy+17);
            } else if (type=="city") {
                cube(cx-55,cy+15,31);
                cube(cx+48,cy+8,38);
                cube(cx,cy+22,14);
            } else if (type=="architecture") {
                cube(cx-30,cy+21,35);
                cube(cx+47,cy+10,24);
            } else if (type=="physics") {
                cube(cx-38,cy+27,17);
                cube(cx+33,cy-5,23);
            } else {
                cube(cx,cy+10,33);
                cube(cx-57,cy+35,14);
            }
            label(g,L"NORTHSTAR / SCENE",view.X+9,view.Y+4,view.Width-18,21,10,secondary,true);
        }

        label(g,title,x+14,y+171,width-28,26,17,ink,true);
        label(g,category,x+14,y+203,width-28,18,12,secondary);
        label(g,sub,x+14,y+225,width-28,15,11,chosen?blue:secondary);
    }

    void paint(gp::Graphics& g)
    {
        auto width=w(),height=h();
        g.SetSmoothingMode(gp::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(gp::TextRenderingHintClearTypeGridFit);
        gp::SolidBrush backdrop(bg);
        g.FillRectangle(&backdrop,0,0,width,height);
        gp::SolidBrush white_brush(white);
        g.FillRectangle(&white_brush,0,0,width,header_h);
        line(g,0.f,static_cast<float>(header_h),static_cast<float>(width));
        gp::SolidBrush side(rail);
        g.FillRectangle(&side,0,header_h,sidebar_w,height-header_h);
        line(g,0.f,static_cast<float>(header_h+tabs_h),static_cast<float>(width));
        // The actual NorthStar brand icon, not a placeholder "N" glyph.
        HICON const engine_icon=LoadIconW(GetModuleHandleW(nullptr),
                                          MAKEINTRESOURCEW(IDI_NORTHSTARCPP_APP));
        if (engine_icon) {
            gp::Bitmap bitmap(engine_icon);
            g.DrawImage(&bitmap,26.f,19.f,43.f,43.f);
        }
        label(g,L"NORTHSTAR",72,15,186,32,23,ink,true);
        label(g,L"CPP  /  PROJECT BROWSER",73,45,320,22,12,secondary,true);
        label(g,L"ENGINE  "+wide(NORTHSTARCPP_ENGINE_VERSION)+L"  ·  C++",
              static_cast<float>(width-625),27,173,23,
              12,secondary,true);
        // Native toolbar on right uses owner-drawn buttons.

        // Navigation tabs aligned with Unreal Project Browser workflow.
        auto draw_tab=[&](int idx,std::wstring const& title,int x,int width_px) {
            if ((page_==Page::recent)==(idx==0)) {
                gp::SolidBrush highlight(blue_tint);
                g.FillRectangle(&highlight,static_cast<float>(x),
                    static_cast<float>(header_h+10),static_cast<float>(width_px),43.f);
                gp::SolidBrush indicator(blue);
                g.FillRectangle(&indicator,static_cast<float>(x),static_cast<float>(header_h+tabs_h-4),
                    static_cast<float>(width_px),4.f);
            }
            label(g,title,static_cast<float>(x+15),static_cast<float>(header_h+14),
                  static_cast<float>(width_px-25),36,15,
                  ((page_==Page::recent)==(idx==0))?blue:secondary,true);
        };
        draw_tab(0,L"RECENT PROJECTS",sidebar_w+26,202);
        draw_tab(1,L"NEW PROJECT",sidebar_w+240,175);

        if (page_==Page::recent) {
            label(g,L"LIBRARY",22,172,sidebar_w-28,23,11,secondary,true);
            static std::array<std::wstring,6> names{
                L"All Projects",L"Recently Opened",L"Favorites",
                L"Local Projects",L"External Projects",L"Needs Attention"
            };
            for (int i=0;i<6;++i) {
                int y=217+i*51;
                if (section_==i) rounded(g,gp::RectF(12.f,static_cast<float>(y),
                    static_cast<float>(sidebar_w-24),43.f),7,blue_tint);
                label(g,names[static_cast<size_t>(i)],26.f,static_cast<float>(y+2),
                    static_cast<float>(sidebar_w-44),39,14,
                    section_==i?blue:ink,section_==i);
            }
        } else {
            label(g,L"TEMPLATE CATEGORIES",22,172,sidebar_w-28,23,11,secondary,true);
            auto const& categories=category_names();
            for (size_t i=0;i<categories.size();++i) {
                int y=217+static_cast<int>(i)*51;
                if (section_==static_cast<int>(i))
                    rounded(g,gp::RectF(12.f,static_cast<float>(y),
                        static_cast<float>(sidebar_w-24),43.f),7,blue_tint);
                label(g,categories[i],26.f,static_cast<float>(y+2),
                    static_cast<float>(sidebar_w-44),39,14,
                    section_==static_cast<int>(i)?blue:ink,
                    section_==static_cast<int>(i));
            }
        }

        // Details and project creation settings pane.
        int rx=right_left();
        rounded(g,gp::RectF(static_cast<float>(rx),160.f,
            static_cast<float>(detail_w-13),static_cast<float>(std::max(400,height-181))),
            10,white,edge);
        if (page_==Page::recent) {
            label(g,L"PROJECT DETAILS",rx+20.f,179,detail_w-54.f,26,13,secondary,true);
            line(g,rx+20.f,218,detail_w-57.f);
            auto p=selected();
            if (p) {
                label(g,p->name,rx+20.f,229,detail_w-61.f,37,22,ink,true);
                auto ready=p->status==ns::Project::Status::ready;
                rounded(g,gp::RectF(rx+20.f,272.f,ready?78.f:155.f,29.f),6,
                    ready?color(225,248,235):color(255,243,230));
                label(g,ns::status_text(p->status),rx+25.f,273,
                    ready?70.f:146.f,25,12,ready?green:orange,true);
                label(g,L"LOCATION",rx+20.f,313,detail_w-56.f,20,11,secondary,true);
                auto location=p->directory.wstring();
                if (location.size()>68) location=location.substr(0,68)+L"...";
                label(g,location,rx+20.f,337,detail_w-54.f,43,12,ink);
                label(g,L"TEMPLATE  "+wide(p->template_id.c_str()),rx+20.f,389,
                    detail_w-60.f,22,12,secondary);
                label(g,L"PHYSICS PROVIDER",rx+20.f,400,detail_w-58.f,18,11,secondary,true);
            } else {
                label(g,L"Select a project",rx+20.f,246,detail_w-54.f,38,16,secondary);
            }
        } else {
            label(g,L"PROJECT SETTINGS",rx+20.f,179,detail_w-53.f,27,13,secondary,true);
            line(g,rx+20.f,217,detail_w-57.f);
            auto it=std::find_if(templates.begin(),templates.end(),
                [&](auto const& t){ return t.id==selected_template_; });
            label(g,it==templates.end()?L"Select template":it->label,
                  rx+20.f,237,detail_w-53.f,36,20,ink,true);
            if (it!=templates.end())
                label(g,it->description,rx+20.f,275,detail_w-56.f,39,12,secondary);
            label(g,L"PROJECT NAME",rx+20.f,320,detail_w-56.f,22,11,secondary,true);
            label(g,L"PROJECT LOCATION",rx+20.f,402,detail_w-56.f,20,11,secondary,true);
            label(g,L"DESCRIPTION",rx+20.f,497,detail_w-56.f,18,11,secondary,true);
            label(g,L"Start with a native .nscn file.",rx+20.f,
                  static_cast<float>(height-142),detail_w-56.f,20,12,secondary);
        }

        // Content heading stays above the scroll clip.
        if (page_ == Page::recent)
            label(g,L"Your Projects",content_left(),215,content_width(),28,19,ink,true);
        else
            label(g,L"Choose a Template",content_left(),167,content_width(),34,21,ink,true);
        // Grid area: clipped so scrolling never covers header and footer.
        int const top=page_==Page::recent?252:219;
        int const bottom=height-27;
        auto saved=g.Save();
        g.SetClip(gp::Rect(content_left(),top,content_width(),std::max(1,bottom-top)));
        hits_.clear();
        if (page_==Page::recent) {
            int columns=card_columns(), cw=card_width();
            if (filtered_.empty())
                label(g,L"No projects found. Import a project or create a new one.",
                    content_left()+15.f,288,content_width()-30.f,60,17,secondary);
            for (size_t i=0;i<filtered_.size();++i) {
                int col=static_cast<int>(i%columns),row=static_cast<int>(i/columns);
                RECT place=rect(content_left()+col*(cw+card_gap),
                                top+row*(card_h+card_gap)-scroll_,cw,card_h);
                if (place.bottom<top||place.top>bottom)
                    continue;
                auto const& p=projects_[filtered_[i]];
                auto const thumb=p.directory/"thumbnail.png";
                painted_card(g,place,p.name,p.category,
                    p.favorite?L"★  FAVORITE":ns::status_text(p.status),
                    p.directory==selected_path_,p.template_id,thumb);
                hits_.push_back(Tile{place,filtered_[i]});
            }
        } else {
            auto indexes=filtered_templates();
            int columns=card_columns(),cw=card_width();
            for (size_t i=0;i<indexes.size();++i) {
                int col=static_cast<int>(i%columns),row=static_cast<int>(i/columns);
                RECT place=rect(content_left()+col*(cw+card_gap),
                                top+row*(card_h+card_gap)-scroll_,cw,card_h);
                if (place.bottom<top||place.top>bottom)
                    continue;
                auto const& t=templates[indexes[i]];
                painted_card(g,place,t.label,t.category,t.description,
                             selected_template_==t.id,t.id);
                hits_.push_back(Tile{place,indexes[i]});
            }
        }
        g.Restore(saved);
    }

    bool draw_button(DRAWITEMSTRUCT const& item)
    {
        if (item.CtlType!=ODT_BUTTON) return false;
        gp::Graphics g(item.hDC);
        g.SetSmoothingMode(gp::SmoothingModeAntiAlias);
        bool const primary=item.CtlID==id_open||item.CtlID==id_create;
        bool const disabled=(item.itemState&ODS_DISABLED)!=0;
        bool const pressed=(item.itemState&ODS_SELECTED)!=0;
        auto base=disabled?color(229,233,238):
            primary?(pressed?color(21,90,159):blue):
            (pressed?color(219,229,240):gray_field);
        auto border_color=primary?base:edge;
        gp::RectF rr(
            static_cast<float>(item.rcItem.left)+1.f,static_cast<float>(item.rcItem.top)+1.f,
            static_cast<float>(item.rcItem.right-item.rcItem.left-2),
            static_cast<float>(item.rcItem.bottom-item.rcItem.top-2));
        rounded(g,rr,7,base,border_color);
        auto str=read_text(item.hwndItem);
        label(g,str,rr.X+6,rr.Y,rr.Width-12,rr.Height,14,
              disabled?secondary:primary?white:ink,true,gp::StringAlignmentCenter);
        return true;
    }

    void choose_parent()
    {
        auto path=pick_folder(hwnd_);
        if (!path.empty())
            SetWindowTextW(GetDlgItem(hwnd_,id_new_location),path.c_str());
    }

    void create()
    {
        auto const name=read_text(GetDlgItem(hwnd_,id_new_name));
        auto const directory=read_text(GetDlgItem(hwnd_,id_new_location));
        if (directory.empty())
            throw std::runtime_error("Select a project location");
        auto p=catalog.create(name,selected_template_,fs::path(directory));
        auto const description=read_text(GetDlgItem(hwnd_,id_new_description));
        if (!description.empty()) {
            auto it=std::find_if(templates.begin(),templates.end(),
                [&](auto const& t){return t.id==selected_template_;});
            catalog.update_description(p.directory,description,
                it==templates.end()?L"General":it->category);
        }
        selected_path_=p.directory;
        page_=Page::recent;
        section_=0;
        refresh();
        layout();
    }

    void start(bool editor)
    {
        auto const* p=selected();
        if (!p) return;
        int idx=static_cast<int>(SendMessageW(GetDlgItem(hwnd_,id_physics),CB_GETCURSEL,0,0));
        start_project(*p,editor,idx==1?L"jolt":L"bullet");
        auto const path=p->directory;
        catalog.opened(path);
        selected_path_=path;
        refresh();
    }

    void import()
    {
        auto folder=pick_folder(hwnd_);
        if (folder.empty()) return;
        auto p=catalog.import(folder);
        selected_path_=p.directory;
        section_=0;
        refresh();
    }

    void scan()
    {
        auto folder=pick_folder(hwnd_);
        if (folder.empty()) return;
        auto count=catalog.scan(folder,3);
        refresh();
        auto message=L"Discovered "+std::to_wstring(count)+L" projects.";
        MessageBoxW(hwnd_,message.c_str(),L"Project Discovery",MB_OK|MB_ICONINFORMATION);
    }

    void favorite()
    {
        auto const* p=selected();if (!p)return;
        auto const path=p->directory;
        bool const was_favorite=p->favorite;
        catalog.favorite(path,!was_favorite);selected_path_=path;refresh();
    }

    void duplicate()
    {
        auto const* p=selected();if (!p)return;
        auto const target=ns::ui::project_properties_dialog(hwnd_,catalog,*p,true);
        if (!target.empty()) {
            selected_path_=target;
            section_=0;
            refresh();
        }
    }

    void edit()
    {
        auto const* p=selected();if (!p)return;
        auto const target=ns::ui::project_properties_dialog(hwnd_,catalog,*p,false);
        if (!target.empty()) {
            selected_path_=target;
            refresh();
        }
    }

    void forget()
    {
        auto const* p=selected();if (!p)return;
        if (MessageBoxW(hwnd_,
            L"Remove project from the library? Project files are NOT deleted.",
            L"Remove from Library",MB_YESNO|MB_ICONQUESTION)!=IDYES) return;
        auto path=p->directory;
        catalog.forget(path);selected_path_.clear();refresh();
    }

    void on_command(int id)
    {
        switch (id) {
            case id_create: create();break;
            case id_browse: choose_parent();break;
            case id_open: start(true);break;
            case id_play: start(false);break;
            case id_import: import();break;
            case id_scan: scan();break;
            case id_refresh: refresh();break;
            case id_favorite: favorite();break;
            case id_duplicate: duplicate();break;
            case id_edit: edit();break;
            case id_folder: {
                auto p=selected();
                if (p&&fs::is_directory(p->directory))
                    ShellExecuteW(hwnd_,L"open",p->directory.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
                break;
            }
            case id_forget: forget();break;
        }
    }

    void on_click(int x,int y,bool dbl)
    {
        if (y>=header_h&&y<header_h+tabs_h) {
            if (x>=sidebar_w+26&&x<=sidebar_w+228)
                {page(Page::recent);section_=0;rebuild_filter();return;}
            if (x>=sidebar_w+240&&x<=sidebar_w+415)
                {page(Page::create);section_=0;scroll_=0;InvalidateRect(hwnd_,nullptr,FALSE);return;}
        }
        if (x>=0&&x<=sidebar_w&&y>=217&&y<217+6*51) {
            auto next=(y-217)/51;
            if (next>=0&&next<6) {
                section_=next;scroll_=0;
                if (page_==Page::recent) rebuild_filter();
                else InvalidateRect(hwnd_,nullptr,FALSE);
            }
            return;
        }
        for (auto const& hit:hits_) {
            if (!inside(hit.bounds,x,y))continue;
            if (page_==Page::recent) {
                if (hit.model_index<projects_.size()) {
                    selected_path_=projects_[hit.model_index].directory;
                    layout();
                    if (dbl) start(true);
                }
            } else if (hit.model_index<templates.size()) {
                selected_template_=templates[hit.model_index].id;
                InvalidateRect(hwnd_,nullptr,FALSE);
            }
            return;
        }
    }

    LRESULT message(UINT msg,WPARAM wp,LPARAM lp)
    {
        switch (msg) {
            case WM_CREATE:
                init();return 0;
            case WM_SIZE:
                layout();return 0;
            case WM_GETMINMAXINFO: {
                auto info=reinterpret_cast<MINMAXINFO*>(lp);
                info->ptMinTrackSize={1090,810};
                return 0;
            }
            case WM_APP+11: {
                // Exercise the real light-theme painter in headless CTest mode.
                HDC dc=GetDC(hwnd_);
                HDC mem=dc ? CreateCompatibleDC(dc) : nullptr;
                HBITMAP buffer=mem ? CreateCompatibleBitmap(dc,1280,820) : nullptr;
                if (mem && buffer) {
                    auto old=SelectObject(mem,buffer);
                    {
                        gp::Graphics canvas(mem);
                        paint(canvas);
                    }
                    auto const pixel=GetPixel(mem,300,10);
                    if (pixel!=RGB(255,255,255))
                        exit_code_=3;
                    SelectObject(mem,old);
                } else {
                    exit_code_=4;
                }
                if (buffer) DeleteObject(buffer);
                if (mem) DeleteDC(mem);
                if (dc) ReleaseDC(hwnd_,dc);
                PostMessageW(hwnd_,WM_CLOSE,0,0);
                return 0;
            }
            case WM_PAINT: {
                PAINTSTRUCT ps{};
                auto hdc=BeginPaint(hwnd_,&ps);
                RECT bounds{};
                GetClientRect(hwnd_,&bounds);
                int const width=std::max(1,static_cast<int>(bounds.right));
                int const height=std::max(1,static_cast<int>(bounds.bottom));
                auto memory=CreateCompatibleDC(hdc);
                auto bitmap=CreateCompatibleBitmap(hdc,width,height);
                if (memory && bitmap) {
                    auto previous=SelectObject(memory,bitmap);
                    {
                        gp::Graphics offscreen(memory);
                        paint(offscreen);
                    }
                    BitBlt(hdc,0,0,width,height,memory,0,0,SRCCOPY);
                    SelectObject(memory,previous);
                } else {
                    gp::Graphics fallback(hdc);
                    paint(fallback);
                }
                if (bitmap) DeleteObject(bitmap);
                if (memory) DeleteDC(memory);
                EndPaint(hwnd_,&ps);
                return 0;
            }
            case WM_ERASEBKGND:return 1;
            case WM_DRAWITEM:
                return draw_button(*reinterpret_cast<DRAWITEMSTRUCT const*>(lp))?TRUE:FALSE;
            case WM_CTLCOLOREDIT: {
                auto hdc=reinterpret_cast<HDC>(wp);
                SetBkColor(hdc,RGB(248,250,253));
                SetTextColor(hdc,RGB(34,47,64));
                return reinterpret_cast<LRESULT>(edit_brush_);
            }
            case WM_CTLCOLORLISTBOX: {
                auto hdc=reinterpret_cast<HDC>(wp);
                SetBkColor(hdc,RGB(248,250,253));
                SetTextColor(hdc,RGB(34,47,64));
                return reinterpret_cast<LRESULT>(edit_brush_);
            }
            case WM_COMMAND:
                if (LOWORD(wp)==id_search&&HIWORD(wp)==EN_CHANGE) {
                    rebuild_filter();return 0;
                }
                if (HIWORD(wp)==BN_CLICKED) {on_command(LOWORD(wp));return 0;}
                break;
            case WM_LBUTTONDOWN:
                on_click(GET_X_LPARAM(lp),GET_Y_LPARAM(lp),false);return 0;
            case WM_LBUTTONDBLCLK:
                on_click(GET_X_LPARAM(lp),GET_Y_LPARAM(lp),true);return 0;
            case WM_MOUSEWHEEL: {
                int content_count=page_==Page::recent?static_cast<int>(filtered_.size()):
                    static_cast<int>(filtered_templates().size());
                int rows=(content_count+card_columns()-1)/card_columns();
                int maxscroll=std::max(0,rows*(card_h+card_gap)-(h()-270));
                scroll_=std::clamp(scroll_-(GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA)*70,0,maxscroll);
                InvalidateRect(hwnd_,nullptr,FALSE);
                return 0;
            }
            case WM_HOTKEY:
                if (wp==1) {page(Page::create);section_=0;layout();}
                if (wp==2) import();
                if (wp==3) refresh();
                return 0;
            case WM_CLOSE:DestroyWindow(hwnd_);return 0;
            case WM_DESTROY:
                UnregisterHotKey(hwnd_,1);UnregisterHotKey(hwnd_,2);UnregisterHotKey(hwnd_,3);
                if (font_) DeleteObject(font_);
                if (edit_brush_) DeleteObject(edit_brush_);
                PostQuitMessage(exit_code_);return 0;
        }
        return DefWindowProcW(hwnd_,msg,wp,lp);
    }

    HWND hwnd_ = nullptr;
    ns::Catalog catalog;
    std::vector<ns::TemplateInfo> templates;
    std::vector<ns::Project> projects_;
    std::vector<size_t> filtered_;
    std::vector<Tile> hits_;
    std::map<std::wstring,std::unique_ptr<gp::Bitmap>> thumbnails_;
    Page page_=Page::recent;
    int section_=0;
    int scroll_=0;
    bool smoke_=false;
    int exit_code_=0;
    std::string selected_template_="sandbox";
    fs::path selected_path_;
    HFONT font_=nullptr;
    HBRUSH edit_brush_=nullptr;
    ULONG_PTR token_=0;
};

} // namespace

int WINAPI WinMain(HINSTANCE app,HINSTANCE,LPSTR,int show)
{
    northstarcpp::diagnostics::install_crash_handler();
    HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    std::unique_ptr<northstarcpp::Logger> process_logger;
    try {
        bool const smoke=wcsstr(GetCommandLineW(),L"--smoke")!=nullptr;
        if (!smoke || northstarcpp::application_log_directory_overridden()) {
            try {
                auto options=northstarcpp::application_logger_options(
                    "NorthStarCpp-ProjectManager");
                process_logger=std::make_unique<northstarcpp::Logger>(options);
                process_logger->info("projects","Project Manager starting");
            } catch(std::exception const& e) {
                std::fprintf(stderr,"Project Manager logger: %s\n",e.what());
            }
        }
        Browser ui;
        auto const result=ui.run(app,show,smoke);
        if(process_logger) {
            try {
                if(result) process_logger->error("projects","Project Manager exited with error");
                else process_logger->info("projects","Project Manager closing");
                process_logger->flush();
            }
            catch(std::exception const&) {}
        }
        if (SUCCEEDED(com)) CoUninitialize();
        return result;
    } catch (std::exception const& e) {
        if(process_logger) {
            try {process_logger->fatal("projects",e.what());process_logger->flush();}
            catch(std::exception const&) {}
        }
        std::fprintf(stderr, "NorthStarCpp Project Browser: %s\n", e.what());
        std::fflush(stderr);
        if (!wcsstr(GetCommandLineW(),L"--smoke"))
            error(nullptr,e);
        if (SUCCEEDED(com)) CoUninitialize();
        return 1;
    }
}
