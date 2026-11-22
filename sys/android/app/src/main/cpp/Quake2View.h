// Map mode on top of Quake2Module, shared by desktop, Android and iOS.
// Converts everything to render space: metres, Y up,
// render = (x, z, -y) / unitsPerMetre. Camera yaw/pitch in radians; yaw 0
// looks down -Z, positive pitch looks up. Without the game module the map is
// walked with the session's MapWalker.
#pragma once

#include "MapSession.h"

#include "realms/Quake2Module.h"

#include <array>
#include <string>
#include <vector>

namespace mapview {

struct Quake2ViewOptions {
    std::string executableDir; // where librealms_game is looked for
    bool startGame = true;     // sv_game
};

// Vertices [first, first + count) of triangles() belong to BSP model `model`.
struct ModelRange {
    int model = 0;
    int first = 0, count = 0;
};

struct ModelPose {
    bool visible = true;
    std::array<float, 16> transform{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; // column-major
};

// Alias model vertex, model space on the render axes, metres.
struct EntityVertex {
    float x, y, z, nx, ny, nz, u, v;
};

struct EntityBatch {
    int first = 0, count = 0; // into the entity vertices
    std::string skin;         // VFS path ("" = untextured)
    std::array<float, 4> color{1, 1, 1, 1};
    std::array<float, 16> transform{}; // model -> render space, column-major
    bool viewWeapon = false;
    bool fallbackBox = false;
    bool fullbright = false;
    bool lit = false;              // light is from the lightmaps; else use the sandbox sun
    std::array<float, 3> light{1, 1, 1};
};

// One corner of an effect quad, render space; shape 0 round, 1 solid.
struct EffectVertex {
    float x, y, z, r, g, b, a, u, v, shape;
};

struct DynamicLight {
    float x, y, z, radius, r, g, b; // render space, metres
};

// Quake 2's MAX_DLIGHTS; the map mode shaders' light arrays have this size.
constexpr size_t kMaxDynamicLights = 32;

// Map faces drawn with the map's own texture (Quake2Module::textures()) have
// layer kMapTextureLayer + 8 * texture + material class (0..7), white tint
// and coordinates over the texture's size. The shaders take the class from
// the low 3 bits and sample layer (layer >> 3) - 1 of the map texture array.
constexpr int kMapTextureLayer = 8;
constexpr int mapTextureLayer(int texture, int materialClass) {
    return kMapTextureLayer + 8 * texture + (materialClass & 7);
}

class Quake2View {
public:
    // Opens the engine's loaded map: the session, then the module (lightmaps
    // and, when wanted and available, the game). False (and inactive)
    // without a map. `geometry` is the extracted map, for triangles().
    bool open(realms::Engine &engine, const Quake2ViewOptions &options, realms::MapGeometry &geometry,
              std::string &error);
    void close();
    bool active() const { return session_.active(); }

    // The map's triangles grouped by BSP model (ranges: empty when there is
    // only the world) and their lightmap coordinates (u, v per vertex, -1 =
    // none; empty without lightmaps). Faces with a texture from the map use
    // mapTextureLayer.
    void triangles(const realms::MapGeometry &geometry, std::vector<sandbox::MeshVertex> &vertices,
                   std::vector<ModelRange> &ranges, std::vector<float> &lightUvs) const;
    // The platform can't draw lightmaps for this map (textured model...).
    void disableLightmaps() { q2_.disableLightmaps(); }
    // ... or the map's textures (call before triangles()).
    void disableTextures() { q2_.disableTextures(); }

    // One frame: the game with this input (attack fires), or the walker.
    void update(realms::Engine &engine, const realms::WalkInput &input, float renderYaw, float renderPitch,
                float dt, bool attack);
    // The game restarted on the same map, or the walker back at its spawn.
    bool respawn(realms::Engine &engine);
    void toggleNoclip();
    bool noclip() const;
    // The camera in render space, and the spawn's yaw in the camera convention.
    physics::Vec3 eye() const;
    float spawnRenderYaw() const;
    // Centre prints, pickups and "Game stopped: ..." since the last call.
    std::vector<std::string> takeMessages();

    // Brush model poses (index = model number; empty without a game).
    const std::vector<ModelPose> &modelPoses() const { return poses_; }
    // The game's entities for this camera. Empty without a game.
    void entities(realms::Engine &engine, float renderYaw, float renderPitch, bool viewWeapon,
                  std::vector<EntityVertex> &vertices, std::vector<EntityBatch> &batches);
    // Effect quads and up to maxLights dynamic lights. Empty without a game.
    void effects(float renderYaw, float renderPitch, size_t maxLights, std::vector<EffectVertex> &vertices,
                 std::vector<DynamicLight> &lights);

    MapSession &session() { return session_; }
    const MapSession &session() const { return session_; }
    realms::Quake2Module &module() { return q2_; }
    const realms::Quake2Module &module() const { return q2_; }
    bool gameRunning() const { return q2_.gameRunning(); }

    // The renderer's camera as Quake 2 view angles (pitch, yaw, 0), degrees.
    static realms::Vec3 cameraAngles(float renderYaw, float renderPitch);

private:
    void syncPoses();

    MapSession session_;
    realms::Quake2Module q2_;
    std::vector<ModelPose> poses_;
    std::vector<std::string> messages_;
    std::vector<realms::Quake2EffectVertex> fxQuads_;
    std::vector<realms::GameDynamicLight> fxLights_;
};

} // namespace mapview
