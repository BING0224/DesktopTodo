#include "Json.h"
#include <windows.h>
#include <cmath>
#include <cwchar>
#include <iomanip>
#include <sstream>

std::wstring FromUtf8(const std::string& text) {
    if (text.empty()) return L"";
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                     static_cast<int>(text.size()), nullptr, 0);
    if (!length) throw std::runtime_error("Invalid UTF-8");
    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                        static_cast<int>(text.size()), result.data(), length);
    if (!result.empty() && result[0] == 0xfeff) result.erase(0, 1);
    return result;
}

std::string ToUtf8(const std::wstring& text) {
    if (text.empty()) return "";
    int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                     static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!length) throw std::runtime_error("Invalid Unicode");
    std::string result(length, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                        static_cast<int>(text.size()), result.data(), length, nullptr, nullptr);
    return result;
}

class Parser {
    const std::wstring& s_;
    size_t i_ = 0;
    unsigned depth_ = 0;
    void spaces() { while (i_ < s_.size() && (s_[i_] == L' ' || s_[i_] == L'\t' || s_[i_] == L'\n' || s_[i_] == L'\r')) ++i_; }
    wchar_t take() { if (i_ == s_.size()) throw std::runtime_error("Unexpected end of JSON"); return s_[i_++]; }
    void expect(wchar_t c) { if (take() != c) throw std::runtime_error("Invalid JSON"); }
    unsigned hex4() {
        unsigned v = 0;
        for (int j = 0; j < 4; ++j) {
            wchar_t c = take(); v <<= 4;
            if (c >= L'0' && c <= L'9') v |= c - L'0';
            else if (c >= L'A' && c <= L'F') v |= c - L'A' + 10;
            else if (c >= L'a' && c <= L'f') v |= c - L'a' + 10;
            else throw std::runtime_error("Invalid JSON escape");
        }
        return v;
    }
    std::wstring string() {
        expect(L'"'); std::wstring out;
        while (true) {
            wchar_t c = take();
            if (c == L'"') return out;
            if (c < 0x20) throw std::runtime_error("Invalid JSON string");
            if (c != L'\\') { out += c; continue; }
            c = take();
            switch (c) {
                case L'"': case L'\\': case L'/': out += c; break;
                case L'b': out += L'\b'; break;
                case L'f': out += L'\f'; break;
                case L'n': out += L'\n'; break;
                case L'r': out += L'\r'; break;
                case L't': out += L'\t'; break;
                case L'u': {
                    unsigned ch = hex4();
                    if (ch >= 0xd800 && ch <= 0xdbff) {
                        expect(L'\\'); expect(L'u'); unsigned lo = hex4();
                        if (lo < 0xdc00 || lo > 0xdfff) throw std::runtime_error("Invalid surrogate pair");
                        out += static_cast<wchar_t>(ch); out += static_cast<wchar_t>(lo);
                    } else if (ch >= 0xdc00 && ch <= 0xdfff) throw std::runtime_error("Unpaired surrogate");
                    else out += static_cast<wchar_t>(ch);
                    break;
                }
                default: throw std::runtime_error("Invalid JSON escape");
            }
        }
    }
    Json node() {
        if (++depth_ > 64) throw std::runtime_error("JSON too deep");
        spaces(); if (i_ >= s_.size()) throw std::runtime_error("Invalid JSON");
        Json result;
        if (s_[i_] == L'{') {
            ++i_; spaces(); Json::Object obj;
            if (i_ < s_.size() && s_[i_] == L'}') ++i_;
            else while (true) {
                spaces(); auto key = string(); spaces(); expect(L':');
                obj[std::move(key)] = node(); spaces();
                wchar_t c = take(); if (c == L'}') break;
                if (c != L',') throw std::runtime_error("Invalid JSON object");
            }
            result = Json(std::move(obj));
        } else if (s_[i_] == L'[') {
            ++i_; spaces(); Json::Array list;
            if (i_ < s_.size() && s_[i_] == L']') ++i_;
            else while (true) {
                list.push_back(node()); spaces(); wchar_t c = take();
                if (c == L']') break;
                if (c != L',') throw std::runtime_error("Invalid JSON array");
            }
            result = Json(std::move(list));
        } else if (s_[i_] == L'"') result = Json(string());
        else if (s_.compare(i_, 4, L"true") == 0) { i_ += 4; result = Json(true); }
        else if (s_.compare(i_, 5, L"false") == 0) { i_ += 5; result = Json(false); }
        else if (s_.compare(i_, 4, L"null") == 0) { i_ += 4; result = Json(); }
        else {
            size_t start = i_;
            if (s_[i_] == L'-') ++i_;
            if (i_ >= s_.size()) throw std::runtime_error("Invalid JSON number");
            if (s_[i_] == L'0') ++i_;
            else { if (s_[i_] < L'1' || s_[i_] > L'9') throw std::runtime_error("Invalid JSON number");
                   while (i_ < s_.size() && s_[i_] >= L'0' && s_[i_] <= L'9') ++i_; }
            if (i_ < s_.size() && s_[i_] == L'.') {
                ++i_; size_t digits = i_;
                while (i_ < s_.size() && s_[i_] >= L'0' && s_[i_] <= L'9') ++i_;
                if (digits == i_) throw std::runtime_error("Invalid JSON number");
            }
            if (i_ < s_.size() && (s_[i_] == L'e' || s_[i_] == L'E')) {
                ++i_; if (i_ < s_.size() && (s_[i_] == L'+' || s_[i_] == L'-')) ++i_;
                size_t digits = i_;
                while (i_ < s_.size() && s_[i_] >= L'0' && s_[i_] <= L'9') ++i_;
                if (digits == i_) throw std::runtime_error("Invalid JSON number");
            }
            double n = std::stod(s_.substr(start, i_ - start));
            if (!std::isfinite(n)) throw std::runtime_error("Invalid JSON number");
            result = Json(n);
        }
        --depth_; return result;
    }
public:
    explicit Parser(const std::wstring& text) : s_(text) {}
    Json parse() { Json x = node(); spaces(); if (i_ != s_.size()) throw std::runtime_error("Trailing JSON"); return x; }
};

Json ParseJson(const std::wstring& source) { return Parser(source).parse(); }

static void Write(const Json& x, std::wstring& out) {
    if (std::holds_alternative<std::nullptr_t>(x.value)) out += L"null";
    else if (auto b = std::get_if<bool>(&x.value)) out += *b ? L"true" : L"false";
    else if (auto n = std::get_if<double>(&x.value)) {
        std::wostringstream stream; stream.imbue(std::locale::classic());
        stream << std::setprecision(17) << *n; out += stream.str();
    } else if (auto s = std::get_if<std::wstring>(&x.value)) {
        out += L'"';
        for (wchar_t c : *s) {
            switch (c) {
                case L'"': out += L"\\\""; break;
                case L'\\': out += L"\\\\"; break;
                case L'\n': out += L"\\n"; break;
                case L'\r': out += L"\\r"; break;
                case L'\t': out += L"\\t"; break;
                default: if (c < 0x20) { wchar_t buf[7]; swprintf_s(buf, L"\\u%04x", c); out += buf; }
                         else out += c;
            }
        }
        out += L'"';
    } else if (auto a = std::get_if<Json::Array>(&x.value)) {
        out += L'['; for (const auto& v : *a) { if (&v != &a->front()) out += L','; Write(v, out); } out += L']';
    } else {
        out += L'{'; bool first = true;
        for (const auto& [key, val] : std::get<Json::Object>(x.value)) {
            if (!first) out += L',';
            first = false;
            Write(Json(key), out);
            out += L':';
            Write(val, out);
        }
        out += L'}';
    }
}

std::wstring SerializeJson(const Json& x) { std::wstring out; Write(x, out); return out; }
