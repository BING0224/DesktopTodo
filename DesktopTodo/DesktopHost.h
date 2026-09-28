#pragma once
#include <windows.h>

class DesktopHost {
    HWND window_ = nullptr;
    HWINEVENTHOOK hook_ = nullptr;
    static void CALLBACK ForegroundEvent(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
public:
    static constexpr UINT changedMessage = WM_APP + 20;
    explicit DesktopHost(HWND window);
    ~DesktopHost();
    void arrange(bool includeOwnWindow = false);
    bool isDesktopForeground() const;
};
