#pragma once
#include <map>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// Small, dependency-free JSON value. Unknown fields in the old files survive saves.
struct Json {
    using Array = std::vector<Json>;
    using Object = std::map<std::wstring, Json>;
    std::variant<std::nullptr_t, bool, double, std::wstring, Array, Object> value;
    Json() : value(nullptr) {}
    Json(bool x) : value(x) {}
    Json(int x) : value(static_cast<double>(x)) {}
    Json(double x) : value(x) {}
    Json(std::wstring x) : value(std::move(x)) {}
    Json(Array x) : value(std::move(x)) {}
    Json(Object x) : value(std::move(x)) {}
    bool isObject() const { return std::holds_alternative<Object>(value); }
    bool isArray() const { return std::holds_alternative<Array>(value); }
    const Object& object() const { return std::get<Object>(value); }
    Object& object() { return std::get<Object>(value); }
    const Array& array() const { return std::get<Array>(value); }
    const Json* find(const std::wstring& key) const {
        if (!isObject()) return nullptr;
        auto it = object().find(key);
        return it == object().end() ? nullptr : &it->second;
    }
    std::wstring stringOr(std::wstring fallback = L"") const {
        auto x = std::get_if<std::wstring>(&value); return x ? *x : fallback;
    }
    double numberOr(double fallback = 0) const {
        auto x = std::get_if<double>(&value); return x ? *x : fallback;
    }
    bool boolOr(bool fallback = false) const {
        auto x = std::get_if<bool>(&value); return x ? *x : fallback;
    }
};

Json ParseJson(const std::wstring& source);
std::wstring SerializeJson(const Json& value);
std::wstring FromUtf8(const std::string& text);
std::string ToUtf8(const std::wstring& text);
