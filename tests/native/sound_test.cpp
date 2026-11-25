// Game sound: every shipped .ogg decodes and is credited, the mixer
// resamples / loops / streams / fades correctly, and the director turns game
// snapshots into the right sounds.
//
// Build (one line): g++ -std=c++17 -O2 -Irealms/include -Isys/android/app/src/main/cpp
//   tests/native/sound_test.cpp sys/android/app/src/main/cpp/Sound{Decode,Mixer,Director,System}.cpp
//   sys/android/app/src/main/cpp/GameSoundPlayer.cpp realms/src/GameSound.cpp
//   realms/src/{Engine,NativeCore,NativeCModel,SandboxBoot,GameSave,MeshMap,MapGeometry,MapLighting,MapWalker}.cpp
//   -ldl -lpthread -o /tmp/sound_test
// Run from the repo root: /tmp/sound_test [assets dir]

#include "GameSoundPlayer.h"
#include "SoundCatalog.h"
#include "SoundDecode.h"
#include "SoundDirector.h"
#include "SoundMixer.h"
#include "SoundSystem.h"
#include "realms/Engine.h"
#include "realms/SandboxBoot.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace ekron::audio;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(cond)) {                                                                                                 \
            ++failures;                                                                                                \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                \
        }                                                                                                              \
    } while (0)

static bool readFile(const fs::path &p, std::vector<uint8_t> &out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

static Pcm tone(int rate, int channels, int frames, float amplitude = 0.5f) {
    Pcm p;
    p.rate = rate;
    p.channels = channels;
    p.samples.resize(size_t(frames) * size_t(channels));
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c)
            p.samples[size_t(i * channels + c)] = int16_t(amplitude * 32767.0f * std::sin(6.2831853f * 440.0f * float(i) / float(rate)));
    return p;
}

// Renders until the mixer is silent; returns frames with sound.
static int renderUntilSilent(Mixer &m, int maxFrames, float *peak = nullptr) {
    std::vector<float> buf(512 * 2);
    int frames = 0, lastLoud = 0;
    float top = 0;
    while (frames < maxFrames) {
        m.render(buf.data(), 512);
        for (int i = 0; i < 512; ++i) {
            const float a = std::max(std::fabs(buf[size_t(2 * i)]), std::fabs(buf[size_t(2 * i + 1)]));
            top = std::max(top, a);
            if (a > 1e-4f) lastLoud = frames + i + 1;
        }
        frames += 512;
        if (m.activeVoices() == 0) break;
    }
    if (peak) *peak = top;
    return lastLoud;
}

static void assetTests(const fs::path &assets) {
    const fs::path dir = assets / "sounds";
    CHECK(fs::is_directory(dir));
    std::set<std::string> expected;
    for (const auto &s : kSounds)
        for (int v = 1; v <= std::max(1, s.variants); ++v) expected.insert(soundFile(s, v).substr(7));
    std::set<std::string> shipped;
    std::string license;
    {
        std::vector<uint8_t> bytes;
        CHECK(readFile(dir / "LICENSE.md", bytes));
        license.assign(bytes.begin(), bytes.end());
        CHECK(license.find("CC0") != std::string::npos);
    }
    size_t total = 0;
    for (const auto &e : fs::directory_iterator(dir)) {
        if (e.path().extension() != ".ogg") continue;
        const std::string name = e.path().filename().string();
        shipped.insert(name);
        std::vector<uint8_t> bytes;
        CHECK(readFile(e.path(), bytes));
        total += bytes.size();
        Pcm pcm;
        std::string error;
        const bool ok = decodeOgg(bytes.data(), bytes.size(), pcm, &error);
        if (!ok) std::printf("  %s: %s\n", name.c_str(), error.c_str());
        CHECK(ok);
        CHECK(pcm.channels == 1 || pcm.channels == 2);
        CHECK(pcm.rate >= 22050 && pcm.rate <= 48000);
        CHECK(pcm.frames() > 0);
        int rate = 0, channels = 0;
        int64_t frames = 0;
        CHECK(probeOgg(bytes.data(), bytes.size(), rate, channels, frames));
        CHECK(rate == pcm.rate && channels == pcm.channels);
        CHECK(frames == int64_t(pcm.frames()));
        // Not clipped to death, not silent.
        int16_t peak = 0;
        for (int16_t s : pcm.samples) peak = std::max<int16_t>(peak, int16_t(s == -32768 ? 32767 : std::abs(s)));
        CHECK(peak > 300);
        // Every file is credited.
        if (license.find("`" + name + "`") == std::string::npos) {
            std::printf("  %s is not in LICENSE.md\n", name.c_str());
            CHECK(false);
        }
    }
    for (const auto &n : expected)
        if (!shipped.count(n)) {
            std::printf("  missing %s\n", n.c_str());
            CHECK(false);
        }
    for (const auto &n : shipped)
        if (!expected.count(n)) {
            std::printf("  %s is not in SoundCatalog.h\n", n.c_str());
            CHECK(false);
        }
    CHECK(total < 4u * 1024u * 1024u); // a few MB at most
    std::printf("[sound] %zu .ogg files, %.2f MiB, all decode\n", shipped.size(), double(total) / 1048576.0);

    // Streams: the long files decode block by block to the same length.
    for (const char *n : {"ambience_city.ogg", "music_infinite_peace.ogg"}) {
        auto bytes = std::make_shared<std::vector<uint8_t>>();
        CHECK(readFile(dir / n, *bytes));
        int rate = 0, channels = 0;
        int64_t frames = 0;
        CHECK(probeOgg(bytes->data(), bytes->size(), rate, channels, frames));
        OggStream s;
        CHECK(s.open(bytes));
        std::vector<float> block(4096 * 2);
        int64_t got = 0;
        for (int n2; (n2 = s.read(block.data(), 4096)) > 0;) got += n2;
        CHECK(got == frames);
        s.rewind();
        CHECK(s.read(block.data(), 4096) == 4096);
    }
}

static void mixerTests() {
    // A 0.25 s 44.1 kHz clip at 48 kHz output lasts 12000 frames; pitch 2 halves it.
    for (float pitch : {1.0f, 2.0f, 0.5f}) {
        Mixer m;
        m.setOutputRate(48000);
        const int clip = m.addClip("tone", tone(44100, 1, 11025));
        CHECK(clip == 0);
        PlayParams p;
        p.pitch = pitch;
        CHECK(m.play(clip, p) != 0);
        float peak = 0;
        const int frames = renderUntilSilent(m, 200000, &peak);
        const int expected = int(12000.0f / pitch);
        CHECK(std::abs(frames - expected) < 600);
        CHECK(peak > 0.3f && peak <= 1.0f);
        CHECK(m.activeVoices() == 0);
    }
    {
        // Pan: hard left is silent on the right.
        Mixer m;
        const int clip = m.addClip("tone", tone(48000, 1, 4800));
        PlayParams p;
        p.pan = -1.0f;
        m.play(clip, p);
        std::vector<float> buf(1024 * 2);
        m.render(buf.data(), 1024);
        float l = 0, r = 0;
        for (int i = 0; i < 1024; ++i) {
            l = std::max(l, std::fabs(buf[size_t(2 * i)]));
            r = std::max(r, std::fabs(buf[size_t(2 * i + 1)]));
        }
        CHECK(l > 0.3f && r < 1e-4f);
    }
    {
        // Loops keep going; stop() fades out; set() changes volume; buses.
        Mixer m;
        const int clip = m.addClip("tone", tone(48000, 2, 480));
        PlayParams p;
        p.loop = true;
        const VoiceId id = m.play(clip, p);
        std::vector<float> buf(512 * 2);
        for (int i = 0; i < 40; ++i) m.render(buf.data(), 512);
        CHECK(m.playing(id));
        m.set(id, 0.0f, 1.0f, 0.0f);
        m.render(buf.data(), 512);
        m.render(buf.data(), 512);
        float top = 0;
        for (float s : buf) top = std::max(top, std::fabs(s));
        CHECK(top < 1e-4f);
        m.set(id, 1.0f, 1.0f, 0.0f);
        m.setBusVolume(Bus::Effects, 0.0f);
        m.render(buf.data(), 512);
        m.render(buf.data(), 512);
        top = 0;
        for (float s : buf) top = std::max(top, std::fabs(s));
        CHECK(top < 1e-4f);
        m.setBusVolume(Bus::Effects, 1.0f);
        m.stop(id, 0.05f);
        CHECK(!m.playing(id));
        const int tail = renderUntilSilent(m, 48000);
        CHECK(tail > 1000 && tail < 4000); // ~2400 frames of fade
        CHECK(m.activeVoices() == 0);
    }
    {
        // Pause holds voices silently and resumes them.
        Mixer m;
        const int clip = m.addClip("tone", tone(48000, 1, 48000));
        m.play(clip, PlayParams{});
        std::vector<float> buf(512 * 2);
        m.render(buf.data(), 512);
        m.setPaused(true);
        for (int i = 0; i < 10; ++i) m.render(buf.data(), 512);
        float top = 0;
        for (float s : buf) top = std::max(top, std::fabs(s));
        CHECK(top == 0.0f);
        CHECK(m.activeVoices() == 1);
        m.setPaused(false);
        for (int i = 0; i < 4; ++i) m.render(buf.data(), 512);
        top = 0;
        for (float s : buf) top = std::max(top, std::fabs(s));
        CHECK(top > 0.3f);
    }
    {
        // Voice limit: one-shots are stolen, loops are kept.
        Mixer m;
        const int clip = m.addClip("tone", tone(48000, 1, 48000));
        PlayParams loop;
        loop.loop = true;
        const VoiceId keep = m.play(clip, loop);
        for (int i = 0; i < Mixer::kMaxVoices + 10; ++i) m.play(clip, PlayParams{});
        CHECK(m.activeVoices() == Mixer::kMaxVoices);
        CHECK(m.playing(keep));
        // Loud mixes stay inside [-1, 1].
        std::vector<float> buf(512 * 2);
        m.render(buf.data(), 512);
        for (float s : buf) CHECK(std::fabs(s) <= 1.0f);
    }
    CHECK(clampVolume(std::nanf(""), 0.3f) == 0.3f);
    CHECK(clampVolume(4.0f) == 1.0f);
}

struct Recorder : SoundOut {
    struct Event {
        std::string name;
        float volume, pitch, pan;
        bool loop;
    };
    std::vector<Event> events;
    std::map<SoundHandle, std::string> loops;
    std::set<SoundHandle> stopped;
    SoundHandle next = 1;
    SoundHandle play(const std::string &name, float volume, float pitch, float pan, bool loop, float) override {
        events.push_back({name, volume, pitch, pan, loop});
        if (loop) loops[next] = name;
        return next++;
    }
    void set(SoundHandle h, float volume, float pitch, float pan) override {
        if (loops.count(h)) lastSet[loops[h]] = {loops[h], volume, pitch, pan, true};
    }
    void stop(SoundHandle h, float) override {
        stopped.insert(h);
        loops.erase(h);
    }
    bool playing(SoundHandle h) const override { return loops.count(h) > 0; }
    int count(const std::string &name) const {
        return int(std::count_if(events.begin(), events.end(), [&](const Event &e) { return e.name == name; }));
    }
    bool looping(const std::string &name) const {
        for (const auto &l : loops)
            if (l.second == name) return true;
        return false;
    }
    std::map<std::string, Event> lastSet;
    void clear() { events.clear(); }
};

static void directorTests() {
    CHECK(surfaceFromTexture("e1u1/GRASS1_2") == Surface::Grass);
    CHECK(surfaceFromTexture("e2u3/woodfloor1") == Surface::Wood);
    CHECK(surfaceFromTexture("e1u1/floor3_3") == Surface::Concrete);
    CHECK(surfaceFromTexture("snow_ground") == Surface::Snow);
    CHECK(surfaceFromTexture("Carpet_red") == Surface::Carpet);
    CHECK(std::string(stepSound(Surface::Wood)) == "step_wood");

    // Spatial helpers: in front = centre, to the right = positive pan, far = silent.
    Listener l; // yaw 0: forward is -z, right is +x
    CHECK(std::fabs(SoundDirector::panFor(l, 0, -5)) < 1e-4f);
    CHECK(SoundDirector::panFor(l, 5, 0) > 0.6f);
    CHECK(SoundDirector::panFor(l, -5, 0) < -0.6f);
    CHECK(SoundDirector::distanceGain(l, 0, 0, 1) > 0.99f);
    CHECK(SoundDirector::distanceGain(l, 0, 0, 20) < SoundDirector::distanceGain(l, 0, 0, 5));
    CHECK(SoundDirector::distanceGain(l, 0, 0, 60) == 0.0f);

    const float dt = 1.0f / 60.0f;
    {
        // Walking at 4.5 m/s on concrete for 4 s: about 2.7 steps a second.
        SoundDirector d;
        d.musicDelay = 1e9f;
        d.startMusicNow();
        Recorder r;
        SandboxFrame f;
        f.onFoot = true;
        f.grounded = true;
        for (int i = 0; i < 240; ++i) {
            f.feetZ = -4.5f * float(i) * dt;
            d.sandbox(f, dt, r);
        }
        const int steps = r.count("step_concrete");
        CHECK(steps >= 9 && steps <= 13);
        CHECK(r.looping("ambience_city"));
        CHECK(r.count("music_infinite_peace") == 1); // startMusicNow
        // Standing still: no more steps.
        r.clear();
        for (int i = 0; i < 120; ++i) d.sandbox(f, dt, r);
        CHECK(r.count("step_concrete") == 0);
        // Wood under the feet.
        f.surface = Surface::Wood;
        for (int i = 0; i < 120; ++i) {
            f.feetX = 4.5f * float(i) * dt;
            d.sandbox(f, dt, r);
        }
        CHECK(r.count("step_wood") >= 4);
        CHECK(r.count("step_concrete") == 0);

        // Jump and land.
        r.clear();
        f.grounded = false;
        f.verticalSpeed = 6.5f;
        d.sandbox(f, dt, r);
        CHECK(r.count("jump") == 1);
        for (int i = 0; i < 40; ++i) {
            f.verticalSpeed -= 18.0f * dt;
            d.sandbox(f, dt, r);
        }
        f.grounded = true;
        f.verticalSpeed = 0;
        d.sandbox(f, dt, r);
        CHECK(r.count("land") == 1);
        CHECK(r.count("jump") == 1);
        // Teleport (respawn) is not a footstep.
        r.clear();
        f.feetX += 50.0f;
        d.sandbox(f, dt, r);
        CHECK(r.count("step_wood") == 0);
        // Flying: no steps.
        f.onFoot = false;
        for (int i = 0; i < 120; ++i) {
            f.feetX += 0.1f;
            d.sandbox(f, dt, r);
        }
        CHECK(r.count("step_wood") == 0);
    }
    {
        // Tools: spawn, grab, throw, freeze, unfreeze, delete, tool switch, doors, impacts.
        SoundDirector d;
        d.musicDelay = 1e9f;
        Recorder r;
        SandboxFrame f;
        d.sandbox(f, dt, r);
        f.tool = 1;
        d.sandbox(f, dt, r);
        CHECK(r.count("tool_switch") == 1);
        SandboxBody box;
        box.z = -3;
        f.bodies.push_back(box);
        d.sandbox(f, dt, r);
        CHECK(r.count("spawn") == 1);
        f.tool = 2;
        f.held = 0;
        d.sandbox(f, dt, r);
        CHECK(r.count("grab") == 1);
        f.held = -1;
        d.sandbox(f, dt, r);
        CHECK(r.count("throw") == 1);
        f.held = 0;
        d.sandbox(f, dt, r);
        f.held = -1;
        f.bodies[0].frozen = true; // freeze while holding: no throw
        d.sandbox(f, dt, r);
        CHECK(r.count("freeze") == 1);
        CHECK(r.count("throw") == 1);
        f.bodies[0].frozen = false;
        d.sandbox(f, dt, r);
        CHECK(r.count("freeze") == 2);
        // Falling body hits the ground: one impact; resting ones are quiet.
        f.bodies[0].vy = -7.0f;
        d.sandbox(f, dt, r);
        f.bodies[0].vy = 0.0f;
        d.sandbox(f, dt, r);
        CHECK(r.count("impact_generic") == 1);
        for (int i = 0; i < 30; ++i) d.sandbox(f, dt, r);
        CHECK(r.count("impact_generic") == 1);
        // A door opens and closes.
        SandboxBody door;
        door.door = true;
        door.x = 4;
        f.bodies.push_back(door);
        d.sandbox(f, dt, r); // spawn
        f.bodies[1].doorOpen = true;
        d.sandbox(f, dt, r);
        CHECK(r.count("door_open") == 1);
        CHECK(r.events.back().pan > 0.3f); // to the right
        f.bodies[1].doorOpen = false;
        d.sandbox(f, dt, r);
        CHECK(r.count("door_close") == 1);
        f.bodies.pop_back();
        d.sandbox(f, dt, r);
        CHECK(r.count("delete") == 1);
        // Loading a save with 40 objects is not 40 spawn sounds.
        f.bodies.assign(40, box);
        d.sandbox(f, dt, r);
        CHECK(r.count("spawn") == 2);
    }
    {
        // Car: doors, engine pitch rises with speed, engine stops on exit.
        SoundDirector d;
        d.musicDelay = 1e9f;
        Recorder r;
        SandboxFrame f;
        SandboxBody car;
        car.car = true;
        car.kind = 2;
        f.bodies.push_back(car);
        d.sandbox(f, dt, r);
        f.seated = 0;
        f.seatedCar = true;
        d.sandbox(f, dt, r);
        CHECK(r.count("car_door_open") == 1);
        CHECK(r.looping("engine"));
        for (int i = 0; i < 60; ++i) d.sandbox(f, dt, r);
        CHECK(r.count("car_door_close") == 1); // after the delay
        const float idle = r.lastSet["engine"].pitch;
        for (int i = 0; i < 120; ++i) {
            f.bodies[0].z -= 15.0f * dt;
            d.sandbox(f, dt, r);
        }
        CHECK(r.lastSet["engine"].pitch > idle + 0.4f);
        f.seated = -1;
        f.seatedCar = false;
        d.sandbox(f, dt, r);
        CHECK(!r.looping("engine"));
        CHECK(r.count("car_door_open") == 2);
        // Pause menu: the ambience keeps going quieter, loops stop.
        f.droneRunning = true;
        d.sandbox(f, dt, r);
        CHECK(r.looping("drone"));
        f.active = false;
        d.sandbox(f, dt, r);
        CHECK(!r.looping("drone"));
        CHECK(r.looping("ambience_city"));
    }
    {
        // Drone: far away is quieter than close by.
        SoundDirector d;
        d.musicDelay = 1e9f;
        Recorder r;
        SandboxFrame f;
        f.droneRunning = true;
        f.droneZ = -2;
        d.sandbox(f, dt, r);
        d.sandbox(f, dt, r);
        const float near = r.lastSet["drone"].volume;
        f.droneZ = -30;
        d.reset();
        d.sandbox(f, dt, r);
        d.sandbox(f, dt, r);
        CHECK(r.lastSet["drone"].volume < near * 0.5f);
    }
    {
        // Map mode: footsteps and landing, ambience turned down, no car / drone.
        SoundDirector d;
        d.musicDelay = 1e9f;
        Recorder r;
        MapFrame f;
        f.onFoot = true;
        f.grounded = true;
        f.surface = Surface::Grass;
        for (int i = 0; i < 120; ++i) {
            f.feetX = 9.0f * float(i) * dt;
            d.map(f, dt, r);
        }
        CHECK(r.count("step_grass") >= 6);
        CHECK(r.lastSet["ambience_city"].volume < 0.5f);
        f.grounded = false;
        for (int i = 0; i < 30; ++i) {
            f.verticalSpeed = -12.0f;
            d.map(f, dt, r);
        }
        f.grounded = true;
        d.map(f, dt, r);
        CHECK(r.count("land") == 1);
    }
    {
        // Music: once after the delay, then a long gap.
        SoundDirector d;
        d.musicDelay = 2.0f;
        d.startMusicNow();
        Recorder r;
        SandboxFrame f;
        d.sandbox(f, dt, r);
        CHECK(r.count("music_infinite_peace") == 1);
        r.loops.clear(); // track finished
        for (int i = 0; i < 60 * 100; ++i) d.sandbox(f, dt, r);
        CHECK(r.count("music_infinite_peace") == 1); // still in the gap (>= 150 s)
        for (int i = 0; i < 60 * 200; ++i) d.sandbox(f, dt, r); // 300 s: the gap is over
        CHECK(r.count("music_infinite_peace") == 2);
    }
    {
        // The game's CD track plays: the city's music fades out and waits.
        SoundDirector d;
        d.musicDelay = 2.0f;
        d.startMusicNow();
        Recorder r;
        MapFrame f;
        d.map(f, dt, r);
        CHECK(r.count("music_infinite_peace") == 1);
        d.gameMusic = true;
        d.map(f, dt, r);
        CHECK(!r.looping("music_infinite_peace") && !r.stopped.empty());
        for (int i = 0; i < 60 * 400; ++i) d.map(f, dt, r);
        CHECK(r.count("music_infinite_peace") == 1);
        d.gameMusic = false; // track gone: back after the usual delay
        for (int i = 0; i < 60; ++i) d.map(f, dt, r);
        CHECK(r.count("music_infinite_peace") == 1);
        for (int i = 0; i < 60 * 2; ++i) d.map(f, dt, r);
        CHECK(r.count("music_infinite_peace") == 2);
    }
}

// Quake 2's CD track from music/trackNN.ogg, music/TrackNN.ogg or
// music/NN.ogg in a search root or its baseq2 folder.
static void cdMusicTests(const fs::path &assets) {
    CHECK(GameSoundPlayer::musicFiles(0).empty());
    CHECK(GameSoundPlayer::musicFiles(1).empty()); // the CD's data track
    CHECK(GameSoundPlayer::musicFiles(100).empty());
    const std::vector<std::string> names = GameSoundPlayer::musicFiles(5);
    CHECK(names.size() == 3 && names[0] == "music/track05.ogg" && names[1] == "music/Track05.ogg" &&
          names[2] == "music/05.ogg");

    std::vector<uint8_t> ogg;
    CHECK(readFile(assets / "sounds" / "jump.ogg", ogg));
    const fs::path root = fs::temp_directory_path() / "ekron_cd_music_test";
    fs::remove_all(root);
    fs::create_directories(root / "music");
    fs::create_directories(root / "baseq2" / "music");
    const auto put = [&](const fs::path &p, const std::vector<uint8_t> &bytes) {
        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
    };
    put(root / "music" / "Track03.ogg", ogg);          // GOG, next to baseq2
    put(root / "baseq2" / "music" / "track07.ogg", ogg); // re-release
    put(root / "baseq2" / "music" / "09.ogg", ogg);
    put(root / "music" / "11.ogg", {'n', 'o', 'p', 'e'});

    realms::Engine engine;
    realms::EngineConfig cfg;
    cfg.backend = realms::BackendPreference::Native;
    cfg.searchRoots = {root.string()};
    cfg.writableRoot = (root / "home").string();
    CHECK(engine.init(cfg));
    Mixer mixer;
    mixer.setOutputRate(48000);
    GameSoundPlayer player;
    player.music(engine, mixer, 3);
    CHECK(player.musicTrack() == 3 && player.musicPlaying(mixer));
    const int first = mixer.activeVoices();
    player.music(engine, mixer, 3); // same track: keeps playing, no restart
    CHECK(mixer.activeVoices() == first);
    std::vector<float> buf(1024 * 2);
    for (int i = 0; i < 200; ++i) mixer.render(buf.data(), 1024); // ~4 s, longer than the clip: loops
    CHECK(player.musicPlaying(mixer));
    player.stop(mixer); // game sounds stop, the music goes on (menu open)
    CHECK(player.musicPlaying(mixer));
    player.music(engine, mixer, 7);
    CHECK(player.musicTrack() == 7 && player.musicPlaying(mixer));
    player.music(engine, mixer, 9);
    CHECK(player.musicPlaying(mixer));
    player.music(engine, mixer, 2); // no file
    CHECK(player.musicTrack() == 2 && !player.musicPlaying(mixer));
    player.music(engine, mixer, 11); // not Ogg Vorbis
    CHECK(!player.musicPlaying(mixer));
    player.music(engine, mixer, 1);
    CHECK(!player.musicPlaying(mixer));
    player.music(engine, mixer, 7);
    CHECK(player.musicPlaying(mixer));
    player.music(engine, mixer, 0); // no game / track 0: silence
    CHECK(player.musicTrack() == 0 && !player.musicPlaying(mixer));
    for (int i = 0; i < 100; ++i) mixer.render(buf.data(), 1024);
    CHECK(mixer.activeVoices() == 0); // faded out
    const int clips = mixer.clipCount();
    player.music(engine, mixer, 3);
    player.stopMusic(mixer);
    CHECK(mixer.clipCount() == clips + 1);
    CHECK(mixer.play(clips, PlayParams{}) == 0); // the dropped track plays nothing
    engine.shutdown();
    fs::remove_all(root);
}

// `--pascal <librealms_core>` runs the console part on the Free Pascal core.
static std::string gPascalLibrary;

static void systemTests(const fs::path &assets) {
    SoundSystem sound;
    const int failed = sound.loadNow([&](const std::string &path, std::vector<uint8_t> &out) {
        return readFile(assets / path, out);
    });
    CHECK(failed == 0);
    CHECK(sound.ready());
    std::printf("[sound] %s\n", sound.summary().c_str());
    CHECK(sound.mixer().clipCount() > 50);
    CHECK(sound.play("jump", 1, 1, 0, false, 0) != 0);
    CHECK(sound.play("step_wood_3", 1, 1, 0, false, 0) != 0);
    CHECK(sound.play("step_wood_9", 1, 1, 0, false, 0) == 0);
    CHECK(sound.play("nope", 1, 1, 0, false, 0) == 0);
    const SoundHandle music = sound.play("music_infinite_peace", 1, 1, 0, false, 0);
    CHECK(music != 0);
    std::vector<float> buf(1024 * 2);
    for (int i = 0; i < 50; ++i) sound.mixer().render(buf.data(), 1024);
    float top = 0;
    for (float s : buf) top = std::max(top, std::fabs(s));
    CHECK(top > 0.01f); // the streamed music is audible
    CHECK(sound.playing(music));

    // Console commands and cvars on the C++ reference core (or the Pascal one).
    realms::Engine engine;
    realms::EngineConfig cfg;
    cfg.backend = gPascalLibrary.empty() ? realms::BackendPreference::Native : realms::BackendPreference::Pascal;
    cfg.pascalLibrary = gPascalLibrary;
    CHECK(engine.init(cfg));
    std::printf("[sound] console on core=%s\n", engine.backendName().c_str());
    if (!gPascalLibrary.empty()) CHECK(engine.backendName() == "pascal-quake2");
    realms::sandbox::registerSharedCvars(engine, "sound-test");
    CHECK(engine.hasCvar("s_volume") && engine.hasCvar("s_musicvolume"));
    CHECK(engine.cvarFlags("s_volume") & REALMS_CVAR_ARCHIVE);
    sound.registerCommands(engine);
    const int before = sound.mixer().activeVoices();
    engine.executeNow("play jump");
    engine.executeNow("play sounds/land_1.ogg 0.5");
    CHECK(sound.mixer().activeVoices() == before + 2);
    engine.executeNow("play nothing_here");
    bool warned = false, listed = false;
    engine.executeNow("soundlist");
    for (const auto &line : engine.console()) {
        if (line.text.find("unknown sound nothing_here") != std::string::npos) warned = true;
        if (line.text.find("step_concrete (x5)") != std::string::npos) listed = true;
    }
    CHECK(warned);
    CHECK(listed);
    engine.executeNow("set s_volume 0.25");
    engine.executeNow("set s_musicvolume 0");
    sound.syncCvars(engine);
    CHECK(sound.mixer().busVolume(Bus::Music) == 0.0f);
    engine.executeNow("set s_volume banana");
    sound.syncCvars(engine); // not a number: falls back, no NaN
    engine.executeNow("stopsound");
    for (int i = 0; i < 20; ++i) sound.mixer().render(buf.data(), 1024);
    CHECK(sound.mixer().activeVoices() == 0);
    sound.unregisterCommands(engine);
    engine.shutdown();
}

int main(int argc, char **argv) {
    fs::path assets = "sys/android/app/src/main/assets";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--pascal" && i + 1 < argc)
            gPascalLibrary = argv[++i];
        else
            assets = arg;
    }
    assetTests(assets);
    mixerTests();
    directorTests();
    cdMusicTests(assets);
    systemTests(assets);
    std::printf("[sound] %d checks, %d failures: %s\n", checks, failures, failures == 0 ? "OK" : "FAILED");
    return failures == 0 ? 0 : 1;
}
