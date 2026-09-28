#include "DeadlinePicker.h"
#include "Deadline.h"
#include <commctrl.h>
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <cwchar>

using namespace Gdiplus;

namespace {
constexpr float pickerWidth = 292, pickerHeight = 392;
const wchar_t* pickerClass = L"DesktopTodoDeadlinePicker";

int px(float value, float scale) { return static_cast<int>(std::lround(value * scale)); }
Color color(BYTE a, BYTE r, BYTE g, BYTE b) { return Color(a, r, g, b); }

void roundRect(Graphics& g, RectF r, float radius, Color fill, Color stroke = Color(0, 0, 0, 0)) {
    GraphicsPath path;
    float d = radius * 2;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();
    SolidBrush brush(fill); g.FillPath(&brush, &path);
    if (stroke.GetAlpha()) { Pen pen(stroke, 1); g.DrawPath(&pen, &path); }
}

void text(Graphics& g, const std::wstring& value, float x, float y, float w, float h,
          float size, Color brushColor, bool bold = false, StringAlignment alignment = StringAlignmentCenter) {
    FontFamily family(L"Microsoft YaHei UI");
    Font font(&family, size, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    SolidBrush brush(brushColor);
    StringFormat format; format.SetAlignment(alignment);
    format.SetLineAlignment(StringAlignmentCenter);
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    g.DrawString(value.c_str(), -1, &font, RectF(x, y, w, h), &format, &brush);
}

void today(SYSTEMTIME& day) { GetLocalTime(&day); }
}

DeadlinePicker::~DeadlinePicker() {
    if (hwnd_) DestroyWindow(hwnd_);
    if (timeFont_) DeleteObject(timeFont_);
}

bool DeadlinePicker::show(HWND owner, HINSTANCE instance, float scale, const std::wstring& dueAt) {
    owner_ = owner; instance_ = instance; scale_ = scale;
    SYSTEMTIME current{}; today(current);
    year_ = current.wYear; month_ = current.wMonth; day_ = current.wDay;
    hasDate_ = ValidDeadline(dueAt);
    dateOnly_ = dueAt.size() != 16;
    int hour = 18, minute = 0;
    if (hasDate_) {
        year_ = std::stoi(dueAt.substr(0, 4)); month_ = std::stoi(dueAt.substr(5, 2));
        day_ = std::stoi(dueAt.substr(8, 2));
        if (!dateOnly_) { hour = std::stoi(dueAt.substr(11, 2)); minute = std::stoi(dueAt.substr(14, 2)); }
    }
    shownYear_ = year_; shownMonth_ = month_;
    WNDCLASSEXW cls{sizeof(cls)};
    cls.hInstance = instance_; cls.lpszClassName = pickerClass;
    cls.lpfnWndProc = WindowProc;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    RECT ownerRect{}; GetWindowRect(owner, &ownerRect);
    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &mi)) return false;
    RECT work = mi.rcWork;
    int w = px(pickerWidth, scale_), h = px(pickerHeight, scale_), gap = px(10, scale_);
    int x = ownerRect.left - w - gap;
    if (x < work.left) x = ownerRect.right + gap;
    if (x + w > work.right) x = ownerRect.left + px(28, scale_);
    x = (std::clamp)(x, static_cast<int>(work.left), (std::max)(static_cast<int>(work.left), static_cast<int>(work.right) - w));
    int y = (std::clamp)(static_cast<int>(ownerRect.bottom) - h, static_cast<int>(work.top),
                         (std::max)(static_cast<int>(work.top), static_cast<int>(work.bottom) - h));
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, pickerClass, L"截止时间", WS_POPUP | WS_CLIPCHILDREN,
                            x, y, w, h, owner, nullptr, instance_, this);
    if (!hwnd_) return false;
    SetWindowRgn(hwnd_, CreateRoundRectRgn(0, 0, w + 1, h + 1, px(20, scale_), px(20, scale_)), TRUE);
    time_ = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"", WS_CHILD | DTS_TIMEFORMAT | DTS_UPDOWN,
                            px(166, scale_), px(313, scale_), px(102, scale_), px(24, scale_),
                            hwnd_, nullptr, instance_, nullptr);
    if (!time_) return false;
    timeFont_ = CreateFontW(-px(12, scale_), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    if (timeFont_) SendMessageW(time_, WM_SETFONT, reinterpret_cast<WPARAM>(timeFont_), FALSE);
    SendMessageW(time_, DTM_SETFORMATW, 0, reinterpret_cast<LPARAM>(L"HH':'mm"));
    SYSTEMTIME selected = current;
    selected.wHour = static_cast<WORD>(hour); selected.wMinute = static_cast<WORD>(minute);
    selected.wSecond = 0; selected.wMilliseconds = 0;
    SendMessageW(time_, DTM_SETSYSTEMTIME, GDT_VALID, reinterpret_cast<LPARAM>(&selected));
    SetWindowSubclass(time_, TimeProc, 1, reinterpret_cast<DWORD_PTR>(this));
    showTime();
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    UpdateWindow(hwnd_);
    return true;
}

void DeadlinePicker::showTime() {
    if (time_) ShowWindow(time_, dateOnly_ ? SW_HIDE : SW_SHOW);
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void DeadlinePicker::pickDay(int year, int month, int day) {
    year_ = shownYear_ = year; month_ = shownMonth_ = month; day_ = day; hasDate_ = true;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void DeadlinePicker::monthBy(int delta) {
    int n = shownYear_ * 12 + shownMonth_ - 1 + delta;
    if (n < 1900 * 12 || n >= 10000 * 12) return;
    shownYear_ = n / 12; shownMonth_ = n % 12 + 1;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

std::wstring DeadlinePicker::selection() const {
    if (!hasDate_) return L"";
    std::wstring result = DateString(year_, month_, day_);
    if (dateOnly_) return result;
    SYSTEMTIME selected{};
    if (SendMessageW(time_, DTM_GETSYSTEMTIME, 0, reinterpret_cast<LPARAM>(&selected)) != GDT_VALID)
        return result;
    wchar_t clock[8]{};
    std::swprintf(clock, sizeof(clock) / sizeof(clock[0]), L"T%02u:%02u", selected.wHour, selected.wMinute);
    return result + clock;
}

void DeadlinePicker::finish(bool save) {
    if (resultPosted_) return;
    if (save && time_ && !dateOnly_) SetFocus(hwnd_); // Commit any in-progress time edit.
    resultPosted_ = true;
    ShowWindow(hwnd_, SW_HIDE);
    PostMessageW(owner_, resultMessage, save ? 1 : 0, 0);
}

void DeadlinePicker::paint() {
    PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd_, &ps);
    Graphics g(dc); g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);
    g.ScaleTransform(scale_, scale_);
    SolidBrush background(color(255, 246, 250, 254));
    g.FillRectangle(&background, RectF(0, 0, pickerWidth, pickerHeight));
    Color primary = color(255, 38, 61, 81), secondary = color(255, 105, 125, 143);
    Color accent = color(255, 8, 104, 239);
    text(g, L"截止时间", 20, 11, 185, 30, 15, primary, true, StringAlignmentNear);
    text(g, L"×", 255, 10, 22, 30, 21, secondary);
    roundRect(g, RectF(17, 46, 119, 30), 9, color(255, 231, 244, 255));
    roundRect(g, RectF(146, 46, 128, 30), 9, color(255, 239, 246, 250));
    text(g, L"选今天", 17, 46, 119, 30, 11, accent);
    text(g, L"不设截止时间", 146, 46, 128, 30, 11, secondary);
    text(g, L"‹", 17, 88, 35, 30, 24, secondary);
    wchar_t heading[32]{};
    std::swprintf(heading, sizeof(heading) / sizeof(heading[0]), L"%04d 年 %d 月", shownYear_, shownMonth_);
    text(g, heading, 55, 90, 181, 26, 13, primary, true);
    text(g, L"›", 240, 88, 35, 30, 24, secondary);
    const wchar_t* weekdays[] = {L"一", L"二", L"三", L"四", L"五", L"六", L"日"};
    for (int column = 0; column < 7; ++column)
        text(g, weekdays[column], 20.0f + column * 36.0f, 123, 34, 20, 11, secondary);
    int first = WeekdayMondayFirst(shownYear_, shownMonth_, 1);
    int last = DaysInMonth(shownYear_, shownMonth_);
    SYSTEMTIME current{}; today(current);
    for (int day = 1; day <= last; ++day) {
        int cell = first + day - 1, row = cell / 7, column = cell % 7;
        float x = 20.0f + column * 36.0f, y = 147.0f + row * 24.0f;
        bool chosen = hasDate_ && year_ == shownYear_ && month_ == shownMonth_ && day_ == day;
        bool isToday = current.wYear == shownYear_ && current.wMonth == shownMonth_ && current.wDay == day;
        if (chosen) roundRect(g, RectF(x + 3, y, 28, 23), 8, accent);
        else if (isToday) roundRect(g, RectF(x + 3, y, 28, 23), 8,
                                    color(255, 246, 250, 254), color(255, 151, 190, 241));
        text(g, std::to_wstring(day), x, y, 34, 23, 11,
             chosen ? color(255, 255, 255, 255) : primary);
    }
    roundRect(g, RectF(17, 304, 258, 39), 10,
              color(255, 255, 255, 255), color(255, 223, 232, 241));
    text(g, L"时刻", 28, 309, 36, 27, 11, secondary);
    roundRect(g, RectF(74, 309, 75, 29), 8,
              dateOnly_ ? color(255, 235, 244, 255) : color(255, 239, 244, 249));
    text(g, dateOnly_ ? L"当天内" : L"指定时间", 74, 309, 75, 29, 10, dateOnly_ ? accent : secondary);
    if (dateOnly_) text(g, L"截止当天 23:59", 156, 309, 111, 28, 10, secondary);
    text(g, L"取消", 110, 354, 64, 29, 12, secondary);
    roundRect(g, RectF(187, 352, 87, 31), 9, accent);
    text(g, L"保存", 187, 352, 87, 31, 12, color(255, 255, 255, 255), true);
    EndPaint(hwnd_, &ps);
}

LRESULT CALLBACK DeadlinePicker::WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<DeadlinePicker*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        self = static_cast<DeadlinePicker*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->message(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK DeadlinePicker::TimeProc(HWND editor, UINT msg, WPARAM wp, LPARAM lp,
                                           UINT_PTR, DWORD_PTR user) {
    auto* self = reinterpret_cast<DeadlinePicker*>(user);
    if (msg == WM_KEYDOWN && (wp == VK_ESCAPE || wp == VK_RETURN)) {
        PostMessageW(self->hwnd_, WM_KEYDOWN, wp, 0); return 0;
    }
    return DefSubclassProc(editor, msg, wp, lp);
}

LRESULT DeadlinePicker::message(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: paint(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE && !resultPosted_) finish(false);
            return 0;
        case WM_MOUSEWHEEL: monthBy(GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -1 : 1); return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) { finish(false); return 0; }
            if (wp == VK_RETURN) { finish(true); return 0; }
            break;
        case WM_LBUTTONDOWN: {
            float x = GET_X_LPARAM(lp) / scale_, y = GET_Y_LPARAM(lp) / scale_;
            if (x >= 250 && y < 42) { finish(false); return 0; }
            if (y >= 46 && y <= 76) {
                SYSTEMTIME current{}; today(current);
                if (x >= 17 && x < 136) {
                    pickDay(current.wYear, current.wMonth, current.wDay);
                    dateOnly_ = true; showTime();
                } else if (x >= 146 && x < 274) {
                    hasDate_ = false; dateOnly_ = true; showTime();
                }
                return 0;
            }
            if (y >= 88 && y < 120) {
                if (x < 58) monthBy(-1);
                else if (x > 236) monthBy(1);
                return 0;
            }
            if (y >= 147 && y < 291) {
                int column = static_cast<int>((x - 20) / 36), row = static_cast<int>((y - 147) / 24);
                if (x >= 20 && x < 272 && column >= 0 && column < 7 && row >= 0 && row < 6) {
                    int day = row * 7 + column - WeekdayMondayFirst(shownYear_, shownMonth_, 1) + 1;
                    if (day >= 1 && day <= DaysInMonth(shownYear_, shownMonth_))
                        pickDay(shownYear_, shownMonth_, day);
                }
                return 0;
            }
            if (y >= 305 && y <= 343 && x >= 70 && x <= 152) {
                dateOnly_ = !dateOnly_;
                if (!hasDate_) {
                    SYSTEMTIME current{}; today(current); pickDay(current.wYear, current.wMonth, current.wDay);
                }
                showTime(); return 0;
            }
            if (y >= 349 && y <= 388) {
                if (x >= 187 && x <= 274) finish(true);
                else if (x >= 103 && x <= 177) finish(false);
                return 0;
            }
            return 0;
        }
        case WM_DESTROY: time_ = nullptr; return 0;
        case WM_NCDESTROY: {
            HWND destroyed = hwnd_;
            hwnd_ = nullptr;
            return DefWindowProcW(destroyed, msg, wp, lp);
        }
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}
