// NorthStarCpp BugTrap: independent, native Windows crash-report viewer.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include "resource_ids.h"
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace {
constexpr COLORREF background = RGB(15, 19, 27);
constexpr COLORREF surface = RGB(25, 31, 42);
constexpr COLORREF accent = RGB(249, 198, 72);
constexpr COLORREF bright = RGB(239, 242, 246);
constexpr COLORREF muted = RGB(159, 170, 184);
constexpr COLORREF danger = RGB(236, 91, 99);
constexpr int open_id = 1001, copy_id = 1002, close_id = 1003;
constexpr int minimum_width = 740, minimum_height = 530;

struct State {
    HINSTANCE instance{};
    HWND window{}, details{}, open{}, copy{}, close{};
    HFONT title_font{}, heading_font{}, body_font{}, mono_font{}, button_font{};
    HBRUSH bg_brush{}, details_brush{};
    std::wstring filename, report, code, application, description;
} state;

std::wstring read_report(std::wstring const& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return L"Unable to read the crash report.";
    LARGE_INTEGER bytes{};
    if (!GetFileSizeEx(file, &bytes) || bytes.QuadPart < 0 ||
        bytes.QuadPart > 1024 * 1024) {
        CloseHandle(file);
        return L"Crash report is too large or unavailable.";
    }
    std::string contents(static_cast<size_t>(bytes.QuadPart), '\0');
    DWORD read = 0;
    bool const ok = contents.empty() || ReadFile(file, contents.data(),
        static_cast<DWORD>(contents.size()), &read, nullptr) != FALSE;
    CloseHandle(file);
    if (!ok) return L"Unable to read the crash report.";
    contents.resize(read);
    if (contents.empty()) return L"The crash report is empty.";
    int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        contents.data(), static_cast<int>(contents.size()), nullptr, 0);
    UINT encoding = CP_UTF8;
    if (required <= 0) {
        encoding = CP_ACP;
        required = MultiByteToWideChar(encoding, 0, contents.data(),
            static_cast<int>(contents.size()), nullptr, 0);
    }
    if (required <= 0) return L"Crash report decoding failed.";
    std::wstring wide(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(encoding, 0, contents.data(),
        static_cast<int>(contents.size()), wide.data(), required);
    return wide;
}

std::wstring field(std::wstring const& report, wchar_t const* label) {
    auto const index = report.find(label);
    if (index == std::wstring::npos) return L"Not available";
    auto const start = index + wcslen(label);
    auto end = report.find_first_of(L"\r\n", start);
    if (end == std::wstring::npos) end = report.size();
    return report.substr(start, end - start);
}

std::wstring code_description(std::wstring const& code) {
    if (code == L"0xC0000005") return L"Access violation: invalid memory access";
    if (code == L"0xC00000FD") return L"Stack overflow";
    if (code == L"0xC000001D") return L"Illegal CPU instruction";
    if (code == L"0xC0000094") return L"Integer division by zero";
    if (code == L"0xC000008E") return L"Floating-point division by zero";
    if (code == L"0xC0000409") return L"Fail-fast / security check failure";
    if (code == L"0xE0000001") return L"Unhandled C++ exception or std::terminate";
    if (code == L"0xE0000002") return L"Process aborted (SIGABRT / fatal assertion)";
    return L"Unexpected fatal exception";
}

HFONT font(int size, int weight, wchar_t const* name) {
    return CreateFontW(-size, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, name);
}

void draw_text(HDC dc, std::wstring const& value, RECT rect,
               HFONT typeface, COLORREF color, UINT flags) {
    SelectObject(dc, typeface);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, value.c_str(), static_cast<int>(value.size()),
        &rect, flags | DT_NOPREFIX);
}

void fill_round(HDC dc, RECT const& bounds, int radius, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    auto old_brush = SelectObject(dc, brush);
    auto old_pen = SelectObject(dc, pen);
    RoundRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom,
              radius, radius);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void layout(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    int const width = rc.right, height = rc.bottom;
    int const buttons_y = height - 67;
    if (state.details)
        MoveWindow(state.details, 42, 265, std::max(100, width - 84),
                   std::max(80, buttons_y - 289), TRUE);
    if (state.open) MoveWindow(state.open, 42, buttons_y, 174, 39, TRUE);
    if (state.copy) MoveWindow(state.copy, 227, buttons_y, 174, 39, TRUE);
    if (state.close) MoveWindow(state.close, width - 171, buttons_y, 129, 39, TRUE);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void copy_report(HWND hwnd) {
    std::wstring const content = state.report + L"\r\nReport path: " + state.filename;
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    SIZE_T const size = (content.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (memory) {
        void* buffer = GlobalLock(memory);
        if (buffer) {
            memcpy(buffer, content.c_str(), size);
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
        } else GlobalFree(memory);
    }
    CloseClipboard();
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE: {
        state.title_font = font(16, FW_BOLD, L"Segoe UI");
        state.heading_font = font(26, FW_SEMIBOLD, L"Segoe UI");
        state.body_font = font(14, FW_NORMAL, L"Segoe UI");
        state.mono_font = font(13, FW_NORMAL, L"Consolas");
        state.button_font = font(13, FW_SEMIBOLD, L"Segoe UI");
        state.bg_brush = CreateSolidBrush(background);
        state.details_brush = CreateSolidBrush(surface);
        state.details = CreateWindowExW(0, L"EDIT", state.report.c_str(),
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE |
            ES_READONLY | ES_AUTOVSCROLL, 42, 265, 650, 200,
            hwnd, nullptr, state.instance, nullptr);
        SendMessageW(state.details, WM_SETFONT, reinterpret_cast<WPARAM>(state.mono_font), TRUE);
        SendMessageW(state.details, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                     MAKELPARAM(12, 12));
        auto make_button = [&](HWND& dest, int id, wchar_t const* text) {
            dest = CreateWindowExW(0, L"BUTTON", text,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 140, 39, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                state.instance, nullptr);
            SendMessageW(dest, WM_SETFONT, reinterpret_cast<WPARAM>(state.button_font), TRUE);
        };
        make_button(state.open, open_id, L"Open report folder");
        make_button(state.copy, copy_id, L"Copy diagnostics");
        make_button(state.close, close_id, L"Close");
        layout(hwnd);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize = POINT{minimum_width, minimum_height};
        return 0;
    }
    case WM_SIZE: layout(hwnd); return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        auto const dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(dc, bright);
        SetBkColor(dc, surface);
        return reinterpret_cast<LRESULT>(state.details_brush);
    }
    case WM_ERASEBKGND: return 1;
    case WM_DRAWITEM: {
        auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (item->CtlType != ODT_BUTTON) break;
        bool const primary = item->CtlID == open_id;
        bool const pressed = (item->itemState & ODS_SELECTED) != 0;
        COLORREF color = primary ? accent : surface;
        if (pressed) color = primary ? RGB(216, 166, 52) : RGB(43, 51, 64);
        fill_round(item->hDC, item->rcItem, 9, color);
        wchar_t caption[90]{};
        GetWindowTextW(item->hwndItem, caption, 90);
        draw_text(item->hDC, caption, item->rcItem, state.button_font,
            primary ? background : bright,
            DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        if ((item->itemState & ODS_FOCUS) != 0)
            DrawFocusRect(item->hDC, &item->rcItem);
        return TRUE;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillRect(dc, &rc, state.bg_brush);
        RECT top{0, 0, rc.right, 6};
        HBRUSH gold = CreateSolidBrush(accent);
        FillRect(dc, &top, gold);
        DeleteObject(gold);
        draw_text(dc, L"NORTHSTAR  /  BUGTRAP", RECT{42, 30, rc.right - 42, 61},
                  state.title_font, accent, DT_LEFT | DT_SINGLELINE);
        draw_text(dc, L"The engine has stopped unexpectedly",
                  RECT{42, 73, rc.right - 42, 115}, state.heading_font,
                  bright, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        draw_text(dc, L"A diagnostic report was saved locally. No data was uploaded.",
                  RECT{42, 119, rc.right - 42, 144}, state.body_font,
                  muted, DT_LEFT | DT_SINGLELINE);
        fill_round(dc, RECT{42, 156, rc.right - 42, 222}, 10, surface);
        draw_text(dc, L"EXCEPTION", RECT{57, 167, 165, 188}, state.body_font,
                  muted, DT_SINGLELINE);
        draw_text(dc, state.code, RECT{57, 188, 202, 215}, state.mono_font,
                  danger, DT_SINGLELINE);
        draw_text(dc, L"CAUSE", RECT{222, 167, 300, 188},
                  state.body_font, muted, DT_SINGLELINE);
        draw_text(dc, state.description, RECT{222, 188, rc.right - 62, 214},
                  state.body_font, bright, DT_SINGLELINE | DT_END_ELLIPSIS);
        draw_text(dc, L"DIAGNOSTIC DETAILS", RECT{42, 236, rc.right - 42, 258},
                  state.body_font, accent, DT_SINGLELINE);
        draw_text(dc, L"Saved: " + state.filename,
                  RECT{42, rc.bottom - 28, rc.right - 42, rc.bottom - 7},
                  state.body_font, muted, DT_SINGLELINE | DT_PATH_ELLIPSIS);
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case open_id: {
            try {
                auto const folder = std::filesystem::path(state.filename).parent_path().wstring();
                ShellExecuteW(hwnd, L"explore", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            } catch (...) { /* Invalid user-supplied report path. */ }
            return 0;
        }
        case copy_id: copy_report(hwnd); return 0;
        case close_id: DestroyWindow(hwnd); return 0;
        }
        break;
    case WM_DESTROY:
        for (HFONT font_handle : {state.title_font, state.heading_font,
                                  state.body_font, state.mono_font, state.button_font})
            if (font_handle) DeleteObject(font_handle);
        if (state.bg_brush) DeleteObject(state.bg_brush);
        if (state.details_brush) DeleteObject(state.details_brush);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 2;
    bool const self_test = argc == 2 && wcscmp(argv[1], L"--self-test") == 0;
    bool silent = false;
    std::wstring path;
    if (!self_test && (argc == 3 || argc == 4) &&
        wcscmp(argv[1], L"--report") == 0) {
        path = argv[2];
        silent = argc == 4 && wcscmp(argv[3], L"--silent") == 0;
        if (argc == 4 && !silent) path.clear();
    }
    LocalFree(argv);
    if (self_test) {
        bool const icon = FindResourceW(instance,
            MAKEINTRESOURCEW(IDI_BUGTRAP_APP), RT_GROUP_ICON) != nullptr;
        bool const fileinfo = FindResourceW(instance,
            MAKEINTRESOURCEW(1), RT_VERSION) != nullptr;
        return icon && fileinfo ? 0 : 5;
    }
    if (path.empty()) return 2;
    DWORD const attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
        return 3;
    if (silent) return 0;
    state.instance = instance;
    state.filename = std::move(path);
    state.report = read_report(state.filename);
    state.code = field(state.report, L"Exception code: ");
    state.application = field(state.report, L"Application: ");
    state.description = code_description(state.code);
    WNDCLASSEXW klass{};
    klass.cbSize = sizeof(klass);
    klass.lpfnWndProc = window_proc;
    klass.hInstance = instance;
    klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    klass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_BUGTRAP_APP));
    klass.hIconSm = klass.hIcon;
    klass.hbrBackground = nullptr;
    klass.lpszClassName = L"NorthStarCppBugTrapWindow";
    if (!RegisterClassExW(&klass)) return 4;
    state.window = CreateWindowExW(0, klass.lpszClassName,
        L"NorthStarCpp - BugTrap", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 850, 600,
        nullptr, nullptr, instance, nullptr);
    if (!state.window) return 4;
    ShowWindow(state.window, show == SW_HIDE ? SW_SHOWNORMAL : show);
    SetForegroundWindow(state.window);
    UpdateWindow(state.window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(state.window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return static_cast<int>(message.wParam);
}
