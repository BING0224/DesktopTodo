#include "DesktopHost.h"
#include <cwchar>

static HWND currentWindow = nullptr;

static BOOL CALLBACK FindHost(HWND candidate, LPARAM result) {
    HWND shell = GetShellWindow();
    DWORD shellPid = 0, candidatePid = 0;
    GetWindowThreadProcessId(shell, &shellPid);
    GetWindowThreadProcessId(candidate, &candidatePid);
    if (shellPid == candidatePid && FindWindowExW(candidate, nullptr, L"SHELLDLL_DefView", nullptr)) {
        *reinterpret_cast<HWND*>(result) = candidate;
        return FALSE;
    }
    return TRUE;
}

static HWND DesktopWindow(HWND shell) {
    if (FindWindowExW(shell, nullptr, L"SHELLDLL_DefView", nullptr)) return shell;
    HWND host = nullptr;
    EnumWindows(FindHost, reinterpret_cast<LPARAM>(&host));
    return host ? host : shell;
}

static bool ShellIsForeground(HWND shell, HWND desktop) {
    HWND foreground = GetForegroundWindow();
    if (!foreground) return false;
    HWND root = GetAncestor(foreground, GA_ROOT);
    if (root == shell || root == desktop) return true;
    DWORD rootPid = 0, shellPid = 0;
    GetWindowThreadProcessId(root, &rootPid);
    GetWindowThreadProcessId(shell, &shellPid);
    if (rootPid != shellPid) return false;
    wchar_t name[32]{}; GetClassNameW(root, name, 32);
    return wcscmp(name, L"WorkerW") == 0 || wcscmp(name, L"Progman") == 0;
}

DesktopHost::DesktopHost(HWND window) : window_(window) {
    currentWindow = window;
    hook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                            nullptr, ForegroundEvent, 0, 0,
                            WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
}

DesktopHost::~DesktopHost() {
    if (hook_) UnhookWinEvent(hook_);
    if (currentWindow == window_) currentWindow = nullptr;
}

void CALLBACK DesktopHost::ForegroundEvent(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    if (currentWindow && IsWindow(currentWindow)) PostMessageW(currentWindow, changedMessage, 0, 0);
}

bool DesktopHost::isDesktopForeground() const {
    HWND shell = GetShellWindow();
    return shell && ShellIsForeground(shell, DesktopWindow(shell));
}

void DesktopHost::arrange(bool includeOwnWindow) {
    HWND shell = GetShellWindow();
    if (!shell || !window_) return;
    HWND foreground = GetForegroundWindow();
    if (!includeOwnWindow && foreground) {
        DWORD fgPid = 0; GetWindowThreadProcessId(foreground, &fgPid);
        if (fgPid == GetCurrentProcessId()) return;
    }
    HWND desktop = DesktopWindow(shell);
    if (IsIconic(window_)) ShowWindow(window_, SW_SHOWNOACTIVATE);
    constexpr UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOOWNERZORDER;
    if (ShellIsForeground(shell, desktop)) {
        SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0, flags);
        return;
    }
    SetWindowPos(window_, HWND_NOTOPMOST, 0, 0, 0, 0, flags);
    HWND aboveDesktop = GetWindow(desktop, GW_HWNDPREV);
    if (aboveDesktop && aboveDesktop != window_) SetWindowPos(window_, aboveDesktop, 0, 0, 0, 0, flags);
}
