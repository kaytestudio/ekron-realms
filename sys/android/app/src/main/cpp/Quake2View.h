// Map mode on top of Quake2Module, shared by desktop, Android and iOS.
// Converts everything to render space: metres, Y up,
// render = (x, z, -y) / unitsPerMetre. Camera yaw/pitch in radians; yaw 0
// looks down -Z, positive pitch looks up. Without the game module the map is
// walked with the session's MapWalker.
#pragma once

#include "MapSession.h"

#include "realms/Quake2Module.h"

#include <array>
#include <map>
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
    // The same pose for the model's triangles where the map file has them
    // (the textured glTF): differs from `transform` for glTF brush models
    // stored relative to an "origin" (func_rotating, func_door_rotating).
    std::array<float, 16> placed{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    int frame = 0; // the entity's frame: its texture animation frame
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

// Quake 2 surface effects and texture animation ride above that layer
// (bits 0..11, at most mapTextureLayer(255, 7)): bits 12..15 hold the
// kMapEffect* bits of the face's texinfo flags, bits 16..21 its animation
// slot + 1. The shaders draw a slot's current layer instead
// (Quake2View::animationLayers, per BSP model), warp and scroll the
// coordinates with mapSurfaceMotion, draw warp and translucent faces
// fullbright like ref_gl, and draw translucent faces in a second, blended
// pass after the entities (R_DrawAlphaSurfaces).
constexpr int kMapLayerMask = 0xFFF;
constexpr int kMapEffectShift = 12;
constexpr int kMapEffectWarp = 1;    // SURF_WARP: water, slime, lava (EmitWaterPolys)
constexpr int kMapEffectFlowing = 2; // SURF_FLOWING: scrolls towards -s
constexpr int kMapEffectTrans33 = 4; // SURF_TRANS33: alpha 0.33
constexpr int kMapEffectTrans66 = 8; // SURF_TRANS66: alpha 0.66
constexpr int kMapAnimationShift = 16;
constexpr size_t kMaxMapAnimations = 32;
// kMapEffect* bits of REALMS_SURF_* flags.
int mapEffects(int32_t surfaceFlags);
// The parts of a vertex layer, as the shaders take them apart.
struct MapLayerParts {
    int layer = 0;     // mapTextureLayer or a material class
    int effects = 0;   // kMapEffect*
    int animation = 0; // slot + 1, 0 = none
};
MapLayerParts mapLayerParts(float layer);
// The shaders' surface motion at `seconds` since the map opened:
//   x  warp phase, radians in [0, 2 pi): with (u, v) in texels / 64 the warp
//      samples (u, v) + 0.125 * sin(8 * (v, u) + x) (r_turbsin, TURBSCALE);
//   y  SURF_FLOWING scroll added to u: -64 * frac(time / 40) texture
//      widths (DrawGLFlowingPoly), wrapped to [0, 1);
//   z  the same for flowing warp faces: -frac(time / 2), wrapped;
//   w  0.
std::array<float, 4> mapSurfaceMotion(double seconds);

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
    // mapTextureLayer; surface effects and animation slots are packed above
    // it (kMapEffectShift). Warp faces keep coordinates in texels / 64, as
    // Quake 2 draws them whatever the texture size.
    void triangles(const realms::MapGeometry &geometry, std::vector<sandbox::MeshVertex> &vertices,
                   std::vector<ModelRange> &ranges, std::vector<float> &lightUvs);
    // Animation slots of the last triangles() (at most kMaxMapAnimations),
    // and the layer each one shows now on BSP model `model`'s faces: the
    // world steps 2 frames a second, brush models follow their entity's
    // frame (R_TextureAnimation). Empty without animated textured faces.
    size_t animationCount() const { return animations_.size(); }
    void animationLayers(int model, std::vector<int> &layers) const;
    // mapSurfaceMotion for the time update() has run since open().
    std::array<float, 4> surfaceMotion() const { return mapSurfaceMotion(skyTime_); }
    // The last triangles() had translucent faces (a second pass is needed).
    bool hasTranslucentFaces() const { return translucent_; }
    // The platform can't draw lightmaps for this map (textured model...).
    void disableLightmaps() { q2_.disableLightmaps(); }
    // ... or the map's textures (call before triangles()).
    void disableTextures() { q2_.disableTextures(); }
    // ... or its skybox.
    void disableSky() { q2_.disableSky(); }
    // The skybox's direction matrix (MapSky::renderToSky) at the time
    // update() has run since open().
    std::array<float, 9> skyMatrix() const { return q2_.sky().renderToSky(skyTime_); }

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
    // Transform for an inline model drawn from the textured glTF (the batch
    // model of city::Scene): identity for the world, without the game or at
    // rest; false while the game hides the entity.
    bool placedTransform(int model, std::array<float, 16> &out) const;
    // glTF maps: node index -> inline model of each brush entity node, for
    // city::LoadOptions::nodeModels. Empty for BSP maps.
    const std::map<int, int> &gltfNodeModels() const { return nodeModels_; }
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

    // The camera as Quake 2 view angles (pitch, yaw, roll), degrees: the
    // player's look (renderYaw / renderPitch) with the game's view kick, or
    // its death view (Quake2Module::cameraAngles).
    realms::Vec3 cameraAngles(float renderYaw, float renderPitch) const;
    // The same in the renderer's convention (radians; roll turns the image
    // like the drone camera's): what the world, the entities, the effects
    // and the sky are drawn with. The player's look itself without a game.
    struct DrawAngles {
        float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    };
    DrawAngles drawAngles(float renderYaw, float renderPitch) const;

private:
    void syncPoses();

    MapSession session_;
    realms::Quake2Module q2_;
    std::vector<ModelPose> poses_;
    std::vector<std::array<float, 3>> modelOrigins_; // glTF inline model origins, render space (index = model)
    std::map<int, int> nodeModels_;
    std::vector<std::vector<int>> animations_; // per slot: the layer of each frame
    bool translucent_ = false;
    std::vector<std::string> messages_;
    double skyTime_ = 0.0;
    std::vector<realms::Quake2EffectVertex> fxQuads_;
    std::vector<realms::GameDynamicLight> fxLights_;
};

} // namespace mapview
