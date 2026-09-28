#include "ResourceUI.h"
#include <commctrl.h>
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>

using namespace Gdiplus;

namespace {
const wchar_t* menuClass = L"DesktopTodoResourceMenu";
const wchar_t* editorClass = L"DesktopTodoResourceEditor";
const wchar_t* confirmClass = L"DesktopTodoConfirm";
constexpr float editorHeight = 226.0f;
constexpr float confirmWidth = 278.0f, confirmHeight = 142.0f;

int pixel(float dip, float scale) { return static_cast<int>(std::lround(dip * scale)); }
Color color(BYTE r, BYTE g, BYTE b) { return Color(255, r, g, b); }

void rounded(Graphics& g, RectF rect, float radius, Color fill, Color border = Color(0, 0, 0, 0)) {
    GraphicsPath path;
    float diameter = 2 * radius;
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180, 90);
    path.AddArc(rect.GetRight() - diameter, rect.Y, diameter, diameter, 270, 90);
    path.AddArc(rect.GetRight() - diameter, rect.GetBottom() - diameter, diameter, diameter, 0, 90);
    path.AddArc(rect.X, rect.GetBottom() - diameter, diameter, diameter, 90, 90);
    path.CloseFigure();
    SolidBrush brush(fill); g.FillPath(&brush, &path);
    if (border.GetAlpha()) { Pen pen(border, 1); g.DrawPath(&pen, &path); }
}

void label(Graphics& g, const std::wstring& content, float x, float y, float w, float h,
           float size, Color ink, bool bold = false, StringAlignment align = StringAlignmentNear) {
    FontFamily family(L"Microsoft YaHei UI");
    Font font(&family, size, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    SolidBrush brush(ink);
    StringFormat format;
    format.SetAlignment(align); format.SetLineAlignment(StringAlignmentCenter);
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    format.SetFormatFlags(StringFormatFlagsNoWrap);
    g.DrawString(content.c_str(), -1, &font, RectF(x,y,w,h), &format, &brush);
}

bool registerPopup(HINSTANCE instance, const wchar_t* name, WNDPROC proc) {
    WNDCLASSEXW cls{sizeof(cls)};
    cls.hInstance = instance; cls.lpszClassName = name; cls.lpfnWndProc = proc;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    return RegisterClassExW(&cls) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

RECT workArea(POINT point) {
    MONITORINFO info{sizeof(info)};
    if (GetMonitorInfoW(MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST), &info)) return info.rcWork;
    RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0); return work;
}

std::wstring editText(HWND hwnd) {
    int size = GetWindowTextLengthW(hwnd);
    std::wstring result(static_cast<size_t>(size) + 1, L'\0');
    GetWindowTextW(hwnd, result.data(), size + 1); result.resize(size);
    size_t first = result.find_first_not_of(L" \r\n\t");
    if (first == std::wstring::npos) return L"";
    return result.substr(first, result.find_last_not_of(L" \r\n\t") - first + 1);
}
}

ResourceUI::Menu::~Menu() { if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_); }

bool ResourceUI::Menu::show(HWND owner, HINSTANCE instance, float scale,
                            std::vector<MenuItem> items, POINT anchor) {
    owner_ = owner; instance_ = instance; scale_ = scale; items_ = std::move(items);
    if (items_.empty() || !registerPopup(instance_, menuClass, WindowProc)) return false;
    float y = 8;
    for (const auto& item : items_) {
        float height = item.separator ? 9.0f : item.command ? 32.0f : 25.0f;
        tops_.push_back(y); heights_.push_back(height); y += height;
    }
    contentHeight_ = y + 8;
    RECT work = workArea(anchor);
    height_ = (std::min)(contentHeight_, (work.bottom - work.top) / scale_ - 24.0f);
    int width = pixel(width_, scale_), height = pixel(height_, scale_);
    int x = anchor.x, top = anchor.y;
    if (x + width > work.right) x -= width;
    if (top + height > work.bottom) top -= height;
    x = (std::clamp)(x, static_cast<int>(work.left), (std::max)(static_cast<int>(work.left), static_cast<int>(work.right) - width));
    top = (std::clamp)(top, static_cast<int>(work.top), (std::max)(static_cast<int>(work.top), static_cast<int>(work.bottom) - height));
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, menuClass, L"DesktopTodo 资源菜单",
                            WS_POPUP, x, top, width, height, owner_, nullptr, instance_, this);
    if (!hwnd_) return false;
    SetWindowRgn(hwnd_, CreateRoundRectRgn(0, 0, width + 1, height + 1,
                                          pixel(24, scale_), pixel(24, scale_)), TRUE);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd_);
    return true;
}

int ResourceUI::Menu::hit(float x, float y) const {
    if (x < 7 || x > width_ - 7 || y < 7 || y > height_ - 7) return -1;
    y += scroll_;
    for (size_t i = 0; i < tops_.size(); ++i)
        if (y >= tops_[i] && y < tops_[i] + heights_[i] && items_[i].command)
            return static_cast<int>(i);
    return -1;
}

void ResourceUI::Menu::draw() {
    PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd_, &ps);
    Graphics g(dc); g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias); g.ScaleTransform(scale_, scale_);
    rounded(g, RectF(0.5f, 0.5f, width_ - 1, height_ - 1), 12,
            color(249, 252, 255), color(207, 228, 241));
    GraphicsState state = g.Save();
    g.SetClip(RectF(7, 7, width_ - 14, height_ - 14));
    for (size_t i = 0; i < items_.size(); ++i) {
        const auto& item = items_[i];
        float top = tops_[i] - scroll_;
        if (top + heights_[i] < 7 || top > height_ - 7) continue;
        if (item.separator) {
            Pen divider(color(221, 235, 245), 1);
            g.DrawLine(&divider, PointF(15, top + 4), PointF(width_ - 15, top + 4));
            continue;
        }
        if (hover_ == static_cast<int>(i) && item.command)
            rounded(g, RectF(7, top, width_ - 14, heights_[i]), 8, color(230, 245, 255));
        Color ink = !item.command ? color(115, 143, 163) :
                    item.danger ? color(211, 83, 99) : color(38, 77, 109);
        label(g, item.label, 18, top, width_ - 36, heights_[i],
              item.command ? 11.5f : 10.5f, ink);
    }
    g.Restore(state);
    if (contentHeight_ > height_) {
        float track = height_ - 19;
        float thumb = (std::max)(22.0f, track * height_ / contentHeight_);
        rounded(g, RectF(width_ - 7, 8 + scroll_ / (contentHeight_ - height_) * (track - thumb), 3, thumb),
                1.5f, color(134, 175, 202));
    }
    EndPaint(hwnd_, &ps);
}

void ResourceUI::Menu::finish(UINT command) {
    if (closed_) return;
    closed_ = true;
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
    if (owner_ && IsWindow(owner_)) PostMessageW(owner_, menuResult, command, 0);
}

bool ResourceUI::Menu::key(UINT key) {
    if (key == VK_ESCAPE) { finish(0); return true; }
    if (key == VK_RETURN) {
        if (hover_ >= 0) finish(items_[hover_].command);
        return true;
    }
    if (key != VK_UP && key != VK_DOWN) return false;
    int direction = key == VK_DOWN ? 1 : -1;
    int current = hover_;
    for (size_t count = 0; count < items_.size(); ++count) {
        current = (current + direction + static_cast<int>(items_.size())) % static_cast<int>(items_.size());
        if (items_[current].command) { hover_ = current; break; }
    }
    if (hover_ >= 0) {
        float top = tops_[hover_], bottom = top + heights_[hover_];
        if (top < scroll_ + 7) scroll_ = (std::max)(0.0f, top - 7);
        if (bottom > scroll_ + height_ - 7)
            scroll_ = (std::min)(contentHeight_ - height_, bottom - height_ + 7);
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

LRESULT CALLBACK ResourceUI::Menu::WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<Menu*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        self = static_cast<Menu*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (msg == WM_NCDESTROY && self) {
        self->hwnd_ = nullptr;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    }
    return self && msg != WM_NCDESTROY ? self->message(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT ResourceUI::Menu::message(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: draw(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_MOUSEMOVE: {
            int row = hit(GET_X_LPARAM(lp) / scale_, GET_Y_LPARAM(lp) / scale_);
            if (row != hover_) { hover_ = row; InvalidateRect(hwnd_, nullptr, FALSE); }
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0}; TrackMouseEvent(&track);
            return 0;
        }
        case WM_MOUSELEAVE: hover_ = -1; InvalidateRect(hwnd_, nullptr, FALSE); return 0;
        case WM_LBUTTONDOWN: {
            int row = hit(GET_X_LPARAM(lp) / scale_, GET_Y_LPARAM(lp) / scale_);
            if (row >= 0) finish(items_[row].command);
            return 0;
        }
        case WM_RBUTTONDOWN: finish(0); return 0;
        case WM_MOUSEWHEEL:
            scroll_ = (std::clamp)(scroll_ - GET_WHEEL_DELTA_WPARAM(wp) / 120.0f * 64.0f,
                                   0.0f, (std::max)(0.0f, contentHeight_ - height_));
            InvalidateRect(hwnd_, nullptr, FALSE); return 0;
        case WM_SETCURSOR: {
            POINT p{}; GetCursorPos(&p); ScreenToClient(hwnd_, &p);
            SetCursor(LoadCursorW(nullptr, hit(p.x / scale_, p.y / scale_) < 0 ? IDC_ARROW : IDC_HAND));
            return TRUE;
        }
        case WM_DPICHANGED: finish(0); return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

ResourceUI::Editor::~Editor() {
    if (name_ && IsWindow(name_)) RemoveWindowSubclass(name_, EditProc, 1);
    if (target_ && IsWindow(target_)) RemoveWindowSubclass(target_, EditProc, 1);
    if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_);
    if (font_) DeleteObject(font_);
    if (brush_) DeleteObject(brush_);
}

bool ResourceUI::Editor::show(HWND owner, HINSTANCE instance, float scale, Resource resource) {
    owner_ = owner; instance_ = instance; scale_ = scale; value_ = std::move(resource);
    if (!registerPopup(instance_, editorClass, WindowProc)) return false;
    RECT parent{}; GetWindowRect(owner_, &parent);
    POINT center{parent.left + (parent.right - parent.left) / 2,
                 parent.top + (parent.bottom - parent.top) / 2};
    RECT work = workArea(center);
    width_ = (std::min)(366.0f, (work.right - work.left) / scale_ - 24.0f);
    int width = pixel(width_, scale_), height = pixel(editorHeight, scale_);
    int x = parent.left + (parent.right - parent.left - width) / 2;
    int top = parent.top + (parent.bottom - parent.top - height) / 2;
    x = (std::clamp)(x, static_cast<int>(work.left), (std::max)(static_cast<int>(work.left), static_cast<int>(work.right) - width));
    top = (std::clamp)(top, static_cast<int>(work.top), (std::max)(static_cast<int>(work.top), static_cast<int>(work.bottom) - height));
    brush_ = CreateSolidBrush(RGB(243, 249, 255));
    font_ = CreateFontW(-pixel(12, scale_), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, editorClass, L"DesktopTodo 关联资源",
                            WS_POPUP | WS_CLIPCHILDREN, x, top, width, height,
                            owner_, nullptr, instance_, this);
    if (!hwnd_) return false;
    SetWindowRgn(hwnd_, CreateRoundRectRgn(0, 0, width + 1, height + 1,
                                          pixel(30, scale_), pixel(30, scale_)), TRUE);
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL;
    name_ = CreateWindowExW(0, L"EDIT", value_.name.c_str(), style,
                            pixel(26, scale_), pixel(78, scale_), pixel(width_ - 52, scale_), pixel(23, scale_),
                            hwnd_, nullptr, instance_, nullptr);
    target_ = CreateWindowExW(0, L"EDIT", value_.target.c_str(), style,
                              pixel(26, scale_), pixel(140, scale_), pixel(width_ - 52, scale_), pixel(23, scale_),
                              hwnd_, nullptr, instance_, nullptr);
    if (!name_ || !target_) return false;
    for (HWND field : {name_, target_}) {
        if (font_) SendMessageW(field, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
        SendMessageW(field, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(2, 2));
        SetWindowSubclass(field, EditProc, 1, reinterpret_cast<DWORD_PTR>(this));
    }
    SendMessageW(name_, EM_LIMITTEXT, 160, 0);
    SendMessageW(target_, EM_LIMITTEXT, 2048, 0);
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetFocus(value_.target.empty() ? target_ : name_);
    UpdateWindow(hwnd_);
    return true;
}

void ResourceUI::Editor::draw() {
    PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd_, &ps);
    Graphics g(dc); g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias); g.ScaleTransform(scale_, scale_);
    rounded(g, RectF(0.5f, 0.5f, width_ - 1, editorHeight - 1), 15,
            color(249, 252, 255), color(207, 228, 241));
    std::wstring type = value_.type == L"Url" ? L"网址" : value_.type == L"Folder" ? L"文件夹" : L"文件";
    std::wstring heading = (value_.target.empty() ? L"关联" : L"编辑") + type;
    label(g, heading, 20, 14, width_ - 64, 29, 15, color(38, 62, 84), true);
    label(g, L"×", width_ - 43, 13, 25, 30, 19,
          hoverCancel_ ? color(8, 104, 239) : color(100, 123, 143), false, StringAlignmentCenter);
    label(g, L"显示名称（可留空）", 21, 50, width_ - 42, 21, 11, color(100, 123, 143));
    label(g, value_.type == L"Url" ? L"网址（http:// 或 https://）" : L"文件或文件夹的完整路径",
          21, 112, width_ - 42, 21, 11, color(100, 123, 143));
    rounded(g, RectF(19, 73, width_ - 38, 34), 9, color(243, 249, 255),
            GetFocus() == name_ ? color(0, 191, 181) : color(211, 227, 241));
    rounded(g, RectF(19, 135, width_ - 38, 34), 9, color(243, 249, 255),
            !error_.empty() ? color(229, 93, 112) :
            GetFocus() == target_ ? color(0, 191, 181) : color(211, 227, 241));
    if (!error_.empty()) label(g, error_, 22, 170, width_ - 43, 17, 10, color(206, 78, 96));
    label(g, L"取消", width_ - 186, 187, 72, 30, 12,
          hoverCancel_ ? color(8, 104, 239) : color(100, 123, 143), false, StringAlignmentCenter);
    rounded(g, RectF(width_ - 108, 186, 87, 31), 9,
            hoverSave_ ? color(0, 165, 175) : color(0, 181, 181));
    label(g, L"保存", width_ - 108, 186, 87, 31, 12, color(255, 255, 255), true, StringAlignmentCenter);
    EndPaint(hwnd_, &ps);
}

void ResourceUI::Editor::save() {
    Resource result = value_;
    result.name = editText(name_); result.target = editText(target_);
    bool valid = !result.target.empty() && result.target.find_first_of(L"\r\n") == std::wstring::npos;
    if (result.type == L"Url") {
        size_t host = result.target.find(L"://");
        valid = valid && (result.target.rfind(L"https://", 0) == 0 ||
                          result.target.rfind(L"http://", 0) == 0) &&
                host != std::wstring::npos && result.target.size() > host + 3 &&
                result.target.find_first_of(L"/?#", host + 3) != host + 3 &&
                result.target.find(L' ') == std::wstring::npos;
    } else {
        try { valid = valid && std::filesystem::path(result.target).is_absolute(); }
        catch (const std::exception&) { valid = false; }
    }
    if (!valid) {
        error_ = result.type == L"Url" ? L"请输入完整的 http:// 或 https:// 网址" :
                                         L"请输入完整的文件或文件夹路径";
        SetFocus(target_); InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    value_ = std::move(result);
    finish(true);
}

void ResourceUI::Editor::finish(bool saveResult) {
    if (finished_) return;
    finished_ = true;
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
    if (owner_ && IsWindow(owner_)) PostMessageW(owner_, editorResult, saveResult ? 1 : 0, 0);
}

LRESULT CALLBACK ResourceUI::Editor::WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<Editor*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        self = static_cast<Editor*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (msg == WM_NCDESTROY && self) {
        self->hwnd_ = nullptr;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    }
    return self && msg != WM_NCDESTROY ? self->message(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK ResourceUI::Editor::EditProc(HWND edit, UINT msg, WPARAM wp, LPARAM lp,
                                              UINT_PTR, DWORD_PTR user) {
    auto* self = reinterpret_cast<Editor*>(user);
    if (msg == WM_KEYDOWN && (wp == VK_RETURN || wp == VK_ESCAPE)) {
        PostMessageW(self->hwnd_, WM_COMMAND, wp == VK_RETURN ? IDOK : IDCANCEL, 0);
        return 0;
    }
    return DefSubclassProc(edit, msg, wp, lp);
}

LRESULT ResourceUI::Editor::message(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: draw(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkColor(dc, RGB(243, 249, 255)); SetTextColor(dc, RGB(33, 55, 77));
            return reinterpret_cast<LRESULT>(brush_);
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) { save(); return 0; }
            if (LOWORD(wp) == IDCANCEL) { finish(false); return 0; }
            if ((reinterpret_cast<HWND>(lp) == name_ || reinterpret_cast<HWND>(lp) == target_) &&
                (HIWORD(wp) == EN_SETFOCUS || HIWORD(wp) == EN_KILLFOCUS || HIWORD(wp) == EN_CHANGE)) {
                if (HIWORD(wp) == EN_CHANGE) error_.clear();
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEMOVE: {
            float x = GET_X_LPARAM(lp) / scale_, y = GET_Y_LPARAM(lp) / scale_;
            bool saveHover = x >= width_ - 108 && x <= width_ - 21 && y >= 186 && y <= 217;
            bool cancelHover = (x >= width_ - 186 && x <= width_ - 114 && y >= 186 && y <= 217) ||
                               (x >= width_ - 43 && x <= width_ - 16 && y >= 13 && y <= 43);
            if (saveHover != hoverSave_ || cancelHover != hoverCancel_) {
                hoverSave_ = saveHover; hoverCancel_ = cancelHover;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0}; TrackMouseEvent(&track);
            return 0;
        }
        case WM_MOUSELEAVE:
            hoverSave_ = hoverCancel_ = false; InvalidateRect(hwnd_, nullptr, FALSE); return 0;
        case WM_LBUTTONDOWN: {
            float x = GET_X_LPARAM(lp) / scale_, y = GET_Y_LPARAM(lp) / scale_;
            if (x >= width_ - 108 && x <= width_ - 21 && y >= 186 && y <= 217) save();
            else if ((x >= width_ - 186 && x <= width_ - 114 && y >= 186 && y <= 217) ||
                     (x >= width_ - 43 && x <= width_ - 16 && y >= 13 && y <= 43)) finish(false);
            return 0;
        }
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) { finish(false); return 0; }
            if (wp == VK_RETURN) { save(); return 0; }
            break;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE && !finished_) finish(false);
            return 0;
        case WM_DPICHANGED: finish(false); return 0;
        case WM_SETCURSOR: {
            POINT p{}; GetCursorPos(&p); ScreenToClient(hwnd_, &p);
            float x = p.x / scale_, y = p.y / scale_;
            if ((x >= width_ - 108 && x <= width_ - 21 && y >= 186 && y <= 217) ||
                (x >= width_ - 186 && x <= width_ - 114 && y >= 186 && y <= 217) ||
                (x >= width_ - 43 && x <= width_ - 16 && y >= 13 && y <= 43)) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE;
            }
            break;
        }
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

ResourceUI::Confirm::~Confirm() {
    if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_);
}

bool ResourceUI::Confirm::show(HWND owner, HINSTANCE instance, float scale,
                               bool resource, POINT anchor) {
    owner_ = owner; scale_ = scale; resource_ = resource;
    if (!registerPopup(instance, confirmClass, WindowProc)) return false;
    RECT work = workArea(anchor);
    int width = pixel(confirmWidth, scale_), height = pixel(confirmHeight, scale_);
    int x = anchor.x - width + pixel(16, scale_);
    int y = anchor.y + pixel(10, scale_);
    if (y + height > work.bottom) y = anchor.y - height - pixel(10, scale_);
    x = (std::clamp)(x, static_cast<int>(work.left),
                     (std::max)(static_cast<int>(work.left), static_cast<int>(work.right) - width));
    y = (std::clamp)(y, static_cast<int>(work.top),
                     (std::max)(static_cast<int>(work.top), static_cast<int>(work.bottom) - height));
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, confirmClass, L"DesktopTodo 确认操作",
                            WS_POPUP, x, y, width, height, owner_, nullptr, instance, this);
    if (!hwnd_) return false;
    SetWindowRgn(hwnd_, CreateRoundRectRgn(0, 0, width + 1, height + 1,
                                          pixel(28, scale_), pixel(28, scale_)), TRUE);
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetFocus(hwnd_);
    UpdateWindow(hwnd_);
    return true;
}

void ResourceUI::Confirm::draw() {
    PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd_, &ps);
    Graphics g(dc); g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias); g.ScaleTransform(scale_, scale_);
    rounded(g, RectF(0.5f, 0.5f, confirmWidth - 1, confirmHeight - 1), 14,
            color(249, 252, 255), color(207, 228, 241));
    label(g, resource_ ? L"移除关联" : L"删除待办", 20, 13, confirmWidth - 72, 26,
          14, color(38, 62, 84), true);
    label(g, L"×", confirmWidth - 42, 11, 24, 26, 17,
          hoverClose_ ? color(8, 104, 239) : color(100, 123, 143), false, StringAlignmentCenter);
    label(g, resource_ ? L"确定移除关联吗？" : L"确定删除这条待办吗？",
          20, 45, confirmWidth - 40, 24, 12, color(76, 101, 124));
    Pen separator(color(221, 235, 245), 1);
    g.DrawLine(&separator, PointF(20, 83), PointF(confirmWidth - 20, 83));
    if (hoverCancel_) rounded(g, RectF(18, 95, 108, 32), 9, color(232, 244, 252));
    label(g, L"取消", 18, 95, 108, 32, 12, color(87, 116, 140), false, StringAlignmentCenter);
    rounded(g, RectF(139, 95, 119, 32), 9,
            hoverConfirm_ ? color(192, 61, 79) : color(213, 83, 99));
    label(g, resource_ ? L"移除" : L"删除", 139, 95, 119, 32, 12,
          color(255, 255, 255), true, StringAlignmentCenter);
    EndPaint(hwnd_, &ps);
}

void ResourceUI::Confirm::finish(bool accepted) {
    if (finished_) return;
    finished_ = true;
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
    if (owner_ && IsWindow(owner_)) PostMessageW(owner_, confirmResult, accepted ? 1 : 0, 0);
}

LRESULT CALLBACK ResourceUI::Confirm::WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<Confirm*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        self = static_cast<Confirm*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (msg == WM_NCDESTROY && self) {
        self->hwnd_ = nullptr;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    }
    return self && msg != WM_NCDESTROY ? self->message(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT ResourceUI::Confirm::message(UINT msg, WPARAM wp, LPARAM lp) {
    auto inCancel = [](float x, float y) { return x >= 18 && x <= 126 && y >= 95 && y <= 127; };
    auto inConfirm = [](float x, float y) { return x >= 139 && x <= 258 && y >= 95 && y <= 127; };
    auto inClose = [](float x, float y) { return x >= 236 && x <= 264 && y >= 10 && y <= 40; };
    switch (msg) {
        case WM_PAINT: draw(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_MOUSEMOVE: {
            float x = GET_X_LPARAM(lp) / scale_, y = GET_Y_LPARAM(lp) / scale_;
            bool cancel = inCancel(x, y), confirm = inConfirm(x, y), close = inClose(x, y);
            if (cancel != hoverCancel_ || confirm != hoverConfirm_ || close != hoverClose_) {
                hoverCancel_ = cancel; hoverConfirm_ = confirm; hoverClose_ = close;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0}; TrackMouseEvent(&track);
            return 0;
        }
        case WM_MOUSELEAVE:
            hoverCancel_ = hoverConfirm_ = hoverClose_ = false;
            InvalidateRect(hwnd_, nullptr, FALSE); return 0;
        case WM_LBUTTONDOWN: {
            float x = GET_X_LPARAM(lp) / scale_, y = GET_Y_LPARAM(lp) / scale_;
            if (inConfirm(x, y)) finish(true);
            else if (inCancel(x, y) || inClose(x, y)) finish(false);
            return 0;
        }
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) { finish(false); return 0; }
            if (wp == VK_RETURN) { finish(true); return 0; }
            break;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) finish(false);
            return 0;
        case WM_DPICHANGED: finish(false); return 0;
        case WM_SETCURSOR: {
            POINT p{}; GetCursorPos(&p); ScreenToClient(hwnd_, &p);
            float x = p.x / scale_, y = p.y / scale_;
            if (inConfirm(x, y) || inCancel(x, y) || inClose(x, y)) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE;
            }
            break;
        }
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}
