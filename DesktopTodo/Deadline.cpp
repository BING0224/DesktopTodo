#include "Deadline.h"
#include <algorithm>
#include <cwchar>
#include <limits>

int DaysInMonth(int year, int month) {
    if (year < 1900 || year > 9999 || month < 1 || month > 12) return 0;
    const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && (year % 400 == 0 || (year % 4 == 0 && year % 100 != 0))) return 29;
    return days[month - 1];
}

int WeekdayMondayFirst(int year, int month, int day) {
    // Sakamoto's Gregorian weekday algorithm; Sunday = 0.
    const int offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) --year;
    int sunday = (year + year / 4 - year / 100 + year / 400 + offsets[month - 1] + day) % 7;
    return (sunday + 6) % 7;
}

static int digits(const std::wstring& value, size_t start, size_t count) {
    int number = 0;
    for (size_t i = start; i < start + count; ++i) {
        if (value[i] < L'0' || value[i] > L'9') return -1;
        number = number * 10 + (value[i] - L'0');
    }
    return number;
}

bool ValidDeadline(const std::wstring& value) {
    if (value.size() != 10 && value.size() != 16) return false;
    if (value[4] != L'-' || value[7] != L'-') return false;
    int year = digits(value, 0, 4), month = digits(value, 5, 2), day = digits(value, 8, 2);
    if (day < 1 || day > DaysInMonth(year, month)) return false;
    if (value.size() == 10) return true;
    if (value[10] != L'T' || value[13] != L':') return false;
    int hour = digits(value, 11, 2), minute = digits(value, 14, 2);
    return hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59;
}

std::wstring DeadlineKey(const std::wstring& value) {
    return value.size() == 10 ? value + L"T24:00" : value;
}

std::wstring DateString(int year, int month, int day) {
    wchar_t buffer[16]{};
    std::swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%04d-%02d-%02d", year, month, day);
    return buffer;
}

static long long minutesSinceEpoch(const std::wstring& value) {
    int year = digits(value, 0, 4), month = digits(value, 5, 2), day = digits(value, 8, 2);
    long long previous = year - 1;
    long long days = previous * 365 + previous / 4 - previous / 100 + previous / 400;
    for (int m = 1; m < month; ++m) days += DaysInMonth(year, m);
    days += day - 1;
    int hour = value.size() == 10 ? 23 : digits(value, 11, 2);
    int minute = value.size() == 10 ? 59 : digits(value, 14, 2);
    return days * 1440 + hour * 60 + minute;
}

long long SecondsUntilExpiry(const std::wstring& dueAt, const std::wstring& nowAt) {
    if (!ValidDeadline(dueAt) || nowAt.size() != 19 || nowAt[16] != L':' ||
        !ValidDeadline(nowAt.substr(0, 16)))
        return (std::numeric_limits<long long>::max)();
    int second = digits(nowAt, 17, 2);
    if (second < 0 || second > 59) return (std::numeric_limits<long long>::max)();
    long long dueSecond = minutesSinceEpoch(dueAt) * 60 + (dueAt.size() == 10 ? 60 : 0);
    long long nowSecond = minutesSinceEpoch(nowAt) * 60 + second;
    return dueSecond - nowSecond;
}

long long SecondsUntilNextRefresh(const std::vector<std::wstring>& dueDates, const std::wstring& nowAt) {
    long long next = (std::numeric_limits<long long>::max)();
    for (const auto& dueAt : dueDates) {
        long long remaining = SecondsUntilExpiry(dueAt, nowAt);
        if (remaining <= 0 || remaining == (std::numeric_limits<long long>::max)()) continue;
        next = (std::min)(next, remaining);
        if (remaining > 7200) next = (std::min)(next, remaining - 7200);
    }
    if (next == (std::numeric_limits<long long>::max)()) return next;
    int hour = digits(nowAt, 11, 2), minute = digits(nowAt, 14, 2), second = digits(nowAt, 17, 2);
    long long midnight = 86400LL - hour * 3600LL - minute * 60LL - second;
    return (std::min)(next, midnight);
}
