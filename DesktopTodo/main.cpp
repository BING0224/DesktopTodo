#include "MainWindow.h"
#include <windows.h>
#include <gdiplus.h>
#include <commctrl.h>
#include <objbase.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    // Physical pixels are converted to the WPF settings file's 96-dpi coordinates.
    HMODULE user = GetModuleHandleW(L"user32.dll");
    using SetDpi = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    auto setDpi = reinterpret_cast<SetDpi>(GetProcAddress(user, "SetProcessDpiAwarenessContext"));
    if (!setDpi || !setDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) SetProcessDPIAware();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_STANDARD_CLASSES | ICC_DATE_CLASSES}; InitCommonControlsEx(&common);
    Gdiplus::GdiplusStartupInput options;
    ULONG_PTR gdiplusToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &options, nullptr) != Gdiplus::Ok) {
        CoUninitialize(); return 1;
    }
    int result = 0;
    {
        MainWindow app(instance);
        if (!app.createAndShow()) {
            MessageBoxW(nullptr, L"无法创建 DesktopTodo 窗口。", L"DesktopTodo", MB_OK | MB_ICONERROR);
            result = 1;
        } else {
            MSG message{};
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                TranslateMessage(&message); DispatchMessageW(&message);
            }
            result = static_cast<int>(message.wParam);
        }
    }
    Gdiplus::GdiplusShutdown(gdiplusToken);
    CoUninitialize();
    return result;
}
