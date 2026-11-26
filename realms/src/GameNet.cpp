// Realms Engine - game snapshots over a NetChannel (see GameNet.h).
#include "realms/GameNet.h"

#include <cmath>
#include <cstring>
#include <utility>

namespace realms {

namespace {

void putVar(std::vector<uint8_t> &b, uint64_t v) {
    while (v >= 0x80) {
        b.push_back(uint8_t(v | 0x80));
        v >>= 7;
    }
    b.push_back(uint8_t(v));
}

bool getVar(const uint8_t *&p, const uint8_t *end, uint64_t &v) {
    v = 0;
    for (int shift = 0; shift < 64 && p < end; shift += 7) {
        const uint8_t c = *p++;
        v |= uint64_t(c & 0x7f) << shift;
        if (!(c & 0x80)) return true;
    }
    return false;
}

// A demo record as encodeGameDemoRecord appends it: the length, then the record.
bool readRecord(const uint8_t *p, const uint8_t *end, GameDemoState &state, GameSnapshot &s) {
    if (end - p < 4) return false;
    const uint32_t n = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    p += 4;
    if (uint64_t(end - p) != n) return false;
    float seconds = 0.0f;
    return decodeGameDemoRecord(state, p, n, s, seconds);
}

void putF32(std::vector<uint8_t> &b, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    for (int i = 0; i < 4; i++) b.push_back(uint8_t(u >> (8 * i)));
}

bool getF32(const uint8_t *&p, const uint8_t *end, float &f) {
    if (end - p < 4) return false;
    const uint32_t u = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    std::memcpy(&f, &u, 4);
    p += 4;
    if (!std::isfinite(f)) f = 0.0f;
    return true;
}

} // namespace

std::vector<uint8_t> encodeGameNetUserCmds(const std::vector<GameNetUserCmd> &cmds) {
    std::vector<uint8_t> b;
    putVar(b, cmds.size());
    if (cmds.empty()) return b;
    putVar(b, cmds.front().number);
    for (const GameNetUserCmd &c : cmds) {
        putF32(b, c.seconds);
        putF32(b, c.cmd.forward);
        putF32(b, c.cmd.side);
        putF32(b, c.cmd.up);
        putF32(b, c.cmd.pitch);
        putF32(b, c.cmd.yaw);
        b.push_back(uint8_t((c.cmd.attack ? 1 : 0) | (c.cmd.use ? 2 : 0)));
        putVar(b, uint64_t(uint32_t(c.cmd.impulse)));
    }
    return b;
}

bool decodeGameNetUserCmds(const uint8_t *data, size_t size, std::vector<GameNetUserCmd> &out) {
    out.clear();
    const uint8_t *p = data, *end = data + size;
    uint64_t count = 0, first = 0;
    if (!getVar(p, end, count) || count > 64) return false;
    if (count == 0) return p == end;
    if (!getVar(p, end, first) || first == 0 || first + count - 1 > UINT32_MAX) return false;
    for (uint64_t i = 0; i < count; i++) {
        GameNetUserCmd c;
        c.number = uint32_t(first + i);
        uint64_t impulse = 0;
        if (!getF32(p, end, c.seconds) || !getF32(p, end, c.cmd.forward) || !getF32(p, end, c.cmd.side) ||
            !getF32(p, end, c.cmd.up) || !getF32(p, end, c.cmd.pitch) || !getF32(p, end, c.cmd.yaw) || p >= end)
            return false;
        const uint8_t buttons = *p++;
        if (!getVar(p, end, impulse) || impulse > 255) return false;
        c.cmd.attack = (buttons & 1) != 0;
        c.cmd.use = (buttons & 2) != 0;
        c.cmd.impulse = int(impulse);
        out.push_back(c);
    }
    return p == end;
}

std::string gameNetInfoValue(const std::string &info, const std::string &key) {
    // Info_ValueForKey: \key\value\key\value
    size_t at = 0;
    while (at < info.size()) {
        if (info[at] == '\\') at++;
        const size_t keyEnd = info.find('\\', at);
        if (keyEnd == std::string::npos) return {};
        const size_t valueEnd = info.find('\\', keyEnd + 1);
        const std::string k = info.substr(at, keyEnd - at);
        const std::string v =
            info.substr(keyEnd + 1, valueEnd == std::string::npos ? std::string::npos : valueEnd - keyEnd - 1);
        if (k == key) return v;
        if (valueEnd == std::string::npos) return {};
        at = valueEnd;
    }
    return {};
}

bool gameNetInfoHas(const std::string &info, const std::string &key) {
    // Info_ValueForKey: \key\value\key\value
    size_t at = 0;
    while (at < info.size()) {
        if (info[at] == '\\') at++;
        const size_t keyEnd = info.find('\\', at);
        if (keyEnd == std::string::npos) return false;
        const size_t valueEnd = info.find('\\', keyEnd + 1);
        const std::string k = info.substr(at, keyEnd - at);
        if (k == key) return true;
        if (valueEnd == std::string::npos) return false;
        at = valueEnd;
    }
    return false;
}

void GameNetEncoder::encode(const GameSnapshot &s, uint32_t sequence, uint32_t acked, std::vector<uint8_t> &unreliable,
                            std::vector<uint8_t> &reliable) {
    if (s.running) {
        if (s.entitiesChanged) {
            listFrame_ = s.frameNumber;
            entities_ = s.entities;
            beams_ = s.beams;
            loopSounds_ = s.loopSounds;
        }
        if (s.inventoryChanged) inventory_ = s.inventory;
    } else {
        listFrame_ = INT_MIN;
        entities_.clear();
        beams_.clear();
        loopSounds_.clear();
        inventory_.fill(0);
    }

    Base next;
    next.sequence = sequence;
    int baseFrame = INT_MIN;
    lastBase_ = 0;
    if (acked != 0)
        for (const Base &b : history_)
            if (b.sequence == acked) {
                next.state = b.state;
                baseFrame = b.entityFrame;
                lastBase_ = acked;
            }

    GameSnapshot u;
    u.running = s.running;
    if (s.running) {
        u.frameNumber = s.frameNumber;
        u.lerpFraction = s.lerpFraction;
        u.hasPlayer = s.hasPlayer;
        u.player = s.player;
        u.playerOrigin = s.playerOrigin;
        // the lists again until the client has a base with them
        u.entitiesChanged = listFrame_ != INT_MIN && listFrame_ != baseFrame;
        if (u.entitiesChanged) {
            u.entities = entities_;
            u.beams = beams_;
            u.loopSounds = loopSounds_;
        }
        u.inventoryChanged = true;
        u.inventory = inventory_;
    }
    next.entityFrame = u.entitiesChanged ? listFrame_ : (s.running ? baseFrame : INT_MIN);
    unreliable.clear();
    putVar(unreliable, lastBase_);
    encodeGameDemoRecord(next.state, u, 0.0f, unreliable);
    history_.push_back(std::move(next));
    while (history_.size() > kHistory) history_.pop_front();

    reliable.clear();
    if (!s.configStrings.empty() || !s.events.empty()) {
        GameSnapshot r;
        r.running = true;
        r.configStrings = s.configStrings;
        r.events = s.events;
        GameDemoState fresh; // the strings travel with the message
        reliable.push_back(kGameNetEvents);
        encodeGameDemoRecord(fresh, r, 0.0f, reliable);
    }
}

bool GameNetDecoder::reliable(const uint8_t *data, size_t size) {
    if (!data || size < 1 || data[0] != kGameNetEvents) return false;
    GameDemoState fresh;
    GameSnapshot r;
    if (!readRecord(data + 1, data + size, fresh, r) || !r.running) return false;
    for (auto &cs : r.configStrings) configStrings_.push_back(std::move(cs));
    for (auto &ev : r.events) events_.push_back(std::move(ev));
    return true;
}

bool GameNetDecoder::decode(const uint8_t *data, size_t size, uint32_t sequence, GameSnapshot &out) {
    if (!data) return false;
    const uint8_t *p = data, *end = data + size;
    uint64_t base = 0;
    if (!getVar(p, end, base) || base > UINT32_MAX || (base != 0 && base >= sequence)) return false;
    GameDemoState state;
    if (base != 0) {
        bool found = false;
        for (const auto &h : history_)
            if (h.first == uint32_t(base)) {
                state = h.second;
                found = true;
            }
        if (!found) return false;
    }
    GameSnapshot s;
    if (!readRecord(p, end, state, s)) return false;
    history_.emplace_back(sequence, std::move(state));
    while (history_.size() > kHistory) history_.pop_front();

    out = std::move(s);
    out.configStrings = std::move(configStrings_);
    out.events = std::move(events_);
    configStrings_.clear();
    events_.clear();
    if (!out.running) {
        entityFrame_ = INT_MIN;
        haveInventory_ = false;
        return true;
    }
    // lists sent again for a client that had them already (GameServer
    // sends new lists with every new frame)
    if (out.entitiesChanged) {
        if (out.frameNumber == entityFrame_) {
            out.entitiesChanged = false;
            out.entities.clear();
            out.beams.clear();
            out.loopSounds.clear();
        } else {
            entityFrame_ = out.frameNumber;
        }
    }
    if (haveInventory_ && out.inventory == inventory_) {
        out.inventoryChanged = false;
    } else {
        inventory_ = out.inventory;
        haveInventory_ = true;
        out.inventoryChanged = true;
    }
    return true;
}

} // namespace realms
