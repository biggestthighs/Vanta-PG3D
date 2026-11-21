#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "injector.h"
#include <shellapi.h>
#include <thread>
#include <memory>

namespace {
constexpr UINT Done = WM_APP + 1;
HWND statusText, loadButton;
HFONT bodyFont, titleFont;
HBRUSH background;
bool busy = false;
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_CREATE: {
        bodyFont = CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        titleFont = CreateFontW(-36,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        auto label = [&](const wchar_t* text, int x, int y, int width, int height, HFONT font) {
            HWND control = CreateWindowW(L"STATIC",text,WS_CHILD|WS_VISIBLE,x,y,width,height,window,nullptr,nullptr,nullptr);
            SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE); return control;
        };
        label(L"VANTA",28,23,430,48,titleFont);
        label(L"PIXEL GUN 3D  /  v0.1.5 preview",30,78,450,26,bodyFont);
        label(L"1. Start the game and wait for the main menu.\n2. Load Vanta, then return to the game.\n3. Press INSERT to open your controls.",30,126,470,80,bodyFont);
        statusText = label(L"Ready. Your settings are saved beside the DLL.",30,225,470,64,bodyFont);
        loadButton = CreateWindowW(L"BUTTON",L"Load Vanta",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,30,310,210,42,window,reinterpret_cast<HMENU>(1),nullptr,nullptr);
        HWND folder = CreateWindowW(L"BUTTON",L"Open folder",WS_CHILD|WS_VISIBLE|WS_TABSTOP,255,310,245,42,window,reinterpret_cast<HMENU>(2),nullptr,nullptr);
        SendMessageW(loadButton,WM_SETFONT,reinterpret_cast<WPARAM>(bodyFont),TRUE);
        SendMessageW(folder,WM_SETFONT,reinterpret_cast<WPARAM>(bodyFont),TRUE);
        label(L"INSERT  Menu     DELETE  Panic     END  Unload",30,380,480,28,bodyFont);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wp),RGB(220,231,237));
        SetBkColor(reinterpret_cast<HDC>(wp),RGB(17,24,32));
        return reinterpret_cast<LRESULT>(background);
    case WM_COMMAND:
        if (LOWORD(wp)==1 && !busy) {
            busy=true; EnableWindow(loadButton,FALSE); SetWindowTextW(statusText,L"Loading into the game...");
            std::thread([window] {
                auto result=std::make_unique<vanta::Result>();
                try { *result=vanta::Inject(vanta::Folder()/L"pg3d_core.dll"); }
                catch (...) { *result={false,L"Could not read the package. Extract it into a writable folder and try again."}; }
                if (PostMessageW(window,Done,0,reinterpret_cast<LPARAM>(result.get()))) result.release();
            }).detach();
        } else if (LOWORD(wp)==2) ShellExecuteW(window,L"open",vanta::Folder().c_str(),nullptr,nullptr,SW_SHOWNORMAL);
        return 0;
    case Done: {
        std::unique_ptr<vanta::Result> result(reinterpret_cast<vanta::Result*>(lp));
        SetWindowTextW(statusText,result->message.c_str());
        busy=false; EnableWindow(loadButton,TRUE); return 0;
    }
    case WM_CLOSE: if (!busy) DestroyWindow(window); return 0;
    case WM_DESTROY: DeleteObject(bodyFont); DeleteObject(titleFont); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR commandLine,int show) {
    // Non-interactive packaging smoke test; never touches the game or opens UI.
    if (commandLine && !wcscmp(commandLine, L"--check-package")) {
        const auto folder = vanta::Folder();
        return std::filesystem::is_regular_file(folder / L"pg3d_core.dll") &&
               std::filesystem::is_regular_file(folder / L"offsets.json") &&
               std::filesystem::is_directory(folder / L"models") ? 0 : 2;
    }
    SetProcessDPIAware();
    background=CreateSolidBrush(RGB(17,24,32));
    WNDCLASSW klass{}; klass.hInstance=instance; klass.lpfnWndProc=WindowProc;
    klass.lpszClassName=L"VantaLauncher"; klass.hbrBackground=background;
    klass.hCursor=LoadCursorW(nullptr,IDC_ARROW); RegisterClassW(&klass);
    RECT rect{0,0,540,440}; const DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;
    AdjustWindowRect(&rect,style,FALSE);
    HWND window=CreateWindowW(klass.lpszClassName,L"Vanta | Pixel Gun 3D",style,CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,nullptr);
    if (!window) return 1;
    ShowWindow(window,show);
    MSG message{}; while (GetMessageW(&message,nullptr,0,0)>0) {
        if (!IsDialogMessageW(window,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    DeleteObject(background); return 0;
}
