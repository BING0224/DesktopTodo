#include "MainWindow.h"
#include "Deadline.h"
#include "DeadlinePicker.h"
#include "ResourceUI.h"
#include <commctrl.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

using namespace Gdiplus;

namespace {
constexpr UINT editAdd = 101, editRow = 102;
constexpr UINT commandCommit = WM_APP + 1, commandCancel = WM_APP + 2,
               commandResort = WM_APP + 3;
constexpr UINT timerSettle = 1, timerDeadlineRefresh = 2;
constexpr int startupValue = 1;
constexpr UINT menuFile = 301, menuFolder = 302, menuUrl = 303,
               menuPendingBase = 500;
const wchar_t* runKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* windowClass = L"DesktopTodoNativeWindow";
const wchar_t* editHostClass = L"DesktopTodoNativeEditHost";

Color rgba(BYTE a, BYTE r, BYTE g, BYTE b) { return Color(a, r, g, b); }
Color blue() { return rgba(255, 8, 104, 239); }
Color teal() { return rgba(255, 0, 191, 181); }
// WPF renders text and vector icons with soft grayscale coverage on a transparent window.
Color ink() { return rgba(215, 33, 55, 77); }
Color muted() { return rgba(215, 100, 119, 139); }

void rounded(Graphics& g, RectF r, REAL radius, Color fill, Color border = Color(0, 0, 0, 0), REAL borderWidth = 0) {
    GraphicsPath path;
    REAL d = (std::min)(2 * radius, (std::min)(r.Width, r.Height));
    if (d < 1) return;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();
    SolidBrush brush(fill); g.FillPath(&brush, &path);
    if (borderWidth > 0) { Pen pen(border, borderWidth); g.DrawPath(&pen, &path); }
}

void roundedGradient(Graphics& g, RectF r, REAL radius, Color start, Color end) {
    GraphicsPath path;
    REAL d = (std::min)(2 * radius, (std::min)(r.Width, r.Height));
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();
    LinearGradientBrush brush(PointF(r.X, r.Y), PointF(r.GetRight(), r.Y), start, end);
    g.FillPath(&brush, &path);
}

void line(Graphics& g, REAL x1, REAL y1, REAL x2, REAL y2, Color color, REAL thickness = 1.5f) {
    Pen pen(color, thickness); pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound);
    g.DrawLine(&pen, x1, y1, x2, y2);
}

void label(Graphics& g, const std::wstring& text, REAL x, REAL y, REAL w, REAL h,
           REAL fontSize, Color color, bool bold = false, StringAlignment align = StringAlignmentNear) {
    FontFamily family(L"Microsoft YaHei UI");
    Font font(&family, fontSize, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    SolidBrush brush(color);
    StringFormat format;
    format.SetAlignment(align);
    format.SetLineAlignment(StringAlignmentCenter);
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    g.DrawString(text.c_str(), -1, &font, RectF(x, y, w, h), &format, &brush);
}

std::wstring trim(std::wstring s) {
    size_t first = s.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return L"";
    size_t last = s.find_last_not_of(L" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::wstring editText(HWND editor) {
    int size = GetWindowTextLengthW(editor);
    std::wstring text(static_cast<size_t>(size) + 1, L'\0');
    GetWindowTextW(editor, text.data(), size + 1);
    text.resize(size);
    return text;
}

int pixel(float dip, float scale) { return static_cast<int>(std::lround(dip * scale)); }

std::wstring resourceLabel(const Resource& resource) {
    if (!resource.name.empty()) return resource.name;
    if (resource.type == L"Url") {
        auto start = resource.target.find(L"://");
        start = start == std::wstring::npos ? 0 : start + 3;
        auto end = resource.target.find_first_of(L"/?#", start);
        return resource.target.substr(start, end == std::wstring::npos ? end : end - start);
    }
    std::wstring name = std::filesystem::path(resource.target).filename().wstring();
    return name.empty() ? resource.target : name;
}

bool pickResourceTarget(HWND owner, Resource& resource) {
    if (resource.type == L"File") {
        std::vector<wchar_t> path(32768);
        OPENFILENAMEW picker{sizeof(picker)};
        picker.hwndOwner = owner;
        picker.lpstrFilter = L"所有文件\0*.*\0\0";
        picker.lpstrFile = path.data();
        picker.nMaxFile = static_cast<DWORD>(path.size());
        picker.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&picker)) return false;
        resource.target = path.data();
    } else if (resource.type == L"Folder") {
        BROWSEINFOW picker{};
        picker.hwndOwner = owner;
        picker.lpszTitle = L"选择要关联的文件夹";
        picker.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&picker);
        if (!item) return false;
        PWSTR path = nullptr;
        HRESULT result = SHGetNameFromIDList(item, SIGDN_FILESYSPATH, &path);
        CoTaskMemFree(item);
        if (FAILED(result) || !path) {
            if (path) CoTaskMemFree(path);
            return false;
        }
        resource.target = path;
        CoTaskMemFree(path);
    } else return false;
    // An empty Name makes the displayed label follow the newly chosen file or folder.
    resource.name.clear();
    return true;
}

}

MainWindow::MainWindow(HINSTANCE instance) : instance_(instance), placement_(store_.loadPlacement()), todos_(store_.loadTodos()) {}

MainWindow::~MainWindow() {
    if (editFont_) DeleteObject(editFont_);
    if (editBrush_) DeleteObject(editBrush_);
    if (rowEditBrush_) DeleteObject(rowEditBrush_);
}

bool MainWindow::createAndShow() {
    WNDCLASSEXW cls{sizeof(cls)};
    cls.hInstance = instance_; cls.lpszClassName = windowClass;
    cls.lpfnWndProc = WindowProc; cls.style = CS_DBLCLKS;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(1), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
    cls.hIconSm = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    if (!RegisterClassExW(&cls)) return false;
    WNDCLASSEXW editorClass{sizeof(editorClass)};
    editorClass.hInstance = instance_; editorClass.lpszClassName = editHostClass;
    editorClass.lpfnWndProc = EditorHostProc;
    editorClass.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    if (!RegisterClassExW(&editorClass)) return false;

    UINT dpi = GetDpiForSystem();
    scale_ = dpi / 96.0f;
    RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int w = (std::min)(pixel(static_cast<float>(placement_.width), scale_), static_cast<int>(work.right - work.left));
    int h = (std::min)(pixel(static_cast<float>(placement_.height), scale_), static_cast<int>(work.bottom - work.top));
    int x = placement_.saved ? pixel(static_cast<float>(placement_.x), scale_) : work.right - w - pixel(16, scale_);
    int y = placement_.saved ? pixel(static_cast<float>(placement_.y), scale_) : work.bottom - h - pixel(16, scale_);
    x = (std::clamp)(x, static_cast<int>(work.left),
                     (std::max)(static_cast<int>(work.left), static_cast<int>(work.right) - w));
    y = (std::clamp)(y, static_cast<int>(work.top),
                     (std::max)(static_cast<int>(work.top), static_cast<int>(work.bottom) - h));
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, windowClass, L"DesktopTodo",
                             WS_POPUP | WS_CLIPCHILDREN, x, y, w, h,
                             nullptr, nullptr, instance_, this);
    if (!hwnd_) return false;
    migrateStartup();
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    draw();
    desktop_ = std::make_unique<DesktopHost>(hwnd_);
    desktop_->arrange(true);
    return true;
}

void MainWindow::layout() {
    RECT rect{}; GetClientRect(hwnd_, &rect);
    width_ = (rect.right - rect.left) / scale_;
    height_ = (rect.bottom - rect.top) / scale_;
    auto build = [&](bool scrollBar) {
        rows_.clear(); contentHeight_ = 0;
        float rowWidth = width_ - 54 - (scrollBar ? 9.0f : 0.0f);
        Bitmap measuring(1, 1, PixelFormat32bppPARGB);
        Graphics g(&measuring);
        FontFamily family(L"Microsoft YaHei UI"); Font font(&family, 13.0f, FontStyleRegular, UnitPixel);
        StringFormat format; format.SetTrimming(StringTrimmingNone);
        for (size_t i = 0; i < todos_.size(); ++i) {
            // The delete button occupies space only while its row is hovered.
            bool showDelete = hoverRow_ == static_cast<int>(i) && editing_ != static_cast<int>(i);
            float textWidth = (std::max)(30.0f, rowWidth - (showDelete ? 159.0f : 130.0f) -
                                       (todos_[i].resources.empty() ? 0.0f : 20.0f));
            RectF measured;
            std::wstring current = editing_ == static_cast<int>(i) && rowEdit_ ? editText(rowEdit_) : todos_[i].text;
            g.MeasureString(current.c_str(), -1, &font,
                            RectF(0, 0, textWidth, 8192), &format, &measured);
            float textHeight = (std::max)(20.0f, measured.Height);
            float cardHeight = (std::max)(55.0f, textHeight + 22.0f);
            const Json* id = todos_[i].original.find(L"Id");
            bool expanded = id && !expandedId_.empty() && id->stringOr() == expandedId_ &&
                            !todos_[i].resources.empty();
            std::vector<float> resourceHeights;
            float rowHeight = cardHeight;
            if (expanded) {
                rowHeight += 36;
                Font itemFont(&family, 11.0f, FontStyleRegular, UnitPixel);
                StringFormat itemFormat;
                itemFormat.SetTrimming(StringTrimmingEllipsisCharacter);
                float available = (std::max)(12.0f, width_ - 27 - (scrollBar ? 9.0f : 0.0f) - 225);
                for (const Resource& resource : todos_[i].resources) {
                    RectF measuredItem;
                    std::wstring name = resourceLabel(resource);
                    g.MeasureString(name.c_str(), -1, &itemFont,
                                    RectF(0, 0, available, 8192), &itemFormat, &measuredItem);
                    float height = measuredItem.Height > 24.0f ? 43.0f : 28.0f;
                    resourceHeights.push_back(height);
                    rowHeight += height;
                }
            }
            rows_.push_back(Row{contentHeight_, rowHeight, cardHeight, i, std::move(resourceHeights)});
            contentHeight_ += rowHeight + 6;
        }
    };
    build(false);
    hasScroll_ = contentHeight_ > listHeight();
    if (hasScroll_) build(true);
    hasScroll_ = contentHeight_ > listHeight();
    float maxScroll = (std::max)(0.0f, contentHeight_ - listHeight());
    scroll_ = (std::clamp)(scroll_, 0.0f, maxScroll);
    if (hasScroll_) {
        float track = listHeight();
        thumbHeight_ = (std::max)(28.0f, track * track / contentHeight_);
        thumbY_ = listTop() + scroll_ / maxScroll * (track - thumbHeight_);
    }
}

void MainWindow::draw() {
    if (!hwnd_ || IsIconic(hwnd_)) return;
    RECT rect{}; GetWindowRect(hwnd_, &rect);
    int pxWidth = rect.right - rect.left, pxHeight = rect.bottom - rect.top;
    if (pxWidth <= 0 || pxHeight <= 0) return;
    layout();
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = pxWidth; info.bmiHeader.biHeight = -pxHeight;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!dib || !memory || !pixels) {
        if (dib) DeleteObject(dib); if (memory) DeleteDC(memory); ReleaseDC(nullptr, screen); return;
    }
    HGDIOBJ previous = SelectObject(memory, dib);
    {
        Bitmap bitmap(pxWidth, pxHeight, pxWidth * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(pixels));
        Graphics g(&bitmap);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAlias);
        // A barely visible alpha value keeps the invisible resize edges clickable.
        g.Clear(Color(1, 0, 0, 0));
        g.ScaleTransform(scale_, scale_);

        // Soft shadow, frosted blue-white panel, and thin outline.
        for (int i = 7; i >= 1; --i)
            rounded(g, RectF(9 - i * 0.55f, 11 - i * 0.35f,
                             width_ - 18 + i * 1.1f, height_ - 20 + i * 0.8f),
                    22.0f + i, rgba(static_cast<BYTE>(2 + i), 25, 58, 89));
        GraphicsPath panel;
        RectF surface(9.5f, 9.5f, width_ - 19, height_ - 19);
        REAL d = 44;
        panel.AddArc(surface.X, surface.Y, d, d, 180, 90);
        panel.AddArc(surface.GetRight() - d, surface.Y, d, d, 270, 90);
        panel.AddArc(surface.GetRight() - d, surface.GetBottom() - d, d, d, 0, 90);
        panel.AddArc(surface.X, surface.GetBottom() - d, d, d, 90, 90);
        panel.CloseFigure();
        LinearGradientBrush gradient(PointF(9, 9), PointF(width_ - 9, height_ - 9),
                                     rgba(235, 255, 255, 255), rgba(222, 228, 247, 253));
        g.FillPath(&gradient, &panel);
        Pen outline(rgba(220, 255, 255, 255), 1); g.DrawPath(&outline, &panel);

        float right = width_ - 27;
        roundedGradient(g, RectF(27, 29, 36, 36), 11,
                        rgba(255, 8, 104, 239), rgba(255, 0, 191, 181));
        Color white(255, 255, 255, 255);
        Color iconWhite(238, 255, 255, 255);
        line(g, 35, 43, 38, 46, iconWhite, 2); line(g, 38, 46, 44, 39, iconWhite, 2);
        line(g, 35, 53, 38, 56, iconWhite, 2); line(g, 38, 56, 44, 49, iconWhite, 2);
        line(g, 47, 42, 55, 42, iconWhite, 2); line(g, 47, 52, 55, 52, iconWhite, 2);
        label(g, L"TODO", 74, 29, right - 145, 21, 16, rgba(225, 8, 104, 239), true);
        SYSTEMTIME today{}; GetLocalTime(&today);
        std::wstring todayDate = DateString(today.wYear, today.wMonth, today.wDay);
        wchar_t clock[12]{};
        swprintf_s(clock, L"T%02u:%02u:%02u", today.wHour, today.wMinute, today.wSecond);
        std::wstring nowKey = todayDate + clock;
        int remaining = 0, overdueCount = 0;
        for (const auto& todo : todos_) {
            if (todo.completed) continue;
            ++remaining;
            if (!todo.dueAt.empty() && SecondsUntilExpiry(todo.dueAt, nowKey) <= 0)
                ++overdueCount;
        }
        std::wstring summary = L"待办 " + std::to_wstring(remaining) +
                               L" · 逾期 " + std::to_wstring(overdueCount) +
                               L" · 完成 " + std::to_wstring(todos_.size() - remaining);
        label(g, summary, 74, 51, right - 145, 15, 11,
              overdueCount ? rgba(235, 170, 67, 83) : muted());
        float settingsX = right - 62, closeX = right - 30;
        if (hoverAction_ == HitKind::Settings || menu_) rounded(g, RectF(settingsX, 32, 30, 30), 9, rgba(34, 112, 169, 212));
        if (hoverAction_ == HitKind::Close) rounded(g, RectF(closeX, 32, 30, 30), 9, rgba(34, 112, 169, 212));
        PointF gearCenter(settingsX + 15, 47);
        GraphicsPath gear;
        for (int k = 0; k < 24; ++k) {
            REAL a = static_cast<REAL>(k * 3.141592653589793 / 12 - 3.141592653589793 / 2);
            REAL r = (k % 4 == 1 || k % 4 == 2) ? 8.2f : 6.8f;
            PointF p(gearCenter.X + std::cos(a) * r, gearCenter.Y + std::sin(a) * r);
            if (!k) gear.StartFigure();
            if (k) gear.AddLine(PointF(gearCenter.X + std::cos(a - static_cast<REAL>(3.141592653589793 / 12)) *
                              ((k - 1) % 4 == 1 || (k - 1) % 4 == 2 ? 8.2f : 6.8f),
                              gearCenter.Y + std::sin(a - static_cast<REAL>(3.141592653589793 / 12)) *
                              ((k - 1) % 4 == 1 || (k - 1) % 4 == 2 ? 8.2f : 6.8f)), p);
        }
        gear.CloseFigure(); Pen gearPen(rgba(220, 73, 101, 122), 1.7f); g.DrawPath(&gearPen, &gear);
        g.DrawEllipse(&gearPen, gearCenter.X - 2.5f, gearCenter.Y - 2.5f, 5.0f, 5.0f);
        line(g, closeX + 9, 41, closeX + 21, 53, rgba(220, 73, 101, 122), 1.8f);
        line(g, closeX + 21, 41, closeX + 9, 53, rgba(220, 73, 101, 122), 1.8f);
        line(g, 27, 83, right, 83, rgba(132, 198, 215, 228), 1);

        if (todos_.empty()) {
            label(g, L"这里还没有待办\n点击下方按钮，记录第一件事", 35, listTop(),
                  width_ - 70, listHeight(), 13, muted(), false, StringAlignmentCenter);
        } else {
            GraphicsState saved = g.Save();
            g.SetClip(RectF(27, listTop(), right - 27, listHeight()));
            for (const Row& row : rows_) {
                float y = listTop() + row.y - scroll_;
                if (y + row.height < listTop() || y > listBottom()) continue;
                const Todo& todo = todos_[row.index];
                float rowWidth = rowRight() - 27;
                bool hover = hoverRow_ == static_cast<int>(row.index);
                long long secondsLeft = todo.completed ? 999999999LL : SecondsUntilExpiry(todo.dueAt, nowKey);
                bool urgent = !todo.completed && !todo.dueAt.empty() && secondsLeft <= 7200;
                bool overdue = urgent && secondsLeft <= 0;
                bool dueToday = !todo.completed && !urgent && !todo.dueAt.empty() &&
                                todo.dueAt.substr(0, 10) == todayDate;
                Color rowFill = todo.completed ? rgba(174, 239, 247, 252) :
                                urgent ? (overdue ? rgba(236, 255, 203, 211) : rgba(238, 255, 219, 226)) :
                                dueToday ? rgba(229, 255, 242, 209) : rgba(199, 232, 247, 255);
                Color rowBorder = urgent ? rgba(234, 248, 135, 147) :
                                  dueToday ? rgba(220, 246, 212, 156) :
                                  hover ? rgba(240, 178, 223, 246) : rgba(215, 255, 255, 255);
                rounded(g, RectF(27, y, rowWidth, row.height), 11,
                        rowFill, rowBorder, 1);
                float checkY = y + (row.cardHeight - 19) / 2;
                rounded(g, RectF(38, checkY, 19, 19), 6,
                        todo.completed ? teal() : rgba(128, 255, 255, 255),
                        todo.completed ? teal() : rgba(140, 175, 195, 207), 1.3f);
                if (todo.completed) {
                    line(g, 42, checkY + 9, 45.5f, checkY + 12.5f, white, 2);
                    line(g, 45.5f, checkY + 12.5f, 52, checkY + 5, white, 2);
                }
                Color tileFill = urgent ? rgba(239, 255, 187, 199) :
                                 dueToday ? rgba(229, 255, 229, 166) :
                                 todo.dueAt.empty() ? rgba(153, 255, 255, 255) : rgba(215, 218, 237, 253);
                Color dateColor = urgent ? rgba(255, 174, 48, 68) :
                                  dueToday ? rgba(255, 163, 100, 18) : rgba(245, 39, 102, 172);
                Color timeColor = urgent ? rgba(245, 174, 92, 103) :
                                  dueToday ? rgba(233, 158, 121, 62) : rgba(228, 102, 132, 156);
                Color divider = urgent ? rgba(172, 202, 105, 117) :
                                dueToday ? rgba(150, 204, 168, 102) : rgba(113, 145, 188, 214);
                rounded(g, RectF(65, y + 7, 71, row.cardHeight - 14), 9, tileFill);
                if (todo.dueAt.empty()) {
                    label(g, L"长期", 68, y + 8, 65, row.cardHeight - 16, 12, muted(), false, StringAlignmentCenter);
                } else if (overdue) {
                    label(g, L"逾期", 68, y + 8, 65, row.cardHeight - 16, 12, dateColor, true, StringAlignmentCenter);
                } else {
                    int month = std::stoi(todo.dueAt.substr(5, 2));
                    int day = std::stoi(todo.dueAt.substr(8, 2));
                    std::wstring dayText = std::to_wstring(month) + L"/" + std::to_wstring(day);
                    float timeGroupY = y + (row.cardHeight - 37.0f) / 2.0f;
                    label(g, dayText, 68, timeGroupY, 65, 20, 12, dateColor, true, StringAlignmentCenter);
                    label(g, todo.dueAt.size() == 10 ? L"当天内" : todo.dueAt.substr(11, 5),
                          68, timeGroupY + 20, 65, 17, 10, timeColor, false, StringAlignmentCenter);
                }
                line(g, 140, y + 13, 140, y + row.cardHeight - 13, divider, 1);
                float textX = todo.resources.empty() ? 144.0f : 164.0f;
                if (!todo.resources.empty()) {
                    bool expanded = row.height > row.cardHeight;
                    Color arrow = hoverAction_ == HitKind::ExpandResources && hoverRow_ == static_cast<int>(row.index) ? blue() : muted();
                    float middle = y + row.cardHeight / 2;
                    if (expanded) {
                        line(g, 149, middle - 2, 154, middle + 3, arrow, 1.6f);
                        line(g, 154, middle + 3, 159, middle - 2, arrow, 1.6f);
                    } else {
                        line(g, 151, middle - 5, 156, middle, arrow, 1.6f);
                        line(g, 156, middle, 151, middle + 5, arrow, 1.6f);
                    }
                }
                if (editing_ != static_cast<int>(row.index)) {
                    FontFamily family(L"Microsoft YaHei UI");
                    Font font(&family, 13, todo.completed ? FontStyleStrikeout : FontStyleRegular, UnitPixel);
                    SolidBrush brush(todo.completed ? rgba(125, 33, 55, 77) : ink());
                    StringFormat fmt; fmt.SetTrimming(StringTrimmingNone); fmt.SetLineAlignment(StringAlignmentCenter);
                    float textRight = rowRight() - (hover ? 42.0f : 13.0f);
                    RectF textRect(textX, y + 9, (std::max)(25.0f, textRight - textX), row.cardHeight - 18);
                    g.DrawString(todo.text.c_str(), -1, &font, textRect, &fmt, &brush);
                } else {
                    rounded(g, RectF(textX - 3, y + 6, rowRight() - textX - 10, row.cardHeight - 12), 8,
                            rgba(255, 242, 249, 255), rgba(230, 99, 156, 225), 1);
                }
                if (hover && editing_ != static_cast<int>(row.index)) {
                    float deleteX = rowRight() - 41;
                    bool deleteActive = hoverAction_ == HitKind::Delete;
                    if (deleteActive) rounded(g, RectF(deleteX, y + 9, 30, 30), 9, rgba(90, 255, 223, 229));
                    Color deleteInk = deleteActive ? rgba(255, 195, 72, 91) : rgba(255, 117, 139, 161);
                    line(g, deleteX + 9, y + 18, deleteX + 21, y + 30, deleteInk, 1.6f);
                    line(g, deleteX + 21, y + 18, deleteX + 9, y + 30, deleteInk, 1.6f);
                }
                if (row.height > row.cardHeight) {
                    line(g, 149, y + row.cardHeight + 2, rowRight() - 13, y + row.cardHeight + 2,
                         rgba(100, 145, 181, 204), 1);
                    float resourceOffset = 0;
                    for (size_t r = 0; r < todo.resources.size(); ++r) {
                        float ry = y + row.cardHeight + 7 + resourceOffset;
                        float itemHeight = row.resourceHeights[r];
                        const Resource& resource = todo.resources[r];
                        bool active = hover && hoverAction_ == HitKind::ResourceOpen &&
                                      hoverResource_ == static_cast<int>(r);
                        bool menuActive = hover && hoverAction_ == HitKind::ResourceRemove &&
                                          hoverResource_ == static_cast<int>(r);
                        if (active) rounded(g, RectF(148, ry, rowRight() - 193, itemHeight - 2), 7, rgba(65, 255, 255, 255));
                        float menuY = ry + (itemHeight - 26) / 2;
                        if (menuActive) rounded(g, RectF(rowRight() - 39, menuY, 28, 26), 7, rgba(90, 255, 223, 229));
                        Color icon = resource.type == L"Url" ? teal() : blue();
                        if (resource.type == L"Folder") {
                            line(g, 156, ry + 6, 160, ry + 6, icon, 1.3f);
                            line(g, 160, ry + 6, 162, ry + 8, icon, 1.3f);
                            rounded(g, RectF(155, ry + 8, 15, 11), 2, rgba(20, 8, 104, 239), icon, 1);
                        } else if (resource.type == L"Url") {
                            Pen pen(icon, 1.1f); g.DrawEllipse(&pen, RectF(155, ry + 5, 14, 14));
                            line(g, 155, ry + 12, 169, ry + 12, icon, 1);
                            line(g, 162, ry + 5, 162, ry + 19, icon, 1);
                        } else {
                            rounded(g, RectF(156, ry + 5, 12, 14), 2, rgba(20, 8, 104, 239), icon, 1);
                            line(g, 159, ry + 11, 165, ry + 11, icon, 1);
                            line(g, 159, ry + 15, 165, ry + 15, icon, 1);
                        }
                        label(g, resourceLabel(resource), 177, ry, (std::max)(12.0f, rowRight() - 225), itemHeight - 2, 11, ink());
                        line(g, rowRight() - 40, ry + 6, rowRight() - 40, ry + itemHeight - 8,
                             rgba(130, 145, 181, 204), 1);
                        Color removeInk = menuActive ? rgba(255, 195, 72, 91) : rgba(215, 100, 123, 143);
                        float crossX = rowRight() - 25, crossY = menuY + 13;
                        line(g, crossX - 4, crossY - 4, crossX + 4, crossY + 4, removeInk, 1.5f);
                        line(g, crossX + 4, crossY - 4, crossX - 4, crossY + 4, removeInk, 1.5f);
                        resourceOffset += itemHeight;
                    }
                    float addY = y + row.cardHeight + 8 + resourceOffset;
                    label(g, L"＋ 添加资源", 152, addY, rowRight() - 164, 26, 11, blue());
                }
            }
            g.Restore(saved);
        }
        if (hasScroll_) {
            float bx = width_ - 38;
            rounded(g, RectF(bx, thumbY_, 6, thumbHeight_), 3,
                    draggingScroll_ || hoverAction_ == HitKind::ScrollThumb ?
                    rgba(210, 8, 104, 239) : rgba(145, 82, 126, 156));
        }

        float footerY = height_ - 68;
        if (adding_) {
            rounded(g, RectF(27, footerY - 33, width_ - 54, 29), 9,
                    rgba(206, 255, 255, 255), rgba(150, 177, 207, 224), 1);
            std::wstring count = pendingResources_.empty() ? L"＋ 关联资源（文件 / 文件夹 / 网站）" :
                                  L"＋ 已关联 " + std::to_wstring(pendingResources_.size()) + L" 项 · 继续添加或移除";
            label(g, count, 37, footerY - 33, width_ - 74, 29, 11, blue());
            rounded(g, RectF(27, footerY, width_ - 54, 43), 11,
                    rgba(246, 255, 255, 255), rgba(245, 121, 177, 227), 1);
            float dateWidth = width_ < 330 ? 64.0f : 82.0f;
            float dateRight = 34 + dateWidth;
            rounded(g, RectF(34, footerY + 6, dateWidth, 31), 9,
                    addDue_.empty() ? rgba(226, 231, 243, 254) : rgba(237, 217, 241, 251),
                    addDue_.empty() ? rgba(167, 145, 184, 218) : rgba(195, 113, 183, 223), 1);
            Color dateInk = addDue_.empty() ? muted() : blue();
            line(g, 42, footerY + 15, 51, footerY + 15, dateInk, 1.2f);
            line(g, 42, footerY + 15, 42, footerY + 26, dateInk, 1.2f);
            line(g, 42, footerY + 26, 51, footerY + 26, dateInk, 1.2f);
            line(g, 51, footerY + 15, 51, footerY + 26, dateInk, 1.2f);
            line(g, 43, footerY + 18, 50, footerY + 18, dateInk, 1.0f);
            std::wstring dateCaption = addDue_.empty() ? L"日期" :
                std::to_wstring(std::stoi(addDue_.substr(5, 2))) + L"/" +
                std::to_wstring(std::stoi(addDue_.substr(8, 2)));
            label(g, dateCaption, 53, footerY + 6, dateWidth - 22, 31,
                  width_ < 330 ? 10.0f : 11.0f, dateInk, false, StringAlignmentCenter);
            line(g, dateRight + 6, footerY + 11, dateRight + 6, footerY + 32,
                 rgba(142, 177, 197, 215), 1);
            float okX = right - 60, noX = right - 30;
            if (hoverAction_ == HitKind::AddConfirm) rounded(g, RectF(okX, footerY + 6, 30, 30), 9, rgba(30, 0, 191, 181));
            if (hoverAction_ == HitKind::AddCancel) rounded(g, RectF(noX, footerY + 6, 30, 30), 9, rgba(34, 112, 169, 212));
            line(g, okX + 8, footerY + 21, okX + 13, footerY + 26, teal(), 2);
            line(g, okX + 13, footerY + 26, okX + 23, footerY + 15, teal(), 2);
            line(g, noX + 9, footerY + 16, noX + 21, footerY + 28, muted(), 1.7f);
            line(g, noX + 21, footerY + 16, noX + 9, footerY + 28, muted(), 1.7f);
        } else {
            roundedGradient(g, RectF(27, footerY, width_ - 54, 43), 11,
                            hoverAction_ == HitKind::Add ? rgba(255, 0, 86, 219) : blue(),
                            hoverAction_ == HitKind::Add ? rgba(255, 0, 158, 173) : rgba(255, 0, 171, 196));
            label(g, L"+  添加待办", 27, footerY, width_ - 54, 43, 13, white, true, StringAlignmentCenter);
        }
        line(g, width_ - 28, height_ - 21, width_ - 21, height_ - 28, rgba(128, 92, 138, 174), 1);
        line(g, width_ - 24, height_ - 21, width_ - 21, height_ - 24, rgba(128, 92, 138, 174), 1);

        if (menu_) {
            float x = menuX(), y = 75;
            for (int i = 6; i > 0; --i)
                rounded(g, RectF(x - i * 0.5f, y + i * 0.6f, 226.0f + i, 155.0f + i),
                        14.0f + i, rgba(static_cast<BYTE>(2 + i), 41, 76, 104));
            rounded(g, RectF(x, y, 226, 155), 14, rgba(248, 250, 253, 255), rgba(215, 217, 230, 239), 1);
            label(g, L"设置", x + 17, y + 10, 130, 18, 11, muted());
            if (hoverAction_ == HitKind::MenuStartup) rounded(g, RectF(x + 7, y + 32, 212, 39), 9, rgba(35, 112, 169, 212));
            if (hoverAction_ == HitKind::MenuFolder) rounded(g, RectF(x + 7, y + 76, 212, 34), 9, rgba(35, 112, 169, 212));
            if (hoverAction_ == HitKind::MenuReset) rounded(g, RectF(x + 7, y + 112, 212, 34), 9, rgba(35, 112, 169, 212));
            label(g, L"开机自启", x + 17, y + 35, 135, 32, 13, ink());
            bool on = startupEnabled();
            rounded(g, RectF(x + 178, y + 43, 34, 19), 9.5f,
                    on ? teal() : rgba(110, 137, 151, 164));
            rounded(g, RectF(x + (on ? 196.0f : 181.0f), y + 46, 13, 13), 6.5f, white);
            line(g, x + 15, y + 75, x + 211, y + 75, rgba(180, 216, 229, 237), 1);
            label(g, L"▱", x + 17, y + 81, 20, 26, 16, blue());
            label(g, L"打开数据文件夹", x + 43, y + 78, 165, 30, 13, ink());
            label(g, L"↻", x + 18, y + 117, 20, 26, 17, blue());
            label(g, L"恢复默认窗口", x + 43, y + 114, 165, 30, 13, ink());
        }
    }
    POINT source{0, 0}, destination{rect.left, rect.top};
    SIZE size{pxWidth, pxHeight};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd_, screen, &destination, &size, memory, &source, 0, &blend, ULW_ALPHA);
    SelectObject(memory, previous); DeleteObject(dib); DeleteDC(memory); ReleaseDC(nullptr, screen);
    positionEditors();
}

void MainWindow::positionEditors() {
    if (!hwnd_) return;
    POINT origin{0, 0}; ClientToScreen(hwnd_, &origin);
    auto place = [](HWND host, int x, int y, int w, int h) {
        RECT current{}; GetWindowRect(host, &current);
        bool visible = IsWindowVisible(host) != FALSE;
        if (visible && current.left == x && current.top == y &&
            current.right - current.left == w && current.bottom - current.top == h) return;
        SetWindowPos(host, visible ? nullptr : HWND_TOP, x, y, w, h,
                     SWP_NOACTIVATE | (visible ? SWP_NOZORDER : SWP_SHOWWINDOW));
        // An owned popup and its EDIT child paint separately from the layered widget.
        // Paint the child after the host whenever the popup is first shown or moved.
        if (HWND edit = GetWindow(host, GW_CHILD))
            RedrawWindow(edit, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    };
    if (adding_ && addHost_) {
        float dateWidth = width_ < 330 ? 64.0f : 82.0f;
        float textX = 34 + dateWidth + 14;
        int x = origin.x + pixel(textX, scale_), y = origin.y + pixel(height_ - 61, scale_);
        int w = pixel((std::max)(24.0f, width_ - 96 - textX), scale_);
        place(addHost_, x, y, w, pixel(28, scale_));
    } else if (addHost_) ShowWindow(addHost_, SW_HIDE);

    if (editing_ >= 0 && rowHost_ && static_cast<size_t>(editing_) < rows_.size()) {
        const Row& row = rows_[editing_];
        float y = listTop() + row.y - scroll_;
        if (y + row.cardHeight <= listTop() || y >= listBottom()) {
            // An edit stays within the scroll viewport; moving it offscreen commits it.
            commitEdit(); return;
        }
        float editTop = (std::max)(y + 10.0f, listTop() + 2.0f);
        float editBottom = (std::min)(y + row.cardHeight - 9.0f, listBottom() - 2.0f);
        float textX = todos_[editing_].resources.empty() ? 145.0f : 165.0f;
        float w = (std::max)(30.0f, rowRight() - 13 - textX);
        place(rowHost_, origin.x + pixel(textX, scale_), origin.y + pixel(editTop, scale_),
              pixel(w, scale_), pixel((std::max)(20.0f, editBottom - editTop), scale_));
    } else if (rowHost_) ShowWindow(rowHost_, SW_HIDE);
}

bool MainWindow::ensureEditor(bool multiline) {
    HWND& host = multiline ? rowHost_ : addHost_;
    HWND& editor = multiline ? rowEdit_ : addEdit_;
    if (host) return true;
    host = CreateWindowExW(WS_EX_TOOLWINDOW, editHostClass, L"", WS_POPUP | WS_CLIPCHILDREN,
                           0, 0, 1, 1, hwnd_, nullptr, instance_, this);
    if (!host) return false;
    DWORD style = WS_CHILD | WS_VISIBLE | (multiline ? ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN : ES_AUTOHSCROLL);
    editor = CreateWindowExW(0, L"EDIT", L"", style, 2, 2, 1, 1, host,
                             reinterpret_cast<HMENU>(static_cast<UINT_PTR>(multiline ? editRow : editAdd)), instance_, nullptr);
    if (!editor) { DestroyWindow(host); host = nullptr; return false; }
    SendMessageW(editor, WM_SETFONT, reinterpret_cast<WPARAM>(editFont_), FALSE);
    SendMessageW(editor, EM_LIMITTEXT, 200, 0);
    SendMessageW(editor, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(2, 2));
    SetWindowSubclass(editor, EditProc, 1, reinterpret_cast<DWORD_PTR>(this));
    return true;
}

MainWindow::Hit MainWindow::hit(float x, float y) const {
    if (menu_) {
        float mx = menuX();
        if (x >= mx && x <= mx + 226 && y >= 75 && y <= 230) {
            if (y >= 107 && y < 146) return {HitKind::MenuStartup};
            if (y >= 151 && y < 185) return {HitKind::MenuFolder};
            if (y >= 187 && y < 223) return {HitKind::MenuReset};
            return {};
        }
    }
    float right = width_ - 27;
    if (y >= 32 && y <= 62) {
        if (x >= right - 62 && x < right - 32) return {HitKind::Settings};
        if (x >= right - 30 && x <= right) return {HitKind::Close};
    }
    if (y >= height_ - 68 && y <= height_ - 25 && x >= 27 && x <= right) {
        if (!adding_) return {HitKind::Add};
        if (x >= 33 && x <= 34 + (width_ < 330 ? 64.0f : 82.0f)) return {HitKind::AddChooseDate};
        if (x >= right - 60 && x < right - 30) return {HitKind::AddConfirm};
        if (x >= right - 30) return {HitKind::AddCancel};
        return {};
    }
    if (adding_ && y >= height_ - 101 && y <= height_ - 72 && x >= 27 && x <= right)
        return {HitKind::AddResource};
    if (y < listTop() || y > listBottom()) return {};
    if (hasScroll_ && x >= width_ - 40 && x <= width_ - 29) {
        return {y >= thumbY_ && y <= thumbY_ + thumbHeight_ ? HitKind::ScrollThumb : HitKind::ScrollTrack,
                y < thumbY_ ? 1u : 0u};
    }
    for (const Row& row : rows_) {
        float rowY = listTop() + row.y - scroll_;
        if (y >= rowY && y <= rowY + row.height && x >= 27 && x <= rowRight()) {
            if (y >= rowY + row.cardHeight) {
                if (x < 148) return {};
                float offset = y - rowY - row.cardHeight - 7;
                if (offset < 0) return {};
                float top = 0;
                for (size_t resource = 0; resource < row.resourceHeights.size(); ++resource) {
                    top += row.resourceHeights[resource];
                    if (offset < top) {
                        if (x >= rowRight() - 40) return {HitKind::ResourceRemove, row.index, resource};
                        return {HitKind::ResourceOpen, row.index, resource};
                    }
                }
                return {HitKind::ResourceAdd, row.index};
            }
            if (x < 63) return {HitKind::Check, row.index};
            if (x < 142) return {HitKind::Due, row.index};
            if (x >= rowRight() - 41) return {HitKind::Delete, row.index};
            if (!todos_[row.index].resources.empty() && x < 164) return {HitKind::ExpandResources, row.index};
            return {HitKind::Row, row.index};
        }
    }
    return {};
}

void MainWindow::beginAdd() {
    if (editing_ >= 0) commitEdit();
    if (!ensureEditor(false)) return;
    menu_ = false; adding_ = true; addDue_.clear(); pendingResources_.clear(); SetWindowTextW(addEdit_, L"");
    redraw(); SetFocus(addEdit_);
}

void MainWindow::commitAdd() {
    if (!adding_) return;
    auto content = trim(editText(addEdit_));
    if (content.empty()) return;
    Todo todo = Store::makeTodo(content, todos_.size());
    todo.dueAt = addDue_;
    todo.resources = std::move(pendingResources_);
    std::wstring id = todo.original.find(L"Id")->stringOr();
    if (!todo.resources.empty()) expandedId_ = id;
    todos_.push_back(std::move(todo));
    Store::sortTodos(todos_);
    store_.saveTodos(todos_);
    scheduleDeadlineRefresh();
    adding_ = false;
    hoverRow_ = -1;
    ignoreEditFocus_ = true; ShowWindow(addHost_, SW_HIDE); SetFocus(hwnd_); ignoreEditFocus_ = false;
    for (size_t i = 0; i < todos_.size(); ++i) {
        const Json* current = todos_[i].original.find(L"Id");
        if (current && current->stringOr() == id) { revealRow(i); break; }
    }
    redraw();
}

void MainWindow::cancelAdd() {
    if (!adding_) return;
    adding_ = false;
    pendingResources_.clear();
    ignoreEditFocus_ = true; ShowWindow(addHost_, SW_HIDE); SetFocus(hwnd_); ignoreEditFocus_ = false;
    redraw();
}

void MainWindow::beginEdit(size_t index) {
    if (index >= todos_.size()) return;
    if (editing_ >= 0) commitEdit();
    if (adding_) cancelAdd();
    if (!ensureEditor(true)) return;
    menu_ = false;
    revealRow(index);
    // SetWindowText sends EN_CHANGE synchronously; populate before entering edit mode.
    SetWindowTextW(rowEdit_, todos_[index].text.c_str());
    editing_ = static_cast<int>(index);
    redraw(); SetFocus(rowEdit_);
    SendMessageW(rowEdit_, EM_SETSEL, 0, -1);
    RedrawWindow(rowEdit_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
}

void MainWindow::commitEdit() {
    if (editing_ < 0) return;
    auto content = trim(editText(rowEdit_));
    if (!content.empty() && content != todos_[editing_].text) {
        todos_[editing_].text = content;
        store_.saveTodos(todos_);
    }
    editing_ = -1;
    ignoreEditFocus_ = true; ShowWindow(rowHost_, SW_HIDE); ignoreEditFocus_ = false;
    PostMessageW(hwnd_, commandResort, 0, 0);
    redraw();
}

void MainWindow::cancelEdit() {
    if (editing_ < 0) return;
    editing_ = -1;
    ignoreEditFocus_ = true; ShowWindow(rowHost_, SW_HIDE); SetFocus(hwnd_); ignoreEditFocus_ = false;
    PostMessageW(hwnd_, commandResort, 0, 0);
    redraw();
}

void MainWindow::sortIdleTodos() {
    if (editing_ >= 0 || picker_ || resourceDialogOpen_ || !Store::sortTodos(todos_)) return;
    updateResourceTooltip({});
    hoverRow_ = -1; hoverAction_ = HitKind::None;
    store_.saveTodos(todos_);
}

void MainWindow::scheduleDeadlineRefresh() {
    if (!hwnd_) return;
    KillTimer(hwnd_, timerDeadlineRefresh);
    SYSTEMTIME now{}; GetLocalTime(&now);
    wchar_t clock[12]{};
    swprintf_s(clock, L"T%02u:%02u:%02u", now.wHour, now.wMinute, now.wSecond);
    const std::wstring nowKey = DateString(now.wYear, now.wMonth, now.wDay) + clock;
    std::vector<std::wstring> dueDates;
    for (const auto& todo : todos_) {
        if (!todo.completed && !todo.dueAt.empty()) dueDates.push_back(todo.dueAt);
    }
    long long nextSecond = SecondsUntilNextRefresh(dueDates, nowKey);
    if (nextSecond == (std::numeric_limits<long long>::max)()) return;
    long long delay = (std::max)(1LL, nextSecond * 1000 - now.wMilliseconds);
    SetTimer(hwnd_, timerDeadlineRefresh, static_cast<UINT>(delay), nullptr);
}

void MainWindow::revealRow(size_t index) {
    layout();
    if (index >= rows_.size()) return;
    float top = rows_[index].y, bottom = top + rows_[index].height;
    if (rows_[index].height >= listHeight() || top < scroll_) scroll_ = top;
    else if (bottom > scroll_ + listHeight()) scroll_ = bottom - listHeight();
    scroll_ = (std::clamp)(scroll_, 0.0f, (std::max)(0.0f, contentHeight_ - listHeight()));
}

void MainWindow::beginDeadline(int target) {
    if (picker_ || (target >= 0 && static_cast<size_t>(target) >= todos_.size())) return;
    if (editing_ >= 0) commitEdit();
    if (target >= 0 && adding_) cancelAdd();
    if (target == -1 && !adding_) return;
    menu_ = false;
    pickerTarget_ = target;
    picker_ = std::make_unique<DeadlinePicker>();
    const std::wstring& existing = target == -1 ? addDue_ : todos_[target].dueAt;
    if (!picker_->show(hwnd_, instance_, scale_, existing)) {
        picker_.reset(); pickerTarget_ = -2;
        MessageBoxW(hwnd_, L"无法打开日期选择器。", L"DesktopTodo", MB_OK | MB_ICONWARNING);
    }
    redraw();
}

void MainWindow::finishDeadline(bool save) {
    if (!picker_) return;
    std::wstring selected = save ? picker_->selection() : L"";
    picker_.reset();
    int target = pickerTarget_;
    pickerTarget_ = -2;
    if (!save) { sortIdleTodos(); scheduleDeadlineRefresh(); redraw(); return; }
    if (target == -1 && adding_) {
        addDue_ = selected;
        if (addEdit_) SetFocus(addEdit_);
        sortIdleTodos();
    } else if (target >= 0 && static_cast<size_t>(target) < todos_.size()) {
        std::wstring id = todos_[target].original.find(L"Id") ? todos_[target].original.find(L"Id")->stringOr() : L"";
        todos_[target].dueAt = selected;
        Store::sortTodos(todos_);
        store_.saveTodos(todos_);
        hoverRow_ = -1;
        if (!id.empty()) {
            for (size_t i = 0; i < todos_.size(); ++i) {
                const Json* current = todos_[i].original.find(L"Id");
                if (current && current->stringOr() == id) { revealRow(i); break; }
            }
        }
    }
    scheduleDeadlineRefresh();
    redraw();
}

void MainWindow::chooseResource(int target) {
    if (target >= 0 && static_cast<size_t>(target) >= todos_.size()) return;
    if (target == -1 && !adding_) return;
    if (resourceMenuPopup_ || resourceEditor_ || confirmation_) return;
    if (editing_ >= 0) commitEdit();
    std::vector<ResourceUI::MenuItem> options{
        {L"文件", menuFile}, {L"文件夹", menuFolder}, {L"网站", menuUrl}
    };
    if (target == -1 && !pendingResources_.empty()) {
        options.push_back({L"", 0, true});
        options.push_back({L"已关联资源（点击移除）", 0});
        for (size_t i = 0; i < pendingResources_.size() && i < 100; ++i) {
            std::wstring name = resourceLabel(pendingResources_[i]);
            if (name.size() > 25) name = name.substr(0, 23) + L"…";
            options.push_back({name, menuPendingBase + static_cast<UINT>(i), false, true});
        }
    }
    POINT p{}; GetCursorPos(&p);
    auto popup = std::make_unique<ResourceUI::Menu>();
    if (!popup->show(hwnd_, instance_, scale_, std::move(options), p)) return;
    resourceTarget_ = target;
    resourceDialogOpen_ = true;
    resourceMenuPopup_ = std::move(popup);
    SetFocus(hwnd_); // The menu remains nonactivating; Esc and arrows go to the owner.
}

bool MainWindow::beginResourceEditor(Resource resource) {
    auto popup = std::make_unique<ResourceUI::Editor>();
    if (!popup->show(hwnd_, instance_, scale_, std::move(resource))) return false;
    resourceDialogOpen_ = true;
    resourceEditor_ = std::move(popup);
    return true;
}

void MainWindow::addSelectedResource(Resource resource) {
    if (resourceTarget_ == -1 && adding_) pendingResources_.push_back(std::move(resource));
    else if (resourceTarget_ >= 0 && static_cast<size_t>(resourceTarget_) < todos_.size()) {
        Todo& todo = todos_[resourceTarget_];
        todo.resources.push_back(std::move(resource));
        if (const Json* id = todo.original.find(L"Id")) expandedId_ = id->stringOr();
        store_.saveTodos(todos_);
        revealRow(static_cast<size_t>(resourceTarget_));
    }
    redraw();
}

void MainWindow::finishResourceInteraction() {
    resourceDialogOpen_ = false;
    resourceTarget_ = -2;
    if (adding_ && addEdit_ && IsWindowVisible(addHost_)) SetFocus(addEdit_);
    PostMessageW(hwnd_, commandResort, 0, 0);
}

void MainWindow::dismissResourcePopup() {
    if (resourceMenuPopup_) resourceMenuPopup_->dismiss();
    if (resourceEditor_) resourceEditor_->dismiss();
    if (confirmation_) confirmation_->dismiss();
}

void MainWindow::finishResourceMenu(UINT command) {
    if (!resourceMenuPopup_) return;
    resourceMenuPopup_.reset();
    if (command >= menuPendingBase && resourceTarget_ == -1 &&
        command - menuPendingBase < pendingResources_.size()) {
        pendingResources_.erase(pendingResources_.begin() + (command - menuPendingBase));
        redraw();
    } else if (command == menuUrl) {
        Resource resource; resource.type = L"Url";
        if (beginResourceEditor(std::move(resource))) return;
    } else if (command == menuFile || command == menuFolder) {
        Resource resource;
        resource.type = command == menuFile ? L"File" : L"Folder";
        if (pickResourceTarget(hwnd_, resource)) addSelectedResource(std::move(resource));
    }
    finishResourceInteraction();
}

void MainWindow::finishResourceEditor(bool save) {
    if (!resourceEditor_) return;
    Resource resource = resourceEditor_->selection();
    resourceEditor_.reset();
    if (save) addSelectedResource(std::move(resource));
    finishResourceInteraction();
}

void MainWindow::openResource(size_t index, size_t resource) {
    if (index >= todos_.size() || resource >= todos_[index].resources.size()) return;
    const Resource& link = todos_[index].resources[resource];
    if (link.type != L"Url") {
        std::error_code error;
        if (!std::filesystem::exists(std::filesystem::path(link.target), error)) {
            MessageBoxW(hwnd_, L"文件或文件夹不存在，可能已移动。可移除关联后重新添加。",
                        L"无法打开资源", MB_OK | MB_ICONINFORMATION);
            return;
        }
    }
    if (link.type == L"File") {
        std::wstring argument = L"/select,\"" + link.target + L"\"";
        if (reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd_, L"open", L"explorer.exe",
                                                  argument.c_str(), nullptr, SW_SHOWNORMAL)) > 32) return;
    } else if (reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd_, L"open", link.target.c_str(),
                                                         nullptr, nullptr, SW_SHOWNORMAL)) > 32) return;
    MessageBoxW(hwnd_, L"无法打开此资源，请检查地址或默认打开程序。", L"DesktopTodo", MB_OK | MB_ICONWARNING);
}

void MainWindow::confirmRemoval(size_t index, bool resource, size_t resourceIndex) {
    if (index >= todos_.size() || (resource && resourceIndex >= todos_[index].resources.size())) return;
    if (resourceMenuPopup_ || resourceEditor_ || confirmation_ || picker_) return;
    POINT p{}; GetCursorPos(&p);
    auto popup = std::make_unique<ResourceUI::Confirm>();
    if (!popup->show(hwnd_, instance_, scale_, resource, p)) return;
    confirmationIndex_ = index;
    confirmationResource_ = resourceIndex;
    confirmingResource_ = resource;
    resourceDialogOpen_ = true;
    confirmation_ = std::move(popup);
}

void MainWindow::finishConfirmation(bool accepted) {
    if (!confirmation_) return;
    confirmation_.reset();
    if (accepted && confirmationIndex_ < todos_.size()) {
        updateResourceTooltip({});
        if (confirmingResource_) {
            auto& resources = todos_[confirmationIndex_].resources;
            if (confirmationResource_ < resources.size()) {
                resources.erase(resources.begin() + confirmationResource_);
                if (resources.empty()) expandedId_.clear();
                store_.saveTodos(todos_);
            }
        } else {
            if (const Json* id = todos_[confirmationIndex_].original.find(L"Id"))
                if (expandedId_ == id->stringOr()) expandedId_.clear();
            todos_.erase(todos_.begin() + confirmationIndex_);
            store_.saveTodos(todos_);
            scheduleDeadlineRefresh();
        }
        hoverRow_ = -1;
        hoverResource_ = -1;
        hoverAction_ = HitKind::None;
    }
    finishResourceInteraction();
    redraw();
}

void MainWindow::updateResourceTooltip(const Hit& hit) {
    if (!tooltip_) return;
    std::wstring target;
    if (hit.kind == HitKind::ResourceOpen &&
        hit.index < todos_.size() && hit.resource < todos_[hit.index].resources.size())
        target = todos_[hit.index].resources[hit.resource].target;
    if (target == tooltipText_) return;
    TOOLINFOW info{sizeof(info)};
    info.uFlags = TTF_TRACK | TTF_ABSOLUTE;
    info.hwnd = hwnd_; info.uId = 1;
    SendMessageW(tooltip_, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&info));
    tooltipText_ = std::move(target);
    if (tooltipText_.empty()) return;
    info.lpszText = const_cast<LPWSTR>(tooltipText_.c_str());
    SendMessageW(tooltip_, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&info));
    POINT p{}; GetCursorPos(&p);
    SendMessageW(tooltip_, TTM_TRACKPOSITION, 0, MAKELPARAM(p.x + 12, p.y + 19));
    SendMessageW(tooltip_, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&info));
}

void MainWindow::click(const Hit& action) {
    switch (action.kind) {
        case HitKind::Close: PostMessageW(hwnd_, WM_CLOSE, 0, 0); break;
        case HitKind::Settings:
            if (editing_ >= 0) commitEdit();
            menu_ = !menu_; redraw(); break;
        case HitKind::MenuStartup:
            if (!setStartup(!startupEnabled()))
                MessageBoxW(hwnd_, L"无法修改开机自启设置，请检查当前用户权限。", L"DesktopTodo", MB_OK | MB_ICONWARNING);
            redraw(); break;
        case HitKind::MenuFolder: menu_ = false; openDataFolder(); redraw(); break;
        case HitKind::MenuReset: menu_ = false; resetPlacement(); break;
        case HitKind::Add: beginAdd(); break;
        case HitKind::AddConfirm: commitAdd(); break;
        case HitKind::AddCancel: cancelAdd(); break;
        case HitKind::AddChooseDate: beginDeadline(-1); break;
        case HitKind::AddResource: chooseResource(-1); break;
        case HitKind::ResourceAdd: chooseResource(static_cast<int>(action.index)); break;
        case HitKind::ResourceOpen:
            if (editing_ >= 0) commitEdit();
            openResource(action.index, action.resource); break;
        case HitKind::ResourceRemove:
            if (editing_ >= 0) commitEdit();
            confirmRemoval(action.index, true, action.resource); break;
        case HitKind::ExpandResources:
            if (editing_ >= 0) commitEdit();
            if (action.index < todos_.size()) {
                const Json* id = todos_[action.index].original.find(L"Id");
                if (id) expandedId_ = expandedId_ == id->stringOr() ? L"" : id->stringOr();
                revealRow(action.index); redraw();
            }
            break;
        case HitKind::Due: beginDeadline(static_cast<int>(action.index)); break;
        case HitKind::Check:
            if (editing_ >= 0) commitEdit();
            if (action.index < todos_.size()) {
                updateResourceTooltip({});
                if (const Json* id = todos_[action.index].original.find(L"Id"))
                    if (expandedId_ == id->stringOr()) expandedId_.clear();
                todos_[action.index].completed = !todos_[action.index].completed;
                todos_[action.index].completedAt = todos_[action.index].completed ? Store::localTimestamp() : L"";
                Store::sortTodos(todos_); hoverRow_ = -1;
                store_.saveTodos(todos_); scheduleDeadlineRefresh(); redraw();
            }
            break;
        case HitKind::Delete:
            if (editing_ >= 0) commitEdit();
            confirmRemoval(action.index, false); 
            break;
        case HitKind::ScrollThumb:
            draggingScroll_ = true; scrollGrab_ = 0; SetCapture(hwnd_); break;
        case HitKind::ScrollTrack:
            updateResourceTooltip({});
            scroll_ += (action.index ? -1 : 1) * listHeight();
            scroll_ = (std::clamp)(scroll_, 0.0f, (std::max)(0.0f, contentHeight_ - listHeight()));
            redraw(); break;
        default: break;
    }
}

void MainWindow::snap() {
    RECT current{}, work{}; GetWindowRect(hwnd_, &current);
    HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    if (GetMonitorInfoW(monitor, &mi)) work = mi.rcWork;
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int x = current.left, y = current.top;
    int right = work.right - pixel(16, scale_), bottom = work.bottom - pixel(16, scale_);
    if (abs(current.right - right) <= pixel(36, scale_)) x = right - (current.right - current.left);
    if (abs(current.bottom - bottom) <= pixel(36, scale_)) y = bottom - (current.bottom - current.top);
    if (x != current.left || y != current.top) SetWindowPos(hwnd_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void MainWindow::resizeToCursor() {
    POINT cursor{};
    if (!GetCursorPos(&cursor)) return;
    RECT next = sizingStart_;
    const int dx = cursor.x - sizingPointer_.x;
    const int dy = cursor.y - sizingPointer_.y;
    const bool left = sizingEdge_ == HTLEFT || sizingEdge_ == HTTOPLEFT || sizingEdge_ == HTBOTTOMLEFT;
    const bool right = sizingEdge_ == HTRIGHT || sizingEdge_ == HTTOPRIGHT || sizingEdge_ == HTBOTTOMRIGHT;
    const bool top = sizingEdge_ == HTTOP || sizingEdge_ == HTTOPLEFT || sizingEdge_ == HTTOPRIGHT;
    const bool bottom = sizingEdge_ == HTBOTTOM || sizingEdge_ == HTBOTTOMLEFT || sizingEdge_ == HTBOTTOMRIGHT;
    if (left) next.left += dx;
    if (right) next.right += dx;
    if (top) next.top += dy;
    if (bottom) next.bottom += dy;
    const int minWidth = pixel(260, scale_), minHeight = pixel(220, scale_);
    if (next.right - next.left < minWidth) {
        if (left) next.left = next.right - minWidth;
        else next.right = next.left + minWidth;
    }
    if (next.bottom - next.top < minHeight) {
        if (top) next.top = next.bottom - minHeight;
        else next.bottom = next.top + minHeight;
    }
    RECT current{};
    GetWindowRect(hwnd_, &current);
    if (EqualRect(&next, &current)) return;
    SetWindowPos(hwnd_, nullptr, next.left, next.top, next.right - next.left, next.bottom - next.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void MainWindow::finishResize() {
    if (!sizingEdge_) return;
    sizingEdge_ = 0;
    if (GetCapture() == hwnd_) ReleaseCapture();
    snap(); savePlacement(); redraw();
}

void MainWindow::savePlacement() {
    if (!hwnd_ || IsIconic(hwnd_)) return;
    RECT rect{}; GetWindowRect(hwnd_, &rect);
    placement_.saved = true;
    placement_.x = rect.left / scale_; placement_.y = rect.top / scale_;
    placement_.width = (rect.right - rect.left) / scale_;
    placement_.height = (rect.bottom - rect.top) / scale_;
    store_.savePlacement(placement_);
}

void MainWindow::resetPlacement() {
    RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int w = (std::min)(pixel(426, scale_), static_cast<int>(work.right - work.left));
    int h = (std::min)(pixel(460, scale_), static_cast<int>(work.bottom - work.top));
    int x = work.right - w - pixel(16, scale_), y = work.bottom - h - pixel(16, scale_);
    SetWindowPos(hwnd_, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    savePlacement(); redraw();
}

std::wstring MainWindow::currentExecutable() const {
    std::wstring path(32768, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) return L"";
    path.resize(length); return path;
}

bool MainWindow::startupEnabled() const {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, runKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    wchar_t value[32768]{}; DWORD bytes = sizeof(value), type = REG_SZ;
    LONG result = RegQueryValueExW(key, L"DesktopTodo", nullptr, &type, reinterpret_cast<BYTE*>(value), &bytes);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS || type != REG_SZ || bytes < sizeof(wchar_t)) return false;
    value[32767] = L'\0';
    std::wstring exe = currentExecutable();
    return !exe.empty() && _wcsicmp(value, (L"\"" + exe + L"\"").c_str()) == 0;
}

bool MainWindow::setStartup(bool enabled) const {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, runKey, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    LONG result;
    if (enabled) {
        std::wstring exe = currentExecutable();
        std::wstring cmd = L"\"" + exe + L"\"";
        result = exe.empty() ? ERROR_FILE_NOT_FOUND :
            RegSetValueExW(key, L"DesktopTodo", 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(cmd.c_str()),
                           static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    } else result = RegDeleteValueW(key, L"DesktopTodo");
    RegCloseKey(key);
    return result == ERROR_SUCCESS || (!enabled && result == ERROR_FILE_NOT_FOUND);
}

void MainWindow::migrateStartup() const {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, runKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return;
    DWORD size = 0;
    LONG result = RegQueryValueExW(key, L"DesktopTodo", nullptr, nullptr, nullptr, &size);
    RegCloseKey(key);
    if (result == ERROR_SUCCESS && size > sizeof(wchar_t) && !startupEnabled()) setStartup(true);
}

void MainWindow::openDataFolder() const {
    try {
        std::filesystem::create_directories(std::filesystem::path(store_.folder()));
        ShellExecuteW(hwnd_, L"open", store_.folder().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } catch (const std::exception&) {
        MessageBoxW(hwnd_, L"无法打开数据文件夹。", L"DesktopTodo", MB_OK | MB_ICONWARNING);
    }
}

LRESULT CALLBACK MainWindow::WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* info = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<MainWindow*>(info->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->message(message, wparam, lparam) : DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT CALLBACK MainWindow::EditorHostProc(HWND host, UINT msg, WPARAM wp, LPARAM lp) {
    auto* owner = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(host, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
        owner = static_cast<MainWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(host, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(owner));
    }
    if (owner && msg == WM_COMMAND) return SendMessageW(owner->hwnd_, msg, wp, lp);
    if (owner && msg == WM_CTLCOLOREDIT) return SendMessageW(owner->hwnd_, msg, wp, lp);
    if (owner && msg == WM_MOUSEWHEEL) return SendMessageW(owner->hwnd_, msg, wp, lp);
    // Only WM_PAINT fills the host's exposed two-pixel border; erasing the full
    // window here can cover the EDIT child before it has a chance to repaint.
    if (msg == WM_ERASEBKGND && owner) return 1;
    if (msg == WM_PAINT && owner) {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(host, &ps);
        FillRect(dc, &ps.rcPaint,
                 host == owner->rowHost_ ? owner->rowEditBrush_ : owner->editBrush_);
        EndPaint(host, &ps);
        if (HWND edit = GetWindow(host, GW_CHILD))
            RedrawWindow(edit, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        return 0;
    }
    if (msg == WM_SIZE) {
        HWND edit = GetWindow(host, GW_CHILD);
        if (edit) {
            RECT old{}; GetWindowRect(edit, &old);
            int w = (std::max)(1, LOWORD(lp) - 4), h = (std::max)(1, HIWORD(lp) - 4);
            if (old.right - old.left != w || old.bottom - old.top != h)
                MoveWindow(edit, 2, 2, w, h, TRUE);
        }
        return 0;
    }
    return DefWindowProcW(host, msg, wp, lp);
}

LRESULT CALLBACK MainWindow::EditProc(HWND editor, UINT message, WPARAM wparam, LPARAM lparam,
                                      UINT_PTR, DWORD_PTR user) {
    auto* self = reinterpret_cast<MainWindow*>(user);
    if (message == WM_KEYDOWN) {
        if (self->resourceMenuPopup_ && self->resourceMenuPopup_->key(static_cast<UINT>(wparam))) return 0;
        if (wparam == VK_RETURN || wparam == VK_ESCAPE) {
            PostMessageW(self->hwnd_, wparam == VK_RETURN ? commandCommit : commandCancel,
                         editor == self->rowEdit_ ? editRow : editAdd, 0);
            return 0;
        }
    }
    if (message == WM_MOUSEWHEEL) {
        SendMessageW(self->hwnd_, message, wparam, lparam);
        return 0;
    }
    return DefSubclassProc(editor, message, wparam, lparam);
}

LRESULT MainWindow::message(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            editBrush_ = CreateSolidBrush(RGB(255, 255, 255));
            rowEditBrush_ = CreateSolidBrush(RGB(242, 249, 255));
            editFont_ = CreateFontW(-pixel(13, scale_), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
            tooltip_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, TOOLTIPS_CLASSW, nullptr,
                                      WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                                      CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                      hwnd_, nullptr, instance_, nullptr);
            if (tooltip_) {
                TOOLINFOW info{sizeof(info)};
                info.uFlags = TTF_TRACK | TTF_ABSOLUTE;
                info.hwnd = hwnd_; info.uId = 1; info.lpszText = const_cast<LPWSTR>(L"");
                SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
                SendMessageW(tooltip_, TTM_SETMAXTIPWIDTH, 0, 440);
            }
            scheduleDeadlineRefresh();
            return 0;
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wp);
            bool row = reinterpret_cast<HWND>(lp) == rowEdit_;
            SetBkColor(dc, row ? RGB(242, 249, 255) : RGB(255, 255, 255));
            SetTextColor(dc, RGB(33, 55, 77));
            return reinterpret_cast<LRESULT>(row ? rowEditBrush_ : editBrush_);
        }
        case WM_NCHITTEST: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}; ScreenToClient(hwnd_, &p);
            RECT rc{}; GetClientRect(hwnd_, &rc);
            float x = p.x / scale_, y = p.y / scale_;
            float w = (rc.right - rc.left) / scale_, h = (rc.bottom - rc.top) / scale_;
            constexpr float edge = 8, corner = 14;
            if (x < corner && y < corner) return HTTOPLEFT;
            if (x >= w - corner && y < corner) return HTTOPRIGHT;
            if (x < corner && y >= h - corner) return HTBOTTOMLEFT;
            if (x >= w - corner && y >= h - corner) return HTBOTTOMRIGHT;
            if (x < edge) return HTLEFT;
            if (x >= w - edge) return HTRIGHT;
            if (y < edge) return HTTOP;
            if (y >= h - edge) return HTBOTTOM;
            if (y >= 18 && y <= 72 && x >= 18 && x < w - 93 && !menu_) return HTCAPTION;
            return HTCLIENT;
        }
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lp);
            info->ptMinTrackSize = POINT{pixel(260, scale_), pixel(220, scale_)};
            return 0;
        }
        case WM_NCLBUTTONDOWN:
            if (resourceMenuPopup_ || resourceEditor_) { dismissResourcePopup(); return 0; }
            if (wp == HTLEFT || wp == HTRIGHT || wp == HTTOP || wp == HTBOTTOM ||
                wp == HTTOPLEFT || wp == HTTOPRIGHT || wp == HTBOTTOMLEFT || wp == HTBOTTOMRIGHT) {
                // The system can show only a drag outline for layered windows.
                // Resize the actual window on every pointer move for live preview.
                sizingEdge_ = static_cast<int>(wp);
                GetWindowRect(hwnd_, &sizingStart_);
                GetCursorPos(&sizingPointer_);
                SetCapture(hwnd_);
                return 0;
            }
            break;
        case WM_NCLBUTTONDBLCLK: return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{}; BeginPaint(hwnd_, &ps); EndPaint(hwnd_, &ps);
            redraw(); return 0;
        }
        case WM_SIZE: redraw(); return 0;
        case WM_MOVE: positionEditors(); return 0;
        case WM_EXITSIZEMOVE: snap(); savePlacement(); redraw(); return 0;
        case WM_DPICHANGED: {
            dismissResourcePopup();
            scale_ = HIWORD(wp) / 96.0f;
            HFONT newFont = CreateFontW(-pixel(13, scale_), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                         CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
            if (newFont) {
                for (HWND editor : {addEdit_, rowEdit_}) if (editor) SendMessageW(editor, WM_SETFONT, reinterpret_cast<WPARAM>(newFont), TRUE);
                if (editFont_) DeleteObject(editFont_);
                editFont_ = newFont;
            }
            auto* suggested = reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            savePlacement(); redraw(); return 0;
        }
        case WM_SETCURSOR:
            if (sizingEdge_) {
                LPCWSTR cursor = sizingEdge_ == HTLEFT || sizingEdge_ == HTRIGHT ? IDC_SIZEWE :
                                 sizingEdge_ == HTTOP || sizingEdge_ == HTBOTTOM ? IDC_SIZENS :
                                 sizingEdge_ == HTTOPLEFT || sizingEdge_ == HTBOTTOMRIGHT ? IDC_SIZENWSE : IDC_SIZENESW;
                SetCursor(LoadCursorW(nullptr, cursor)); return TRUE;
            }
            if (LOWORD(lp) == HTCLIENT) {
                POINT p{}; GetCursorPos(&p); ScreenToClient(hwnd_, &p);
                Hit h = hit(p.x / scale_, p.y / scale_);
                HCURSOR cursor = h.kind == HitKind::Row ? LoadCursorW(nullptr, IDC_IBEAM) :
                                 h.kind != HitKind::None ? LoadCursorW(nullptr, IDC_HAND) : LoadCursorW(nullptr, IDC_ARROW);
                SetCursor(cursor); return TRUE;
            }
            break;
        case WM_MOUSEMOVE: {
            if (sizingEdge_) { resizeToCursor(); return 0; }
            float x = GET_X_LPARAM(lp) / scale_, y = GET_Y_LPARAM(lp) / scale_;
            if (draggingScroll_) {
                float travel = listHeight() - thumbHeight_;
                if (travel > 0) {
                    float desired = (std::clamp)(y - scrollGrab_ - listTop(), 0.0f, travel);
                    scroll_ = desired / travel * (contentHeight_ - listHeight()); redraw();
                }
                return 0;
            }
            if (!tracking_) { TRACKMOUSEEVENT event{sizeof(event), TME_LEAVE, hwnd_, 0}; TrackMouseEvent(&event); tracking_ = true; }
            Hit h = hit(x, y);
            int row = h.kind == HitKind::Row || h.kind == HitKind::Due ||
                      h.kind == HitKind::Check || h.kind == HitKind::Delete ||
                      h.kind == HitKind::ExpandResources || h.kind == HitKind::ResourceOpen ||
                      h.kind == HitKind::ResourceRemove || h.kind == HitKind::ResourceAdd ?
                      static_cast<int>(h.index) : -1;
            int resource = h.kind == HitKind::ResourceOpen || h.kind == HitKind::ResourceRemove ?
                           static_cast<int>(h.resource) : -1;
            updateResourceTooltip(h);
            if (row != hoverRow_ || h.kind != hoverAction_ || resource != hoverResource_) {
                hoverRow_ = row; hoverAction_ = h.kind; hoverResource_ = resource; redraw();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            tracking_ = false; hoverRow_ = -1; hoverResource_ = -1;
            hoverAction_ = HitKind::None; updateResourceTooltip({}); redraw(); return 0;
        case WM_LBUTTONDOWN: {
            if (resourceMenuPopup_ || resourceEditor_ || confirmation_) { dismissResourcePopup(); return 0; }
            if (picker_) { picker_.reset(); pickerTarget_ = -2; sortIdleTodos(); scheduleDeadlineRefresh(); redraw(); return 0; }
            float x = GET_X_LPARAM(lp) / scale_, y = GET_Y_LPARAM(lp) / scale_;
            Hit h = hit(x, y);
            updateResourceTooltip({});
            if (menu_ && h.kind != HitKind::Settings && h.kind != HitKind::MenuStartup &&
                h.kind != HitKind::MenuFolder && h.kind != HitKind::MenuReset) {
                menu_ = false; redraw();
            }
            if (h.kind == HitKind::ScrollThumb) scrollGrab_ = y - thumbY_;
            click(h); return 0;
        }
        case WM_LBUTTONDBLCLK: {
            if (resourceMenuPopup_ || resourceEditor_ || confirmation_) { dismissResourcePopup(); return 0; }
            Hit h = hit(GET_X_LPARAM(lp) / scale_, GET_Y_LPARAM(lp) / scale_);
            if (picker_) {
                if ((h.kind == HitKind::Due && pickerTarget_ == static_cast<int>(h.index)) ||
                    (h.kind == HitKind::AddChooseDate && pickerTarget_ == -1)) return 0;
                picker_.reset(); pickerTarget_ = -2; sortIdleTodos(); scheduleDeadlineRefresh(); redraw(); return 0;
            }
            if (h.kind == HitKind::Row) beginEdit(h.index);
            else if (h.kind == HitKind::Due) beginDeadline(static_cast<int>(h.index));
            else if (h.kind != HitKind::ExpandResources && h.kind != HitKind::ResourceOpen &&
                     h.kind != HitKind::ResourceRemove && h.kind != HitKind::ResourceAdd &&
                     h.kind != HitKind::AddResource) click(h);
            return 0;
        }
        case WM_RBUTTONUP: {
            if (resourceMenuPopup_ || resourceEditor_ || confirmation_) { dismissResourcePopup(); return 0; }
            Hit h = hit(GET_X_LPARAM(lp) / scale_, GET_Y_LPARAM(lp) / scale_);
            if (h.kind == HitKind::ResourceOpen || h.kind == HitKind::ResourceRemove) return 0;
            if (h.kind == HitKind::Row || h.kind == HitKind::ExpandResources) {
                chooseResource(static_cast<int>(h.index)); return 0;
            }
            return 0;
        }
        case WM_LBUTTONUP:
            if (sizingEdge_) { resizeToCursor(); finishResize(); return 0; }
            if (draggingScroll_) { draggingScroll_ = false; ReleaseCapture(); redraw(); }
            return 0;
        case WM_CAPTURECHANGED:
            draggingScroll_ = false;
            if (sizingEdge_) finishResize();
            return 0;
        case WM_MOUSEWHEEL: {
            if (resourceMenuPopup_ || resourceEditor_ || confirmation_) { dismissResourcePopup(); return 0; }
            updateResourceTooltip({});
            if (editing_ >= 0) commitEdit();
            float step = GET_WHEEL_DELTA_WPARAM(wp) / static_cast<float>(WHEEL_DELTA) * 72;
            scroll_ = (std::clamp)(scroll_ - step, 0.0f, (std::max)(0.0f, contentHeight_ - listHeight()));
            redraw(); return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == editRow) {
                if (HIWORD(wp) == EN_KILLFOCUS && editing_ >= 0 && !ignoreEditFocus_) commitEdit();
                if (HIWORD(wp) == EN_CHANGE && editing_ >= 0) {
                    redraw();
                    if (editing_ >= 0)
                        RedrawWindow(rowEdit_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
                }
                return 0;
            }
            break;
        case commandCommit:
            if (wp == editRow) commitEdit(); else commitAdd(); return 0;
        case commandCancel:
            if (wp == editRow) cancelEdit(); else cancelAdd(); return 0;
        case commandResort:
            sortIdleTodos(); scheduleDeadlineRefresh(); redraw(); return 0;
        case ResourceUI::menuResult:
            finishResourceMenu(static_cast<UINT>(wp)); return 0;
        case ResourceUI::editorResult:
            finishResourceEditor(wp != 0); return 0;
        case ResourceUI::confirmResult:
            finishConfirmation(wp != 0); return 0;
        case DeadlinePicker::resultMessage:
            finishDeadline(wp != 0); return 0;
        case DesktopHost::changedMessage:
            if (desktop_) {
                desktop_->arrange();
                if (desktop_->isDesktopForeground()) { KillTimer(hwnd_, timerSettle); SetTimer(hwnd_, timerSettle, 160, nullptr); }
            }
            return 0;
        case WM_TIMER:
            if (wp == timerSettle) { KillTimer(hwnd_, timerSettle); if (desktop_) desktop_->arrange(); return 0; }
            if (wp == timerDeadlineRefresh) {
                sortIdleTodos();
                scheduleDeadlineRefresh();
                redraw(); return 0;
            }
            break;
        case WM_TIMECHANGE:
        case WM_SETTINGCHANGE:
            sortIdleTodos(); scheduleDeadlineRefresh(); redraw(); return 0;
        case WM_POWERBROADCAST:
            if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND || wp == PBT_APMRESUMECRITICAL) {
                sortIdleTodos(); scheduleDeadlineRefresh(); redraw(); return TRUE;
            }
            break;
        case WM_KEYDOWN:
            if (resourceMenuPopup_ && resourceMenuPopup_->key(static_cast<UINT>(wp))) return 0;
            if (wp == VK_ESCAPE && menu_) { menu_ = false; redraw(); return 0; }
            break;
        case WM_ACTIVATEAPP:
            if (!wp) dismissResourcePopup();
            break;
        case WM_CLOSE:
            resourceMenuPopup_.reset(); resourceEditor_.reset(); confirmation_.reset(); resourceDialogOpen_ = false;
            picker_.reset();
            if (editing_ >= 0) commitEdit();
            DestroyWindow(hwnd_); return 0;
        case WM_DESTROY:
            KillTimer(hwnd_, timerSettle); KillTimer(hwnd_, timerDeadlineRefresh);
            resourceMenuPopup_.reset(); resourceEditor_.reset(); confirmation_.reset();
            if (tooltip_) { DestroyWindow(tooltip_); tooltip_ = nullptr; }
            picker_.reset(); desktop_.reset();
            store_.saveTodos(todos_); savePlacement(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}
