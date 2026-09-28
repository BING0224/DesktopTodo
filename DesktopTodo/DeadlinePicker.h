#pragma once
#include <windows.h>
#include <string>

class DeadlinePicker {
    HWND hwnd_ = nullptr, owner_ = nullptr;
    HINSTANCE instance_ = nullptr;
    float scale_ = 1;
    int year_ = 0, month_ = 0, day_ = 0;
    int shownYear_ = 0, shownMonth_ = 0;
    int timeHour_ = 18, timeMinute_ = 0;
    int activeTimeField_ = -1, pendingTimeDigit_ = -1;
    bool hasDate_ = false, dateOnly_ = true, resultPosted_ = false;
    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT message(UINT, WPARAM, LPARAM);
    void paint();
    void pickDay(int, int, int);
    void monthBy(int);
    void finish(bool save);
    void showTime();
    void stepTime(int delta);
    void typeTimeDigit(int digit);
public:
    static constexpr UINT resultMessage = WM_APP + 4;
    DeadlinePicker() = default;
    ~DeadlinePicker();
    bool show(HWND owner, HINSTANCE instance, float scale, const std::wstring& dueAt);
    std::wstring selection() const;
};
