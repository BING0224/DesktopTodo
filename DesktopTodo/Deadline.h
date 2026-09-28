#pragma once
#include <string>
#include <vector>

// Local wall-clock deadline: YYYY-MM-DD means by the end of that day;
// YYYY-MM-DDTHH:mm means the specified local time. Empty means unscheduled.
int DaysInMonth(int year, int month);
int WeekdayMondayFirst(int year, int month, int day);
bool ValidDeadline(const std::wstring& value);
std::wstring DeadlineKey(const std::wstring& value);
std::wstring DateString(int year, int month, int day);
// nowAt is YYYY-MM-DDTHH:mm:ss. A date-only deadline expires at the next midnight.
// A timed deadline expires when its specified minute begins.
long long SecondsUntilExpiry(const std::wstring& dueAt, const std::wstring& nowAt);
// Next reminder, expiry, or midnight transition; LLONG_MAX if none remain.
long long SecondsUntilNextRefresh(const std::vector<std::wstring>& dueDates, const std::wstring& nowAt);
