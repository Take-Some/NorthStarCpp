// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026
#include "project_dialog.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
namespace ns = northstarcpp::projects;

namespace northstarcpp::projects::ui {
namespace {

constexpr wchar_t class_name[] = L"NorthStarCppLightProjectProperties";
constexpr COLORREF white = RGB(255,255,255);
constexpr COLORREF ink = RGB(35,48,64);
constexpr COLORREF surface = RGB(247,249,252);

enum Id { field_name=2001, field_category, field_description,
          field_location, button_browse, button_save, button_cancel };

std::wstring content(HWND control)
{
    if (!control) return {};
    int count = GetWindowTextLengthW(control);
    std::wstring text(static_cast<size_t>(count+1),L'\0');
    GetWindowTextW(control,text.data(),count+1);
    text.resize(static_cast<size_t>(count));
    return text;
}

void put(HWND owner,int id,int x,int y,int width,int height)
{
    MoveWindow(GetDlgItem(owner,id),x,y,width,height,TRUE);
}

fs::path choose_directory(HWND owner)
{
    IFileDialog* dialog=nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog))))
        throw std::runtime_error("Windows folder picker unavailable");
    fs::path result;
    DWORD flags=0;
    if (SUCCEEDED(dialog->GetOptions(&flags))) {
        dialog->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        dialog->SetTitle(L"Choose destination for the project");
        if (dialog->Show(owner)==S_OK) {
            IShellItem* item=nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR value=nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&value))) {
                    result=fs::path(value);
                    CoTaskMemFree(value);
                }
                item->Release();
            }
        }
    }
    dialog->Release();
    return result;
}

class Editor {
public:
    Editor(HWND owner,ns::Catalog& catalog,ns::Project const& project,bool duplicate)
        : owner_(owner),catalog_(catalog),project_(project),duplicate_(duplicate) {}

    fs::path show()
    {
        WNDCLASSW cls{};
        cls.hInstance=GetModuleHandleW(nullptr);
        cls.lpfnWndProc=dispatch;
        cls.lpszClassName=class_name;
        cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        cls.hbrBackground=GetSysColorBrush(COLOR_WINDOW);
        RegisterClassW(&cls);
        RECT r{};
        GetWindowRect(owner_,&r);
        int constexpr width=620,height=555;
        int x=r.left+((r.right-r.left)-width)/2;
        int y=r.top+((r.bottom-r.top)-height)/2;
        window_=CreateWindowExW(WS_EX_DLGMODALFRAME,class_name,
            duplicate_?L"Duplicate Project":L"Project Properties",
            WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
            x,y,width,height,owner_,nullptr,GetModuleHandleW(nullptr),this);
        if (!window_) throw std::runtime_error("Cannot open native metadata editor");
        EnableWindow(owner_,FALSE);
        SetForegroundWindow(window_);
        MSG message{};
        while (IsWindow(window_)) {
            int result=GetMessageW(&message,nullptr,0,0);
            if (result<=0) break;
            if (!IsDialogMessageW(window_,&message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        EnableWindow(owner_,TRUE);
        SetForegroundWindow(owner_);
        return result_;
    }

private:
    static LRESULT CALLBACK dispatch(HWND hwnd,UINT message,WPARAM wp,LPARAM lp)
    {
        auto* self=reinterpret_cast<Editor*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if (message==WM_NCCREATE) {
            self=static_cast<Editor*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            self->window_=hwnd;
            SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(hwnd,message,wp,lp);
        try {return self->handle(message,wp,lp);}
        catch (std::exception const& e) {
            std::wstring details;
            try {details=ns::to_wide(e.what());}
            catch (...) {details=L"Unexpected metadata error";}
            MessageBoxW(hwnd,details.c_str(),L"Project Properties",MB_OK|MB_ICONERROR);
            return 0;
        }
    }

    HWND add(wchar_t const* cls,wchar_t const* initial,DWORD style,int id)
    {
        HWND c=CreateWindowExW(0,cls,initial,WS_CHILD|WS_VISIBLE|style,
            0,0,100,26,window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr),nullptr);
        if (!c) throw std::runtime_error("Cannot create project properties control");
        SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);
        return c;
    }

    void controls()
    {
        font_=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
                         DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        brush_=CreateSolidBrush(surface);
        add(L"STATIC",L"PROJECT NAME",0,0);
        add(L"EDIT",project_.name.c_str(),WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,field_name);
        add(L"STATIC",L"CATEGORY",0,0);
        add(L"EDIT",project_.category.c_str(),WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,field_category);
        add(L"STATIC",L"DESCRIPTION",0,0);
        add(L"EDIT",project_.description.c_str(),
            WS_TABSTOP|WS_BORDER|ES_MULTILINE|WS_VSCROLL|ES_AUTOVSCROLL,field_description);
        add(L"STATIC",L"PROJECT LOCATION",0,0);
        add(L"EDIT",(duplicate_?catalog_.projects_directory():
            project_.directory).c_str(),WS_BORDER|ES_AUTOHSCROLL,field_location);
        add(L"BUTTON",L"Browse",WS_TABSTOP|BS_PUSHBUTTON,button_browse);
        add(L"BUTTON",duplicate_?L"Duplicate Project":L"Save Changes",
            WS_TABSTOP|BS_DEFPUSHBUTTON,button_save);
        add(L"BUTTON",L"Cancel",WS_TABSTOP|BS_PUSHBUTTON,button_cancel);

        int const left=27,width=548;
        put(window_,field_name,left,57,width,33);
        put(window_,field_category,left,132,width,33);
        put(window_,field_description,left,207,width,100);
        put(window_,field_location,left,352,width-113,34);
        put(window_,button_browse,left+width-107,352,107,34);
        put(window_,button_save,left+width-202,438,202,43);
        put(window_,button_cancel,left,438,150,43);
        int y[4]={31,106,181,326},n=0;
        for (HWND ctrl=GetWindow(window_,GW_CHILD);ctrl;
             ctrl=GetWindow(ctrl,GW_HWNDNEXT)) {
            wchar_t cls[40]{};
            GetClassNameW(ctrl,cls,40);
            if (_wcsicmp(cls,L"Static")==0&&n<4)
                MoveWindow(ctrl,left,y[3-n++],width,22,TRUE);
        }
        if (!duplicate_) {
            EnableWindow(GetDlgItem(window_,field_location),FALSE);
            EnableWindow(GetDlgItem(window_,button_browse),FALSE);
        } else
            SetWindowTextW(GetDlgItem(window_,field_name),(project_.name+L" Copy").c_str());
    }

    void save()
    {
        auto const title=content(GetDlgItem(window_,field_name));
        auto const category=content(GetDlgItem(window_,field_category));
        auto const description=content(GetDlgItem(window_,field_description));
        if (duplicate_) {
            auto const dest=fs::path(content(GetDlgItem(window_,field_location)));
            auto copy=catalog_.duplicate(project_.directory,title,dest);
            catalog_.update_description(copy.directory,description,category);
            result_=copy.directory;
        } else {
            catalog_.rename(project_.directory,title);
            catalog_.update_description(project_.directory,description,category);
            result_=project_.directory;
        }
        DestroyWindow(window_);
    }

    LRESULT handle(UINT message,WPARAM wp,LPARAM lp)
    {
        switch (message) {
        case WM_CREATE: controls();return 0;
        case WM_CTLCOLORSTATIC:
            SetBkMode(reinterpret_cast<HDC>(wp),TRANSPARENT);
            SetTextColor(reinterpret_cast<HDC>(wp),ink);
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        case WM_CTLCOLOREDIT: {
            HDC hdc=reinterpret_cast<HDC>(wp);
            SetBkColor(hdc,surface);
            SetTextColor(hdc,ink);
            return reinterpret_cast<LRESULT>(brush_);
        }
        case WM_COMMAND:
            if (HIWORD(wp)==BN_CLICKED) {
                if (LOWORD(wp)==button_cancel) {DestroyWindow(window_);return 0;}
                if (LOWORD(wp)==button_save) {save();return 0;}
                if (LOWORD(wp)==button_browse) {
                    auto path=choose_directory(window_);
                    if (!path.empty())
                        SetWindowTextW(GetDlgItem(window_,field_location),path.c_str());
                    return 0;
                }
            }
            break;
        case WM_CLOSE:DestroyWindow(window_);return 0;
        case WM_DESTROY:
            if (font_) DeleteObject(font_);
            if (brush_) DeleteObject(brush_);
            return 0;
        }
        return DefWindowProcW(window_,message,wp,lp);
    }

    HWND owner_=nullptr,window_=nullptr;
    ns::Catalog& catalog_;
    ns::Project const& project_;
    bool duplicate_=false;
    HFONT font_=nullptr;
    HBRUSH brush_=nullptr;
    fs::path result_;
};
} // namespace

fs::path project_properties_dialog(HWND owner, Catalog& catalog, Project const& project,
                                   bool duplicate)
{
    return Editor(owner,catalog,project,duplicate).show();
}
} // namespace northstarcpp::projects::ui
