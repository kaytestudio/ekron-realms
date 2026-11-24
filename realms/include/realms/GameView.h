// Client-side handling of server frames (CL_AddPacketEntities,
// CL_AddViewWeapon): entities interpolated between the last two 100 ms game
// frames, no lerp after a teleport (>512 units) or model change, EF_ROTATE
// and EF_ANIM*, and the view weapon. Call GameEntityLerp::update once per
// sandbox frame after GameServer::frame.
//
// buildGameScene turns the result into triangle batches, one per surface of
// each entity's .md3 model (the game's "tris.md2" names load "tris.md3"), or
// a coloured box from the entity bounds when the model file is missing.
#pragma once

#include "realms/GameServer.h"
#include "realms/Image.h"
#include "realms/Md3Model.h"

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace realms {

struct GameLerpedEntity {
    const GameRenderEntity *entity = nullptr;
    std::array<float, 3> origin{}, angles{}; // Quake 2 map space / degrees
    int frame = 0, oldframe = 0;
    float backlerp = 0.0f; // weight of oldframe
};

struct GameViewWeapon {
    bool visible = false;
    std::string model;
    std::array<float, 3> origin{}, angles{}; // map space / degrees
    int frame = 0, oldframe = 0;
    float backlerp = 0.0f;
};

class GameEntityLerp {
public:
    void reset();
    // Takes a snapshot when the server ran a new game frame since the last call.
    void update(const GameServer &server);

    // The entities of the current game frame (GameServer::renderEntities,
    // brush models included: doors and lifts move smoothly too),
    // interpolated at the server's lerpFraction(). Pointers into this object
    // stay valid until the next update().
    std::vector<GameLerpedEntity> entities(const GameServer &server) const;
    GameViewWeapon viewWeapon(const GameServer &server) const;

    int snapshots() const { return snapshots_; }

    // Shortest-path angle interpolation (LerpAngle), degrees.
    static float lerpAngle(float a2, float a1, float frac);

private:
    struct Snap {
        int modelindex = 0;
        int frame = 0;
        std::array<float, 3> origin{}, angles{};
    };
    int serverFrame_ = -1;
    int snapshots_ = 0;
    std::vector<GameRenderEntity> current_;
    std::unordered_map<int, Snap> prev_; // by edict number, the frame before current_
    std::unordered_map<int, Snap> cur_;
    int gunIndex_ = 0, gunFrame_ = 0;
    int prevGunIndex_ = 0, prevGunFrame_ = 0;
};

// .md3 models loaded through the Engine's VFS (pak files or loose), by the
// game's model name: "models/x/tris.md2" loads "models/x/tris.md3"
// (md3PathFor). A missing or corrupt file is remembered and reported once.
class GameModelCache {
public:
    // nullptr when the model can't be loaded.
    const Md3Model *model(const Engine &engine, const std::string &name);
    // Decodes a skin from the VFS: TGA or 8-bit PCX. When the named file is
    // missing, the other format with the same base name is tried
    // ("skin.pcx" -> "skin.tga" and back). False when missing / corrupt.
    static bool loadSkin(const Engine &engine, const std::string &name, RgbaImage &out, std::string &error);
    void clear() { models_.clear(); }
    size_t loaded() const;
    size_t missing() const;
    // "name: why" for each model that failed, oldest first, cleared on read.
    std::vector<std::string> takeErrors();

private:
    struct Entry {
        std::unique_ptr<Md3Model> model;
    };
    std::unordered_map<std::string, Entry> models_;
    std::vector<std::string> errors_;
};

struct GameDrawBatch {
    int first = 0, count = 0;      // into GameScene::vertices
    int entity = 0;                // edict number, 0 for the view weapon
    std::string model;             // config string name
    std::string skin;              // skin path to texture with; "" = untextured
    std::array<float, 4> color{1, 1, 1, 1}; // tint (alpha < 1: translucent)
    // Placement, Quake 2 map space: rotate like R_RotateForEntity with these
    // angles (yaw, then pitch about -Y, then roll about -X; alias pitch is
    // already negated as R_DrawAliasModel does), then translate to origin.
    std::array<float, 3> origin{}, angles{};
    bool viewWeapon = false;
    bool fallbackBox = false;
    bool fullbright = false; // RF_FULLBRIGHT: not lit (explosions, flashes)
    bool effect = false;     // from GameEffects (appendEffectModels)
    // The light under it from the map's lightmaps (R_LightPoint, 0..~2 per
    // channel), filled by Quake2Module::buildScene; unlit: the sandbox sun.
    bool lit = false;
    std::array<float, 3> light{1, 1, 1};
};

struct GameScene {
    std::vector<ModelVertex> vertices; // model space, Quake units
    std::vector<GameDrawBatch> batches;
    void clear() {
        vertices.clear();
        batches.clear();
    }
};

struct GameSceneOptions {
    bool viewWeapon = true;   // cl_gun
    bool fallbackBoxes = true;
    // The view weapon follows this camera (map space, Quake 2 angles) when
    // set, so it can't lag the drawn view; else the game's view origin.
    bool hasCamera = false;
    std::array<float, 3> cameraOrigin{}, cameraAngles{};
};

// One batch per surface of `model`, each a copy of `base` (entity, pose,
// colour) with its own vertices and skin (Md3Model::skin for `skinnum`).
void appendModelBatches(const Md3Model &model, const GameDrawBatch &base, int frame, int oldframe, float backlerp,
                        int skinnum, GameScene &scene);

// Alias models and fallback boxes for every non-brush entity, and the view
// weapon. Brush models ("*n") are left to the map renderer (use the lerped
// GameLerpedEntity origins / angles for their poses); sprites (.sp2) are
// left to GameEffects, which draws them as glows. EF_QUAD / EF_PENT add the
// blue / red shell and EF_BFG / EF_PLASMA / EF_SPHERETRANS fade the model,
// like cl_ents.c.
void buildGameScene(const Engine &engine, const GameServer &server, const GameEntityLerp &lerp,
                    GameModelCache &models, const GameSceneOptions &options, GameScene &out);

} // namespace realms
