// Client half of Quake 2 for the sandboxes: the state the client keeps from
// the server's frames (cl.frame, cl.configstrings, cl.inventory), built only
// from GameSnapshots (realms/GameServer.h). Quake2Module draws, plays and
// lays out the HUD from it, never from the server's edicts, so the same
// snapshots can come from a recorded demo or, later, from the network.
//
// Its read side matches GameServer's "what the sandbox draws" part.
#pragma once

#include "realms/GameServer.h"

#include <array>
#include <string>
#include <vector>

namespace realms {

class GameClient {
public:
    GameClient();

    // Forgets everything (a new game, a stopped one).
    void reset();
    // Takes one snapshot: the player state and the time every frame, the
    // entity lists on a new server frame, the changed config strings and
    // inventory, and the events (queued for takeEvents). A snapshot with
    // `running` false resets the client.
    void apply(GameSnapshot snapshot);
    // A full snapshot is wanted: nothing received yet (or reset since).
    bool needsFull() const { return !running_; }
    // Shorthand: server.snapshot(full when needed), then apply.
    void update(GameServer &server);
    // Everything the client holds as one full snapshot (no events): what a
    // demo starts with when it is recorded in the middle of a game. Applied
    // to a reset client it gives the same state. Not running: running false.
    GameSnapshot fullSnapshot() const;

    bool running() const { return running_; }
    int frameNumber() const { return frameNumber_; }
    double time() const { return frameNumber_ * GameServer::kFrameSeconds; }
    double lerpFraction() const { return running_ ? lerpFraction_ : 0.0; }
    const q2player_state_t *playerState() const { return running_ && hasPlayer_ ? &player_ : nullptr; }
    Vec3 playerOrigin() const { return running_ ? playerOrigin_ : Vec3{}; }
    Vec3 viewOrigin() const;  // origin + viewoffset
    Vec3 viewAngles() const;  // ps.viewangles + kick_angles
    // The last server frame's entities, in edict order; `withInline` keeps brush models.
    std::vector<GameRenderEntity> renderEntities(bool withInline = false) const;
    const std::vector<GameBeamEntity> &beamEntities() const { return beams_; }
    const std::vector<GameLoopSound> &loopSounds() const { return loopSounds_; }
    const std::string &configString(int index) const;
    std::string modelName(int modelindex) const;
    int inlineModel(int modelindex) const;
    const std::array<int16_t, Q2_MAX_ITEMS> &inventory() const { return inventory_; }
    // Events received since the last call.
    std::vector<GameEvent> takeEvents();
    // How many snapshots / server frames have come in since the last reset.
    int snapshots() const { return snapshots_; }
    int serverFrames() const { return serverFrames_; }

private:
    bool running_ = false;
    int frameNumber_ = 0;
    double lerpFraction_ = 0.0;
    bool hasPlayer_ = false;
    q2player_state_t player_{};
    Vec3 playerOrigin_{};
    std::vector<GameRenderEntity> entities_;
    std::vector<GameBeamEntity> beams_;
    std::vector<GameLoopSound> loopSounds_;
    std::vector<std::string> configStrings_;
    std::array<int16_t, Q2_MAX_ITEMS> inventory_{};
    std::vector<GameEvent> events_;
    int snapshots_ = 0;
    int serverFrames_ = 0;
};

} // namespace realms
