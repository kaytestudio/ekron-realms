// Realms Engine - glTF 2.0 -> triangle mesh map (see realms/MeshMap.h).
//
// Self-contained on purpose: the engine library must not pull a second copy
// of tiny_gltf into hosts that already link one. Every offset, count and
// index read from the file is validated before use.
#include "realms/MeshMap.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <utility>

// Bit-exact with the Free Pascal core: no fused multiply-add contraction
// (clang contracts a*b+c by default on arm64 / FMA targets).
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace realms {

namespace {

// ----------------------------------------------------------------- JSON ---

struct JValue {
    enum Kind : uint8_t { Null, Bool, Number, String, Array, Object };
    Kind kind = Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<JValue> items;     // Array elements, or Object values
    std::vector<std::string> keys; // Object keys (parallel to items)

    const JValue *get(const char *key) const {
        if (kind != Object) return nullptr;
        for (size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == key) return &items[i];
        return nullptr;
    }
    size_t size() const { return kind == Array ? items.size() : 0; }
    const JValue *at(size_t i) const { return kind == Array && i < items.size() ? &items[i] : nullptr; }
};

class JsonParser {
public:
    JsonParser(const char *begin, const char *end) : p_(begin), end_(end) {}

    bool parse(JValue &out, std::string &error) {
        if (!value(out, 0)) {
            error = err_.empty() ? "bad JSON" : err_;
            return false;
        }
        skipWs();
        if (p_ != end_) {
            error = "JSON: trailing data";
            return false;
        }
        return true;
    }

private:
    static constexpr int kMaxDepth = 64;

    bool fail(const char *msg) {
        if (err_.empty()) err_ = std::string("JSON: ") + msg;
        return false;
    }
    void skipWs() {
        while (p_ < end_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r')) ++p_;
    }
    bool digit() const { return p_ < end_ && *p_ >= '0' && *p_ <= '9'; }
    bool literal(const char *word) {
        const size_t n = std::strlen(word);
        if (size_t(end_ - p_) < n || std::memcmp(p_, word, n) != 0) return fail("bad literal");
        p_ += n;
        return true;
    }

    bool value(JValue &v, int depth) {
        if (depth > kMaxDepth) return fail("nesting is too deep");
        skipWs();
        if (p_ >= end_) return fail("unexpected end");
        switch (*p_) {
        case '{': return object(v, depth);
        case '[': return array(v, depth);
        case '"': v.kind = JValue::String; return string(v.string);
        case 't': v.kind = JValue::Bool; v.boolean = true; return literal("true");
        case 'f': v.kind = JValue::Bool; v.boolean = false; return literal("false");
        case 'n': v.kind = JValue::Null; return literal("null");
        default: v.kind = JValue::Number; return number(v.number);
        }
    }

    bool object(JValue &v, int depth) {
        v.kind = JValue::Object;
        ++p_;
        skipWs();
        if (p_ < end_ && *p_ == '}') {
            ++p_;
            return true;
        }
        for (;;) {
            skipWs();
            if (p_ >= end_ || *p_ != '"') return fail("expected a key");
            std::string key;
            if (!string(key)) return false;
            skipWs();
            if (p_ >= end_ || *p_ != ':') return fail("expected ':'");
            ++p_;
            v.keys.push_back(std::move(key));
            v.items.emplace_back();
            if (!value(v.items.back(), depth + 1)) return false;
            skipWs();
            if (p_ >= end_) return fail("unexpected end");
            if (*p_ == ',') {
                ++p_;
                continue;
            }
            if (*p_ == '}') {
                ++p_;
                return true;
            }
            return fail("expected ',' or '}'");
        }
    }

    bool array(JValue &v, int depth) {
        v.kind = JValue::Array;
        ++p_;
        skipWs();
        if (p_ < end_ && *p_ == ']') {
            ++p_;
            return true;
        }
        for (;;) {
            v.items.emplace_back();
            if (!value(v.items.back(), depth + 1)) return false;
            skipWs();
            if (p_ >= end_) return fail("unexpected end");
            if (*p_ == ',') {
                ++p_;
                continue;
            }
            if (*p_ == ']') {
                ++p_;
                return true;
            }
            return fail("expected ',' or ']'");
        }
    }

    static void utf8(std::string &out, uint32_t cp) {
        if (cp < 0x80) {
            out += char(cp);
        } else if (cp < 0x800) {
            out += char(0xC0 | (cp >> 6));
            out += char(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += char(0xE0 | (cp >> 12));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        } else {
            out += char(0xF0 | (cp >> 18));
            out += char(0x80 | ((cp >> 12) & 0x3F));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        }
    }

    bool hex4(uint32_t &out) {
        if (end_ - p_ < 4) return fail("bad \\u escape");
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = *p_++;
            out <<= 4;
            if (c >= '0' && c <= '9') out |= uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') out |= uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= uint32_t(c - 'A' + 10);
            else return fail("bad \\u escape");
        }
        return true;
    }

    bool string(std::string &out) {
        ++p_; // opening quote
        for (;;) {
            if (p_ >= end_) return fail("unterminated string");
            const char c = *p_++;
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (p_ >= end_) return fail("unterminated string");
            const char e = *p_++;
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                uint32_t cp = 0;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp < 0xDC00 && end_ - p_ >= 6 && p_[0] == '\\' && p_[1] == 'u') {
                    p_ += 2;
                    uint32_t lo = 0;
                    if (!hex4(lo)) return false;
                    if (lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    else cp = 0xFFFD;
                } else if (cp >= 0xD800 && cp < 0xE000) {
                    cp = 0xFFFD;
                }
                utf8(out, cp);
                break;
            }
            default: return fail("bad escape");
            }
        }
    }

    // Locale independent (strtod follows LC_NUMERIC).
    bool number(double &out) {
        bool neg = false;
        if (p_ < end_ && *p_ == '-') {
            neg = true;
            ++p_;
        }
        if (!digit()) return fail("bad number");
        double mant = 0.0;
        int digits = 0;
        long exp10 = 0;
        while (digit()) {
            if (digits < 18) {
                mant = mant * 10.0 + double(*p_ - '0');
                if (mant > 0) ++digits;
            } else {
                ++exp10;
            }
            ++p_;
        }
        if (p_ < end_ && *p_ == '.') {
            ++p_;
            if (!digit()) return fail("bad number");
            while (digit()) {
                if (digits < 18) {
                    mant = mant * 10.0 + double(*p_ - '0');
                    if (mant > 0) ++digits;
                    --exp10;
                }
                ++p_;
            }
        }
        if (p_ < end_ && (*p_ == 'e' || *p_ == 'E')) {
            ++p_;
            bool eneg = false;
            if (p_ < end_ && (*p_ == '+' || *p_ == '-')) eneg = *p_++ == '-';
            if (!digit()) return fail("bad number");
            long e = 0;
            while (digit()) {
                if (e < 100000) e = e * 10 + (*p_ - '0');
                ++p_;
            }
            exp10 += eneg ? -e : e;
        }
        exp10 = std::max(-400L, std::min(400L, exp10));
        out = exp10 < 0 ? mant / std::pow(10.0, double(-exp10)) : mant * std::pow(10.0, double(exp10));
        if (neg) out = -out;
        return true;
    }

    const char *p_;
    const char *end_;
    std::string err_;
};

// JSON accessors with glTF defaults.
int64_t jint(const JValue *v, int64_t fallback) {
    if (!v || v->kind != JValue::Number || !std::isfinite(v->number)) return fallback;
    if (v->number < -9e15 || v->number > 9e15 || std::floor(v->number) != v->number) return fallback;
    return int64_t(v->number);
}
double jnum(const JValue *v, double fallback) {
    return v && v->kind == JValue::Number && std::isfinite(v->number) ? v->number : fallback;
}
std::string jstr(const JValue *v) { return v && v->kind == JValue::String ? v->string : std::string(); }

std::string lower(std::string s) {
    for (char &c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// --------------------------------------------------------------- buffers ---

struct BufferView {
    const unsigned char *data = nullptr;
    size_t size = 0;
};

bool base64Decode(const std::string &in, size_t start, std::vector<unsigned char> &out) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+' || c == '-') return 62;
        if (c == '/' || c == '_') return 63;
        return -1;
    };
    out.clear();
    out.reserve((in.size() - start) / 4 * 3);
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = start; i < in.size(); ++i) {
        const char c = in[i];
        if (c == '=') break;
        if (c == '\n' || c == '\r' || c == ' ') continue;
        const int v = val(c);
        if (v < 0) return false;
        acc = (acc << 6) | uint32_t(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<unsigned char>((acc >> bits) & 0xFF));
        }
    }
    return true;
}

std::string percentDecode(const std::string &s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            out += char(std::stoi(s.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

// ------------------------------------------------------------- matrices ---

using Mat4 = std::array<double, 16>; // column major, as glTF stores it

Mat4 identity() {
    Mat4 m{};
    m[0] = m[5] = m[10] = m[15] = 1.0;
    return m;
}

Mat4 mul(const Mat4 &a, const Mat4 &b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            double s = 0.0;
            for (int k = 0; k < 4; ++k) s += a[size_t(k * 4 + row)] * b[size_t(c * 4 + k)];
            r[size_t(c * 4 + row)] = s;
        }
    return r;
}

bool nodeMatrix(const JValue &node, Mat4 &out) {
    out = identity();
    if (const JValue *m = node.get("matrix")) {
        if (m->size() != 16) return false;
        for (size_t i = 0; i < 16; ++i) out[i] = jnum(m->at(i), i % 5 == 0 ? 1.0 : 0.0);
        return true;
    }
    double t[3] = {0, 0, 0}, s[3] = {1, 1, 1}, q[4] = {0, 0, 0, 1};
    if (const JValue *v = node.get("translation"))
        for (size_t i = 0; i < 3; ++i) t[i] = jnum(v->at(i), 0.0);
    if (const JValue *v = node.get("scale"))
        for (size_t i = 0; i < 3; ++i) s[i] = jnum(v->at(i), 1.0);
    if (const JValue *v = node.get("rotation"))
        for (size_t i = 0; i < 4; ++i) q[i] = jnum(v->at(i), i == 3 ? 1.0 : 0.0);
    const double len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (len > 1e-12)
        for (double &c : q) c /= len;
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    const double r[9] = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w),     2 * (x * z - y * w),
                         2 * (x * y - z * w),     1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
                         2 * (x * z + y * w),     2 * (y * z - x * w),     1 - 2 * (x * x + y * y)};
    for (int c = 0; c < 3; ++c)
        for (int row = 0; row < 3; ++row) out[size_t(c * 4 + row)] = r[c * 3 + row] * s[c];
    out[12] = t[0];
    out[13] = t[1];
    out[14] = t[2];
    return true;
}

double det3(const Mat4 &m) {
    return m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) +
           m[8] * (m[1] * m[6] - m[5] * m[2]);
}

// ------------------------------------------------------------ converter ---

int32_t contentsFromName(const std::string &value, bool &ok) {
    ok = true;
    const std::string v = lower(value);
    if (v == "solid") return REALMS_CONTENTS_SOLID;
    if (v == "window") return REALMS_CONTENTS_WINDOW;
    if (v == "water") return REALMS_CONTENTS_WATER;
    if (v == "slime") return REALMS_CONTENTS_SLIME;
    if (v == "lava") return REALMS_CONTENTS_LAVA;
    if (v == "playerclip") return REALMS_CONTENTS_PLAYERCLIP;
    if (v == "monsterclip") return REALMS_CONTENTS_MONSTERCLIP;
    if (v == "clip") return REALMS_CONTENTS_PLAYERCLIP | REALMS_CONTENTS_MONSTERCLIP;
    if (v == "none" || v == "nocollide") return 0;
    ok = false;
    return REALMS_CONTENTS_SOLID;
}

// realms_contents from extras: returns true when present.
bool extrasContents(const JValue *owner, int32_t &contents) {
    const JValue *extras = owner ? owner->get("extras") : nullptr;
    const JValue *v = extras ? extras->get("realms_contents") : nullptr;
    if (!v) return false;
    if (v->kind == JValue::Number) {
        contents = int32_t(jint(v, REALMS_CONTENTS_SOLID));
        return true;
    }
    bool ok = false;
    const int32_t c = contentsFromName(jstr(v), ok);
    if (ok) contents = c;
    return ok;
}

// Quake 2 point entities a node name may spawn. Brush entities (func_door,
// trigger_multiple, ...) need inline models, which mesh maps do not have:
// the game stops on them (PF_setmodel: NULL), so those nodes are skipped.
bool pointEntityClass(const std::string &c) {
    static const char *const prefixes[] = {"info_",    "item_",   "weapon_", "ammo_",  "key_",
                                           "monster_", "misc_",   "target_", "path_",  "point_", "light_"};
    static const char *const names[] = {"light",       "trigger_relay", "trigger_always", "trigger_counter",
                                        "trigger_key", "func_timer",    "func_clock"};
    for (char ch : c)
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') return false;
    for (const char *p : prefixes)
        if (c.size() > std::strlen(p) && c.compare(0, std::strlen(p), p) == 0) return true;
    for (const char *n : names)
        if (c == n) return true;
    return false;
}

// An extras key that can become an entity key: printable, no quotes or
// braces, not one the converter sets itself or a realms_ tag.
bool entityKeyAllowed(const std::string &key) {
    if (key.empty() || key.size() > 63) return false;
    for (char ch : key) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c <= ' ' || c >= 127 || c == '"' || c == '{' || c == '}') return false;
    }
    const std::string l = lower(key);
    return l != "classname" && l != "origin" && l.rfind("realms_", 0) != 0;
}

std::string entityNumber(double v) {
    char buf[32];
    if (v == std::floor(v) && std::fabs(v) < 1e9) std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
    else std::snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}

// Entity value text for an extras value: strings (without quotes, braces
// or control characters: ED_ParseEdict stops on a value starting with "}"),
// numbers, booleans (1/0) and arrays of up to 4 numbers ("_color":
// [1, 0.5, 0] -> "1 0.5 0").
bool entityValueText(const JValue &v, std::string &out) {
    out.clear();
    switch (v.kind) {
    case JValue::String:
        for (char c : v.string)
            if (c != '"' && c != '{' && c != '}' && static_cast<unsigned char>(c) >= ' ') out += c;
        if (out.size() > 255) out.resize(255);
        return true;
    case JValue::Number:
        if (!std::isfinite(v.number)) return false;
        out = entityNumber(v.number);
        return true;
    case JValue::Bool:
        out = v.boolean ? "1" : "0";
        return true;
    case JValue::Array:
        if (v.size() == 0 || v.size() > 4) return false;
        for (size_t i = 0; i < v.size(); ++i) {
            const JValue *e = v.at(i);
            if (e->kind != JValue::Number || !std::isfinite(e->number)) return false;
            out += (i ? " " : "") + entityNumber(e->number);
        }
        return true;
    default:
        return false;
    }
}

// "key" "value" lines for an extras object, skipping `skip` keys.
std::string entityKeys(const JValue *extras, const std::vector<std::string> &skip) {
    std::string r, value;
    if (!extras || extras->kind != JValue::Object) return r;
    for (size_t i = 0; i < extras->keys.size(); ++i) {
        const std::string &k = extras->keys[i];
        if (!entityKeyAllowed(k) || std::find(skip.begin(), skip.end(), lower(k)) != skip.end()) continue;
        if (!entityValueText(extras->items[i], value)) continue;
        if (lower(k) == "model" && !value.empty() && value[0] == '*') continue; // no inline models
        r += "\"" + k + "\" \"" + value + "\"\n";
    }
    return r;
}

class Converter {
public:
    Converter(const JValue &root, const std::vector<BufferView> &buffers, const GltfMapOptions &options, MeshMap &out)
        : root_(root), buffers_(buffers), opt_(options), out_(out) {}

    bool run(const std::string &mapName, std::string &error) {
        out_ = MeshMap{};
        buildSurfaces();
        std::vector<int64_t> roots;
        if (!sceneRoots(roots)) return fail(error, "bad scene");
        for (int64_t r : roots)
            if (!visit(r, identity(), 0, error)) return false;
        if (out_.surfaces.empty()) out_.surfaces.push_back(MeshMapSurface{"default", 0, 0, REALMS_CONTENTS_SOLID});
        if (!haveBounds_) out_.sourceMins = out_.sourceMaxs = {0, 0, 0};
        // worldspawn: the scene's extras ("sky", "gravity", "message", ...).
        const JValue *extras = sceneExtras();
        out_.entities = "{\n\"classname\" \"worldspawn\"\n";
        if (!extras || !extras->get("message")) out_.entities += "\"message\" \"" + clean(mapName) + "\"\n";
        out_.entities += entityKeys(extras, {}) + "}\n" + entities_;
        return true;
    }

private:
    struct Accessor {
        const unsigned char *base = nullptr;
        size_t count = 0, stride = 0;
        int64_t type = 0;
        int comps = 0;
        bool normalized = false;
    };

    static bool fail(std::string &error, const std::string &msg) {
        error = msg;
        return false;
    }

    static std::string clean(const std::string &s) {
        std::string r;
        for (char c : s)
            if (c != '"' && c != '\n' && c != '\r') r += c;
        return r;
    }

    const JValue *array(const char *key) const {
        const JValue *v = root_.get(key);
        return v && v->kind == JValue::Array ? v : nullptr;
    }

    void buildSurfaces() {
        const JValue *mats = array("materials");
        const size_t n = mats ? mats->size() : 0;
        materialSurface_.assign(n, -1);
        for (size_t i = 0; i < n; ++i) {
            const JValue &m = *mats->at(i);
            MeshMapSurface s;
            s.name = jstr(m.get("name"));
            if (s.name.empty()) s.name = "material_" + std::to_string(i);
            if (s.name.size() > 31) s.name.resize(31);
            const std::string l = lower(s.name);
            if (l.find("nocollide") != std::string::npos) s.contents = 0;
            else if (l.find("water") != std::string::npos) s.contents = REALMS_CONTENTS_WATER;
            else if (l.find("slime") != std::string::npos) s.contents = REALMS_CONTENTS_SLIME;
            else if (l.find("lava") != std::string::npos) s.contents = REALMS_CONTENTS_LAVA;
            extrasContents(&m, s.contents);
            if (const JValue *extras = m.get("extras")) {
                s.flags = int32_t(jint(extras->get("realms_flags"), 0));
                s.value = int32_t(jint(extras->get("realms_value"), 0));
            }
            materialSurface_[i] = int32_t(out_.surfaces.size());
            out_.surfaces.push_back(s);
        }
    }

    // Surface for a material (-1 = none) with an optional node override.
    int32_t surfaceFor(int64_t material, bool overridden, int32_t contents) {
        int32_t base = -1;
        if (material >= 0 && size_t(material) < materialSurface_.size()) {
            base = materialSurface_[size_t(material)];
        } else {
            if (defaultSurface_ < 0) {
                defaultSurface_ = int32_t(out_.surfaces.size());
                out_.surfaces.push_back(MeshMapSurface{"default", 0, 0, REALMS_CONTENTS_SOLID});
            }
            base = defaultSurface_;
        }
        if (!overridden || out_.surfaces[size_t(base)].contents == contents) return base;
        const auto key = std::make_pair(base, contents);
        auto it = variants_.find(key);
        if (it != variants_.end()) return it->second;
        MeshMapSurface s = out_.surfaces[size_t(base)];
        s.contents = contents;
        const int32_t index = int32_t(out_.surfaces.size());
        out_.surfaces.push_back(s);
        variants_[key] = index;
        return index;
    }

    const JValue *sceneExtras() const {
        const JValue *scenes = array("scenes");
        if (!scenes || scenes->size() == 0) return nullptr;
        int64_t scene = jint(root_.get("scene"), 0);
        if (scene < 0 || size_t(scene) >= scenes->size()) scene = 0;
        return scenes->at(size_t(scene))->get("extras");
    }

    bool sceneRoots(std::vector<int64_t> &roots) const {
        const JValue *nodes = array("nodes");
        const size_t numNodes = nodes ? nodes->size() : 0;
        const JValue *scenes = array("scenes");
        if (scenes && scenes->size() > 0) {
            int64_t scene = jint(root_.get("scene"), 0);
            if (scene < 0 || size_t(scene) >= scenes->size()) scene = 0;
            const JValue *list = scenes->at(size_t(scene))->get("nodes");
            for (size_t i = 0; list && i < list->size(); ++i) {
                const int64_t n = jint(list->at(i), -1);
                if (n < 0 || size_t(n) >= numNodes) return false;
                roots.push_back(n);
            }
            return true;
        }
        // No scenes: every node that is nobody's child.
        std::vector<char> child(numNodes, 0);
        for (size_t i = 0; i < numNodes; ++i) {
            const JValue *kids = nodes->at(i)->get("children");
            for (size_t k = 0; kids && k < kids->size(); ++k) {
                const int64_t c = jint(kids->at(k), -1);
                if (c >= 0 && size_t(c) < numNodes) child[size_t(c)] = 1;
            }
        }
        for (size_t i = 0; i < numNodes; ++i)
            if (!child[i]) roots.push_back(int64_t(i));
        return true;
    }

    bool accessor(int64_t index, int comps, Accessor &a, std::string &error) const {
        const JValue *accessors = array("accessors");
        const JValue *bufferViews = array("bufferViews");
        if (!accessors || index < 0 || size_t(index) >= accessors->size()) return fail(error, "bad accessor index");
        const JValue &acc = *accessors->at(size_t(index));
        if (acc.get("sparse")) return fail(error, "sparse accessors are not supported");
        const int64_t view = jint(acc.get("bufferView"), -1);
        if (!bufferViews || view < 0 || size_t(view) >= bufferViews->size())
            return fail(error, "accessor without a valid bufferView");
        const std::string type = jstr(acc.get("type"));
        const int typeComps = type == "SCALAR" ? 1 : type == "VEC2" ? 2 : type == "VEC3" ? 3 : type == "VEC4" ? 4 : 0;
        if (typeComps != comps) return fail(error, "unexpected accessor type");
        a.type = jint(acc.get("componentType"), 0);
        size_t compSize = 0;
        switch (a.type) {
        case 5120: case 5121: compSize = 1; break;
        case 5122: case 5123: compSize = 2; break;
        case 5125: case 5126: compSize = 4; break;
        default: return fail(error, "bad accessor componentType");
        }
        const int64_t count = jint(acc.get("count"), -1);
        if (count < 0 || count > (int64_t(1) << 28)) return fail(error, "bad accessor count");
        const JValue &bv = *bufferViews->at(size_t(view));
        const int64_t buffer = jint(bv.get("buffer"), -1);
        if (buffer < 0 || size_t(buffer) >= buffers_.size()) return fail(error, "bad bufferView buffer");
        const int64_t viewOffset = jint(bv.get("byteOffset"), 0), viewLength = jint(bv.get("byteLength"), -1);
        const int64_t stride = jint(bv.get("byteStride"), 0), accOffset = jint(acc.get("byteOffset"), 0);
        const BufferView &buf = buffers_[size_t(buffer)];
        if (viewOffset < 0 || viewLength < 0 || uint64_t(viewOffset) + uint64_t(viewLength) > buf.size)
            return fail(error, "bufferView is out of range");
        const size_t elem = compSize * size_t(comps);
        if (stride != 0 && (stride < int64_t(elem) || stride > 4096)) return fail(error, "bad bufferView byteStride");
        a.stride = stride ? size_t(stride) : elem;
        a.count = size_t(count);
        a.comps = comps;
        a.normalized = acc.get("normalized") && acc.get("normalized")->kind == JValue::Bool &&
                       acc.get("normalized")->boolean;
        if (accOffset < 0) return fail(error, "bad accessor byteOffset");
        if (a.count > 0 &&
            uint64_t(accOffset) + uint64_t(a.count - 1) * a.stride + elem > uint64_t(viewLength))
            return fail(error, "accessor is out of range");
        a.base = buf.data + viewOffset + accOffset;
        return true;
    }

    static double component(const Accessor &a, size_t i, int c) {
        const unsigned char *p = a.base + i * a.stride;
        switch (a.type) {
        case 5126: {
            float f;
            std::memcpy(&f, p + 4 * c, 4);
            return f;
        }
        case 5120: {
            const double v = double(int8_t(p[c]));
            return a.normalized ? std::max(v / 127.0, -1.0) : v;
        }
        case 5121: {
            const double v = double(p[c]);
            return a.normalized ? v / 255.0 : v;
        }
        case 5122: {
            int16_t s;
            std::memcpy(&s, p + 2 * c, 2);
            return a.normalized ? std::max(double(s) / 32767.0, -1.0) : double(s);
        }
        case 5123: {
            uint16_t s;
            std::memcpy(&s, p + 2 * c, 2);
            return a.normalized ? double(s) / 65535.0 : double(s);
        }
        default: {
            uint32_t u;
            std::memcpy(&u, p + 4 * c, 4);
            return double(u);
        }
        }
    }

    bool emitPrimitive(const JValue &prim, const Mat4 &world, bool overridden, int32_t contents, std::string &error) {
        const int64_t mode = jint(prim.get("mode"), 4);
        if (mode != 4 && mode != 5 && mode != 6) return true; // points / lines: nothing to collide with
        const JValue *attrs = prim.get("attributes");
        const int64_t posIndex = jint(attrs ? attrs->get("POSITION") : nullptr, -1);
        if (posIndex < 0) return true;
        Accessor pos;
        if (!accessor(posIndex, 3, pos, error)) return false;
        if (pos.type != 5126 && pos.type != 5120 && pos.type != 5121 && pos.type != 5122 && pos.type != 5123)
            return fail(error, "bad POSITION componentType");
        std::vector<uint32_t> idx;
        const int64_t indicesIndex = jint(prim.get("indices"), -1);
        if (indicesIndex >= 0) {
            Accessor ia;
            if (!accessor(indicesIndex, 1, ia, error)) return false;
            if (ia.type != 5121 && ia.type != 5123 && ia.type != 5125) return fail(error, "bad index componentType");
            idx.resize(ia.count);
            for (size_t i = 0; i < ia.count; ++i) {
                const double v = component(ia, i, 0);
                if (v >= double(pos.count)) return fail(error, "index is out of range");
                idx[i] = uint32_t(v);
            }
        } else {
            if (pos.count > (size_t(1) << 26)) return fail(error, "primitive is too large");
            idx.resize(pos.count);
            for (size_t i = 0; i < pos.count; ++i) idx[i] = uint32_t(i);
        }
        size_t tris = 0;
        if (mode == 4) tris = idx.size() / 3;
        else if (idx.size() >= 3) tris = idx.size() - 2;
        if (tris == 0) return true;
        if (out_.vertices.size() / 3 + pos.count > opt_.maxVertices || out_.triangleCount() + tris > opt_.maxTriangles)
            return fail(error, "mesh map is too large (more than " + std::to_string(opt_.maxTriangles) + " triangles)");

        // Vertices: node transform in glTF space, then axes + scale.
        const size_t base = out_.vertices.size() / 3;
        out_.vertices.reserve(out_.vertices.size() + pos.count * 3);
        for (size_t i = 0; i < pos.count; ++i) {
            const double x = component(pos, i, 0), y = component(pos, i, 1), z = component(pos, i, 2);
            const double wx = world[0] * x + world[4] * y + world[8] * z + world[12];
            const double wy = world[1] * x + world[5] * y + world[9] * z + world[13];
            const double wz = world[2] * x + world[6] * y + world[10] * z + world[14];
            const double src[3] = {wx, wy, wz};
            for (int k = 0; k < 3; ++k) {
                if (!std::isfinite(src[k])) return fail(error, "vertex is not finite");
                if (!haveBounds_) {
                    out_.sourceMins[size_t(k)] = out_.sourceMaxs[size_t(k)] = float(src[k]);
                } else {
                    out_.sourceMins[size_t(k)] = std::min(out_.sourceMins[size_t(k)], float(src[k]));
                    out_.sourceMaxs[size_t(k)] = std::max(out_.sourceMaxs[size_t(k)], float(src[k]));
                }
            }
            haveBounds_ = true;
            const double mapped[3] = {wx * opt_.scale, -wz * opt_.scale, wy * opt_.scale};
            for (double m : mapped) {
                if (!(std::fabs(m) <= 1e6)) return fail(error, "mesh does not fit in map space (lower map_mesh_scale)");
                out_.vertices.push_back(float(m));
            }
        }
        const int32_t surface = surfaceFor(jint(prim.get("material"), -1), overridden, contents);
        const bool flip = det3(world) < 0; // mirrored instance: keep front faces CCW
        auto tri = [&](uint32_t a, uint32_t b, uint32_t c) {
            if (flip) std::swap(b, c);
            out_.indices.push_back(int32_t(base + a));
            out_.indices.push_back(int32_t(base + b));
            out_.indices.push_back(int32_t(base + c));
            out_.triangleSurfaces.push_back(surface);
        };
        if (mode == 4) {
            for (size_t t = 0; t < tris; ++t) tri(idx[t * 3], idx[t * 3 + 1], idx[t * 3 + 2]);
        } else if (mode == 5) {
            for (size_t t = 0; t < tris; ++t) {
                if (t & 1) tri(idx[t + 1], idx[t], idx[t + 2]);
                else tri(idx[t], idx[t + 1], idx[t + 2]);
            }
        } else {
            for (size_t t = 0; t < tris; ++t) tri(idx[t + 1], idx[t + 2], idx[0]);
        }
        return true;
    }

    // A point entity at the node's origin. Facing: extras "angle" /
    // "angles" / "realms_angle", else the yaw of the node's +X axis.
    void addEntity(const JValue &node, const std::string &classname, const Mat4 &world) {
        const double o[3] = {world[12] * opt_.scale, -world[14] * opt_.scale, world[13] * opt_.scale};
        for (double v : o)
            if (!(std::fabs(v) <= 1e6)) return;
        entities_ += "{\n\"classname\" \"" + classname + "\"\n\"origin\" \"" + std::to_string(std::lround(o[0])) + " " +
                     std::to_string(std::lround(o[1])) + " " + std::to_string(std::lround(o[2])) + "\"\n";
        const JValue *extras = node.get("extras");
        if (extras && extras->kind != JValue::Object) extras = nullptr;
        const JValue *realmsAngle = extras ? extras->get("realms_angle") : nullptr;
        const bool explicitAngle = extras && (extras->get("angle") || extras->get("angles"));
        if (realmsAngle && !explicitAngle) {
            entities_ += "\"angle\" \"" + std::to_string(std::lround(jnum(realmsAngle, 0.0))) + "\"\n";
        } else if (!explicitAngle) {
            // glTF +X axis (wx, wy, wz) -> map (wx, -wz): yaw about map +Z.
            const double fx = world[0], fy = -world[2];
            if (fx * fx + fy * fy > 1e-12) {
                long yaw = std::lround(std::atan2(fy, fx) * 180.0 / 3.14159265358979323846);
                if (yaw < 0) yaw += 360;
                if (yaw % 360 != 0) entities_ += "\"angle\" \"" + std::to_string(yaw % 360) + "\"\n";
            }
        }
        entities_ += entityKeys(extras, {}) + "}\n";
    }

    bool visit(int64_t index, const Mat4 &parent, int depth, std::string &error) {
        if (depth > 64) return fail(error, "node hierarchy is too deep");
        if (++visits_ > (size_t(1) << 20)) return fail(error, "too many node instances");
        const JValue &node = *array("nodes")->at(size_t(index));
        Mat4 local;
        if (!nodeMatrix(node, local)) return fail(error, "bad node matrix");
        const Mat4 world = mul(parent, local);
        const std::string name = jstr(node.get("name"));
        std::string classname = lower(name);
        const size_t dot = classname.find('.'); // Blender ".001" suffixes
        if (dot != std::string::npos) classname.resize(dot);
        if (pointEntityClass(classname)) addEntity(node, classname, world);

        int32_t contents = REALMS_CONTENTS_SOLID;
        bool overridden = extrasContents(&node, contents);
        if (lower(name).find("nocollide") != std::string::npos) {
            overridden = true;
            contents = 0;
        }
        const int64_t mesh = jint(node.get("mesh"), -1);
        if (mesh >= 0) {
            const JValue *meshes = array("meshes");
            if (!meshes || size_t(mesh) >= meshes->size()) return fail(error, "bad node mesh");
            const JValue *prims = meshes->at(size_t(mesh))->get("primitives");
            for (size_t p = 0; prims && p < prims->size(); ++p)
                if (!emitPrimitive(*prims->at(p), world, overridden, contents, error)) return false;
        }
        const JValue *kids = node.get("children");
        const size_t numNodes = array("nodes")->size();
        for (size_t k = 0; kids && k < kids->size(); ++k) {
            const int64_t c = jint(kids->at(k), -1);
            if (c < 0 || size_t(c) >= numNodes) return fail(error, "bad node child");
            if (!visit(c, world, depth + 1, error)) return false;
        }
        return true;
    }

    const JValue &root_;
    const std::vector<BufferView> &buffers_;
    GltfMapOptions opt_;
    MeshMap &out_;
    std::vector<int32_t> materialSurface_;
    int32_t defaultSurface_ = -1;
    std::map<std::pair<int32_t, int32_t>, int32_t> variants_;
    std::string entities_;
    size_t visits_ = 0;
    bool haveBounds_ = false;
};

uint32_t rd32(const unsigned char *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

} // namespace

bool isMeshMapPath(const std::string &path) {
    const std::string l = lower(path);
    auto ends = [&](const char *suffix) {
        const size_t n = std::strlen(suffix);
        return l.size() >= n && l.compare(l.size() - n, n, suffix) == 0;
    };
    return ends(".glb") || ends(".gltf");
}

uint32_t crc32(const unsigned char *data, size_t size) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool gltfToMeshMap(const unsigned char *data, size_t size, const std::string &path, const MeshMapReadFn &readFile,
                   const GltfMapOptions &options, MeshMap &out, std::string &error) {
    out = MeshMap{};
    error.clear();
    if (!data || size == 0) {
        error = "empty file";
        return false;
    }
    if (!(options.scale > 0.0f) || !std::isfinite(options.scale)) {
        error = "bad scale";
        return false;
    }
    const char *jsonBegin = nullptr;
    const char *jsonEnd = nullptr;
    BufferView bin;
    if (size >= 12 && rd32(data) == 0x46546C67u) { // "glTF"
        if (rd32(data + 4) != 2) {
            error = "unsupported GLB version";
            return false;
        }
        const size_t total = std::min<size_t>(rd32(data + 8), size);
        size_t at = 12;
        while (at + 8 <= total) {
            const size_t len = rd32(data + at);
            const uint32_t type = rd32(data + at + 4);
            if (len > total - at - 8) {
                error = "GLB chunk is out of range";
                return false;
            }
            const unsigned char *chunk = data + at + 8;
            if (type == 0x4E4F534Au && !jsonBegin) { // "JSON"
                jsonBegin = reinterpret_cast<const char *>(chunk);
                jsonEnd = jsonBegin + len;
            } else if (type == 0x004E4942u && !bin.data) { // "BIN\0"
                bin.data = chunk;
                bin.size = len;
            }
            at += 8 + ((len + 3) & ~size_t(3));
        }
        if (!jsonBegin) {
            error = "GLB has no JSON chunk";
            return false;
        }
    } else {
        jsonBegin = reinterpret_cast<const char *>(data);
        jsonEnd = jsonBegin + size;
        if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) jsonBegin += 3; // UTF-8 BOM
    }

    JValue root;
    JsonParser parser(jsonBegin, jsonEnd);
    if (!parser.parse(root, error)) return false;
    if (root.kind != JValue::Object) {
        error = "glTF root is not an object";
        return false;
    }
    const JValue *asset = root.get("asset");
    const std::string version = jstr(asset ? asset->get("version") : nullptr);
    if (version.empty() || version[0] != '2') {
        error = "not a glTF 2.0 asset";
        return false;
    }
    if (const JValue *required = root.get("extensionsRequired"))
        for (size_t i = 0; i < required->size(); ++i) {
            const std::string ext = jstr(required->at(i));
            if (ext != "KHR_mesh_quantization" && ext != "KHR_materials_emissive_strength" &&
                ext != "KHR_texture_transform" && ext != "KHR_materials_unlit") {
                error = "unsupported required extension " + ext;
                return false;
            }
        }

    // Buffers: GLB BIN chunk, base64 data URIs or files next to the asset.
    std::vector<std::vector<unsigned char>> owned;
    std::vector<BufferView> buffers;
    const JValue *bufs = root.get("buffers");
    const size_t numBuffers = bufs ? bufs->size() : 0;
    owned.resize(numBuffers);
    const std::string dir = path.find('/') == std::string::npos ? "" : path.substr(0, path.rfind('/') + 1);
    for (size_t i = 0; i < numBuffers; ++i) {
        const JValue &b = *bufs->at(i);
        const int64_t length = jint(b.get("byteLength"), -1);
        if (length < 0) {
            error = "bad buffer byteLength";
            return false;
        }
        BufferView view;
        const std::string uri = jstr(b.get("uri"));
        if (uri.empty()) {
            if (i != 0 || !bin.data) {
                error = "buffer has no data";
                return false;
            }
            view = bin;
        } else if (uri.compare(0, 5, "data:") == 0) {
            const size_t comma = uri.find(',');
            if (comma == std::string::npos || uri.rfind(";base64", comma) == std::string::npos ||
                !base64Decode(uri, comma + 1, owned[i])) {
                error = "bad data URI";
                return false;
            }
            view = {owned[i].data(), owned[i].size()};
        } else {
            const std::string rel = percentDecode(uri);
            if (rel.find("..") != std::string::npos || rel[0] == '/' || rel.find(':') != std::string::npos ||
                rel.find('\\') != std::string::npos) {
                error = "unsafe buffer uri " + uri;
                return false;
            }
            if (!readFile || !readFile(dir + rel, owned[i])) {
                error = "couldn't load buffer " + dir + rel;
                return false;
            }
            view = {owned[i].data(), owned[i].size()};
        }
        if (view.size < size_t(length)) {
            error = "buffer is shorter than its byteLength";
            return false;
        }
        view.size = size_t(length);
        buffers.push_back(view);
    }

    std::string name = path;
    if (name.find('/') != std::string::npos) name = name.substr(name.rfind('/') + 1);
    Converter conv(root, buffers, options, out);
    if (!conv.run(name, error)) {
        out = MeshMap{};
        return false;
    }
    return true;
}

RealmsMeshDesc meshMapDesc(const MeshMap &map, std::vector<RealmsMeshSurface> &surfaceStorage, const char *name,
                           uint32_t checksum) {
    surfaceStorage.assign(map.surfaces.size(), RealmsMeshSurface{});
    for (size_t i = 0; i < map.surfaces.size(); ++i) {
        RealmsMeshSurface &s = surfaceStorage[i];
        const std::string &n = map.surfaces[i].name;
        std::memcpy(s.name, n.data(), std::min(n.size(), sizeof(s.name) - 1));
        s.flags = map.surfaces[i].flags;
        s.value = map.surfaces[i].value;
        s.contents = map.surfaces[i].contents;
    }
    RealmsMeshDesc d{};
    d.struct_size = sizeof(RealmsMeshDesc);
    d.checksum = checksum;
    d.name = name;
    d.vertices = map.vertices.empty() ? nullptr : map.vertices.data();
    d.num_vertices = int32_t(map.vertices.size() / 3);
    d.indices = map.indices.empty() ? nullptr : map.indices.data();
    d.num_triangles = int32_t(map.indices.size() / 3);
    d.triangle_surfaces = map.triangleSurfaces.empty() ? nullptr : map.triangleSurfaces.data();
    d.surfaces = surfaceStorage.empty() ? nullptr : surfaceStorage.data();
    d.num_surfaces = int32_t(surfaceStorage.size());
    d.thickness = map.thickness;
    d.entities = map.entities.c_str();
    return d;
}

} // namespace realms
