#pragma once

#include "GlLoader.h"
#include "DesktopFont.h"
#include "GltfScene.h"
#include "MeshGeometry.h"
#define STBI_NO_STDIO
#include "third_party/stb_image.h"

#include <array>
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
    // an optional glTF map model drawn in its source space.
    void setMapGeometry(ekron::gl::Api &gl, const std::vector<sandbox::MeshVertex> &vertices);
    void clearMapGeometry(ekron::gl::Api &gl);
    void renderMapWorld(ekron::gl::Api &gl,
                        int drawableW,
                        int drawableH,
                        const DesktopGltfModel *mapModel,
                        physics::Vec3 eye,
                        float yaw,
                        float pitch,
                        float roll);

    // 2D UI layer (coordinates in logical window pixels [0..winW] x [0..winH], top-left origin)
    void beginUi(int logicalW, int logicalH);
    void drawRect(float x, float y, float w, float h, Color color);
    void drawBorder(float x, float y, float w, float h, float thickness, Color color);
    void drawText(float x, float y, std::string_view utf8, float scale, Color color);
    void drawCenteredText(float cx, float cy, std::string_view utf8, float scale, Color color);
    void drawIcon(ekron::gl::Api &gl, GLuint tex, float x, float y, float w, float h, Color tint = {1, 1, 1, 1});
    void flushUi(ekron::gl::Api &gl, int drawableW, int drawableH);

private:
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

    void drawGltfModel(ekron::gl::Api &gl,
                       const DesktopGltfModel &model,
                       const std::array<float, 16> &transform,
                       float eyeX,
                       float eyeZ,
                       bool hologram);

    GLuint compileProgram(ekron::gl::Api &gl, const char *vsSrc, const char *fsSrc, std::string &errOut);
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
