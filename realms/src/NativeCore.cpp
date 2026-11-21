// Realms Engine - C++ reference core.
//
// Implements the exact same RealmsCoreApi contract as the Free Pascal core
// (realms/pascal) so every host can run even when the Pascal library is not
// packaged for a platform yet, and so both implementations can be checked
// against one conformance suite (tests/realms). Behaviour mirrors Quake 2
// qcommon/cvar.c, cmd.c and files.c; keep it in lock-step with rlm_cvar.pas,
// rlm_cmd.pas and rlm_files.pas; the collision model (cmodel.c) lives in
// NativeCModel.cpp next to rlm_cmodel.pas.

#include "realms/realms_bridge.h"

#include "NativeCModel.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr size_t kMaxCbufText = 65536;
constexpr size_t kMaxStringTokens = 80;
constexpr int kAliasLoopCount = 16;
constexpr int kMacroLoopLimit = 100;
constexpr int kMaxFilesInPack = 4096;
constexpr int kPakHeaderSize = 12;
constexpr int kPakEntrySize = 64;

struct Cvar {
    std::string name, value, defaultValue, latchedValue;
    bool hasLatched = false;
    int32_t flags = 0;
    bool modified = false;
    float numValue = 0.0f;
};

enum class CmdKind { Builtin, Host };
using CmdProc = void (*)();

struct Command {
    std::string name;
    CmdKind kind = CmdKind::Builtin;
    CmdProc proc = nullptr;
};

struct Alias {
    std::string name, value;
};

struct PakEntry {
    std::string name;
    int32_t filePos = 0, fileLen = 0;
};

struct Pack {
    std::string hostPath;
    std::vector<PakEntry> entries;
    std::string resident;
};

struct CoreState {
    RealmsHostApi host{};
    bool hostBound = false;
    bool initialized = false;
    int64_t timeMs = 0;

    std::vector<std::unique_ptr<Cvar>> cvars; // registration order
    std::vector<Command> commands;            // newest first, like Cmd_AddCommand
    std::vector<Alias> aliases;               // newest first
    std::vector<Pack> packs;                  // newest first

    std::string cmdText;
    bool cmdWait = false;
    int aliasCount = 0;
    std::vector<std::string> argv;
    std::string args;
};

CoreState &S() {
    static CoreState state;
    return state;
}

// ------------------------------------------------------------ helpers ---

std::string lower(std::string s) {
    for (auto &c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool sameNameCI(const std::string &a, const std::string &b) {
    return a.size() == b.size() && lower(a) == lower(b);
}

std::string str(const char *p) { return p ? std::string(p) : std::string(); }

void emit(int32_t level, const std::string &text) {
    auto &s = S();
    if (s.hostBound && s.host.print) s.host.print(s.host.user, level, text.c_str());
}
void comPrintf(const std::string &t) { emit(REALMS_PRINT_INFO, t); }
void comWarning(const std::string &t) { emit(REALMS_PRINT_WARNING, t); }

bool hostReadAll(const std::string &path, std::string &out) {
    auto &s = S();
    out.clear();
    if (!s.hostBound || !s.host.read_file) return false;
    void *raw = nullptr;
    int32_t size = 0;
    if (s.host.read_file(s.host.user, path.c_str(), &raw, &size) != 1) return false;
    if (raw && size > 0) out.assign(static_cast<const char *>(raw), size_t(size));
    if (raw && s.host.free_file) s.host.free_file(s.host.user, raw);
    return true;
}

bool hostHasRangeReads() { return S().hostBound && S().host.read_file_range; }

int32_t hostReadRange(const std::string &path, int64_t offset, int32_t size, void *buffer) {
    if (!hostHasRangeReads()) return -1;
    return S().host.read_file_range(S().host.user, path.c_str(), offset, size, buffer);
}

bool hostWriteAll(const std::string &path, const std::string &data) {
    auto &s = S();
    if (!s.hostBound || !s.host.write_file) return false;
    return s.host.write_file(s.host.user, path.c_str(), data.data(), int32_t(data.size())) == 1;
}

// atof(): longest numeric prefix, same as RealmsAtof in rlm_cvar.pas.
float realmsAtof(const std::string &s) {
    size_t i = 0, n = s.size();
    while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    double sign = 1;
    if (i < n && (s[i] == '+' || s[i] == '-')) { if (s[i] == '-') sign = -1; ++i; }
    double value = 0;
    bool sawDigit = false;
    while (i < n && std::isdigit(static_cast<unsigned char>(s[i]))) { value = value * 10 + (s[i] - '0'); sawDigit = true; ++i; }
    if (i < n && s[i] == '.') {
        ++i;
        double scale = 0.1;
        while (i < n && std::isdigit(static_cast<unsigned char>(s[i]))) { value += (s[i] - '0') * scale; scale /= 10; sawDigit = true; ++i; }
    }
    if (!sawDigit) return 0.0f;
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        size_t j = i + 1;
        double expSign = 1;
        if (j < n && (s[j] == '+' || s[j] == '-')) { if (s[j] == '-') expSign = -1; ++j; }
        if (j < n && std::isdigit(static_cast<unsigned char>(s[j]))) {
            int expo = 0;
            while (j < n && std::isdigit(static_cast<unsigned char>(s[j])) && expo < 400) { expo = expo * 10 + (s[j] - '0'); ++j; }
            expo = std::min(expo, 38);
            while (expo-- > 0) value = expSign > 0 ? value * 10 : value / 10;
        }
    }
    value = std::min(value, 3.4e38);
    return float(sign * value);
}

// --------------------------------------------------------------- cvars ---

bool infoValidate(const std::string &s) {
    return s.find('\\') == std::string::npos && s.find('"') == std::string::npos && s.find(';') == std::string::npos;
}

bool validName(const std::string &name) {
    if (name.empty() || name.size() >= 64) return false;
    for (unsigned char c : name)
        if (c <= ' ' || c == '"' || c == ';' || c == '\\' || c == '$') return false;
    return true;
}

Cvar *cvarFind(const std::string &name) {
    for (auto &v : S().cvars)
        if (sameNameCI(v->name, name)) return v.get();
    return nullptr;
}

void assignValue(Cvar &v, const std::string &value) {
    v.value = value;
    v.numValue = realmsAtof(value);
    v.modified = true;
}

Cvar *cvarGet(const std::string &name, const std::string &def, int32_t flags) {
    if (!validName(name)) { comWarning("invalid cvar name \"" + name + "\""); return nullptr; }
    if ((flags & (REALMS_CVAR_USERINFO | REALMS_CVAR_SERVERINFO)) && !infoValidate(def)) {
        comWarning("invalid info cvar value for " + name);
        return nullptr;
    }
    if (Cvar *v = cvarFind(name)) {
        v->flags |= flags;
        v->defaultValue = def;
        return v;
    }
    auto v = std::make_unique<Cvar>();
    v->name = name;
    v->defaultValue = def;
    v->flags = flags;
    assignValue(*v, def);
    S().cvars.push_back(std::move(v));
    return S().cvars.back().get();
}

float cvarValue(const std::string &name) {
    const Cvar *v = cvarFind(name);
    return v ? v->numValue : 0.0f;
}

std::string cvarString(const std::string &name) {
    const Cvar *v = cvarFind(name);
    return v ? v->value : std::string();
}

int32_t cvarSet2(const std::string &name, const std::string &value, bool force) {
    Cvar *v = cvarFind(name);
    if (!v) return cvarGet(name, value, 0) ? REALMS_CVAR_SET_OK : REALMS_CVAR_SET_UNKNOWN;

    if ((v->flags & (REALMS_CVAR_USERINFO | REALMS_CVAR_SERVERINFO)) && !infoValidate(value)) {
        comPrintf("invalid info cvar value");
        return REALMS_CVAR_SET_READONLY;
    }
    if (!force) {
        if (v->flags & REALMS_CVAR_NOSET) {
            comPrintf(name + " is write protected.");
            return REALMS_CVAR_SET_READONLY;
        }
        if ((v->flags & REALMS_CVAR_CHEAT) && value != v->value && !sameNameCI(name, "sv_cheats") &&
            cvarValue("sv_cheats") == 0.0f) {
            comPrintf(name + " is cheat protected.");
            return REALMS_CVAR_SET_CHEAT;
        }
        if (v->flags & REALMS_CVAR_LATCH) {
            if (v->hasLatched) {
                if (value == v->latchedValue) return REALMS_CVAR_SET_LATCHED;
            } else if (value == v->value) {
                return REALMS_CVAR_SET_OK;
            }
            v->latchedValue = value;
            v->hasLatched = true;
            comPrintf(name + " will be changed for next game.");
            return REALMS_CVAR_SET_LATCHED;
        }
    } else if (v->hasLatched) {
        v->hasLatched = false;
        v->latchedValue.clear();
    }
    if (value == v->value) return REALMS_CVAR_SET_OK;
    assignValue(*v, value);
    return REALMS_CVAR_SET_OK;
}

Cvar *cvarFullSet(const std::string &name, const std::string &value, int32_t flags) {
    Cvar *v = cvarFind(name);
    if (!v) return cvarGet(name, value, flags);
    if (value != v->value) assignValue(*v, value);
    v->flags = flags;
    return v;
}

std::string cvarArchiveText() {
    std::string out = "// generated by Realms Engine (native C++ core), do not modify\n";
    for (const auto &v : S().cvars)
        if (v->flags & REALMS_CVAR_ARCHIVE)
            out += "seta " + v->name + " \"" + (v->hasLatched ? v->latchedValue : v->value) + "\"\n";
    return out;
}

// ---------------------------------------------------------- filesystem ---

std::string normalizePath(const std::string &path) {
    std::string r = lower(path);
    std::replace(r.begin(), r.end(), '\\', '/');
    while (!r.empty() && r[0] == '/') r.erase(0, 1);
    return r;
}

bool safeRelativePath(const std::string &p) {
    return !p.empty() && p[0] != '/' && p.find("..") == std::string::npos && p.find(':') == std::string::npos;
}

int32_t le32(const std::string &buf, size_t off) {
    const auto b = [&](size_t i) { return uint32_t(static_cast<unsigned char>(buf[off + i])); };
    return int32_t(b(0) | (b(1) << 8) | (b(2) << 16) | (b(3) << 24));
}

bool readBytes(const std::string &path, int64_t offset, int32_t size, std::string &out) {
    out.assign(size_t(size), '\0');
    if (size == 0) return true;
    return hostReadRange(path, offset, size, out.data()) == size;
}

int32_t fsAddPak(const std::string &path) {
    std::string header, dir, resident;
    if (hostHasRangeReads()) {
        if (!readBytes(path, 0, kPakHeaderSize, header)) { comWarning("FS_AddPak: could not read " + path); return -1; }
    } else {
        if (!hostReadAll(path, resident)) { comWarning("FS_AddPak: could not read " + path); return -1; }
        if (resident.size() < size_t(kPakHeaderSize)) { comWarning(path + " is not a packfile"); return -1; }
        header = resident.substr(0, kPakHeaderSize);
    }
    if (header.compare(0, 4, "PACK") != 0) { comWarning(path + " is not a packfile"); return -1; }
    const int32_t dirofs = le32(header, 4), dirlen = le32(header, 8);
    if (dirofs < kPakHeaderSize || dirlen < 0 || dirlen % kPakEntrySize != 0) {
        comWarning(path + " has a corrupt directory");
        return -1;
    }
    const int32_t count = dirlen / kPakEntrySize;
    if (count > kMaxFilesInPack) { comWarning(path + " has " + std::to_string(count) + " files"); return -1; }
    if (!resident.empty()) {
        if (int64_t(dirofs) + dirlen > int64_t(resident.size())) { comWarning(path + " has a truncated directory"); return -1; }
        dir = resident.substr(size_t(dirofs), size_t(dirlen));
    } else if (!readBytes(path, dirofs, dirlen, dir)) {
        comWarning(path + " has a truncated directory");
        return -1;
    }
    Pack pack;
    pack.hostPath = path;
    pack.entries.resize(size_t(count));
    for (int32_t i = 0; i < count; ++i) {
        const size_t base = size_t(i) * kPakEntrySize;
        std::string name;
        for (size_t j = 0; j < 56 && dir[base + j] != '\0'; ++j) name += dir[base + j];
        PakEntry &e = pack.entries[size_t(i)];
        e.name = normalizePath(name);
        e.filePos = le32(dir, base + 56);
        e.fileLen = le32(dir, base + 60);
        if (e.filePos < 0 || e.fileLen < 0 ||
            (!resident.empty() && int64_t(e.filePos) + e.fileLen > int64_t(resident.size()))) {
            comWarning(path + ": entry " + name + " is out of range");
            return -1;
        }
    }
    pack.resident = std::move(resident);
    S().packs.insert(S().packs.begin(), std::move(pack));
    comPrintf("Added packfile " + path + " (" + std::to_string(count) + " files)");
    return count;
}

bool fsLoadFile(const std::string &path, void **outData, int32_t *outSize) {
    *outData = nullptr;
    *outSize = 0;
    const std::string name = normalizePath(path);
    if (!safeRelativePath(name)) return false;
    for (const Pack &pack : S().packs) {
        for (auto it = pack.entries.rbegin(); it != pack.entries.rend(); ++it) {
            if (it->name != name) continue;
            auto *data = static_cast<unsigned char *>(std::malloc(size_t(it->fileLen) + 1));
            if (!data) return false;
            data[it->fileLen] = 0;
            if (it->fileLen > 0) {
                if (!pack.resident.empty()) {
                    std::memcpy(data, pack.resident.data() + it->filePos, size_t(it->fileLen));
                } else if (hostReadRange(pack.hostPath, it->filePos, it->fileLen, data) != it->fileLen) {
                    std::free(data);
                    return false;
                }
            }
            *outData = data;
            *outSize = it->fileLen;
            return true;
        }
    }
    std::string hostPath = path, blob;
    std::replace(hostPath.begin(), hostPath.end(), '\\', '/');
    if (!hostReadAll(hostPath, blob)) return false;
    auto *data = static_cast<unsigned char *>(std::malloc(blob.size() + 1));
    if (!data) return false;
    if (!blob.empty()) std::memcpy(data, blob.data(), blob.size());
    data[blob.size()] = 0;
    *outData = data;
    *outSize = int32_t(blob.size());
    return true;
}

bool fsLoadText(const std::string &path, std::string &text) {
    void *data = nullptr;
    int32_t size = 0;
    if (!fsLoadFile(path, &data, &size)) return false;
    text.assign(static_cast<const char *>(data), size_t(size));
    std::free(data);
    return true;
}

// ------------------------------------------------------------- command ---

int argc() { return int(S().argv.size()); }
std::string argvAt(int i) { return i >= 0 && i < argc() ? S().argv[size_t(i)] : std::string(); }

void cmdExecuteString(const std::string &text);

void cbufAddText(const std::string &text) {
    if (S().cmdText.size() + text.size() > kMaxCbufText) { comWarning("Cbuf_AddText: overflow"); return; }
    S().cmdText += text;
}

void cbufInsertText(const std::string &text) {
    if (S().cmdText.size() + text.size() + 1 > kMaxCbufText) { comWarning("Cbuf_InsertText: overflow"); return; }
    if (!text.empty() && text.back() != '\n') S().cmdText = text + "\n" + S().cmdText;
    else S().cmdText = text + S().cmdText;
}

void cbufExecute() {
    auto &s = S();
    s.aliasCount = 0;
    while (!s.cmdText.empty()) {
        int quotes = 0;
        size_t i = 0;
        for (; i < s.cmdText.size(); ++i) {
            if (s.cmdText[i] == '"') ++quotes;
            if (!(quotes & 1) && s.cmdText[i] == ';') break;
            if (s.cmdText[i] == '\n') break;
        }
        const std::string line = s.cmdText.substr(0, i);
        s.cmdText.erase(0, std::min(i + 1, s.cmdText.size()));
        cmdExecuteString(line);
        if (s.cmdWait) { s.cmdWait = false; break; }
    }
}

bool isNameChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.';
}

std::string macroExpand(const std::string &text) {
    std::string out;
    bool inQuote = false;
    int expansions = 0;
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '"') inQuote = !inQuote;
        if (!inQuote && text[i] == '$' && i + 1 < text.size() && isNameChar(text[i + 1])) {
            size_t j = i + 1;
            while (j < text.size() && isNameChar(text[j])) ++j;
            if (++expansions > kMacroLoopLimit) { comWarning("Macro expansion loop, discarded."); return {}; }
            out += cvarString(text.substr(i + 1, j - i - 1));
            i = j;
            continue;
        }
        out += text[i++];
    }
    if (inQuote) { comPrintf("Line has unmatched quote, discarded."); return {}; }
    return out;
}

void tokenize(const std::string &text, bool expand) {
    auto &s = S();
    s.argv.clear();
    s.args.clear();
    const std::string src = expand ? macroExpand(text) : text;
    const size_t n = src.size();
    size_t i = 0;
    for (;;) {
        while (i < n && static_cast<unsigned char>(src[i]) <= ' ' && src[i] != '\n') ++i;
        if (i >= n || src[i] == '\n') break;
        if (src[i] == '/' && i + 1 < n && src[i + 1] == '/') break;
        if (s.argv.size() == 1) {
            s.args = src.substr(i);
            while (!s.args.empty() && static_cast<unsigned char>(s.args.back()) <= ' ') s.args.pop_back();
        }
        std::string token;
        if (src[i] == '"') {
            ++i;
            while (i < n && src[i] != '"') token += src[i++];
            if (i < n) ++i;
        } else {
            while (i < n && static_cast<unsigned char>(src[i]) > ' ') token += src[i++];
        }
        if (s.argv.size() < kMaxStringTokens) s.argv.push_back(token);
    }
}

Command *findCmd(const std::string &name) {
    for (auto &c : S().commands)
        if (sameNameCI(c.name, name)) return &c;
    return nullptr;
}

Alias *findAlias(const std::string &name) {
    for (auto &a : S().aliases)
        if (sameNameCI(a.name, name)) return &a;
    return nullptr;
}

bool addCommand(const std::string &name, CmdProc proc, CmdKind kind) {
    if (name.empty()) return false;
    if (cvarFind(name)) { comPrintf("Cmd_AddCommand: " + name + " already defined as a var"); return false; }
    if (findCmd(name)) { comPrintf("Cmd_AddCommand: " + name + " already defined"); return false; }
    S().commands.insert(S().commands.begin(), Command{name, kind, proc});
    return true;
}

void removeCommand(const std::string &name) {
    auto &c = S().commands;
    auto it = std::find_if(c.begin(), c.end(), [&](const Command &x) { return sameNameCI(x.name, name); });
    if (it != c.end()) c.erase(it);
}

bool cvarCommand() {
    Cvar *v = cvarFind(argvAt(0));
    if (!v) return false;
    if (argc() == 1) comPrintf("\"" + v->name + "\" is \"" + v->value + "\"");
    else cvarSet2(v->name, argvAt(1), false);
    return true;
}

void setF() {
    if (argc() != 3 && argc() != 4) { comPrintf("usage: set <variable> <value> [u / s]"); return; }
    if (argc() == 4) {
        int32_t flags;
        if (argvAt(3) == "u") flags = REALMS_CVAR_USERINFO;
        else if (argvAt(3) == "s") flags = REALMS_CVAR_SERVERINFO;
        else { comPrintf("flags can only be 'u' or 's'"); return; }
        cvarFullSet(argvAt(1), argvAt(2), flags);
    } else {
        cvarSet2(argvAt(1), argvAt(2), false);
    }
}

void setaF() {
    if (argc() != 3) { comPrintf("usage: seta <variable> <value>"); return; }
    cvarSet2(argvAt(1), argvAt(2), false);
    if (Cvar *v = cvarFind(argvAt(1))) v->flags |= REALMS_CVAR_ARCHIVE;
}

void toggleF() {
    if (argc() != 2) { comPrintf("usage: toggle <variable>"); return; }
    Cvar *v = cvarFind(argvAt(1));
    if (!v) { comPrintf("toggle: unknown variable " + argvAt(1)); return; }
    cvarSet2(v->name, v->numValue != 0.0f ? "0" : "1", false);
}

void resetF() {
    if (argc() != 2) { comPrintf("usage: reset <variable>"); return; }
    if (Cvar *v = cvarFind(argvAt(1))) cvarSet2(v->name, v->defaultValue, false);
}

void cvarListF() {
    int count = 0;
    for (const auto &v : S().cvars) {
        std::string line;
        line += (v->flags & REALMS_CVAR_ARCHIVE) ? '*' : ' ';
        line += (v->flags & REALMS_CVAR_USERINFO) ? 'U' : ' ';
        line += (v->flags & REALMS_CVAR_SERVERINFO) ? 'S' : ' ';
        line += (v->flags & REALMS_CVAR_NOSET) ? '-' : (v->flags & REALMS_CVAR_LATCH) ? 'L' : ' ';
        line += (v->flags & REALMS_CVAR_CHEAT) ? 'C' : ' ';
        comPrintf(line + " " + v->name + " \"" + v->value + "\"");
        ++count;
    }
    comPrintf(std::to_string(count) + " cvars");
}

void writeConfigF() {
    const std::string name = argc() > 1 ? argvAt(1) : "config.cfg";
    if (hostWriteAll(name, cvarArchiveText())) comPrintf("Wrote " + name);
    else comWarning("Couldn't write " + name);
}

void execF() {
    if (argc() != 2) { comPrintf("exec <filename> : execute a script file"); return; }
    std::string text;
    if (!fsLoadText(argvAt(1), text)) { comPrintf("couldn't exec " + argvAt(1)); return; }
    comPrintf("execing " + argvAt(1));
    cbufInsertText(text);
}

void echoF() {
    std::string line;
    for (int i = 1; i < argc(); ++i) { if (i > 1) line += ' '; line += argvAt(i); }
    comPrintf(line);
}

void aliasF() {
    auto &s = S();
    if (argc() == 1) {
        comPrintf("Current alias commands:");
        for (const auto &a : s.aliases) {
            std::string v = a.value;
            while (!v.empty() && static_cast<unsigned char>(v.back()) <= ' ') v.pop_back();
            comPrintf(a.name + " : " + v);
        }
        return;
    }
    const std::string name = argvAt(1);
    if (name.size() >= 32) { comPrintf("Alias name is too long"); return; }
    std::string value;
    for (int i = 2; i < argc(); ++i) { if (i > 2) value += ' '; value += argvAt(i); }
    value += '\n';
    if (Alias *a = findAlias(name)) a->value = value;
    else s.aliases.insert(s.aliases.begin(), Alias{name, value});
}

void unaliasF() {
    if (argc() != 2) { comPrintf("unalias <name> : delete an alias"); return; }
    auto &a = S().aliases;
    const std::string name = argvAt(1);
    auto it = std::find_if(a.begin(), a.end(), [&](const Alias &x) { return sameNameCI(x.name, name); });
    if (it != a.end()) a.erase(it);
}

void waitF() { S().cmdWait = true; }

void cmdListF() {
    for (const auto &c : S().commands) comPrintf(c.name);
    comPrintf(std::to_string(S().commands.size()) + " commands");
}

void cmdExecuteString(const std::string &text) {
    auto &s = S();
    tokenize(text, true);
    if (s.argv.empty()) return;
    if (Command *c = findCmd(s.argv[0])) {
        if (c->kind == CmdKind::Builtin) {
            if (c->proc) c->proc();
        } else if (s.hostBound && s.host.command) {
            // Copy so the host may re-enter the core from its handler.
            const std::vector<std::string> args = s.argv;
            std::vector<const char *> ptrs;
            ptrs.reserve(args.size() + 1);
            for (const auto &a : args) ptrs.push_back(a.c_str());
            ptrs.push_back(nullptr);
            s.host.command(s.host.user, int32_t(args.size()), ptrs.data());
        }
        return;
    }
    if (Alias *a = findAlias(s.argv[0])) {
        if (++s.aliasCount >= kAliasLoopCount) { comPrintf("ALIAS_LOOP_COUNT"); return; }
        cbufInsertText(a->value);
        return;
    }
    if (cvarCommand()) return;
    comPrintf("Unknown command \"" + s.argv[0] + "\"");
}

void cmdInit() {
    addCommand("cmdlist", cmdListF, CmdKind::Builtin);
    addCommand("exec", execF, CmdKind::Builtin);
    addCommand("echo", echoF, CmdKind::Builtin);
    addCommand("alias", aliasF, CmdKind::Builtin);
    addCommand("unalias", unaliasF, CmdKind::Builtin);
    addCommand("wait", waitF, CmdKind::Builtin);
    addCommand("set", setF, CmdKind::Builtin);
    addCommand("seta", setaF, CmdKind::Builtin);
    addCommand("toggle", toggleF, CmdKind::Builtin);
    addCommand("reset", resetF, CmdKind::Builtin);
    addCommand("cvarlist", cvarListF, CmdKind::Builtin);
    addCommand("writeconfig", writeConfigF, CmdKind::Builtin);
}

void resetState() {
    auto &s = S();
    s.cvars.clear();
    s.commands.clear();
    s.aliases.clear();
    s.packs.clear();
    s.cmdText.clear();
    s.cmdWait = false;
    s.aliasCount = 0;
    s.argv.clear();
    s.args.clear();
    realms_native::cmUnloadMap();
}

// ---------------------------------------------------------- ABI shims ---

const char *coreBackendName() { return "native-cpp"; }

int32_t coreInit(const RealmsHostApi *host) {
    auto &s = S();
    resetState();
    s.host = RealmsHostApi{};
    if (host) {
        const size_t n = host->struct_size > 0 && host->struct_size < sizeof(RealmsHostApi) ? host->struct_size
                                                                                          : sizeof(RealmsHostApi);
        std::memcpy(&s.host, host, n);
    }
    s.hostBound = host != nullptr;
    s.timeMs = 0;
    cmdInit();
    cvarGet("sv_cheats", "0", REALMS_CVAR_SERVERINFO | REALMS_CVAR_LATCH);
    cvarGet("developer", "0", 0);
    cvarGet("realms_core", "native-cpp", REALMS_CVAR_NOSET);
    s.initialized = true;
    comPrintf("Realms core: native C++ reference stack online");
    return 1;
}

void coreShutdown() {
    resetState();
    S().initialized = false;
    S().hostBound = false;
    S().host = RealmsHostApi{};
}

void coreFrame(int32_t msec) {
    if (!S().initialized) return;
    if (msec > 0) S().timeMs += msec;
    cbufExecute();
}

int32_t coreCvarRegister(const char *name, const char *def, int32_t flags) {
    return cvarGet(str(name), str(def), flags) ? 1 : 0;
}
int32_t coreCvarSet(const char *name, const char *value) { return cvarSet2(str(name), str(value), false); }
int32_t coreCvarForceSet(const char *name, const char *value) { return cvarSet2(str(name), str(value), true); }

int32_t coreCvarGet(const char *name, char *buffer, int32_t size) {
    if (buffer && size > 0) buffer[0] = '\0';
    const Cvar *v = cvarFind(str(name));
    if (!v) return -1;
    if (buffer && size > 0) {
        const size_t n = std::min(v->value.size(), size_t(size - 1));
        std::memcpy(buffer, v->value.data(), n);
        buffer[n] = '\0';
    }
    return int32_t(v->value.size());
}

float coreCvarValue(const char *name) { return cvarValue(str(name)); }

int32_t coreCvarFlags(const char *name) {
    const Cvar *v = cvarFind(str(name));
    return v ? v->flags : -1;
}

int32_t coreCvarModified(const char *name, int32_t clear) {
    Cvar *v = cvarFind(str(name));
    if (!v) return 0;
    const int32_t was = v->modified ? 1 : 0;
    if (clear) v->modified = false;
    return was;
}

void coreCvarForeach(RealmsCvarVisitor visitor, void *user) {
    if (!visitor) return;
    for (const auto &v : S().cvars) visitor(user, v->name.c_str(), v->value.c_str(), v->flags);
}

int32_t coreCmdRegister(const char *name) { return addCommand(str(name), nullptr, CmdKind::Host) ? 1 : 0; }
void coreCmdUnregister(const char *name) { removeCommand(str(name)); }
int32_t coreCmdExists(const char *name) { return findCmd(str(name)) ? 1 : 0; }
void coreCbufAddText(const char *text) { cbufAddText(str(text)); }
void coreCbufInsertText(const char *text) { cbufInsertText(str(text)); }
void coreCbufExecute() { cbufExecute(); }
void coreCmdExecuteString(const char *text) { cmdExecuteString(str(text)); }

int32_t coreFsAddPak(const char *path) { return fsAddPak(str(path)); }
int32_t coreFsLoad(const char *path, void **outData, int32_t *outSize) {
    void *data = nullptr;
    int32_t size = 0;
    const bool ok = fsLoadFile(str(path), &data, &size);
    if (outData) *outData = data;
    else std::free(data);
    if (outSize) *outSize = size;
    return ok ? 1 : 0;
}
void coreFsFree(void *data) { std::free(data); }
int32_t coreFsPakCount() { return int32_t(S().packs.size()); }

int32_t coreMapLoad(const char *path) {
    const std::string name = normalizePath(str(path));
    if (name.empty()) {
        realms_native::cmUnloadMap();
        return 1;
    }
    realms_native::cmUnloadMap();
    void *data = nullptr;
    int32_t size = 0;
    if (!fsLoadFile(name, &data, &size)) {
        comWarning("map_load: couldn't load " + name);
        return 0;
    }
    std::string error;
    const bool ok = realms_native::cmLoadMap(name, static_cast<const unsigned char *>(data), size_t(size), error);
    std::free(data);
    if (!ok) {
        comWarning("map_load: " + name + ": " + error);
        return 0;
    }
    return 1;
}
int32_t coreMapInfo(RealmsMapInfo *out) { return realms_native::cmMapInfo(out); }
int32_t coreMapEntities(char *buffer, int32_t size) { return realms_native::cmEntities(buffer, size); }
int32_t coreMapModel(int32_t index, float *mins, float *maxs, float *origin, int32_t *headnode) {
    return realms_native::cmModel(index, mins, maxs, origin, headnode);
}
int32_t coreMapHeadnodeForBox(const float *mins, const float *maxs) {
    return realms_native::cmHeadnodeForBox(mins, maxs);
}
int32_t coreMapPointContents(const float *p, int32_t headnode, const float *origin, const float *angles) {
    return realms_native::cmPointContents(p, headnode, origin, angles);
}
void coreMapBoxTrace(const float *start, const float *end, const float *mins, const float *maxs, int32_t headnode,
                     int32_t brushmask, const float *origin, const float *angles, RealmsTrace *out) {
    realms_native::cmBoxTrace(start, end, mins, maxs, headnode, brushmask, origin, angles, out);
}
int32_t coreMapPointLeaf(const float *p) { return realms_native::cmPointLeaf(p); }
int32_t coreMapLeafInfo(int32_t leaf, int32_t *contents, int32_t *cluster, int32_t *area) {
    return realms_native::cmLeafInfo(leaf, contents, cluster, area);
}
int32_t coreMapClusterPvs(int32_t cluster, uint8_t *buffer, int32_t size) {
    return realms_native::cmClusterPVS(cluster, buffer, size);
}
int32_t coreMapClusterPhs(int32_t cluster, uint8_t *buffer, int32_t size) {
    return realms_native::cmClusterPHS(cluster, buffer, size);
}
void coreMapSetAreaPortal(int32_t portal, int32_t open) { realms_native::cmSetAreaPortal(portal, open); }
int32_t coreMapAreaPortalOpen(int32_t portal) { return realms_native::cmAreaPortalOpen(portal); }
int32_t coreMapAreasConnected(int32_t a, int32_t b) { return realms_native::cmAreasConnected(a, b); }
int32_t coreMapWriteAreaBits(int32_t area, uint8_t *buffer, int32_t size) {
    return realms_native::cmWriteAreaBits(area, buffer, size);
}
int32_t coreMapBoxLeafs(const float *mins, const float *maxs, int32_t *list, int32_t size, int32_t headnode,
                        int32_t *topnode) {
    return realms_native::cmBoxLeafs(mins, maxs, list, size, headnode, topnode);
}
int32_t coreMapHeadnodeVisible(int32_t headnode, const uint8_t *visbits, int32_t size) {
    return realms_native::cmHeadnodeVisible(headnode, visbits, size);
}
int32_t coreMapLoadMesh(const RealmsMeshDesc *desc) {
    std::string error;
    if (realms_native::cmLoadMesh(desc, error)) return 1;
    comWarning("map_load_mesh: " + std::string(desc && desc->name ? desc->name : "?") + ": " + error);
    return 0;
}

const RealmsCoreApi kNativeApi = {
    sizeof(RealmsCoreApi),
    REALMS_BRIDGE_VERSION,
    coreBackendName,
    coreInit,
    coreShutdown,
    coreFrame,
    coreCvarRegister,
    coreCvarSet,
    coreCvarForceSet,
    coreCvarGet,
    coreCvarValue,
    coreCvarFlags,
    coreCvarModified,
    coreCvarForeach,
    coreCmdRegister,
    coreCmdUnregister,
    coreCmdExists,
    coreCbufAddText,
    coreCbufInsertText,
    coreCbufExecute,
    coreCmdExecuteString,
    coreFsAddPak,
    coreFsLoad,
    coreFsFree,
    coreFsPakCount,
    coreMapLoad,
    coreMapInfo,
    coreMapEntities,
    coreMapModel,
    coreMapHeadnodeForBox,
    coreMapPointContents,
    coreMapBoxTrace,
    coreMapPointLeaf,
    coreMapLeafInfo,
    coreMapClusterPvs,
    coreMapClusterPhs,
    coreMapSetAreaPortal,
    coreMapAreaPortalOpen,
    coreMapAreasConnected,
    coreMapWriteAreaBits,
    coreMapBoxLeafs,
    coreMapHeadnodeVisible,
    coreMapLoadMesh,
};

} // namespace

extern "C" const RealmsCoreApi *realms_native_get_api(uint32_t requested_version) {
    return requested_version == REALMS_BRIDGE_VERSION ? &kNativeApi : nullptr;
}
