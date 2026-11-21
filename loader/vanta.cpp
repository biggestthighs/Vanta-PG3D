#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "injector.h"
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace {
constexpr UINT Done = WM_APP + 1;
constexpr int Width = 660, Height = 482;
constexpr int LoadId = 1, UpdateId = 3, FolderId = 2;
constexpr DWORD WindowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                              WS_CLIPCHILDREN;
constexpr COLORREF Background = RGB(16, 13, 26);
constexpr COLORREF Panel = RGB(30, 24, 47);
constexpr COLORREF Frame = RGB(62, 48, 89);
constexpr COLORREF Text = RGB(247, 240, 255);
constexpr COLORREF Muted = RGB(188, 175, 212);
constexpr COLORREF Lavender = RGB(199, 177, 255);
constexpr COLORREF Mint = RGB(146, 244, 215);
constexpr COLORREF Warning = RGB(252, 190, 148);
constexpr wchar_t ClassName[] = L"VantaLauncher";

enum class Status { Ready, Loading, Success, Error };
Status status = Status::Ready;
bool busy = false;
UINT dpi = 96;
HWND statusHeading, statusText, loadButton, updateButton, folderButton;
HFONT titleFont, bodyFont, boldFont, smallFont;
HBRUSH backgroundBrush, panelBrush;

int Scale(int pixels) { return MulDiv(pixels, dpi, 96); }

RECT Area(int x, int y, int w, int h) {
    return {Scale(x), Scale(y), Scale(x + w), Scale(y + h)};
}

HFONT Font(int size, int weight) {
    return CreateFontW(-Scale(size), 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

void FillRounded(HDC dc, RECT bounds, int radius, COLORREF fill, COLORREF outline) {
    auto brush = CreateSolidBrush(fill);
    auto pen = CreatePen(PS_SOLID, 1, outline);
    const auto oldBrush = SelectObject(dc, brush);
    const auto oldPen = SelectObject(dc, pen);
    RoundRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom, Scale(radius*2), Scale(radius*2));
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(brush);
    DeleteObject(pen);
}

void Write(HDC dc, RECT rect, const wchar_t* text, HFONT font, COLORREF color, UINT format) {
    const auto previous = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &rect, format | DT_NOPREFIX);
    SelectObject(dc, previous);
}

struct Rgb { float r, g, b; };

Rgb Mix(Rgb a, Rgb b, float f) {
    f = std::clamp(f, 0.f, 1.f);
    return {std::lerp(a.r,b.r,f), std::lerp(a.g,b.g,f), std::lerp(a.b,b.b,f)};
}

float Sample(unsigned value) {
    value ^= value >> 16; value *= 0x7feb352du;
    value ^= value >> 15; value *= 0x846ca68bu; value ^= value >> 16;
    return static_cast<float>(value & 0xffffffu) / 16777216.f;
}

void DrawAurora(HDC dc) {
    constexpr int artWidth = 612, artHeight = 178;
    // Generated once so repainting or moving the window doesn't regenerate the artwork.
    static const auto pixels = [] {
        constexpr float pi = 3.14159265f;
        std::vector<uint32_t> result(artWidth * artHeight);
        struct Star { int x, y; float brightness; };
        std::vector<Star> stars;
        for (unsigned i=1; i<=220; ++i)
            stars.push_back({int(Sample(i*3)*artWidth), int(Sample(i*11)*artHeight*.78f),
                             .21f + .64f*Sample(i*7)});
        std::vector<float> starlight(artWidth * artHeight);
        for (const auto& star : stars) {
            starlight[star.y*artWidth+star.x] = star.brightness;
            if (star.brightness > .67f && star.x+1<artWidth)
                starlight[star.y*artWidth+star.x+1] = star.brightness*.42f;
        }
        for (int x=0; x<artWidth; ++x) {
            const float u = float(x)/artWidth;
            const float ribbon = .49f + .14f*std::sin(u*9.f) + .06f*std::sin(u*24.f);
            const float folds = .5f + .5f*std::sin(u*92.f + 1.5f*std::sin(u*23.f));
            for (int y=0; y<artHeight; ++y) {
                const float v = float(y)/artHeight;
                Rgb color = Mix({11,13,34}, {80,49,117}, v*v);
                const float dist = (v-ribbon)*9.f;
                const float glow = std::exp(-dist*dist)*(.18f+.17f*folds);
                color = Mix(color,{68,205,174},glow*(.28f+.72f*u));
                const float dx = (u-.81f)*artWidth, dy = (v-.37f)*artHeight;
                const float moon = std::sqrt(dx*dx+dy*dy);
                color = Mix(color,{121,224,211},.24f*std::exp(-moon*moon/1400.f));
                color = Mix(color,{205,242,230},std::clamp(19.f-moon,0.f,1.f)*.94f);
                color = Mix(color,{221,226,255},starlight[y*artWidth+x]);
                for (int layer=0; layer<3; ++layer) {
                    const float edge = .72f + .092f*layer +
                        .057f*std::sin(u*13.f+layer) + .025f*std::sin(u*29.f+pi*layer*.4f);
                    if (v>edge) color = Mix(color, Mix({47,37,80},{22,18,42},layer*.47f), .91f);
                }
                color = Mix({20,16,36},color,std::clamp((u-.15f)*1.48f,0.f,1.f));
                result[y*artWidth+x] = (uint32_t(color.r)<<16)|(uint32_t(color.g)<<8)|uint32_t(color.b);
            }
        }
        return result;
    }();
    const auto bounds = Area(24, 22, artWidth, artHeight);
    auto clip = CreateRoundRectRgn(bounds.left,bounds.top,bounds.right,bounds.bottom,Scale(28),Scale(28));
    const int saved = SaveDC(dc);
    ExtSelectClipRgn(dc,clip,RGN_AND);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = artWidth;
    info.bmiHeader.biHeight = -artHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(dc, HALFTONE);
    StretchDIBits(dc,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,
        0,0,artWidth,artHeight,pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);
    RestoreDC(dc,saved);
    DeleteObject(clip);
}

void DrawShell(HWND window, HDC dc) {
    RECT client{};
    GetClientRect(window, &client);
    FillRect(dc, &client, backgroundBrush);
    DrawAurora(dc);
    FillRounded(dc, Area(44, 40, 31, 31), 9, RGB(89,63,136), RGB(145,107,207));
    Write(dc,Area(44,40,31,31),L"V",boldFont,Text,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    Write(dc,Area(86,42,235,25),L"PIXEL GUN 3D",smallFont,Lavender,DT_VCENTER|DT_SINGLELINE);
    Write(dc,Area(44,83,317,73),L"Vanta",titleFont,Text,DT_VCENTER|DT_SINGLELINE);
    FillRounded(dc,Area(504,40,110,27),10,RGB(37,27,58),RGB(100,75,141));
    Write(dc,Area(504,40,110,27),L"v" VANTA_VERSION,smallFont,Text,DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    FillRounded(dc,Area(24,219,612,106),12,Panel,Frame);
    COLORREF indicator = Lavender;
    if (status == Status::Success) indicator = Mint;
    if (status == Status::Error) indicator = Warning;
    FillRounded(dc,Area(43,238,9,9),4,indicator,indicator);

    auto divider = Area(24,416,612,1);
    auto brush = CreateSolidBrush(Frame);
    FillRect(dc,&divider,brush);
    DeleteObject(brush);
    const wchar_t* keys[] = {L"INSERT", L"DELETE", L"END"};
    const wchar_t* captions[] = {L"Menu", L"Panic", L"Unload"};
    for (int i=0; i<3; ++i) {
        const int x = 24+i*158;
        FillRounded(dc,Area(x,435,66,25),6,Panel,Frame);
        Write(dc,Area(x,435,66,25),keys[i],smallFont,Lavender,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        Write(dc,Area(x+76,435,72,25),captions[i],smallFont,Muted,DT_VCENTER|DT_SINGLELINE);
    }
}

void DrawButton(const DRAWITEMSTRUCT& item) {
    const bool primary = item.CtlID == LoadId;
    const bool inactive = item.itemState & ODS_DISABLED;
    const bool pressed = item.itemState & ODS_SELECTED;
    const bool focused = item.itemState & ODS_FOCUS;
    const bool hot = item.itemState & ODS_HOTLIGHT;
    COLORREF fill = primary ? RGB(204,181,255) : RGB(37,29,56);
    COLORREF border = primary ? RGB(204,181,255) : Frame;
    COLORREF ink = primary ? RGB(30,19,49) : Text;
    if (hot) fill = primary ? RGB(223,205,255) : RGB(57,42,83);
    if (pressed) fill = primary ? RGB(178,151,238) : RGB(66,49,94);
    if (inactive) {
        fill = RGB(33,27,47);
        border = Frame;
        ink = Muted;
    }
    FillRect(item.hDC,&item.rcItem,backgroundBrush);
    FillRounded(item.hDC,item.rcItem,10,fill,focused && !inactive ? Mint : border);
    wchar_t label[96]{};
    GetWindowTextW(item.hwndItem,label,96);
    Write(item.hDC,item.rcItem,label,boldFont,ink,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    if (focused && !(item.itemState & ODS_NOFOCUSRECT)) {
        auto focus = item.rcItem;
        InflateRect(&focus,-Scale(5),-Scale(5));
        DrawFocusRect(item.hDC,&focus);
    }
}

void Layout() {
    if (titleFont) DeleteObject(titleFont);
    if (bodyFont) DeleteObject(bodyFont);
    if (boldFont) DeleteObject(boldFont);
    if (smallFont) DeleteObject(smallFont);
    titleFont = Font(51, FW_BOLD);
    bodyFont = Font(16, FW_NORMAL);
    boldFont = Font(16, FW_SEMIBOLD);
    smallFont = Font(13, FW_SEMIBOLD);
    auto place = [](HWND control, int x,int y,int w,int h,HFONT font) {
        const auto rc = Area(x,y,w,h);
        SetWindowPos(control,nullptr,rc.left,rc.top,rc.right-rc.left,rc.bottom-rc.top,SWP_NOZORDER|SWP_NOACTIVATE);
        SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    };
    place(statusHeading,63,230,546,26,boldFont);
    place(statusText,43,264,570,54,bodyFont);
    place(loadButton,24,344,297,51,boldFont);
    place(updateButton,333,344,146,51,boldFont);
    place(folderButton,491,344,145,51,boldFont);
}

void SetStatus(HWND window, Status value, const wchar_t* heading, const wchar_t* body) {
    status = value;
    SetWindowTextW(statusHeading,heading);
    SetWindowTextW(statusText,body);
    InvalidateRect(window,nullptr,FALSE);
}

void SetBusy(bool value) {
    busy = value;
    EnableWindow(loadButton,!busy);
    EnableWindow(updateButton,!busy);
    SetWindowTextW(loadButton,busy ? L"Loading..." : L"Load Vanta");
}

void Load(HWND window) {
    SetBusy(true);
    SetStatus(window,Status::Loading,L"Loading Vanta",L"Keep the game open while Vanta loads.");
    try {
        std::thread([window] {
            auto result = std::make_unique<vanta::Result>();
            try { *result = vanta::Inject(vanta::Folder()/L"pg3d_core.dll"); }
            catch (...) { *result = {false,L"Could not read the package. Extract it into a writable folder and try again."}; }
            if (PostMessageW(window,Done,0,reinterpret_cast<LPARAM>(result.get()))) result.release();
        }).detach();
    } catch (...) {
        SetBusy(false);
        SetStatus(window,Status::Error,L"Load unavailable",L"Could not start the loading task. Try again.");
    }
}

void Update(HWND window) {
    if (const auto pid = vanta::FindGame(); pid && vanta::HasCore(pid)) {
        SetStatus(window,Status::Error,L"Unload Vanta first",L"Press END in the game, then click Update Vanta again.");
        return;
    }
    const auto folder = vanta::Folder();
    const auto script = folder/L"update.ps1";
    std::error_code fileError;
    if (!std::filesystem::is_regular_file(script,fileError)) {
        SetStatus(window,Status::Error,L"Updater missing",L"Extract the complete Vanta package and try again.");
        return;
    }
    wchar_t system[MAX_PATH]{};
    GetSystemDirectoryW(system,MAX_PATH);
    const std::wstring powershell = std::wstring(system)+L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    std::wstring command = L"\""+powershell+L"\" -NoProfile -ExecutionPolicy Bypass -File \""+
        script.wstring()+L"\" -InstallDir \""+folder.wstring()+L"\" -CurrentVersion " VANTA_VERSION
        L" -LauncherPid "+std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(powershell.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,folder.c_str(),&startup,&process)) {
        SetStatus(window,Status::Error,L"Updater could not start",L"Check the folder permissions, then try again.");
        return;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    DestroyWindow(window);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_CREATE: {
        dpi = GetDpiForWindow(window);
        const auto instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window,GWLP_HINSTANCE));
        auto label = [&](const wchar_t* text) {
            return CreateWindowW(L"STATIC",text,WS_CHILD|WS_VISIBLE|SS_LEFT,
                0,0,0,0,window,nullptr,instance,nullptr);
        };
        auto button = [&](const wchar_t* text, int id) {
            return CreateWindowW(L"BUTTON",text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
                0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);
        };
        statusHeading = label(L"Ready to load");
        statusText = label(L"Wait for the game's main menu, then click Load Vanta.");
        loadButton = button(L"Load Vanta",LoadId);
        updateButton = button(L"Update Vanta",UpdateId);
        folderButton = button(L"Open folder",FolderId);
        if (!statusHeading || !statusText || !loadButton || !updateButton || !folderButton) return -1;
        Layout();
        return 0;
    }
    case WM_DPICHANGED: {
        dpi = HIWORD(wp);
        const auto* rect = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(window,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,
            SWP_NOZORDER|SWP_NOACTIVATE);
        Layout();
        InvalidateRect(window,nullptr,TRUE);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window,&paint);
        DrawShell(window,dc);
        EndPaint(window,&paint);
        return 0;
    }
    case WM_PRINTCLIENT:
        DrawShell(window,reinterpret_cast<HDC>(wp));
        return 0;
    case WM_DRAWITEM:
        DrawButton(*reinterpret_cast<const DRAWITEMSTRUCT*>(lp));
        return TRUE;
    case WM_CTLCOLORSTATIC: {
        const auto dc = reinterpret_cast<HDC>(wp);
        SetTextColor(dc,reinterpret_cast<HWND>(lp)==statusHeading ? Text : Muted);
        SetBkColor(dc,Panel);
        return reinterpret_cast<LRESULT>(panelBrush);
    }
    case WM_COMMAND:
        if (HIWORD(wp)==BN_CLICKED) {
            if (LOWORD(wp)==LoadId && !busy) Load(window);
            if (LOWORD(wp)==UpdateId && !busy) Update(window);
            if (LOWORD(wp)==FolderId &&
                reinterpret_cast<INT_PTR>(ShellExecuteW(window,L"open",vanta::Folder().c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)
                SetStatus(window,Status::Error,L"Could not open the folder",L"Open the Vanta folder from File Explorer.");
        }
        return 0;
    case Done: {
        std::unique_ptr<vanta::Result> result(reinterpret_cast<vanta::Result*>(lp));
        SetBusy(false);
        SetStatus(window,result->ok ? Status::Success : Status::Error,
            result->ok ? L"Vanta loaded" : L"Could not load Vanta",result->message.c_str());
        return 0;
    }
    case WM_CLOSE:
        if (!busy) DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        DeleteObject(titleFont);
        DeleteObject(bodyFont);
        DeleteObject(boldFont);
        DeleteObject(smallFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}

HWND CreateLauncher(HINSTANCE instance) {
    backgroundBrush = CreateSolidBrush(Background);
    panelBrush = CreateSolidBrush(Panel);
    WNDCLASSW klass{};
    klass.hInstance = instance;
    klass.lpfnWndProc = WindowProc;
    klass.lpszClassName = ClassName;
    klass.hbrBackground = backgroundBrush;
    klass.hCursor = LoadCursorW(nullptr,IDC_ARROW);
    if (!RegisterClassW(&klass)) return nullptr;
    dpi = GetDpiForSystem();
    RECT rect = Area(0,0,Width,Height);
    AdjustWindowRectExForDpi(&rect,WindowStyle,FALSE,0,dpi);
    return CreateWindowW(ClassName,L"Vanta | Pixel Gun 3D",WindowStyle,
        CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,nullptr);
}
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    int argumentCount = 0;
    auto arguments = CommandLineToArgvW(GetCommandLineW(),&argumentCount);
    const bool checkPackage = arguments && argumentCount == 2 && !wcscmp(arguments[1],L"--check-package");
    if (arguments) LocalFree(arguments);
    if (checkPackage) {
        const auto folder = vanta::Folder();
        return std::filesystem::is_regular_file(folder/L"pg3d_core.dll") &&
               std::filesystem::is_regular_file(folder/L"offsets.json") &&
               std::filesystem::is_directory(folder/L"models") ? 0 : 2;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto window = CreateLauncher(instance);
    if (!window) return 1;
    ShowWindow(window,show);
    SetFocus(loadButton);
    MSG message{};
    int result;
    while ((result = GetMessageW(&message,nullptr,0,0)) > 0) {
        if (message.message==WM_KEYDOWN && message.wParam==VK_RETURN) {
            const auto focused = GetFocus();
            if ((focused==loadButton || focused==updateButton || focused==folderButton) && IsWindowEnabled(focused)) {
                SendMessageW(focused,BM_CLICK,0,0);
                continue;
            }
        }
        if (!IsDialogMessageW(window,&message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    DeleteObject(backgroundBrush);
    DeleteObject(panelBrush);
    return result < 0 ? 1 : 0;
}
