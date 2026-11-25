#pragma once

#include "GlLoader.h"
#include "DesktopFont.h"
#include "GltfScene.h"
#include "MeshGeometry.h"
#include "realms/ViewFov.h"
#define STBI_NO_STDIO
#include "third_party/stb_image.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ekron::desktop {

inline bool decodeImageBytes(const unsigned char *data,
                             size_t size,
                             int &outW,
                             int &outH,
                             std::vector<unsigned char> &outRgba) {
    if (!data || size == 0) return false;
    int w = 0, h = 0, comp = 0;
    stbi_uc *pixels = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &comp, 4);
    if (!pixels || w <= 0 || h <= 0) {
        if (pixels) stbi_image_free(pixels);
        return false;
    }
    outW = w;
    outH = h;
    outRgba.assign(pixels, pixels + size_t(w) * size_t(h) * 4);
    stbi_image_free(pixels);
    return true;
}

struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

struct UploadedGlMesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLsizei count = 0;
    int material = -1;
    int model = 0; // city::Batch::model (glTF map inline model)
    float x = 0.0f;
    float z = 0.0f;
};

class DesktopGltfModel {
public:
    bool loadFromDisk(const std::string &assetRoot,
                      const std::string &relativePath,
                      std::string &logOut,
                      city::LoadOptions options = {});
    // Loads through a reader (e.g. the Realms PAK filesystem).
    bool loadWithReader(const std::string &path,
                        const city::ReadFile &read,
                        std::string &logOut,
                        city::LoadOptions options = {});

    void upload(ekron::gl::Api &gl);
    void release(ekron::gl::Api &gl);

    bool valid() const { return loaded_; }
    bool ready() const { return uploaded_ && !meshes_.empty(); }
    const city::Scene &scene() const { return scene_; }
    const std::vector<UploadedGlMesh> &meshes() const { return meshes_; }
    const std::vector<GLuint> &textures() const { return textures_; }

private:
    city::Scene scene_;
    bool loaded_ = false;
    bool uploaded_ = false;
    std::vector<UploadedGlMesh> meshes_;
    std::vector<GLuint> textures_;
};

class DesktopRenderer {
public:
    bool init(ekron::gl::Api &gl, const std::string &assetRoot, std::string &errorOut);
    void shutdown(ekron::gl::Api &gl);

    GLuint loadCatalogIcon(ekron::gl::Api &gl, const std::string &assetRoot, const std::string &iconFile);

    void renderMonitorPass(ekron::gl::Api &gl,
                           const std::vector<sandbox::MeshVertex> &worldVertices,
                           const DesktopGltfModel &cityModel,
                           const DesktopGltfModel &carModel,
                           const std::vector<std::array<float, 16>> &carTransforms,
                           physics::Vec3 eye,
                           float yaw,
                           float pitch,
                           float roll);

    void renderMainWorld(ekron::gl::Api &gl,
                         int drawableW,
                         int drawableH,
                         const std::vector<sandbox::MeshVertex> &worldVertices,
                         const std::vector<sandbox::MeshVertex> &ghostVertices,
                         const DesktopGltfModel &cityModel,
                         const DesktopGltfModel &carModel,
                         const std::vector<std::array<float, 16>> &carTransforms,
                         const std::array<float, 16> *ghostCarTransform,
                         const DesktopGltfModel *droneModel,
                         const std::array<float, 16> *droneTransform,
                         const DesktopGltfModel *remoteViewmodel,
                         bool showMonitorOverlay,
                         physics::Vec3 eye,
                         float yaw,
                         float pitch,
                         float roll);

    // Realms map mode: static map geometry (render space: metres, Y up) and
    // an optional glTF map model drawn in its source space. `ranges` splits
    // the vertices by BSP model (0 world, n inline model "*n"); inline models
    // are then drawn with the poses from setMapModelPoses (the game server's
    // doors, platforms, trains...). Without ranges everything is static.
    struct MapDrawRange {
        int32_t model = 0;
        GLint first = 0;
        GLsizei count = 0;
    };
    struct MapModelPose {
        bool visible = true;
        std::array<float, 16> transform{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; // render space, column-major
        // for the glTF map model's batches of this model (mapview::ModelPose::placed)
        std::array<float, 16> placed{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    };
    void setMapGeometry(ekron::gl::Api &gl, const std::vector<sandbox::MeshVertex> &vertices,
                        std::vector<MapDrawRange> ranges = {});
    // Indexed by model number; empty = every model static and visible.
    void setMapModelPoses(std::vector<MapModelPose> poses) { mapPoses_ = std::move(poses); }
    // Quake 2 surface effects of the map geometry (mapview::kMapEffectShift):
    // the surface motion (mapview::mapSurfaceMotion), the current layer of
    // each animation slot per model (mapview::Quake2View::animationLayers;
    // empty = no animated faces) and whether there are translucent faces.
    struct MapSurfaces {
        std::array<float, 4> motion{};
        std::vector<std::vector<int32_t>> animations; // [model][slot]
        bool translucent = false;
    };
    void setMapSurfaces(MapSurfaces surfaces) { mapSurfaces_ = std::move(surfaces); }
    void clearMapGeometry(ekron::gl::Api &gl);
    // Quake 2 lightmaps (realms::MapLighting) for the map set with
    // setMapGeometry: atlas coordinates per vertex (u, v; u < 0 = no
    // lightmap: that face keeps the sandbox sun), and the RGBA8 atlas.
    // Lightmapped faces are lit by it (and the dynamic lights) instead of
    // the sun; updateMapLightmap re-uploads it when light styles change.
    void setMapLightmap(ekron::gl::Api &gl, const std::vector<float> &uvs, int width, int height,
                        const std::vector<uint8_t> &rgba);
    void updateMapLightmap(ekron::gl::Api &gl, const std::vector<uint8_t> &rgba);
    void setMapLightmapEnabled(bool on) { lightmapEnabled_ = on; }
    bool mapLightmapped() const { return lightmapTex_ != 0 && lightmapEnabled_; }
    // The map's own textures (realms::MapTextures::pixels(): `count` RGBA
    // layers of size x size) for faces with mapview::mapTextureLayer;
    // false when they can't be uploaded (those faces fall back to the
    // material layers). Kept until clearMapGeometry.
    bool setMapTextures(ekron::gl::Api &gl, int size, int count, const std::vector<uint8_t> &rgba);
    bool mapTextured() const { return mapTexturesTex_ != 0; }
    // The Quake 2 skybox (realms::MapSky::pixels(): six RGBA layers of size
    // x size) drawn by renderMapWorld instead of the sky gradient, turned by
    // setMapSkyMatrix (MapSky::renderToSky). Kept until clearMapGeometry.
    void setMapSky(ekron::gl::Api &gl, int size, const std::vector<uint8_t> &rgba);
    void setMapSkyMatrix(const std::array<float, 9> &m) { skyMatrix_ = m; }
    bool mapSky() const { return skyTex_ != 0; }

    // Game entities for this frame (.md3 models, fallback boxes),
    // drawn by renderMapWorld after the map with the textured model shader.
    // Vertices are in each batch's model space; `transform` (render space,
    // column-major) places them. `viewWeapon` batches are drawn last over a
    // cleared depth buffer, like Quake 2's RF_DEPTHHACK gun.
    struct EntityVertex {
        float x, y, z, nx, ny, nz, u, v;
    };
    struct EntityBatch {
        GLint first = 0;
        GLsizei count = 0;
        GLuint texture = 0; // 0: untextured (color only)
        Color color{};
        std::array<float, 16> transform{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        bool viewWeapon = false;
        bool fullbright = false; // RF_FULLBRIGHT: not lit
        // Lit by the map's lightmaps (R_LightPoint, 0..~2 per channel)
        // instead of the sun.
        bool lit = false;
        std::array<float, 3> light{1, 1, 1};
    };
    void setMapEntities(ekron::gl::Api &gl, const std::vector<EntityVertex> &vertices, std::vector<EntityBatch> batches);
    void clearMapEntities() { entityBatches_.clear(); }
    size_t mapEntityBatches() const { return entityBatches_.size(); }

    // Quake 2 client effects for this frame: particle / glow / beam quads
    // (render space, two triangles each; shape 0 round with uv -1..1, 1
    // solid), drawn blended after the entities and before the view weapon,
    // and dynamic lights (render space, metres) that light the map and the
    // entities - the first kMaxMapLights.
    struct EffectVertex {
        float x, y, z, r, g, b, a, u, v, shape;
    };
    struct DynamicLight {
        float x = 0, y = 0, z = 0, radius = 0, r = 1, g = 1, b = 1;
    };
    static constexpr size_t kMaxMapLights = 32; // MAX_DLIGHTS; the shader arrays match
    void setMapEffects(ekron::gl::Api &gl, const std::vector<EffectVertex> &vertices, std::vector<DynamicLight> lights);
    void clearMapEffects() {
        effectCount_ = 0;
        mapLights_.clear();
    }
    size_t mapEffectVertices() const { return size_t(effectCount_); }
    size_t mapLights() const { return mapLights_.size(); }

    void renderMapWorld(ekron::gl::Api &gl,
                        int drawableW,
                        int drawableH,
                        const DesktopGltfModel *mapModel,
                        physics::Vec3 eye,
                        float yaw,
                        float pitch,
                        float roll,
                        float focal); // realms::mapFocalLength(fov)

    // 2D UI layer (coordinates in logical window pixels [0..winW] x [0..winH], top-left origin)
    void beginUi(int logicalW, int logicalH);
    void drawRect(float x, float y, float w, float h, Color color);
    void drawBorder(float x, float y, float w, float h, float thickness, Color color);
    void drawText(float x, float y, std::string_view utf8, float scale, Color color);
    void drawCenteredText(float cx, float cy, std::string_view utf8, float scale, Color color);
    void drawIcon(ekron::gl::Api &gl, GLuint tex, float x, float y, float w, float h, Color tint = {1, 1, 1, 1});
    void flushUi(ekron::gl::Api &gl, int drawableW, int drawableH);

private:
    // The 3D passes' focal length: the sandbox's, or map mode's from the fov.
    float focal_ = realms::kSandboxFocalLength;

    struct UiVertex {
        float x, y;
        float u, v;
        float r, g, b, a;
        float mode; // 0 = solid color, 1 = font alpha, 2 = RGBA icon
    };

    struct UiDrawCmd {
        GLuint texture = 0;
        GLint first = 0;
        GLsizei count = 0;
    };

    void drawWorldScene(ekron::gl::Api &gl,
                        int viewW,
                        int viewH,
                        const std::vector<sandbox::MeshVertex> &worldVertices,
                        const DesktopGltfModel &cityModel,
                        const DesktopGltfModel &carModel,
                        const std::vector<std::array<float, 16>> &carTransforms,
                        physics::Vec3 eye,
                        float yaw,
                        float pitch,
                        float roll);

    // `poses` (glTF maps): batches of inline model n > 0 are drawn with
    // poses[n].placed after `transform`, and skipped while hidden.
    void drawGltfModel(ekron::gl::Api &gl,
                       const DesktopGltfModel &model,
                       const std::array<float, 16> &transform,
                       float eyeX,
                       float eyeZ,
                       bool hologram,
                       const std::vector<MapModelPose> *poses = nullptr);

    GLuint compileProgram(ekron::gl::Api &gl, const char *vsSrc, const char *fsSrc, std::string &errOut);
    void applyMapLights(ekron::gl::Api &gl, GLuint prog) const;
    void drawMapEffects(ekron::gl::Api &gl, physics::Vec3 eye, float yaw, float pitch, float roll, float aspect);
    // The map geometry: pass 0 the opaque faces, 1 the translucent ones (blended).
    void drawMapTriangles(ekron::gl::Api &gl, physics::Vec3 eye, float yaw, float pitch, float roll, float aspect,
                          int pass);
    bool loadMaterialArray(ekron::gl::Api &gl, const std::string &assetRoot);

    GLuint worldProg_ = 0;
    GLuint modelProg_ = 0;
    GLuint skyProg_ = 0;
    GLuint pipProg_ = 0;
    GLuint uiProg_ = 0;

    GLuint emptyVao_ = 0;
    GLuint worldVao_ = 0;
    GLuint worldVbo_ = 0;
    GLuint ghostVao_ = 0;
    GLuint ghostVbo_ = 0;
    GLuint uiVao_ = 0;
    GLuint uiVbo_ = 0;
    GLuint mapVao_ = 0;
    GLuint mapVbo_ = 0;
    GLsizei mapCount_ = 0;
    GLuint mapLightVbo_ = 0; // attribute 5 of mapVao_: lightmap coordinates
    GLuint lightmapTex_ = 0;
    int lightmapW_ = 0, lightmapH_ = 0;
    GLuint mapTexturesTex_ = 0; // unit 3: the map's own textures
    void releaseMapTextures(ekron::gl::Api &gl);
    GLuint skyTex_ = 0; // unit 4 (sky program): the skybox
    std::array<float, 9> skyMatrix_{1, 0, 0, 0, 1, 0, 0, 0, 1};
    void releaseMapSky(ekron::gl::Api &gl);
    bool lightmapEnabled_ = true;
    void uploadMapLightUvs(ekron::gl::Api &gl, const std::vector<float> &uvs);
    std::vector<MapDrawRange> mapRanges_;
    std::vector<MapModelPose> mapPoses_;
    MapSurfaces mapSurfaces_;
    GLuint entityVao_ = 0;
    GLuint entityVbo_ = 0;
    std::vector<EntityBatch> entityBatches_;
    GLuint effectProg_ = 0;
    GLuint effectVao_ = 0;
    GLuint effectVbo_ = 0;
    GLsizei effectCount_ = 0;
    std::vector<DynamicLight> mapLights_;

    GLuint whiteTex_ = 0;
    GLuint materialArrayTex_ = 0;
    bool texturesReady_ = false;
    GLuint fontTex_ = 0;

    GLuint monitorFbo_ = 0;
    GLuint monitorColorTex_ = 0;
    GLuint monitorDepthRbo_ = 0;
    bool monitorReady_ = false;

    int uiLogicalW_ = 1280;
    int uiLogicalH_ = 720;
    std::vector<UiVertex> uiVertices_;
    std::vector<UiDrawCmd> uiCmds_;
    std::unordered_map<std::string, GLuint> iconCache_;
};

} // namespace ekron::desktop
