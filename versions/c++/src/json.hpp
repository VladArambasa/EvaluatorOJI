// JSON.HPP - MINIMAL JSON LIBRARY (C++17) FOR THE OLYMPIAD EVALUATOR
// OBJECTS KEEP KEY ORDER; STRINGS ARE UTF-8 (INVALID BYTES BECOME U+FFFD ON OUTPUT)
#pragma once
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace js {

enum class T { Null, Bool, Num, Str, Arr, Obj };

class Value {
public:
    T t = T::Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Value> a;
    std::vector<std::pair<std::string, Value>> o;

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool v) : t(T::Bool), b(v) {}
    Value(int v) : t(T::Num), n(v) {}
    Value(long v) : t(T::Num), n(static_cast<double>(v)) {}
    Value(long long v) : t(T::Num), n(static_cast<double>(v)) {}
    Value(unsigned v) : t(T::Num), n(v) {}
    Value(unsigned long v) : t(T::Num), n(static_cast<double>(v)) {}
    Value(unsigned long long v) : t(T::Num), n(static_cast<double>(v)) {}
    Value(double v) : t(T::Num), n(v) {}
    Value(const char *v) : t(T::Str), s(v ? v : "") {}
    Value(const std::string &v) : t(T::Str), s(v) {}
    Value(std::string &&v) : t(T::Str), s(std::move(v)) {}

    static Value array() { Value v; v.t = T::Arr; return v; }
    static Value object() { Value v; v.t = T::Obj; return v; }

    bool isNull() const { return t == T::Null; }
    bool isObj() const { return t == T::Obj; }
    bool isArr() const { return t == T::Arr; }
    bool isStr() const { return t == T::Str; }
    bool isNum() const { return t == T::Num; }
    bool isBool() const { return t == T::Bool; }

    const Value *find(const std::string &k) const {
        if (t != T::Obj) return nullptr;
        for (auto &p : o) if (p.first == k) return &p.second;
        return nullptr;
    }
    Value *find(const std::string &k) {
        if (t != T::Obj) return nullptr;
        for (auto &p : o) if (p.first == k) return &p.second;
        return nullptr;
    }
    const Value &operator[](const std::string &k) const {
        static const Value nil;
        const Value *v = find(k);
        return v ? *v : nil;
    }
    Value &set(const std::string &k, Value v) {
        if (t != T::Obj) { t = T::Obj; o.clear(); }
        for (auto &p : o) if (p.first == k) { p.second = std::move(v); return p.second; }
        o.emplace_back(k, std::move(v));
        return o.back().second;
    }
    Value &push(Value v) {
        if (t != T::Arr) { t = T::Arr; a.clear(); }
        a.push_back(std::move(v));
        return a.back();
    }
    size_t size() const { return t == T::Arr ? a.size() : (t == T::Obj ? o.size() : 0); }

    // VALUE WITH DEFAULT: NULL/MISSING -> DEFAULT
    std::string str(const std::string &def = "") const {
        if (t == T::Str) return s;
        if (t == T::Num) return numStr(n);
        if (t == T::Bool) return b ? "true" : "false";
        return def;
    }
    double num(double def = 0) const {
        if (t == T::Num) return n;
        if (t == T::Bool) return b ? 1 : 0;
        if (t == T::Str) {
            if (s.empty()) return def;
            char *end = nullptr;
            double v = std::strtod(s.c_str(), &end);
            if (end && *end == 0 && std::isfinite(v)) return v;
        }
        return def;
    }
    bool truthy(bool def) const {
        if (t == T::Null) return def;
        if (t == T::Bool) return b;
        if (t == T::Num) return n != 0;
        if (t == T::Str) return !s.empty();
        return size() > 0;
    }

    static std::string numStr(double x) {
        char buf[64];
        if (std::isnan(x) || std::isinf(x)) return "null";
        if (x == std::floor(x) && std::fabs(x) < 1e15) {
            std::snprintf(buf, sizeof buf, "%.0f", x);
            return buf;
        }
        for (int prec = 15; prec <= 17; prec++) {
            std::snprintf(buf, sizeof buf, "%.*g", prec, x);
            if (std::strtod(buf, nullptr) == x) break;
        }
        return buf;
    }

    static void escape(std::string &out, const std::string &str) {
        out += '"';
        const unsigned char *p = reinterpret_cast<const unsigned char *>(str.data());
        size_t n = str.size(), i = 0;
        while (i < n) {
            unsigned char c = p[i];
            if (c == '"') { out += "\\\""; i++; }
            else if (c == '\\') { out += "\\\\"; i++; }
            else if (c == '\n') { out += "\\n"; i++; }
            else if (c == '\r') { out += "\\r"; i++; }
            else if (c == '\t') { out += "\\t"; i++; }
            else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; i++; }
            else if (c < 0x80) { out += static_cast<char>(c); i++; }
            else {
                // UTF-8 VALIDATION
                size_t len = 0;
                unsigned cp = 0;
                if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
                else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
                else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
                bool ok = len > 0 && i + len <= n;
                for (size_t k = 1; ok && k < len; k++) {
                    if ((p[i + k] & 0xC0) != 0x80) ok = false; else cp = (cp << 6) | (p[i + k] & 0x3F);
                }
                if (ok && ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) ||
                           (cp >= 0xD800 && cp <= 0xDFFF))) ok = false;
                if (ok) { out.append(reinterpret_cast<const char *>(p + i), len); i += len; }
                else { out += "\xEF\xBF\xBD"; i++; }
            }
        }
        out += '"';
    }

    void dumpTo(std::string &out, int indent, int level) const {
        auto nl = [&](int lv) {
            if (indent < 0) return;
            out += '\n';
            out.append(static_cast<size_t>(indent * lv), ' ');
        };
        switch (t) {
        case T::Null: out += "null"; break;
        case T::Bool: out += b ? "true" : "false"; break;
        case T::Num: out += numStr(n); break;
        case T::Str: escape(out, s); break;
        case T::Arr:
            out += '[';
            for (size_t i = 0; i < a.size(); i++) {
                if (i) out += ',';
                nl(level + 1);
                a[i].dumpTo(out, indent, level + 1);
            }
            if (!a.empty()) nl(level);
            out += ']';
            break;
        case T::Obj:
            out += '{';
            for (size_t i = 0; i < o.size(); i++) {
                if (i) out += ',';
                nl(level + 1);
                escape(out, o[i].first);
                out += indent >= 0 ? ": " : ":";
                o[i].second.dumpTo(out, indent, level + 1);
            }
            if (!o.empty()) nl(level);
            out += '}';
            break;
        }
    }
    std::string dump(int indent = -1) const { std::string out; dumpTo(out, indent, 0); return out; }

    // --- PARSER ---
    struct Parser {
        const char *p, *e;
        std::string err;
        void ws() { while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++; }
        bool fail(const char *m) { if (err.empty()) err = m; return false; }
        static void putUtf8(std::string &s, unsigned cp) {
            if (cp < 0x80) s += static_cast<char>(cp);
            else if (cp < 0x800) { s += static_cast<char>(0xC0 | (cp >> 6)); s += static_cast<char>(0x80 | (cp & 0x3F)); }
            else if (cp < 0x10000) { s += static_cast<char>(0xE0 | (cp >> 12)); s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); s += static_cast<char>(0x80 | (cp & 0x3F)); }
            else { s += static_cast<char>(0xF0 | (cp >> 18)); s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); s += static_cast<char>(0x80 | (cp & 0x3F)); }
        }
        bool hex4(unsigned &v) {
            if (e - p < 4) return fail("incomplete \\u");
            v = 0;
            for (int i = 0; i < 4; i++) {
                char c = *p++;
                v <<= 4;
                if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
                else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
                else return fail("invalid \\u");
            }
            return true;
        }
        bool str(std::string &out) {
            if (p >= e || *p != '"') return fail("expected \"");
            p++;
            while (p < e && *p != '"') {
                char c = *p++;
                if (c != '\\') { out += c; continue; }
                if (p >= e) return fail("unterminated string");
                char x = *p++;
                switch (x) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && e - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                        const char *save = p;
                        p += 2;
                        unsigned lo;
                        if (hex4(lo) && lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else { p = save; err.clear(); }
                    }
                    putUtf8(out, cp);
                    break;
                }
                default: return fail("invalid escape");
                }
            }
            if (p >= e) return fail("unterminated string");
            p++;
            return true;
        }
        bool value(Value &v, int depth) {
            if (depth > 200) return fail("nesting too deep");
            ws();
            if (p >= e) return fail("unexpected end");
            char c = *p;
            if (c == '{') {
                p++;
                v = Value::object();
                ws();
                if (p < e && *p == '}') { p++; return true; }
                for (;;) {
                    ws();
                    std::string k;
                    if (!str(k)) return false;
                    ws();
                    if (p >= e || *p != ':') return fail("expected :");
                    p++;
                    Value x;
                    if (!value(x, depth + 1)) return false;
                    v.set(k, std::move(x));
                    ws();
                    if (p < e && *p == ',') { p++; continue; }
                    if (p < e && *p == '}') { p++; return true; }
                    return fail("expected , or }");
                }
            }
            if (c == '[') {
                p++;
                v = Value::array();
                ws();
                if (p < e && *p == ']') { p++; return true; }
                for (;;) {
                    Value x;
                    if (!value(x, depth + 1)) return false;
                    v.a.push_back(std::move(x));
                    ws();
                    if (p < e && *p == ',') { p++; continue; }
                    if (p < e && *p == ']') { p++; return true; }
                    return fail("expected , or ]");
                }
            }
            if (c == '"') { v = Value(std::string()); return str(v.s); }
            if (e - p >= 4 && std::string(p, 4) == "true") { p += 4; v = Value(true); return true; }
            if (e - p >= 5 && std::string(p, 5) == "false") { p += 5; v = Value(false); return true; }
            if (e - p >= 4 && std::string(p, 4) == "null") { p += 4; v = Value(); return true; }
            if (c == '-' || (c >= '0' && c <= '9')) {
                const char *s0 = p;
                if (*p == '-') p++;
                while (p < e && ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-')) p++;
                std::string tok(s0, p);
                char *end = nullptr;
                double d = std::strtod(tok.c_str(), &end);
                if (!end || *end) return fail("invalid number");
                v = Value(d);
                return true;
            }
            return fail("invalid value");
        }
    };

    static Value parse(const std::string &text, std::string *err = nullptr) {
        Parser ps;
        ps.p = text.data();
        ps.e = text.data() + text.size();
        if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
            static_cast<unsigned char>(text[2]) == 0xBF) ps.p += 3;
        Value v;
        if (!ps.value(v, 0)) {
            if (err) *err = ps.err.empty() ? "invalid JSON" : ps.err;
            return Value();
        }
        ps.ws();
        if (ps.p != ps.e) {
            if (err) *err = "text after JSON";
            return Value();
        }
        if (err) err->clear();
        return v;
    }
};

}  // NAMESPACE JS
