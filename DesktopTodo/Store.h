#pragma once
#include "Json.h"
#include <string>
#include <vector>

struct Resource {
    std::wstring type; // File, Folder, Url
    std::wstring target;
    std::wstring name;
    Json original = Json(Json::Object{});
};

struct Todo {
    std::wstring text;
    bool completed = false;
    std::wstring dueAt;
    std::wstring completedAt;
    Json original = Json(Json::Object{});
    std::vector<Resource> resources;
};

struct Placement {
    bool saved = false;
    double x = 0, y = 0, width = 426, height = 460;
    Json original = Json(Json::Object{});
};

class Store {
    std::wstring folder_;
public:
    Store();
    const std::wstring& folder() const { return folder_; }
    std::vector<Todo> loadTodos() const;
    Placement loadPlacement() const;
    bool saveTodos(const std::vector<Todo>& todos) const;
    bool savePlacement(const Placement& p) const;
    static Todo makeTodo(const std::wstring& text, size_t index);
    // Returns true when the visible order changes.
    static bool sortTodos(std::vector<Todo>& todos);
    static std::wstring localTimestamp();
};
