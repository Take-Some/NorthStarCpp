// NorthStarCpp Content Browser: project-scoped native asset navigator.
// Copyright (c) 2026 Kayla's Systems - Kayla Verner.
#include "content_browser.hpp"
#include <northstarcpp/assets.hpp>

#include <shellapi.h>
#include <algorithm>
#include <cwctype>
#include <system_error>
#include <unordered_map>
#include <cstring>
#include <chrono>
#include <memory>

namespace northstarcpp {
namespace fs = std::filesystem;
namespace {
std::wstring lower(std::wstring s) {
    for (auto& c:s) c=static_cast<wchar_t>(std::towlower(c));
    return s;
}
HWND make(HWND parent, wchar_t const* cls, wchar_t const* label,
          DWORD style, int id=0) {
    auto h=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|style,
        0,0,1,1,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr),nullptr);
    if (!h) throw std::runtime_error("Cannot create Content Browser control");
    HFONT font=static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    return h;
}
int image_index(fs::path const& path, bool directory) {
    SHFILEINFOW info{};
    DWORD const attrs=directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    auto const flags=SHGFI_SYSICONINDEX|SHGFI_LARGEICON|SHGFI_USEFILEATTRIBUTES;
    return SHGetFileInfoW(path.c_str(),attrs,&info,sizeof(info),flags) ? info.iIcon : 0;
}
bool contains(std::wstring const& haystack,std::wstring const& needle) {
    return lower(haystack).find(lower(needle))!=std::wstring::npos;
}
}
EditorContentBrowser::EditorContentBrowser(HWND parent) : parent_(parent) {
    INITCOMMONCONTROLSEX init{sizeof(init),ICC_TREEVIEW_CLASSES|ICC_LISTVIEW_CLASSES};
    if (!InitCommonControlsEx(&init))
        throw std::runtime_error("Cannot initialize Content Browser controls");
    tree_=make(parent,WC_TREEVIEWW,L"",WS_TABSTOP|WS_BORDER|TVS_HASBUTTONS|
        TVS_LINESATROOT|TVS_HASLINES|TVS_SHOWSELALWAYS,tree_id);
    files_=make(parent,WC_LISTVIEWW,L"",WS_TABSTOP|WS_BORDER|LVS_ICON|
        LVS_SINGLESEL|LVS_SHOWSELALWAYS|LVS_SHAREIMAGELISTS,files_id);
    ListView_SetExtendedListViewStyle(files_,LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);
    previews_=ImageList_Create(editor_tools::Thumbnail::size,
        editor_tools::Thumbnail::size,ILC_COLOR32|ILC_MASK,32,32);
    if (!previews_)throw std::runtime_error("Cannot initialize asset preview image list");
    ListView_SetImageList(files_,previews_,LVSIL_NORMAL);
    search_=make(parent,L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,search_id);
    SendMessageW(search_,EM_SETCUEBANNER,0,reinterpret_cast<LPARAM>(L"Search assets..."));
    categories_=make(parent,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,category_id);
    for (auto* name: {L"All Assets",L"Meshes",L"Materials",L"Textures",
                      L"Audio",L"Scenes",L"Scripts",L"Other"})
        SendMessageW(categories_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
    SendMessageW(categories_,CB_SETCURSEL,0,0);
    path_=make(parent,L"STATIC",L"Content /",SS_LEFTNOWORDWRAP);
    caption_=make(parent,L"STATIC",L"Choose a project scene to browse assets",SS_LEFTNOWORDWRAP);
    up_=make(parent,L"BUTTON",L"Up",BS_PUSHBUTTON|WS_TABSTOP,up_id);
    reload_=make(parent,L"BUTTON",L"Refresh",BS_PUSHBUTTON|WS_TABSTOP,reload_id);
    preview_worker_=std::thread([this] {thumbnail_worker();});
}
EditorContentBrowser::~EditorContentBrowser() {
    {
        std::lock_guard<std::mutex> lock(preview_mutex_);
        stop_worker_=true;
        preview_jobs_.clear();
    }
    preview_wake_.notify_all();
    if(preview_worker_.joinable())preview_worker_.join();
    if(previews_) {
        ListView_SetImageList(files_,nullptr,LVSIL_NORMAL);
        ImageList_Destroy(previews_);
    }
}
void EditorContentBrowser::layout(int x,int y,int w,int h) {
    w=std::max(180,w); h=std::max(120,h);
    int const folders=std::clamp(w/4,132,245);
    MoveWindow(up_,x,y,36,26,TRUE);
    MoveWindow(reload_,x+40,y,74,26,TRUE);
    MoveWindow(path_,x+121,y+5,std::max(1,w-125),19,TRUE);
    MoveWindow(search_,x+2,y+32,std::max(70,w-folders-158),26,TRUE);
    MoveWindow(categories_,x+std::max(70,w-154),y+32,150,400,TRUE);
    MoveWindow(tree_,x,y+64,folders,std::max(55,h-85),TRUE);
    MoveWindow(files_,x+folders+7,y+64,std::max(85,w-folders-7),std::max(55,h-85),TRUE);
    MoveWindow(caption_,x+2,y+h-19,std::max(1,w-4),18,TRUE);
}
void EditorContentBrowser::set_visible(bool visible) {
    int const mode=visible ? SW_SHOW : SW_HIDE;
    for (auto hwnd: {tree_,files_,search_,categories_,path_,caption_,up_,reload_})
        if(hwnd) ShowWindow(hwnd,mode);
}
bool EditorContentBrowser::available() const {
    std::error_code ec;
    return !root_.empty() && fs::is_directory(root_,ec);
}
bool EditorContentBrowser::within_project(fs::path const& path) const {
    if (!available())return false;
    auto const relative=path.lexically_normal();
    if (relative.is_absolute())return false;
    for(auto const& part:relative)if(part=="..")return false;
    return true;
}
void EditorContentBrowser::set_project(fs::path scene_file) {
    fs::path const next=scene_file.empty() ? fs::path{} :
        fs::absolute(scene_file).parent_path()/"assets";
    if (next==root_)return;
    root_=next;
    current_.clear();
    build_tree();
    refresh_files();
}
void EditorContentBrowser::build_tree() {
    populating_tree_=true;
    TreeView_DeleteAllItems(tree_);
    folders_.clear();
    if (!available()) {populating_tree_=false;return;}
    struct Node {fs::path path;HTREEITEM parent;unsigned depth;};
    std::vector<Node> pending{{{},TVI_ROOT,0}};
    std::size_t visited=0;
    while(!pending.empty() && visited<1500) {
        auto const node=pending.back();pending.pop_back();
        if (!within_project(node.path)) continue;
        folders_.push_back(node.path);
        auto const idx=folders_.size()-1;
        std::wstring const label=idx==0?L"Content":node.path.filename().wstring();
        TVINSERTSTRUCTW insert{};
        insert.hParent=node.parent;
        insert.hInsertAfter=TVI_SORT;
        insert.item.mask=TVIF_TEXT|TVIF_PARAM;
        insert.item.pszText=const_cast<wchar_t*>(label.c_str());
        insert.item.lParam=static_cast<LPARAM>(idx);
        auto* item=TreeView_InsertItem(tree_,&insert);
        ++visited;
        if (!item || node.depth>=9)continue;
        std::error_code ec;
        auto const folder=root_/node.path;
        std::vector<fs::path> children;
        for (fs::directory_iterator it(folder,fs::directory_options::skip_permission_denied,ec),end;
             it!=end && !ec;it.increment(ec)) {
            if (it->is_directory(ec) && !it->is_symlink(ec))
                children.push_back(node.path/it->path().filename());
            if (children.size()>800)break;
        }
        std::sort(children.begin(),children.end());
        for(auto it=children.rbegin();it!=children.rend();++it)
            pending.push_back({*it,item,node.depth+1});
        if (idx==0)TreeView_Expand(tree_,item,TVE_EXPAND);
        if (idx==0)TreeView_SelectItem(tree_,item);
    }
    populating_tree_=false;
}
bool EditorContentBrowser::accepts(fs::path const& p) const {
    auto const ext=lower(p.extension().wstring());
    auto const category=SendMessageW(categories_,CB_GETCURSEL,0,0);
    if (category<=0)return true;
    if (category==1)return (ext==L".ydd" || ext==L".ydr") || ext==L".mesh" || ext==L".obj" ||
        ext==L".fbx" || ext==L".gltf" || ext==L".glb";
    if (category==2)return ext==L".ymat" || ext==L".material" || ext==L".mtl";
    if (category==3)return ext==L".ytd" || ext==L".png" || ext==L".jpg" ||
        ext==L".dds" || ext==L".tga" || ext==L".webp";
    if (category==4)return ext==L".wav" || ext==L".ogg" || ext==L".mp3";
    if (category==5)return ext==L".nscn" || ext==L".nscene" || ext==L".nprefab" || ext==L".json" ||
        ext==L".ymap";
    if (category==6)return ext==L".lua" || ext==L".js" || ext==L".mjs" ||
        ext==L".ysc" || ext==L".py";
    static wchar_t const* known[]={L".ydd",L".ydr",L".mesh",L".obj",L".fbx",L".gltf",
        L".glb",L".ymat",L".material",L".mtl",L".ytd",L".png",L".jpg",
        L".dds",L".tga",L".webp",L".wav",L".ogg",L".mp3",L".nscn",L".nscene",
        L".json",L".ymap",L".nprefab",L".lua",L".js",L".mjs",L".ysc",L".py"};
    return std::none_of(std::begin(known),std::end(known),
        [&](auto* e){return ext==e;});
}
void EditorContentBrowser::refresh_files() {
    ListView_DeleteAllItems(files_);
    entries_.clear();
    ImageList_RemoveAll(previews_);
    icon_indices_.clear();
    {
        std::lock_guard<std::mutex> lock(preview_mutex_);
        ++generation_;
        preview_jobs_.clear();
        preview_results_.clear();
    }
    if (!available()) {
        SetWindowTextW(path_,L"Content / (no project assets folder)");
        SetWindowTextW(caption_,L"Save a scene in a project with an assets directory");
        return;
    }
    if (!within_project(current_))current_.clear();
    auto folder=root_/current_;
    if (!fs::is_directory(folder)) {current_.clear();folder=root_;}
    auto const crumb=L"Content / "+current_.generic_wstring();
    SetWindowTextW(path_,crumb.c_str());
    auto const filter=lower(std::wstring([&]{
        int length=GetWindowTextLengthW(search_);
        std::wstring s(static_cast<std::size_t>(length)+1,L'\0');
        s.resize(GetWindowTextW(search_,s.data(),length+1));return s;}()));
    std::error_code ec;
    for (fs::directory_iterator it(folder,fs::directory_options::skip_permission_denied,ec),end;
         it!=end && !ec && entries_.size()<4000;it.increment(ec)) {
        auto const name=it->path().filename();
        if (name.empty() || name==".")continue;
        bool const is_folder=it->is_directory(ec);
        if (ec) {ec.clear();continue;}
        if (it->is_symlink(ec))continue;
        if (!is_folder && !accepts(name))continue;
        if (!filter.empty() && !contains(name.wstring(),filter))continue;
        entries_.push_back({current_/name,is_folder,name.wstring()});
    }
    std::sort(entries_.begin(),entries_.end(),[](Entry const& a,Entry const& b) {
        if (a.folder!=b.folder)return a.folder>b.folder;
        return lower(a.label)<lower(b.label);
    });
    for (std::size_t i=0;i<entries_.size();++i) {
        auto const& entry=entries_[i];
        LVITEMW row{};
        row.mask=LVIF_TEXT|LVIF_IMAGE|LVIF_PARAM;
        row.iItem=static_cast<int>(i);
        row.pszText=const_cast<wchar_t*>(entry.label.c_str());
        row.iImage=fallback_icon(root_/entry.relative,entry.folder);
        row.lParam=static_cast<LPARAM>(i);
        ListView_InsertItem(files_,&row);
    }
    queue_thumbnails();
    SetWindowTextW(caption_,(std::to_wstring(entries_.size())+
        L" items  |  Double-click folders to navigate  |  Select an asset to bind").c_str());
}
int EditorContentBrowser::fallback_icon(fs::path const& path,bool folder) {
    auto const ext=folder?std::wstring(L"<folder>"):lower(path.extension().wstring());
    auto it=icon_indices_.find(ext);
    if(it!=icon_indices_.end())return it->second;
    SHFILEINFOW info{};
    DWORD const flags=SHGFI_ICON|SHGFI_LARGEICON|SHGFI_USEFILEATTRIBUTES;
    DWORD const attrs=folder?FILE_ATTRIBUTE_DIRECTORY:FILE_ATTRIBUTE_NORMAL;
    int index=0;
    if(SHGetFileInfoW(path.c_str(),attrs,&info,sizeof(info),flags)) {
        index=ImageList_AddIcon(previews_,info.hIcon);
        if(info.hIcon)DestroyIcon(info.hIcon);
    }
    if(index<0)index=0;
    icon_indices_[ext]=index;
    return index;
}
void EditorContentBrowser::queue_thumbnails() {
    if(!available())return;
    std::lock_guard<std::mutex> lock(preview_mutex_);
    std::size_t queued=0;
    for(auto const& entry:entries_) {
        if(entry.folder)continue;
        auto const ext=lower(entry.relative.extension().wstring());
        if(ext!=L".ytd" && ext!=L".ydd" && ext!=L".ydr" && ext!=L".ymat")continue;
        preview_jobs_.push_back({generation_,root_,entry.relative});
        if(++queued>=300)break;
    }
    preview_wake_.notify_one();
}
void EditorContentBrowser::thumbnail_worker() {
    std::unique_ptr<AssetManager> assets;
    fs::path mounted_root;
    while(true) {
        ThumbnailJob job;
        {
            std::unique_lock<std::mutex> lock(preview_mutex_);
            preview_wake_.wait(lock,[&]{return stop_worker_ || !preview_jobs_.empty();});
            if(stop_worker_)break;
            job=std::move(preview_jobs_.front());
            preview_jobs_.pop_front();
            if(job.generation!=generation_)continue;
        }
        ThumbnailResult result;
        result.generation=job.generation;
        result.relative=job.relative;
        try {
            std::error_code ec;
            auto const file=job.root/job.relative;
            if(!fs::is_regular_file(file,ec) ||
                fs::file_size(file,ec)>128u*1024u*1024u)continue;
            if(!assets || mounted_root!=job.root) {
                assets=std::make_unique<AssetManager>();
                assets->mount_content_assets(job.root);
                mounted_root=job.root;
            }
            auto const key=job.relative.generic_u8string();
            auto const ext=lower(job.relative.extension().wstring());
            if(ext==L".ytd") {
                auto textures=shared_assets::load_textures(*assets,key);
                if(!textures.empty())
                    result.image=editor_tools::thumbnail_texture(textures.front());
            } else if(ext==L".ydd" || ext==L".ydr") {
                auto models=shared_assets::load_drawables(*assets,key);
                if(!models.empty())
                    result.image=editor_tools::thumbnail_drawable(models.front());
            } else if(ext==L".ymat") {
                auto materials=shared_assets::load_materials(*assets,key);
                if(!materials.empty()) {
                    auto const& m=materials.front();
                    result.image=editor_tools::thumbnail_material(m);
                    for(auto const& tex:m.textures)
                        if(tex.slot=="base_color" || tex.slot=="albedo") {
                            try {
                                auto decoded=assets->load_texture(tex.reference);
                                auto thumb=editor_tools::thumbnail_texture(decoded);
                                if(thumb.valid())result.image=std::move(thumb);
                            } catch(std::exception const&) {}
                            break;
                        }
                }
            }
        } catch(std::exception const&) {
            // A malformed or unavailable asset retains its native file icon.
        }
        if(result.image.valid()) {
            std::lock_guard<std::mutex> lock(preview_mutex_);
            if(!stop_worker_ && result.generation==generation_)
                preview_results_.push_back(std::move(result));
        }
    }
}
void EditorContentBrowser::poll_thumbnails() {
    std::deque<ThumbnailResult> ready;
    {
        std::lock_guard<std::mutex> lock(preview_mutex_);
        for(int i=0;i<4&&!preview_results_.empty();++i) {
            ready.push_back(std::move(preview_results_.front()));
            preview_results_.pop_front();
        }
    }
    for(auto& result:ready) {
        if(result.generation!=generation_ || !result.image.valid())continue;
        auto found=std::find_if(entries_.begin(),entries_.end(),
            [&](Entry const& e){return e.relative==result.relative;});
        if(found==entries_.end())continue;
        BITMAPINFO info{};
        info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=editor_tools::Thumbnail::size;
        info.bmiHeader.biHeight=-editor_tools::Thumbnail::size;
        info.bmiHeader.biPlanes=1;
        info.bmiHeader.biBitCount=32;
        info.bmiHeader.biCompression=BI_RGB;
        void* pixels=nullptr;
        HDC context=GetDC(parent_);
        HBITMAP bitmap=CreateDIBSection(context,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
        ReleaseDC(parent_,context);
        if(!bitmap||!pixels) {
            if(bitmap)DeleteObject(bitmap);
            continue;
        }
        std::memcpy(pixels,result.image.bgra.data(),
            result.image.bgra.size()*sizeof(std::uint32_t));
        int const preview_index=ImageList_Add(previews_,bitmap,nullptr);
        DeleteObject(bitmap);
        if(preview_index<0)continue;
        LVITEMW item{};
        item.mask=LVIF_IMAGE;
        item.iItem=static_cast<int>(found-entries_.begin());
        item.iImage=preview_index;
        ListView_SetItem(files_,&item);
    }
}
void EditorContentBrowser::enter(fs::path relative) {
    if (!within_project(relative))return;
    std::error_code ec;
    if (!fs::is_directory(root_/relative,ec))return;
    current_=relative;
    refresh_files();
}
std::optional<fs::path> EditorContentBrowser::selected_relative() const {
    int const selected=ListView_GetNextItem(files_,-1,LVNI_SELECTED);
    if (selected<0)return {};
    LVITEMW item{};
    item.mask=LVIF_PARAM;item.iItem=selected;
    if (!ListView_GetItem(files_,&item) ||
        item.lParam<0 || static_cast<std::size_t>(item.lParam)>=entries_.size())return {};
    return entries_[static_cast<std::size_t>(item.lParam)].relative;
}
std::optional<std::string> EditorContentBrowser::selected_asset() const {
    auto relative=selected_relative();
    if (!relative || !within_project(*relative))return {};
    std::error_code ec;
    if (!fs::is_regular_file(root_/(*relative),ec))return {};
    return relative->generic_u8string();
}
bool EditorContentBrowser::on_notify(NMHDR const* hdr) {
    if (!hdr)return false;
    if (hdr->hwndFrom==tree_ && hdr->code==TVN_SELCHANGEDW) {
        if (populating_tree_)return true;
        auto* event=reinterpret_cast<NMTREEVIEWW const*>(hdr);
        auto const idx=event->itemNew.lParam;
        if (idx>=0 && static_cast<std::size_t>(idx)<folders_.size())
            enter(folders_[static_cast<std::size_t>(idx)]);
        return true;
    }
    if (hdr->hwndFrom==files_) {
        if (hdr->code==LVN_BEGINDRAG) {
            auto const* item=reinterpret_cast<NMLISTVIEW const*>(hdr);
            if (item->iItem>=0) {
                ListView_SetItemState(files_,item->iItem,LVIS_SELECTED,LVIS_SELECTED);
                drag_asset_=selected_asset();
                dragging_=drag_asset_.has_value();
                if (dragging_) {
                    SetCapture(parent_);
                    SetCursor(LoadCursorW(nullptr,IDC_HAND));
                }
            }
            return true;
        }
        if (hdr->code==NM_DBLCLK || hdr->code==NM_RETURN) {
            auto const selected=selected_relative();
            if (!selected)return true;
            std::error_code ec;
            if (fs::is_directory(root_/(*selected),ec))enter(*selected);
            else if (fs::is_regular_file(root_/(*selected),ec))
                ShellExecuteW(parent_,L"open",(root_/(*selected)).c_str(),
                    nullptr,nullptr,SW_SHOWNORMAL);
            return true;
        }
        if (hdr->code==LVN_ITEMCHANGED) {
            auto selected=selected_asset();
            if (selected) {
                auto const title=std::wstring(L"Asset: ")+fs::u8path(*selected).generic_wstring();
                SetWindowTextW(caption_,title.c_str());
            }
            return true;
        }
    }
    return false;
}
bool EditorContentBrowser::on_command(int id,int code) {
    if (id==search_id && code==EN_CHANGE) {refresh_files();return true;}
    if (id==category_id && code==CBN_SELCHANGE) {refresh_files();return true;}
    if (id==up_id && code==BN_CLICKED) {
        if (!current_.empty())enter(current_.parent_path());return true;
    }
    if (id==reload_id && code==BN_CLICKED) {build_tree();refresh_files();return true;}
    return false;
}
std::optional<std::string> EditorContentBrowser::finish_drag() {
    if (!dragging_)return {};
    dragging_=false;
    if (GetCapture()==parent_)ReleaseCapture();
    auto result=std::move(drag_asset_);
    drag_asset_.reset();
    return result;
}
void EditorContentBrowser::cancel_drag() noexcept {
    dragging_=false;
    drag_asset_.reset();
    if (GetCapture()==parent_)ReleaseCapture();
}
void EditorContentBrowser::refresh() {build_tree();refresh_files();}
void EditorContentBrowser::show_selected_in_explorer() const {
    auto const selected=selected_relative();
    if (!available())return;
    fs::path const path=(selected && within_project(*selected)) ?
        root_/(*selected) : root_/current_;
    ShellExecuteW(parent_,L"open",fs::is_directory(path)?path.c_str():
        path.parent_path().c_str(),nullptr,nullptr,SW_SHOWNORMAL);
}
} // namespace northstarcpp
