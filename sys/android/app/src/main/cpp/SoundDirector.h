#pragma once
// Turns what happens in the game into sounds. Each frame the platform hands
// over a snapshot (SandboxFrame for the city, MapFrame for Realms map mode,
// see SoundSnapshot.h); the director compares it with the last one and plays
// footsteps, landings, tool and door sounds, impacts, the car engine, the
// drone propellers, the city ambience and now and then some music.
// No platform or game headers here, so it is easy to test.

#include <cstdint>
#include <string>
#include <vector>

namespace ekron::audio {

enum class Surface { Concrete = 0, Wood, Grass, Carpet, Snow };
const char *stepSound(Surface s);
// Q2 / glTF texture name -> surface ("grass1", "e1u1/woodfloor" ...).
Surface surfaceFromTexture(const std::string &texture);

struct Listener {
    float x = 0, y = 0, z = 0;
    float yaw = 0; // forward is (-sin yaw, 0, -cos yaw), as in the sandbox
};

struct SandboxBody {
    float x = 0, y = 0, z = 0;
    float vy = 0;
    int kind = 0; // 0 generic, 1 wood, 2 metal
    bool frozen = false;
    bool door = false;
    bool doorOpen = false;
    bool car = false;
};

struct SandboxFrame {
    bool active = true; // gameplay running (no pause menu, loaded)
    Listener listener;
    bool onFoot = false; // walking: not flying, not seated, not piloting
    bool grounded = false;
    float feetX = 0, feetY = 0, feetZ = 0;
    float verticalSpeed = 0; // m/s, up positive
    Surface surface = Surface::Concrete;
    int tool = 0;
    int held = -1;
    int seated = -1;     // seated object index
    bool seatedCar = false;
    bool droneRunning = false;
    float droneX = 0, droneY = 0, droneZ = 0;
    float droneThrottle = 0; // stick input -1..1
    std::vector<SandboxBody> bodies;
};

struct MapFrame {
    bool active = true;
    Listener listener;
    bool onFoot = false; // not noclip
    bool grounded = false;
    float feetX = 0, feetY = 0, feetZ = 0; // metres
    float verticalSpeed = 0;
    Surface surface = Surface::Concrete;
};

using SoundHandle = uint32_t;

// Where the director's sounds go (SoundSystem, or a recorder in tests).
// Names are SoundCatalog names; volume 0..1 before the catalog volume.
class SoundOut {
public:
    virtual ~SoundOut() = default;
    virtual SoundHandle play(const std::string &name, float volume, float pitch, float pan, bool loop,
                             float fadeIn) = 0;
    virtual void set(SoundHandle handle, float volume, float pitch, float pan) = 0;
    virtual void stop(SoundHandle handle, float fadeSeconds) = 0;
    virtual bool playing(SoundHandle handle) const = 0;
};

class SoundDirector {
public:
    explicit SoundDirector(uint32_t seed = 0x9e3779b9u);

    void sandbox(const SandboxFrame &frame, float dt, SoundOut &out);
    void map(const MapFrame &frame, float dt, SoundOut &out);
    // Forget the last snapshot (map change, respawn): no sounds for the jump.
    void reset();
    // Stops every loop (shutdown, mode switch).
    void stopLoops(SoundOut &out);

    // Music timing: first track after `musicDelay` seconds of play, then a
    // pause between plays. Public for tests and the "music" command.
    float musicDelay = 25.0f;
    float musicGapMin = 150.0f, musicGapMax = 300.0f;
    void startMusicNow() { musicTimer_ = 0.0f; }
    // The game plays its own music (Quake 2's CD track): the city's fades
    // out and waits until it ends.
    bool gameMusic = false;

    // Spatial gain / pan of a sound at (x, y, z) heard from `l`.
    static float distanceGain(const Listener &l, float x, float y, float z);
    static float panFor(const Listener &l, float x, float z);

private:
    struct Track {
        float x = 0, z = 0, vy = 0;
        bool frozen = false, doorOpen = false;
        float cooldown = 0;
    };
    struct Pending {
        std::string name;
        float delay, volume;
    };

    bool haveSandbox_ = false, haveMap_ = false;
    SandboxFrame last_;
    MapFrame lastMap_;
    std::vector<Track> tracks_;
    std::vector<Pending> pending_;
    float stepDistance_ = 0, airTime_ = 0, lowestSpeed_ = 0;
    float carSpeed_ = 0, carX_ = 0, carZ_ = 0;
    bool carSampled_ = false;
    float droneSpeed_ = 0, dronePitch_ = 1.0f;
    float droneLastX_ = 0, droneLastY_ = 0, droneLastZ_ = 0;
    bool droneSampled_ = false;
    SoundHandle engine_ = 0, drone_ = 0, ambience_ = 0, music_ = 0;
    float musicTimer_;
    bool musicWasPlaying_ = false;
    uint32_t rng_;

    float random(float lo, float hi);
    void footsteps(bool onFoot, bool grounded, float fx, float fy, float fz, float lastX, float lastZ, bool hadLast,
                   float verticalSpeed, bool lastGrounded, Surface surface, float dt, SoundOut &out);
    void ambience(float volume, SoundOut &out);
    void music(float dt, SoundOut &out);
    void runPending(float dt, SoundOut &out);
};

} // namespace ekron::audio
