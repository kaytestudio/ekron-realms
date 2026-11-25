// The game's snapshots (realms/GameServer.h GameSnapshot) over a
// NetChannel (realms/NetChannel.h), in Ekron's own format: the demo
// records of realms/GameDemo.h split in two.
//
// - The state (player, its origin, entities, beams, loop sounds,
//   inventory) goes in each message's unreliable part, as changes from a
//   message the client has acknowledged (Quake 2's delta frames): a lost
//   message costs nothing but its own update. Without an acknowledged one
//   in the history, everything is sent.
// - Config strings and events (prints, sounds, temp entities...) happen
//   once, so they go as reliable messages: delivered exactly once, in
//   order, and attached to the next snapshot the client decodes.
//
// Unreliable part: varint base (the message number the changes are from,
// 0: none), then a demo record (realms/GameDemo.h). Reliable message: the
// type byte kGameNetEvents, then a demo record holding only the config
// strings and events (its strings sent with it).
#pragma once

#include "realms/GameDemo.h"
#include "realms/GameServer.h"

#include <array>
#include <climits>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

namespace realms {

// Reliable message types (the first byte).
enum : uint8_t { kGameNetEvents = 1 };

// Server side, one per client.
class GameNetEncoder {
public:
    // Messages kept as bases for the changes.
    static constexpr size_t kHistory = 32;

    void reset() { *this = GameNetEncoder(); }
    // `snapshot` (GameServer::snapshot for this client) for the message
    // numbered `sequence`, as changes from the client's acknowledged
    // message `acked`: the unreliable part, and the reliable message for its
    // config strings and events (empty when it has none).
    void encode(const GameSnapshot &snapshot, uint32_t sequence, uint32_t acked, std::vector<uint8_t> &unreliable,
                std::vector<uint8_t> &reliable);
    // The base the last encode() used (0: none, everything sent).
    uint32_t lastBase() const { return lastBase_; }

private:
    struct Base {
        uint32_t sequence = 0;
        GameDemoState state;
        int entityFrame = INT_MIN; // the frame of the entity lists the client has with this base
    };
    std::deque<Base> history_;
    // the newest entity lists and inventory (GameSnapshot sends them on changes only)
    int listFrame_ = INT_MIN;
    std::vector<GameRenderEntity> entities_;
    std::vector<GameBeamEntity> beams_;
    std::vector<GameLoopSound> loopSounds_;
    std::array<int16_t, Q2_MAX_ITEMS> inventory_{};
    uint32_t lastBase_ = 0;
};

// Client side.
class GameNetDecoder {
public:
    static constexpr size_t kHistory = 64;

    void reset() { *this = GameNetDecoder(); }
    // A reliable message of type kGameNetEvents: its config strings and
    // events wait for the next decode(). False when it is damaged.
    bool reliable(const uint8_t *data, size_t size);
    // The unreliable part of message `sequence` into a snapshot for
    // GameClient::apply, with the waiting config strings and events. False
    // when it is damaged or its base is not in the history.
    bool decode(const uint8_t *data, size_t size, uint32_t sequence, GameSnapshot &out);
    size_t waitingEvents() const { return events_.size(); }

private:
    std::deque<std::pair<uint32_t, GameDemoState>> history_;
    std::vector<std::pair<int, std::string>> configStrings_;
    std::vector<GameEvent> events_;
    int entityFrame_ = INT_MIN; // the frame of the entity lists handed out last
    bool haveInventory_ = false;
    std::array<int16_t, Q2_MAX_ITEMS> inventory_{};
};

} // namespace realms
