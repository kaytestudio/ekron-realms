// Quake 2 save games (sv_ccmds.c SV_Savegame_f / SV_Loadgame_f). One folder
// per save, <writable root>/save/<name>/, holding
//   server.ssv  the comment, map, skill, config strings and area portal
//               states (Quake 2 keeps these in server.ssv and <map>.sv2)
//   game.ssv    the game's WriteGame: clients and cross-level state
//   level.sav   the game's WriteLevel: every entity of the level
//
// "save <name>" (GameServer, while a single-player game runs) writes one;
// "load <name>" (sandbox::registerMapCommands) sets the NOSET cvar
// sv_loadgame to the folder and loads the saved map, and GameServer::start
// then reads the game back (ReadGame after Init, ReadLevel after
// SpawnEntities) instead of starting it fresh. Quake 2's default keys: F6
// "save quick", F9 "load quick".
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace realms {

class Engine;

constexpr const char *kGameSaveServerFile = "server.ssv";
constexpr const char *kGameSaveGameFile = "game.ssv";
constexpr const char *kGameSaveLevelFile = "level.sav";

struct GameSaveInfo {
    std::string comment;                    // "base1  2026-11-21 14:05"
    std::string mapPath;                    // Engine::mapPath() when saved
    int skill = 1;
    std::vector<std::string> configStrings; // by index; may be shorter than Q2_MAX_CONFIGSTRINGS
    std::vector<uint8_t> portals;           // by area portal: 1 open, 0 closed
};

// 1 to 32 of A-Z a-z 0-9 _ - (no paths: Quake 2 refuses "..", "/" and "\").
bool validGameSaveName(const std::string &name);
// <writable root>/save/<name>; "" for a bad name or without a writable root.
std::string gameSaveDir(const Engine &engine, const std::string &name);
// True when the folder holds all three files.
bool gameSaveComplete(const std::string &dir);

bool writeGameSaveInfo(const std::string &dir, const GameSaveInfo &info, std::string &error);
bool readGameSaveInfo(const std::string &dir, GameSaveInfo &info, std::string &error);

} // namespace realms
