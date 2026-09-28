#include "Store.h"
#include "Deadline.h"
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

static void BackupBroken(const std::wstring& filename) {
    SYSTEMTIME t{}; GetLocalTime(&t);
    wchar_t suffix[48]{};
    swprintf_s(suffix, L".corrupted-%04u%02u%02u%02u%02u%02u",
               t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    MoveFileExW(filename.c_str(), (filename + suffix).c_str(), MOVEFILE_REPLACE_EXISTING);
}

static Json LoadFile(const std::wstring& filename, bool array) {
    try {
        if (!fs::exists(fs::path(filename))) return array ? Json(Json::Array{}) : Json(Json::Object{});
        std::ifstream in(fs::path(filename), std::ios::binary);
        if (!in) return array ? Json(Json::Array{}) : Json(Json::Object{});
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (bytes.size() > 8 * 1024 * 1024) throw std::runtime_error("JSON file too large");
        Json value = ParseJson(FromUtf8(bytes));
        if (array ? !value.isArray() : !value.isObject()) throw std::runtime_error("Unexpected JSON root");
        return value;
    } catch (const std::exception&) {
        BackupBroken(filename);
        return array ? Json(Json::Array{}) : Json(Json::Object{});
    }
}

static bool SaveFile(const std::wstring& filename, const Json& value) {
    auto tmp = filename + L".tmp";
    try {
        fs::create_directories(fs::path(filename).parent_path());
        std::string bytes = ToUtf8(SerializeJson(value));
        {
            std::ofstream out(fs::path(tmp), std::ios::binary | std::ios::trunc);
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            out.flush();
            if (!out) { out.close(); DeleteFileW(tmp.c_str()); return false; }
        }
        if (!MoveFileExW(tmp.c_str(), filename.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(tmp.c_str()); return false;
        }
        return true;
    } catch (const std::exception&) { DeleteFileW(tmp.c_str()); return false; }
}

static const Json& Field(const Json& x, const wchar_t* name) {
    static const Json empty;
    const Json* found = x.find(name); return found ? *found : empty;
}

Store::Store() {
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &path))) {
        folder_ = path; CoTaskMemFree(path);
    } else {
        wchar_t buffer[MAX_PATH]{};
        DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
        if (count && count < MAX_PATH) folder_ = buffer;
    }
    if (folder_.empty()) folder_ = L".";
    folder_ += L"\\DesktopTodo";
    // Preserve the last .NET version's files on the first native launch.
    for (const wchar_t* file : {L"todos.json", L"settings.json"}) {
        std::wstring source = folder_ + L"\\" + file;
        std::wstring backup = source + L".before-native.bak";
        CopyFileW(source.c_str(), backup.c_str(), TRUE);
    }
    std::wstring todoFile = folder_ + L"\\todos.json";
    CopyFileW(todoFile.c_str(), (todoFile + L".before-deadlines.bak").c_str(), TRUE);
    CopyFileW(todoFile.c_str(), (todoFile + L".before-resources.bak").c_str(), TRUE);
}

std::vector<Todo> Store::loadTodos() const {
    Json json = LoadFile(folder_ + L"\\todos.json", true);
    struct Ordered { Todo todo; double order; std::wstring created; size_t index; };
    std::vector<Ordered> ordered;
    size_t i = 0;
    for (const auto& entry : json.array()) {
        if (!entry.isObject()) { ++i; continue; }
        auto text = Field(entry, L"Content").stringOr();
        if (text.find_first_not_of(L" \r\n\t") == std::wstring::npos) { ++i; continue; }
        auto due = Field(entry, L"DueAt").stringOr();
        if (!ValidDeadline(due)) due.clear();
        Todo todo{text, Field(entry, L"IsCompleted").boolOr(), due,
                  Field(entry, L"CompletedAt").stringOr(), entry};
        if (Field(todo.original, L"Id").stringOr().empty()) {
            GUID id{};
            if (SUCCEEDED(CoCreateGuid(&id))) {
                wchar_t guid[40]{};
                StringFromGUID2(id, guid, 40);
                std::wstring value = guid;
                todo.original.object()[L"Id"] = Json(value.substr(1, value.size() - 2));
            }
        }
        const Json& resources = Field(entry, L"Resources");
        if (resources.isArray()) for (const auto& resource : resources.array()) {
            if (!resource.isObject()) continue;
            std::wstring type = Field(resource, L"Type").stringOr();
            std::wstring target = Field(resource, L"Target").stringOr();
            if ((type == L"File" || type == L"Folder" || type == L"Url") && !target.empty())
                todo.resources.push_back({type, target, Field(resource, L"Name").stringOr(), resource});
        }
        ordered.push_back({std::move(todo),
                           Field(entry, L"SortOrder").numberOr(), Field(entry, L"CreatedAt").stringOr(), i++});
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const Ordered& a, const Ordered& b) {
        if (a.order != b.order) return a.order < b.order;
        return a.created < b.created;
    });
    std::vector<Todo> result;
    for (auto& entry : ordered) result.push_back(std::move(entry.todo));
    sortTodos(result);
    return result;
}

bool Store::sortTodos(std::vector<Todo>& todos) {
    SYSTEMTIME now{}; GetLocalTime(&now);
    wchar_t clock[12]{};
    swprintf_s(clock, L"T%02u:%02u:%02u", now.wHour, now.wMinute, now.wSecond);
    const std::wstring nowKey = DateString(now.wYear, now.wMonth, now.wDay) + clock;
    auto group = [&](const Todo& todo) {
        if (todo.completed) return 3;
        if (todo.dueAt.empty() || !ValidDeadline(todo.dueAt)) return 2;
        return SecondsUntilExpiry(todo.dueAt, nowKey) <= 0 ? 0 : 1;
    };
    auto before = [&](const Todo& a, const Todo& b) {
        int aGroup = group(a), bGroup = group(b);
        if (aGroup != bGroup) return aGroup < bGroup;
        if (aGroup == 3) {
            if (a.completedAt != b.completedAt) {
                if (a.completedAt.empty()) return false;
                if (b.completedAt.empty()) return true;
                return a.completedAt > b.completedAt;
            }
            return false;
        }
        if (aGroup == 0) return DeadlineKey(a.dueAt) > DeadlineKey(b.dueAt);
        if (aGroup == 1) return DeadlineKey(a.dueAt) < DeadlineKey(b.dueAt);
        return false; // Keep manual order for long-term tasks.
    };
    if (std::is_sorted(todos.begin(), todos.end(), before)) return false;
    std::stable_sort(todos.begin(), todos.end(), before);
    return true;
}

Placement Store::loadPlacement() const {
    Json json = LoadFile(folder_ + L"\\settings.json", false);
    Placement p;
    p.original = json;
    p.saved = Field(json, L"HasWindowPlacement").boolOr();
    p.x = Field(json, L"Left").numberOr(); p.y = Field(json, L"Top").numberOr();
    p.width = Field(json, L"Width").numberOr(426);
    p.height = Field(json, L"Height").numberOr(460);
    p.docked = Field(json, L"Docked").boolOr();
    p.dockY = Field(json, L"DockY").numberOr(1.0);
    p.dockMonitor = Field(json, L"DockMonitor").stringOr();
    if (!std::isfinite(p.dockY)) p.dockY = 1.0;
    p.dockY = (std::clamp)(p.dockY, 0.0, 1.0);
    if (!std::isfinite(p.width) || p.width < 260 || p.width > 10000) p.width = 426;
    if (!std::isfinite(p.height) || p.height < 220 || p.height > 10000) p.height = 460;
    if (!std::isfinite(p.x) || !std::isfinite(p.y)) p.saved = false;
    return p;
}

bool Store::saveTodos(const std::vector<Todo>& todos) const {
    Json::Array items;
    for (size_t index = 0; index < todos.size(); ++index) {
        const auto& todo = todos[index];
        Json item = todo.original;
        if (!item.isObject()) item = Json(Json::Object{});
        item.object()[L"Content"] = Json(todo.text);
        item.object()[L"IsCompleted"] = Json(todo.completed);
        if (todo.dueAt.empty()) item.object().erase(L"DueAt");
        else item.object()[L"DueAt"] = Json(todo.dueAt);
        if (todo.completedAt.empty()) item.object().erase(L"CompletedAt");
        else item.object()[L"CompletedAt"] = Json(todo.completedAt);
        if (todo.resources.empty()) item.object().erase(L"Resources");
        else {
            Json::Array resources;
            for (const auto& resource : todo.resources) {
                Json value = resource.original.isObject() ? resource.original : Json(Json::Object{});
                value.object()[L"Type"] = Json(resource.type);
                value.object()[L"Target"] = Json(resource.target);
                if (resource.name.empty()) value.object().erase(L"Name");
                else value.object()[L"Name"] = Json(resource.name);
                resources.push_back(std::move(value));
            }
            item.object()[L"Resources"] = Json(std::move(resources));
        }
        item.object()[L"SortOrder"] = Json(static_cast<int>(index));
        items.push_back(std::move(item));
    }
    return SaveFile(folder_ + L"\\todos.json", Json(std::move(items)));
}

bool Store::savePlacement(const Placement& p) const {
    Json json = p.original.isObject() ? p.original : Json(Json::Object{});
    json.object()[L"HasWindowPlacement"] = Json(true);
    json.object()[L"Left"] = Json(p.x); json.object()[L"Top"] = Json(p.y);
    json.object()[L"Width"] = Json(p.width); json.object()[L"Height"] = Json(p.height);
    json.object()[L"Docked"] = Json(p.docked);
    json.object()[L"DockY"] = Json(p.dockY);
    json.object()[L"DockMonitor"] = Json(p.dockMonitor);
    return SaveFile(folder_ + L"\\settings.json", json);
}

std::wstring Store::localTimestamp() {
    SYSTEMTIME t{}; GetLocalTime(&t);
    wchar_t stamp[50]{};
    swprintf_s(stamp, L"%04u-%02u-%02uT%02u:%02u:%02u.%03u",
               t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    return stamp;
}

Todo Store::makeTodo(const std::wstring& text, size_t index) {
    GUID id{}; CoCreateGuid(&id);
    wchar_t guid[40]{}; StringFromGUID2(id, guid, 40);
    std::wstring idString = guid;
    if (idString.size() > 2) idString = idString.substr(1, idString.size() - 2);
    Json::Object object;
    object[L"Id"] = Json(idString); object[L"Content"] = Json(text);
    object[L"IsCompleted"] = Json(false); object[L"CreatedAt"] = Json(localTimestamp());
    object[L"SortOrder"] = Json(static_cast<int>(index));
    return Todo{text, false, L"", L"", Json(std::move(object))};
}
