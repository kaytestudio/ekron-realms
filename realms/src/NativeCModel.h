// Realms Engine - C++ reference collision model (Quake 2 cmodel.c).
//
// Internal to the C++ reference core (NativeCore.cpp). Mirrors
// realms/pascal/rlm_cmodel.pas: both load Quake 2 IBSP v38 maps, validate
// every index (maps can come from user PAKs), and answer point-contents and
// box-trace queries with Quake 2 semantics. Keep the two in lock-step; the
// conformance suite in tests/realms runs the same checks against both.
#pragma once

#include "realms/realms_bridge.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace realms_native {

// Parses and validates a .bsp image. On failure returns false, sets `error`
// and leaves no map loaded.
bool cmLoadMap(const std::string &name, const unsigned char *data, size_t size, std::string &error);
void cmUnloadMap();
bool cmLoaded();

int32_t cmMapInfo(RealmsMapInfo *out);
int32_t cmEntities(char *buffer, int32_t bufferSize);
int32_t cmModel(int32_t index, float *mins, float *maxs, float *origin, int32_t *headnode);
int32_t cmHeadnodeForBox(const float *mins, const float *maxs);
int32_t cmPointContents(const float *p, int32_t headnode, const float *origin, const float *angles);
void cmBoxTrace(const float *start, const float *end, const float *mins, const float *maxs, int32_t headnode,
                int32_t brushmask, const float *origin, const float *angles, RealmsTrace *out);
int32_t cmPointLeaf(const float *p);
int32_t cmLeafInfo(int32_t leaf, int32_t *contents, int32_t *cluster, int32_t *area);

bool cmLoadMesh(const RealmsMeshDesc *desc, std::string &error);
int32_t cmClusterPVS(int32_t cluster, uint8_t *buffer, int32_t size);
int32_t cmClusterPHS(int32_t cluster, uint8_t *buffer, int32_t size);
void cmSetAreaPortal(int32_t portal, int32_t open);
int32_t cmAreaPortalOpen(int32_t portal);
int32_t cmAreasConnected(int32_t area1, int32_t area2);
int32_t cmWriteAreaBits(int32_t area, uint8_t *buffer, int32_t size);
int32_t cmBoxLeafs(const float *mins, const float *maxs, int32_t *list, int32_t listSize, int32_t headnode,
                   int32_t *topnode);
int32_t cmHeadnodeVisible(int32_t headnode, const uint8_t *visbits, int32_t size);

uint32_t crc32(const unsigned char *data, size_t size);

} // namespace realms_native
