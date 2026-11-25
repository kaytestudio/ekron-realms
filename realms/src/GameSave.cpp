#include "realms/GameSave.h"

#include "realms/Engine.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace realms {

namespace {

constexpr const char *kMagic = "EKRON-SAVE 1";
constexpr size_t kMaxConfigStrings = 4096;
constexpr size_t kMaxStringBytes = 1 << 16;
constexpr size_t kMaxPortals = 1 << 16;

std::string oneLine(std::string s) {
    for (char &c : s)
        if (c == '\n' || c == '\r') c = ' ';
    return s;
}

} // namespace

bool validGameSaveName(const std::string &name) {
    if (name.empty() || name.size() > 32) return false;
    for (char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    return true;
}

std::string gameSaveDir(const Engine &engine, const std::string &name) {
    if (!validGameSaveName(name) || engine.writableRoot().empty()) return std::string();
    return (fs::path(engine.writableRoot()) / "save" / name).string();
}

bool gameSaveComplete(const std::string &dir) {
    std::error_code ec;
    for (const char *f : {kGameSaveServerFile, kGameSaveGameFile, kGameSaveLevelFile})
        if (!fs::is_regular_file(fs::path(dir) / f, ec)) return false;
    return true;
}

bool writeGameSaveInfo(const std::string &dir, const GameSaveInfo &info, std::string &error) {
    std::ostringstream out;
    out << kMagic << '\n';
    out << "comment " << oneLine(info.comment) << '\n';
    out << "map " << oneLine(info.mapPath) << '\n';
    out << "skill " << info.skill << '\n';
    out << "portals ";
    for (uint8_t p : info.portals) out << (p ? '1' : '0');
    out << '\n';
    for (size_t i = 0; i < info.configStrings.size(); i++) {
        const std::string &cs = info.configStrings[i];
        if (cs.empty()) continue;
        out << "cs " << i << ' ' << cs.size() << '\n';
        out.write(cs.data(), std::streamsize(cs.size()));
        out << '\n';
    }
    out << "end\n";
    // written whole, then renamed over the old file
    const fs::path target = fs::path(dir) / kGameSaveServerFile, temp = fs::path(dir) / "server.ssv.tmp";
    {
        std::ofstream f(temp, std::ios::binary | std::ios::trunc);
        const std::string bytes = out.str();
        if (!f || !f.write(bytes.data(), std::streamsize(bytes.size()))) {
            error = "can't write " + temp.string();
            return false;
        }
    }
    std::error_code ec;
    fs::rename(temp, target, ec);
    if (ec) {
        error = "can't write " + target.string() + ": " + ec.message();
        return false;
    }
    return true;
}

bool readGameSaveInfo(const std::string &dir, GameSaveInfo &info, std::string &error) {
    info = GameSaveInfo{};
    std::ifstream f(fs::path(dir) / kGameSaveServerFile, std::ios::binary);
    if (!f) {
        error = "no saved game in " + dir;
        return false;
    }
    std::string line;
    if (!std::getline(f, line) || line != kMagic) {
        error = "not an Ekron save game";
        return false;
    }
    bool ended = false;
    while (std::getline(f, line)) {
        if (line == "end") {
            ended = true;
            break;
        }
        const size_t sp = line.find(' ');
        const std::string key = line.substr(0, sp), value = sp == std::string::npos ? std::string() : line.substr(sp + 1);
        if (key == "comment") {
            info.comment = value;
        } else if (key == "map") {
            info.mapPath = value;
        } else if (key == "skill") {
            info.skill = std::atoi(value.c_str());
        } else if (key == "portals") {
            if (value.size() > kMaxPortals) break;
            for (char c : value) info.portals.push_back(c == '1' ? 1 : 0);
        } else if (key == "cs") {
            std::istringstream in(value);
            long long index = -1, length = -1;
            if (!(in >> index >> length) || index < 0 || size_t(index) >= kMaxConfigStrings || length < 0 ||
                size_t(length) > kMaxStringBytes)
                break;
            std::string bytes(size_t(length), '\0');
            if (!f.read(&bytes[0], std::streamsize(length)) || f.get() != '\n') break;
            if (info.configStrings.size() <= size_t(index)) info.configStrings.resize(size_t(index) + 1);
            info.configStrings[size_t(index)] = std::move(bytes);
        } // unknown keys: newer versions' additions, skipped
    }
    if (!ended || info.mapPath.empty()) {
        error = "damaged save game (" + std::string(kGameSaveServerFile) + ")";
        info = GameSaveInfo{};
        return false;
    }
    return true;
}

} // namespace realms
