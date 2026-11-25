// Ekron demos: a recorded stream of GameSnapshots (realms/GameServer.h),
// what the client got from the server each frame, played back into a
// GameClient (realms/GameClient.h). Quake 2 records its server messages
// (.dm2); Ekron records its own snapshots instead, so a demo replays
// everything the client draws and plays, without the game module.
//
//   record <name>   (Quake2Module, while a game runs) writes
//                   <writable root>/demos/<name>.ekd until "stop", a map
//                   change or the game stopping
//   stop            stops recording
//   demomap <name>  (sandbox::registerMapCommands) loads the demo's map and
//                   plays demos/<name>.ekd on it (the writable root first,
//                   then the other search roots)
//
// File: the magic "EKRDEMO", a version byte (1), the header (map path and
// its checksum), then records: a 32-bit little-endian length and that many
// bytes. A record is one snapshot and the seconds since the previous one.
// Strings (models, sounds, config strings, texts) are sent once and then
// referred to by number; the player state, its origin, entities (by edict
// number) and the inventory are sent as changes from the last ones. A file
// cut short (a crash while recording) plays up to its last whole record.
#pragma once

#include "realms/GameServer.h"

#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace realms {

class Engine;

constexpr const char *kGameDemoDir = "demos";
constexpr const char *kGameDemoExtension = ".ekd";
// NOSET cvar: the demo ("demos/<name>.ekd") that "demomap" loaded the map
// for; Quake2Module::open plays it (and clears it) instead of starting the game.
constexpr const char *kGameDemoCvar = "cl_demofile";

// The same names as save games: 1 to 32 of A-Z a-z 0-9 _ -.
bool validGameDemoName(const std::string &name);
// "demos/<name>.ekd" (a trailing ".ekd" in `name` is accepted); "" for a bad name.
std::string gameDemoFile(const std::string &name);
// <writable root>/demos/<name>.ekd; "" for a bad name or without a writable root.
std::string gameDemoPath(const Engine &engine, const std::string &name);

struct GameDemoHeader {
    std::string mapPath;      // Engine::mapPath() when recorded
    uint32_t mapChecksum = 0; // RealmsMapInfo::checksum (0: unknown)
};

// Encoder / decoder state shared by both ends: the string table and the
// last values changes are sent against.
struct GameDemoState {
    std::vector<std::string> strings;
    std::unordered_map<std::string, uint32_t> ids; // encoder only
    std::vector<uint32_t> player, origin, inventory;
    std::map<int, std::vector<uint32_t>> entities;
};

// In memory, for the file classes below and tests.
void encodeGameDemoHeader(const GameDemoHeader &header, std::vector<uint8_t> &out);
// Appends one record (the length, then the record).
void encodeGameDemoRecord(GameDemoState &state, const GameSnapshot &snapshot, float seconds,
                          std::vector<uint8_t> &out);
// Reads back one record (the bytes after its length); false when they are
// damaged or don't fit `state` (then `state` is left half updated).
bool decodeGameDemoRecord(GameDemoState &state, const uint8_t *data, size_t size, GameSnapshot &snapshot,
                          float &seconds);

class GameDemoWriter {
public:
    ~GameDemoWriter() { close(); }
    // Creates the file (and its folder); false + error when it can't.
    bool open(const std::string &path, const GameDemoHeader &header, std::string &error);
    // Appends a record; false (and closed) when the write fails.
    bool write(const GameSnapshot &snapshot, float seconds);
    void close();
    bool isOpen() const { return file_.is_open(); }
    const std::string &path() const { return path_; }
    uint64_t bytes() const { return bytes_; }
    int records() const { return records_; }

private:
    std::ofstream file_;
    std::string path_;
    GameDemoState state_;
    std::vector<uint8_t> buffer_;
    uint64_t bytes_ = 0;
    int records_ = 0;
};

class GameDemoReader {
public:
    // A whole demo file in memory; false + error for a bad header.
    bool open(std::vector<uint8_t> bytes, std::string &error);
    // Only the header (demomap reads it to find the map).
    static bool readHeader(const std::vector<uint8_t> &bytes, GameDemoHeader &header, std::string &error);
    const GameDemoHeader &header() const { return header_; }
    // The next record; false at the end. error() is set when the rest of the
    // file is damaged (a cut-short last record is not an error).
    bool next(GameSnapshot &snapshot, float &seconds);
    const std::string &error() const { return error_; }
    int records() const { return records_; }
    // Seconds of all the records read so far.
    double time() const { return time_; }

private:
    std::vector<uint8_t> bytes_;
    size_t pos_ = 0;
    GameDemoHeader header_;
    GameDemoState state_;
    std::string error_;
    int records_ = 0;
    double time_ = 0.0;
};

} // namespace realms
