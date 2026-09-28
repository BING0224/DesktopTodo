#pragma once
#include "Store.h"
#include <windows.h>
#include <string>
#include <vector>

namespace ResourceUI {
// Results are posted to the owner after the popup closes; opening never runs a modal loop.
constexpr UINT menuResult = WM_APP + 5;
constexpr UINT editorResult = WM_APP + 6;
constexpr UINT confirmResult = WM_APP + 7;

struct MenuItem {
    std::wstring label;
    UINT command = 0;
    bool separator = false;
    bool danger = false;
};

class Menu {
    HWND hwnd_ = nullptr, owner_ = nullptr;
    HINSTANCE instance_ = nullptr;
    float scale_ = 1, width_ = 244, height_ = 0, contentHeight_ = 0, scroll_ = 0;
    int hover_ = -1;
    bool closed_ = false;
    std::vector<MenuItem> items_;
    std::vector<float> tops_, heights_;
    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT message(UINT, WPARAM, LPARAM);
    int hit(float x, float y) const;
    void draw();
    void finish(UINT command);
public:
    ~Menu();
    bool show(HWND owner, HINSTANCE instance, float scale, std::vector<MenuItem> items, POINT anchor);
    void dismiss() { finish(0); }
    bool key(UINT key);
};

class Editor {
    HWND hwnd_ = nullptr, owner_ = nullptr, name_ = nullptr, target_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HBRUSH brush_ = nullptr;
    HFONT font_ = nullptr;
    float scale_ = 1, width_ = 366;
    bool finished_ = false, hoverSave_ = false, hoverCancel_ = false;
    Resource value_;
    std::wstring error_;
    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT message(UINT, WPARAM, LPARAM);
    void draw();
    void finish(bool save);
    void save();
public:
    ~Editor();
    bool show(HWND owner, HINSTANCE instance, float scale, Resource resource);
    void dismiss() { finish(false); }
    const Resource& selection() const { return value_; }
};

class Confirm {
    HWND hwnd_ = nullptr, owner_ = nullptr;
    float scale_ = 1;
    bool resource_ = false, finished_ = false;
    bool hoverCancel_ = false, hoverConfirm_ = false, hoverClose_ = false;
    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT message(UINT, WPARAM, LPARAM);
    void draw();
    void finish(bool accepted);
public:
    ~Confirm();
    bool show(HWND owner, HINSTANCE instance, float scale, bool resource, POINT anchor);
    void dismiss() { finish(false); }
};
}
