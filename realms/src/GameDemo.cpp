// Realms Engine - Ekron demo files (see GameDemo.h).
#include "realms/GameDemo.h"

#include "realms/Engine.h"
#include "realms/GameSave.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <utility>

namespace fs = std::filesystem;

namespace realms {

namespace {

constexpr char kMagic[7] = {'E', 'K', 'R', 'D', 'E', 'M', 'O'};
constexpr uint8_t kVersion = 1;
constexpr uint32_t kMaxRecordBytes = 64u << 20;
constexpr size_t kMaxStringBytes = 1u << 20;
constexpr size_t kMaxStrings = 1u << 20;
constexpr size_t kMaxCount = 1u << 16;

// Words per item: everything as 32-bit values (floats by their bits), in a
// fixed order, so changes can be sent word by word.
constexpr size_t kPlayerWords = 68;
constexpr size_t kOriginWords = 3;
constexpr size_t kEntityWords = 25;
constexpr size_t kBeamWords = 9;
constexpr size_t kLoopWords = 5;
constexpr size_t kEventWords = 29;
constexpr size_t kInventoryWords = Q2_MAX_ITEMS;
constexpr uint32_t kMaxEventKind = uint32_t(GameEvent::Kind::EntityEvent);

enum : uint8_t { kRunning = 1, kHasPlayer = 2, kEntities = 4, kInventory = 8 };

uint32_t fbits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, sizeof u);
    return u;
}
float bitsf(uint32_t u) {
    float f;
    std::memcpy(&f, &u, sizeof f);
    return f;
}
uint32_t ibits(int32_t v) { return uint32_t(v); }
int32_t bitsi(uint32_t u) { return int32_t(u); }

struct Out {
    std::vector<uint8_t> &b;
    void u8(uint8_t v) { b.push_back(v); }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; i++) b.push_back(uint8_t(v >> (8 * i)));
    }
    void u64(uint64_t v) {
        u32(uint32_t(v));
        u32(uint32_t(v >> 32));
    }
    void var(uint64_t v) {
        while (v >= 0x80) {
            b.push_back(uint8_t(v | 0x80));
            v >>= 7;
        }
        b.push_back(uint8_t(v));
    }
    void str(const std::string &s) {
        var(s.size());
        b.insert(b.end(), s.begin(), s.end());
    }
};

struct In {
    const uint8_t *p, *end;
    bool ok = true;
    bool need(size_t n) {
        if (!ok || size_t(end - p) < n) ok = false;
        return ok;
    }
    uint8_t u8() { return need(1) ? *p++ : 0; }
    uint32_t u32() {
        if (!need(4)) return 0;
        const uint32_t v = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
        p += 4;
        return v;
    }
    uint64_t u64() {
        const uint64_t lo = u32();
        return lo | uint64_t(u32()) << 32;
    }
    uint64_t var() {
        uint64_t v = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (!need(1)) return 0;
            const uint8_t c = *p++;
            v |= uint64_t(c & 0x7f) << shift;
            if (!(c & 0x80)) return v;
        }
        ok = false;
        return 0;
    }
    size_t count(size_t max) {
        const uint64_t n = var();
        if (n > max) ok = false;
        return ok ? size_t(n) : 0;
    }
    std::string str() {
        const size_t n = count(kMaxStringBytes);
        if (!need(n)) return {};
        std::string s(reinterpret_cast<const char *>(p), n);
        p += n;
        return s;
    }
};

// `w` as changes from `ref` (missing words count as 0): for every 32 words
// a varint mask of the ones that differ, then those words.
void putDelta(Out &o, const std::vector<uint32_t> &w, const std::vector<uint32_t> &ref) {
    for (size_t base = 0; base < w.size(); base += 32) {
        const size_t n = std::min<size_t>(32, w.size() - base);
        uint32_t mask = 0;
        for (size_t i = 0; i < n; i++)
            if (w[base + i] != (base + i < ref.size() ? ref[base + i] : 0u)) mask |= 1u << i;
        o.var(mask);
        for (size_t i = 0; i < n; i++)
            if (mask & (1u << i)) o.u32(w[base + i]);
    }
}

void getDelta(In &in, std::vector<uint32_t> &w, const std::vector<uint32_t> &ref, size_t size) {
    w.assign(size, 0u);
    for (size_t i = 0; i < size && i < ref.size(); i++) w[i] = ref[i];
    for (size_t base = 0; base < size && in.ok; base += 32) {
        const size_t n = std::min<size_t>(32, size - base);
        const uint64_t mask = in.var();
        if (n < 64 && (mask >> n) != 0) in.ok = false;
        for (size_t i = 0; i < n && in.ok; i++)
            if (mask & (uint64_t(1) << i)) w[base + i] = in.u32();
    }
}

void put3(std::vector<uint32_t> &w, const float *v) {
    for (int i = 0; i < 3; i++) w.push_back(fbits(v[i]));
}
void get3(const std::vector<uint32_t> &w, size_t &k, float *v) {
    for (int i = 0; i < 3; i++) v[i] = bitsf(w[k++]);
}

std::vector<uint32_t> playerWords(const q2player_state_t &ps) {
    std::vector<uint32_t> w;
    w.reserve(kPlayerWords);
    const q2pmove_state_t &pm = ps.pmove;
    w.push_back(ibits(int32_t(pm.pm_type)));
    for (int i = 0; i < 3; i++) w.push_back(ibits(pm.origin[i]));
    for (int i = 0; i < 3; i++) w.push_back(ibits(pm.velocity[i]));
    w.push_back(pm.pm_flags);
    w.push_back(pm.pm_time);
    w.push_back(ibits(pm.gravity));
    for (int i = 0; i < 3; i++) w.push_back(ibits(pm.delta_angles[i]));
    put3(w, ps.viewangles);
    put3(w, ps.viewoffset);
    put3(w, ps.kick_angles);
    put3(w, ps.gunangles);
    put3(w, ps.gunoffset);
    w.push_back(ibits(ps.gunindex));
    w.push_back(ibits(ps.gunframe));
    for (int i = 0; i < 4; i++) w.push_back(fbits(ps.blend[i]));
    w.push_back(fbits(ps.fov));
    w.push_back(ibits(ps.rdflags));
    for (int i = 0; i < Q2_MAX_STATS; i++) w.push_back(ibits(ps.stats[i]));
    return w;
}

void playerFrom(const std::vector<uint32_t> &w, q2player_state_t &ps) {
    ps = q2player_state_t{};
    size_t k = 0;
    q2pmove_state_t &pm = ps.pmove;
    pm.pm_type = q2pmtype_t(bitsi(w[k++]));
    for (int i = 0; i < 3; i++) pm.origin[i] = int16_t(bitsi(w[k++]));
    for (int i = 0; i < 3; i++) pm.velocity[i] = int16_t(bitsi(w[k++]));
    pm.pm_flags = uint8_t(w[k++]);
    pm.pm_time = uint8_t(w[k++]);
    pm.gravity = int16_t(bitsi(w[k++]));
    for (int i = 0; i < 3; i++) pm.delta_angles[i] = int16_t(bitsi(w[k++]));
    get3(w, k, ps.viewangles);
    get3(w, k, ps.viewoffset);
    get3(w, k, ps.kick_angles);
    get3(w, k, ps.gunangles);
    get3(w, k, ps.gunoffset);
    ps.gunindex = bitsi(w[k++]);
    ps.gunframe = bitsi(w[k++]);
    for (int i = 0; i < 4; i++) ps.blend[i] = bitsf(w[k++]);
    ps.fov = bitsf(w[k++]);
    ps.rdflags = bitsi(w[k++]);
    for (int i = 0; i < Q2_MAX_STATS; i++) ps.stats[i] = int16_t(bitsi(w[k++]));
}

// Strings by number: the encoder sends each new one once, before the record.
struct Encoder {
    GameDemoState &state;
    std::vector<std::string> fresh;
    uint32_t id(const std::string &s) {
        const auto it = state.ids.find(s);
        if (it != state.ids.end()) return it->second;
        const uint32_t n = uint32_t(state.ids.size());
        state.ids.emplace(s, n);
        fresh.push_back(s);
        return n;
    }
};

struct Decoder {
    GameDemoState &state;
    In &in;
    std::string str(uint32_t id) {
        if (id >= state.strings.size()) {
            in.ok = false;
            return {};
        }
        return state.strings[id];
    }
};

std::vector<uint32_t> entityWords(const GameRenderEntity &e, Encoder &enc) {
    std::vector<uint32_t> w;
    w.reserve(kEntityWords);
    w.push_back(ibits(e.modelindex));
    w.push_back(enc.id(e.model));
    w.push_back(ibits(e.inlineModel));
    put3(w, e.origin.data());
    put3(w, e.oldOrigin.data());
    put3(w, e.angles.data());
    put3(w, e.mins.data());
    put3(w, e.maxs.data());
    w.push_back(ibits(e.frame));
    w.push_back(ibits(e.skinnum));
    w.push_back(e.effects);
    w.push_back(ibits(e.renderfx));
    w.push_back(ibits(e.svflags));
    w.push_back(ibits(e.solid));
    w.push_back(enc.id(e.sound));
    return w;
}

GameRenderEntity entityFrom(int number, const std::vector<uint32_t> &w, Decoder &dec) {
    GameRenderEntity e;
    size_t k = 0;
    e.number = number;
    e.modelindex = bitsi(w[k++]);
    e.model = dec.str(w[k++]);
    e.inlineModel = bitsi(w[k++]);
    get3(w, k, e.origin.data());
    get3(w, k, e.oldOrigin.data());
    get3(w, k, e.angles.data());
    get3(w, k, e.mins.data());
    get3(w, k, e.maxs.data());
    e.frame = bitsi(w[k++]);
    e.skinnum = bitsi(w[k++]);
    e.effects = w[k++];
    e.renderfx = bitsi(w[k++]);
    e.svflags = bitsi(w[k++]);
    e.solid = bitsi(w[k++]);
    e.sound = dec.str(w[k++]);
    return e;
}

std::vector<uint32_t> beamWords(const GameBeamEntity &b) {
    std::vector<uint32_t> w{ibits(b.number)};
    put3(w, b.origin.data());
    put3(w, b.oldOrigin.data());
    w.push_back(ibits(b.frame));
    w.push_back(ibits(b.skinnum));
    return w;
}

GameBeamEntity beamFrom(const std::vector<uint32_t> &w) {
    GameBeamEntity b;
    size_t k = 0;
    b.number = bitsi(w[k++]);
    get3(w, k, b.origin.data());
    get3(w, k, b.oldOrigin.data());
    b.frame = bitsi(w[k++]);
    b.skinnum = bitsi(w[k++]);
    return b;
}

std::vector<uint32_t> loopWords(const GameLoopSound &l, Encoder &enc) {
    std::vector<uint32_t> w{ibits(l.entity), enc.id(l.name)};
    put3(w, l.origin.data());
    return w;
}

GameLoopSound loopFrom(const std::vector<uint32_t> &w, Decoder &dec) {
    GameLoopSound l;
    size_t k = 0;
    l.entity = bitsi(w[k++]);
    l.name = dec.str(w[k++]);
    get3(w, k, l.origin.data());
    return l;
}

std::vector<uint32_t> eventWords(const GameEvent &ev, Encoder &enc) {
    std::vector<uint32_t> w;
    w.reserve(kEventWords);
    w.push_back(uint32_t(ev.kind));
    w.push_back(ibits(ev.level));
    w.push_back(ibits(ev.entity));
    put3(w, ev.origin.data());
    w.push_back(fbits(ev.volume));
    w.push_back(fbits(ev.attenuation));
    w.push_back(fbits(ev.delay));
    w.push_back(enc.id(ev.text));
    w.push_back(ibits(ev.entityEvent));
    put3(w, ev.angles.data());
    put3(w, ev.end.data());
    put3(w, ev.dir.data());
    put3(w, ev.offset.data());
    w.push_back(ibits(ev.count));
    w.push_back(ibits(ev.color));
    w.push_back(ibits(ev.entity2));
    w.push_back(ibits(ev.magnitude));
    w.push_back(ibits(ev.id));
    w.push_back(ibits(ev.durationMs));
    return w;
}

GameEvent eventFrom(const std::vector<uint32_t> &w, Decoder &dec) {
    GameEvent ev;
    size_t k = 0;
    const uint32_t kind = w[k++];
    if (kind > kMaxEventKind) dec.in.ok = false;
    ev.kind = GameEvent::Kind(std::min(kind, kMaxEventKind));
    ev.level = bitsi(w[k++]);
    ev.entity = bitsi(w[k++]);
    get3(w, k, ev.origin.data());
    ev.volume = bitsf(w[k++]);
    ev.attenuation = bitsf(w[k++]);
    ev.delay = bitsf(w[k++]);
    ev.text = dec.str(w[k++]);
    ev.entityEvent = bitsi(w[k++]);
    get3(w, k, ev.angles.data());
    get3(w, k, ev.end.data());
    get3(w, k, ev.dir.data());
    get3(w, k, ev.offset.data());
    ev.count = bitsi(w[k++]);
    ev.color = bitsi(w[k++]);
    ev.entity2 = bitsi(w[k++]);
    ev.magnitude = bitsi(w[k++]);
    ev.id = bitsi(w[k++]);
    ev.durationMs = bitsi(w[k++]);
    return ev;
}

// The record after its new strings.
void encodeBody(Encoder &enc, const GameSnapshot &s, float seconds, Out &o) {
    GameDemoState &st = enc.state;
    o.u32(fbits(seconds));
    uint8_t flags = 0;
    if (s.running) flags |= kRunning;
    if (s.running && s.hasPlayer) flags |= kHasPlayer;
    if (s.running && s.entitiesChanged) flags |= kEntities;
    if (s.running && s.inventoryChanged) flags |= kInventory;
    o.u8(flags);
    if (!s.running) return;
    o.var(uint32_t(s.frameNumber));
    uint64_t lerp;
    std::memcpy(&lerp, &s.lerpFraction, sizeof lerp);
    o.u64(lerp);
    if (s.hasPlayer) {
        std::vector<uint32_t> w = playerWords(s.player);
        putDelta(o, w, st.player);
        st.player = std::move(w);
    }
    std::vector<uint32_t> origin;
    put3(origin, s.playerOrigin.data());
    putDelta(o, origin, st.origin);
    st.origin = std::move(origin);
    if (s.entitiesChanged) {
        o.var(s.entities.size());
        for (const GameRenderEntity &e : s.entities) {
            o.var(uint32_t(e.number));
            std::vector<uint32_t> w = entityWords(e, enc);
            std::vector<uint32_t> &ref = st.entities[e.number];
            putDelta(o, w, ref);
            ref = std::move(w);
        }
        o.var(s.beams.size());
        for (const GameBeamEntity &b : s.beams) putDelta(o, beamWords(b), {});
        o.var(s.loopSounds.size());
        for (const GameLoopSound &l : s.loopSounds) putDelta(o, loopWords(l, enc), {});
    }
    o.var(s.configStrings.size());
    for (const auto &cs : s.configStrings) {
        o.var(uint32_t(cs.first));
        o.var(enc.id(cs.second));
    }
    if (s.inventoryChanged) {
        std::vector<uint32_t> w;
        w.reserve(kInventoryWords);
        for (int16_t v : s.inventory) w.push_back(ibits(v));
        putDelta(o, w, st.inventory);
        st.inventory = std::move(w);
    }
    o.var(s.events.size());
    for (const GameEvent &ev : s.events) putDelta(o, eventWords(ev, enc), {});
}

bool decodeRecord(GameDemoState &st, In &in, GameSnapshot &s, float &seconds) {
    s = GameSnapshot{};
    const size_t fresh = in.count(kMaxCount);
    for (size_t i = 0; i < fresh && in.ok; i++) {
        if (st.strings.size() >= kMaxStrings) return false;
        st.strings.push_back(in.str());
    }
    Decoder dec{st, in};
    seconds = bitsf(in.u32());
    const uint8_t flags = in.u8();
    if (!in.ok || (flags & ~uint8_t(kRunning | kHasPlayer | kEntities | kInventory))) return false;
    s.running = (flags & kRunning) != 0;
    if (!s.running) return in.ok && flags == 0;
    s.hasPlayer = (flags & kHasPlayer) != 0;
    s.entitiesChanged = (flags & kEntities) != 0;
    s.inventoryChanged = (flags & kInventory) != 0;
    s.frameNumber = int(uint32_t(in.var()));
    const uint64_t lerp = in.u64();
    std::memcpy(&s.lerpFraction, &lerp, sizeof lerp);
    std::vector<uint32_t> w;
    if (s.hasPlayer) {
        getDelta(in, w, st.player, kPlayerWords);
        if (!in.ok) return false;
        playerFrom(w, s.player);
        st.player = w;
    }
    getDelta(in, w, st.origin, kOriginWords);
    if (!in.ok) return false;
    for (size_t i = 0; i < 3; i++) s.playerOrigin[i] = bitsf(w[i]);
    st.origin = w;
    if (s.entitiesChanged) {
        const size_t n = in.count(kMaxCount);
        s.entities.reserve(n);
        for (size_t i = 0; i < n && in.ok; i++) {
            const uint64_t number = in.var();
            if (number > uint64_t(INT32_MAX)) return false;
            std::vector<uint32_t> &ref = st.entities[int(number)];
            getDelta(in, w, ref, kEntityWords);
            if (!in.ok) return false;
            ref = w;
            s.entities.push_back(entityFrom(int(number), w, dec));
        }
        const size_t beams = in.count(kMaxCount);
        for (size_t i = 0; i < beams && in.ok; i++) {
            getDelta(in, w, {}, kBeamWords);
            if (in.ok) s.beams.push_back(beamFrom(w));
        }
        const size_t loops = in.count(kMaxCount);
        for (size_t i = 0; i < loops && in.ok; i++) {
            getDelta(in, w, {}, kLoopWords);
            if (in.ok) s.loopSounds.push_back(loopFrom(w, dec));
        }
    }
    const size_t cs = in.count(kMaxCount);
    for (size_t i = 0; i < cs && in.ok; i++) {
        const uint64_t index = in.var();
        const uint64_t id = in.var();
        if (index >= uint64_t(Q2_MAX_CONFIGSTRINGS) || id > UINT32_MAX) return false;
        s.configStrings.emplace_back(int(index), dec.str(uint32_t(id)));
    }
    if (s.inventoryChanged && in.ok) {
        getDelta(in, w, st.inventory, kInventoryWords);
        if (!in.ok) return false;
        for (size_t i = 0; i < kInventoryWords; i++) s.inventory[i] = int16_t(bitsi(w[i]));
        st.inventory = w;
    }
    const size_t events = in.count(kMaxCount);
    for (size_t i = 0; i < events && in.ok; i++) {
        getDelta(in, w, {}, kEventWords);
        if (in.ok) s.events.push_back(eventFrom(w, dec));
    }
    return in.ok;
}

bool parseHeader(const std::vector<uint8_t> &bytes, GameDemoHeader &header, std::string &error, size_t &pos) {
    header = GameDemoHeader{};
    In in{bytes.data(), bytes.data() + bytes.size()};
    if (!in.need(sizeof kMagic) || std::memcmp(in.p, kMagic, sizeof kMagic) != 0) {
        error = "not an Ekron demo";
        return false;
    }
    in.p += sizeof kMagic;
    const uint8_t version = in.u8();
    if (in.ok && version != kVersion) {
        error = "demo version " + std::to_string(version) + " (this build plays version " +
                std::to_string(kVersion) + ")";
        return false;
    }
    header.mapPath = in.str();
    header.mapChecksum = in.u32();
    if (!in.ok || header.mapPath.empty()) {
        error = "damaged demo header";
        return false;
    }
    pos = size_t(in.p - bytes.data());
    return true;
}

} // namespace

bool validGameDemoName(const std::string &name) { return validGameSaveName(name); }

std::string gameDemoFile(const std::string &name) {
    std::string n = name;
    const size_t ext = std::strlen(kGameDemoExtension);
    if (n.size() > ext && n.compare(n.size() - ext, ext, kGameDemoExtension) == 0) n.resize(n.size() - ext);
    if (!validGameDemoName(n)) return std::string();
    return std::string(kGameDemoDir) + "/" + n + kGameDemoExtension;
}

std::string gameDemoPath(const Engine &engine, const std::string &name) {
    const std::string file = gameDemoFile(name);
    if (file.empty() || engine.writableRoot().empty()) return std::string();
    // the folder and the file name as parts: native separators on Windows
    return (fs::path(engine.writableRoot()) / kGameDemoDir / file.substr(std::strlen(kGameDemoDir) + 1)).string();
}

void encodeGameDemoHeader(const GameDemoHeader &header, std::vector<uint8_t> &out) {
    Out o{out};
    out.insert(out.end(), kMagic, kMagic + sizeof kMagic);
    o.u8(kVersion);
    o.str(header.mapPath);
    o.u32(header.mapChecksum);
}

void encodeGameDemoRecord(GameDemoState &state, const GameSnapshot &snapshot, float seconds,
                          std::vector<uint8_t> &out) {
    Encoder enc{state, {}};
    std::vector<uint8_t> body;
    Out ob{body};
    encodeBody(enc, snapshot, seconds, ob);
    std::vector<uint8_t> strings;
    Out os{strings};
    os.var(enc.fresh.size());
    for (const std::string &s : enc.fresh) os.str(s);
    Out o{out};
    o.u32(uint32_t(strings.size() + body.size()));
    out.insert(out.end(), strings.begin(), strings.end());
    out.insert(out.end(), body.begin(), body.end());
}

bool decodeGameDemoRecord(GameDemoState &state, const uint8_t *data, size_t size, GameSnapshot &snapshot,
                          float &seconds) {
    if (!data && size) return false;
    In in{data, data + size};
    return decodeRecord(state, in, snapshot, seconds) && in.p == in.end;
}

bool GameDemoWriter::open(const std::string &path, const GameDemoHeader &header, std::string &error) {
    close();
    std::error_code ec;
    const fs::path p(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    file_.open(p, std::ios::binary | std::ios::trunc);
    if (!file_) {
        error = "can't write " + path;
        file_ = std::ofstream();
        return false;
    }
    std::vector<uint8_t> head;
    encodeGameDemoHeader(header, head);
    file_.write(reinterpret_cast<const char *>(head.data()), std::streamsize(head.size()));
    file_.flush();
    if (!file_) {
        error = "can't write " + path;
        close();
        return false;
    }
    path_ = path;
    state_ = GameDemoState{};
    bytes_ = head.size();
    records_ = 0;
    return true;
}

bool GameDemoWriter::write(const GameSnapshot &snapshot, float seconds) {
    if (!file_.is_open()) return false;
    buffer_.clear();
    encodeGameDemoRecord(state_, snapshot, seconds, buffer_);
    file_.write(reinterpret_cast<const char *>(buffer_.data()), std::streamsize(buffer_.size()));
    file_.flush();
    if (!file_) {
        close();
        return false;
    }
    bytes_ += buffer_.size();
    records_++;
    return true;
}

void GameDemoWriter::close() {
    if (file_.is_open()) file_.close();
    file_.clear();
    state_ = GameDemoState{};
}

bool GameDemoReader::readHeader(const std::vector<uint8_t> &bytes, GameDemoHeader &header, std::string &error) {
    size_t pos = 0;
    return parseHeader(bytes, header, error, pos);
}

bool GameDemoReader::open(std::vector<uint8_t> bytes, std::string &error) {
    *this = GameDemoReader{};
    size_t pos = 0;
    if (!parseHeader(bytes, header_, error, pos)) return false;
    bytes_ = std::move(bytes);
    pos_ = pos;
    return true;
}

bool GameDemoReader::next(GameSnapshot &snapshot, float &seconds) {
    if (!error_.empty() || bytes_.size() - pos_ < 4) return false; // the end, or a cut-short length
    In len{bytes_.data() + pos_, bytes_.data() + bytes_.size()};
    const uint32_t n = len.u32();
    if (n > kMaxRecordBytes) {
        error_ = "damaged record " + std::to_string(records_ + 1);
        return false;
    }
    if (bytes_.size() - pos_ - 4 < n) return false; // cut short while recording
    In in{bytes_.data() + pos_ + 4, bytes_.data() + pos_ + 4 + n};
    if (!decodeRecord(state_, in, snapshot, seconds) || in.p != in.end || !std::isfinite(seconds) || seconds < 0.0f) {
        error_ = "damaged record " + std::to_string(records_ + 1);
        snapshot = GameSnapshot{};
        return false;
    }
    pos_ += 4 + size_t(n);
    records_++;
    time_ += double(seconds);
    return true;
}

} // namespace realms
