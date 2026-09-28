#pragma once
#include "DesktopHost.h"
#include "Store.h"
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <memory>
#include <vector>

class DeadlinePicker;
namespace ResourceUI { class Menu; class Editor; class Confirm; }

class MainWindow {
    struct Row { float y, height, cardHeight; size_t index; std::vector<float> resourceHeights; };
    enum class HitKind { None, Row, Due, Check, Delete, Add, AddConfirm, AddCancel,
                         AddChooseDate, AddResource, ExpandResources, ResourceOpen,
                         ResourceRemove, ResourceAdd,
                         Settings, Close, MenuStartup, MenuFolder, MenuReset,
                         ScrollThumb, ScrollTrack };
    struct Hit { HitKind kind = HitKind::None; size_t index = 0, resource = 0; };

    HINSTANCE instance_;
    HWND hwnd_ = nullptr, addHost_ = nullptr, rowHost_ = nullptr;
    HWND tooltip_ = nullptr;
    HWND addEdit_ = nullptr, rowEdit_ = nullptr;
    HFONT editFont_ = nullptr;
    HBRUSH editBrush_ = nullptr, rowEditBrush_ = nullptr;
    Store store_;
    Placement placement_;
    std::vector<Todo> todos_;
    std::unique_ptr<DesktopHost> desktop_;
    std::unique_ptr<DeadlinePicker> picker_;
    std::unique_ptr<ResourceUI::Menu> resourceMenuPopup_;
    std::unique_ptr<ResourceUI::Editor> resourceEditor_;
    std::unique_ptr<ResourceUI::Confirm> confirmation_;
    int resourceTarget_ = -2;
    size_t confirmationIndex_ = 0, confirmationResource_ = 0;
    bool confirmingResource_ = false;
    std::wstring addDue_;
    std::vector<Resource> pendingResources_;
    std::wstring expandedId_;
    std::wstring tooltipText_;
    int pickerTarget_ = -2; // -1 = new task; >= 0 = existing row
    float scale_ = 1;
    float width_ = 426, height_ = 460;
    float scroll_ = 0, contentHeight_ = 0, thumbY_ = 0, thumbHeight_ = 0;
    bool hasScroll_ = false, adding_ = false, menu_ = false, tracking_ = false;
    bool draggingScroll_ = false, ignoreEditFocus_ = false;
    bool resourceDialogOpen_ = false;
    int sizingEdge_ = 0;
    RECT sizingStart_{};
    POINT sizingPointer_{};
    float scrollGrab_ = 0;
    int editing_ = -1;
    int hoverRow_ = -1;
    int hoverResource_ = -1;
    HitKind hoverAction_ = HitKind::None;
    std::vector<Row> rows_;

    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK EditorHostProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT message(UINT, WPARAM, LPARAM);
    void draw();
    void layout();
    void positionEditors();
    bool ensureEditor(bool multiline);
    Hit hit(float x, float y) const;
    void click(const Hit& hit);
    void beginAdd();
    void commitAdd();
    void cancelAdd();
    void beginEdit(size_t index);
    void commitEdit();
    void cancelEdit();
    void beginDeadline(int target);
    void finishDeadline(bool save);
    void chooseResource(int target);
    void confirmRemoval(size_t index, bool resource, size_t resourceIndex = 0);
    void finishConfirmation(bool accepted);
    void finishResourceMenu(UINT command);
    void finishResourceEditor(bool save);
    bool beginResourceEditor(Resource resource);
    void addSelectedResource(Resource resource);
    void finishResourceInteraction();
    void dismissResourcePopup();
    void openResource(size_t index, size_t resource);
    void updateResourceTooltip(const Hit& hit);
    void revealRow(size_t index);
    void sortIdleTodos();
    void scheduleDeadlineRefresh();
    void savePlacement();
    void resizeToCursor();
    void finishResize();
    void snap();
    void resetPlacement();
    bool startupEnabled() const;
    bool setStartup(bool enabled) const;
    void migrateStartup() const;
    void openDataFolder() const;
    std::wstring currentExecutable() const;
    float listTop() const { return 92; }
    float listBottom() const { return height_ - (adding_ ? 110.0f : 78.0f); }
    float listHeight() const { return (std::max)(1.0f, listBottom() - listTop()); }
    float rowRight() const { return width_ - 27 - (hasScroll_ ? 9 : 0); }
    float menuX() const { return width_ - 253; }
    void redraw() { if (hwnd_ && IsWindowVisible(hwnd_) && !IsIconic(hwnd_)) draw(); }
public:
    explicit MainWindow(HINSTANCE instance);
    ~MainWindow();
    bool createAndShow();
    HWND handle() const { return hwnd_; }
};
