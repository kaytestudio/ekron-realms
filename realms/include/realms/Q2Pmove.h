// Realms Engine - Quake 2 player movement (qcommon/pmove.c) on pmove_t.
//
// This is game_import_t::Pmove for the Quake 2 game module: it moves a
// pmove_t through its own trace / pointcontents callbacks (the game's
// PM_trace, i.e. GameServer::trace with the player as passent), exactly like
// the original, including the 12.3 fixed-point origin snapping. It is pure
// C++ on top of those callbacks, so it gives the same results on the Pascal
// and the C++ core.
//
// realms::MapWalker is the compact variant the sandbox uses when no game
// module is running.
#pragma once

#include "realms/q2game.h"

namespace realms::q2 {

struct PmoveParams {
    float stopSpeed = 100.0f;
    float maxSpeed = 300.0f;
    float duckSpeed = 100.0f;
    float accelerate = 10.0f;
    float airAccelerate = 0.0f; // sv_airaccelerate (deathmatch only in Quake 2)
    float waterAccelerate = 10.0f;
    float friction = 6.0f;
    float waterFriction = 1.0f;
    float waterSpeed = 400.0f;
};

void Pmove(q2pmove_t *pm, const PmoveParams &params = {});

// q_shared helpers used by the server and the sandbox.
inline float shortToAngle(int x) { return float(x) * (360.0f / 65536.0f); }
inline int angleToShort(float x) { return int(x * 65536.0f / 360.0f) & 65535; }
void angleVectors(const float angles[3], float forward[3], float right[3], float up[3]);

} // namespace realms::q2
