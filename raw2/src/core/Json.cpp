#include "core/Json.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace raw2::json {
namespace {

const Value kNull;

struct Parser {
    const char* p;
    const char* end;
    int depth = 0;

    void ws() { while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p; }
    bool lit(const char* s) {
        const size_t n = std::strlen(s);
        if ((size_t)(end - p) < n || std::strncmp(p, s, n) != 0) return false;
        p += n;
        return true;
    }
    static void utf8(std::string& o, unsigned c) {
        if (c < 0x80) o += (char)c;
        else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
        else { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 0x3F)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
    }
    bool hex4(unsigned& c) {
        if (end - p < 4) return false;
        c = 0;
        for (int i = 0; i < 4; ++i) {
            const char h = *p++;
            c <<= 4;
            if (h >= '0' && h <= '9') c |= (unsigned)(h - '0');
            else if (h >= 'a' && h <= 'f') c |= (unsigned)(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') c |= (unsigned)(h - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool string(std::string& o) {
        if (p >= end || *p != '"') return false;
        ++p;
        while (p < end && *p != '"') {
            if (*p == '\\') {
                if (++p >= end) return false;
                const char e = *p++;
                switch (e) {
                    case 'n': o += '\n'; break;
                    case 't': o += '\t'; break;
                    case 'r': o += '\r'; break;
                    case 'b': o += '\b'; break;
                    case 'f': o += '\f'; break;
                    case 'u': {
                        unsigned c;
                        if (!hex4(c)) return false;
                        if (c >= 0xD800 && c < 0xDC00 && end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                            p += 2;
                            unsigned lo;
                            if (!hex4(lo)) return false;
                            c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        utf8(o, c);
                        break;
                    }
                    default: o += e; break;
                }
            } else {
                o += *p++;
            }
        }
        if (p >= end) return false;
        ++p;
        return true;
    }
    bool value(Value& v) {
        if (++depth > 64) return false;
        ws();
        if (p >= end) return false;
        bool ok = true;
        if (*p == '{') {
            ++p;
            v = Value::object();
            ws();
            if (p < end && *p == '}') { ++p; --depth; return true; }
            while (ok) {
                ws();
                std::string k;
                if (!string(k)) return false;
                ws();
                if (p >= end || *p != ':') return false;
                ++p;
                Value child;
                if (!value(child)) return false;
                v.set(k, std::move(child));
                ws();
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == '}') { ++p; break; }
                return false;
            }
        } else if (*p == '[') {
            ++p;
            v = Value::array();
            ws();
            if (p < end && *p == ']') { ++p; --depth; return true; }
            while (true) {
                Value child;
                if (!value(child)) return false;
                v.push(std::move(child));
                ws();
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == ']') { ++p; break; }
                return false;
            }
        } else if (*p == '"') {
            std::string s;
            if (!string(s)) return false;
            v = Value(std::move(s));
        } else if (lit("true")) v = Value(true);
        else if (lit("false")) v = Value(false);
        else if (lit("null")) v = Value();
        else {
            char* e = nullptr;
            const std::string tmp(p, (size_t)std::min<ptrdiff_t>(end - p, 64));
            const double d = std::strtod(tmp.c_str(), &e);
            if (e == tmp.c_str()) return false;
            p += (e - tmp.c_str());
            v = Value(std::isfinite(d) ? d : 0.0);
        }
        --depth;
        return ok;
    }
};

void escape(std::string& o, const std::string& s) {
    o += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", c);
                    o += b;
                } else {
                    o += (char)c;
                }
        }
    }
    o += '"';
}

} // namespace

const Value& Value::operator[](size_t i) const { return type_ == Type::Array && i < arr_.size() ? arr_[i] : kNull; }

const Value& Value::operator[](const char* key) const {
    if (type_ != Type::Object) return kNull;
    for (const auto& m : obj_)
        if (m.first == key) return m.second;
    return kNull;
}

bool Value::has(const char* key) const {
    if (type_ != Type::Object) return false;
    for (const auto& m : obj_)
        if (m.first == key) return true;
    return false;
}

Value& Value::set(const std::string& key, Value v) {
    if (type_ != Type::Object) *this = object();
    for (auto& m : obj_)
        if (m.first == key) { m.second = std::move(v); return m.second; }
    obj_.emplace_back(key, std::move(v));
    return obj_.back().second;
}

void Value::dumpTo(std::string& o, bool pretty, int indent) const {
    auto nl = [&](int ind) {
        if (!pretty) return;
        o += '\n';
        o.append((size_t)ind * 2, ' ');
    };
    switch (type_) {
        case Type::Null: o += "null"; break;
        case Type::Bool: o += b_ ? "true" : "false"; break;
        case Type::Number: {
            char b[32];
            if (n_ == std::floor(n_) && std::fabs(n_) < 1e15) std::snprintf(b, sizeof b, "%.0f", n_);
            else std::snprintf(b, sizeof b, "%.6g", n_);
            o += b;
            break;
        }
        case Type::String: escape(o, s_); break;
        case Type::Array: {
            o += '[';
            // tableaux de nombres sur une ligne, plus lisible
            bool flat = true;
            for (const auto& v : arr_) flat = flat && (v.type_ == Type::Number || v.type_ == Type::Bool);
            for (size_t i = 0; i < arr_.size(); ++i) {
                if (i) o += pretty && flat ? ", " : ",";
                if (!flat) nl(indent + 1);
                arr_[i].dumpTo(o, pretty, indent + 1);
            }
            if (!flat && !arr_.empty()) nl(indent);
            o += ']';
            break;
        }
        case Type::Object: {
            o += '{';
            for (size_t i = 0; i < obj_.size(); ++i) {
                if (i) o += ',';
                nl(indent + 1);
                escape(o, obj_[i].first);
                o += pretty ? ": " : ":";
                obj_[i].second.dumpTo(o, pretty, indent + 1);
            }
            if (!obj_.empty()) nl(indent);
            o += '}';
            break;
        }
    }
}

std::string Value::dump(bool pretty) const {
    std::string o;
    dumpTo(o, pretty, 0);
    return o;
}

bool Value::parse(const std::string& text, Value& out) {
    Parser ps{text.data(), text.data() + text.size()};
    // BOM UTF-8 éventuel
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) ps.p += 3;
    if (!ps.value(out)) return false;
    ps.ws();
    return ps.p == ps.end;
}

static const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const unsigned char* d, size_t n) {
    std::string o;
    o.reserve((n + 2) / 3 * 4);
    for (size_t i = 0; i < n; i += 3) {
        const unsigned v = (unsigned)d[i] << 16 | (i + 1 < n ? (unsigned)d[i + 1] << 8 : 0u) | (i + 2 < n ? d[i + 2] : 0u);
        o += kB64[(v >> 18) & 63];
        o += kB64[(v >> 12) & 63];
        o += i + 1 < n ? kB64[(v >> 6) & 63] : '=';
        o += i + 2 < n ? kB64[v & 63] : '=';
    }
    return o;
}

bool base64Decode(const std::string& s, std::vector<unsigned char>& out) {
    out.clear();
    unsigned acc = 0;
    int bits = 0;
    for (char c : s) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        else return false;
        acc = (acc << 6) | (unsigned)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((unsigned char)((acc >> bits) & 0xFF));
        }
    }
    return true;
}

} // namespace raw2::json
