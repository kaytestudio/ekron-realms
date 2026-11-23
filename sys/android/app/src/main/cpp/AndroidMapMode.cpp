// Realms map mode on Android (see AndroidMapMode.h).
#include "AndroidMapMode.h"

#include "RealmsAndroid.h"

#include <android/log.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <map>
#include <vector>

namespace {
const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

AndroidMapMode::~AndroidMapMode() {
    // GL objects die with the context; only wait for a running worker.
    if (prepare_.valid()) prepare_.wait();
}

bool AndroidMapMode::open(realms::Engine &engine, const city::GltfRenderer &sandboxCity,
                          const std::string &sandboxCityPath, std::string &error,
                          std::unique_ptr<mapview::Quake2View> retained) {
    close();
    engine.registerCvar("sv_game", "1", REALMS_CVAR_ARCHIVE);
    engine.registerCvar("cl_gun", "1", REALMS_CVAR_ARCHIVE);
    engine.registerCvar("r_lightmaps", "1", REALMS_CVAR_ARCHIVE);
    realms::MapGeometry geometry;
    bool fresh = true;
    if (retained && retained->active() && retained->gameRunning() && retained->session().path() == engine.mapPath()) {
        std::string geometryError;
        if (realms::loadMapGeometry(engine, geometry, geometryError)) {
            view_ = std::move(retained); // the game carries on
            fresh = false;
        } else {
            geometry = {};
        }
    }
    if (retained) retained->close();
    if (fresh) {
        mapview::Quake2ViewOptions options; // librealms_game.so: the app's native library dir
        options.startGame = engine.cvarBool("sv_game", true);
        if (!view_->open(engine, options, geometry, error)) return false;
    }

    const city::Scene &cityScene = sandboxCity.scene();
    // (only without brush models: the sandbox's scene has no per-model batches)
    if (session().mesh() && session().path() == sandboxCityPath && sandboxCity.ready() && cityScene.sourceMapped &&
        cityScene.sourceScale > 0.0f && view_->gltfNodeModels().empty()) {
        reuseCity_ = true;
        cityScale_ = cityScene.sourceScale;
        cityOffset_ = cityScene.sourceOffset;
        view_->disableLightmaps();
        if (fresh) session().summary() += ", textured";
        return true;
    }

    std::vector<sandbox::MeshVertex> vertices;
    std::vector<float> lightUvs;
    const realms::MapTextures &textures = view_->module().textures();
    if (session().mesh() || !uploadMapTextures(textures))
        view_->disableTextures();
    else if (fresh)
        session().summary() += ", " + std::to_string(textures.count()) + " textures";
    view_->triangles(geometry, vertices, ranges_, lightUvs);
    if (session().mesh()) view_->disableSky();
    uploadSky(view_->module().sky());
    if (skyTexture_ && fresh) session().summary() += ", sky " + view_->module().sky().settings().name;
    if (!vertices.empty()) {
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(vertices.size() * sizeof(sandbox::MeshVertex)), vertices.data(),
                     GL_STATIC_DRAW);
        using V = sandbox::MeshVertex;
        const GLsizei stride = sizeof(V);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(offsetof(V, x)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(offsetof(V, r)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(offsetof(V, nx)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(offsetof(V, u)));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(offsetof(V, layer)));
        // BSP lightmaps: atlas coordinates per vertex (attribute 5) and the atlas
        const realms::Quake2Module &q2 = view_->module();
        if (!session().mesh() && q2.lightmapped() && lightUvs.size() == vertices.size() * 2) {
            glGenBuffers(1, &lightUvVbo_);
            glBindBuffer(GL_ARRAY_BUFFER, lightUvVbo_);
            glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(lightUvs.size() * sizeof(float)), lightUvs.data(), GL_STATIC_DRAW);
            glEnableVertexAttribArray(5);
            glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
            glGenTextures(1, &lightmapTexture_);
            glBindTexture(GL_TEXTURE_2D, lightmapTexture_);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, q2.lighting().width(), q2.lighting().height(), 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, q2.lightmap().data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);
            if (fresh) {
                char buf[96];
                std::snprintf(buf, sizeof(buf), ", lightmaps (%zu faces, %dx%d)", q2.lighting().litFaces(),
                              q2.lighting().width(), q2.lighting().height());
                session().summary() += buf;
            }
        } else {
            view_->disableLightmaps();
        }
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        count_ = GLsizei(vertices.size());
    }

    if (session().mesh()) {
        // The textured file in its own space; the worker reads storage and
        // assets directly (never the engine, which belongs to this thread).
        const std::string path = session().path();
        const std::map<int, int> nodeModels = view_->gltfNodeModels(); // doors and platforms move
        prepare_ = std::async(std::launch::async, [path, nodeModels]() -> std::optional<city::Scene> {
            try {
                city::Scene scene;
                city::LoadOptions options;
                options.level = false;
                options.buildPhysics = false;
                options.keepSourceSpace = true;
                options.nodeModels = nodeModels;
                std::string log;
                if (scene.load(path, realms_android::readFile, log, options)) return scene;
                __android_log_print(ANDROID_LOG_WARN, "Realms", "map: %s: no textured model (%s)", path.c_str(),
                                    log.c_str());
            } catch (const std::exception &e) {
                __android_log_print(ANDROID_LOG_WARN, "Realms", "map: %s: %s", path.c_str(), e.what());
            }
            return std::nullopt;
        });
    }
    return true;
}

bool AndroidMapMode::uploadMapTextures(const realms::MapTextures &textures) {
    if (textures.empty()) return false;
    const GLsizei size = textures.layerSize(), count = GLsizei(textures.count());
    int levels = 1;
    while ((size >> levels) > 0) levels++;
    while (glGetError() != GL_NO_ERROR) {
    }
    glGenTextures(1, &mapTextures_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, mapTextures_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // sRGB like the material layers (TextureLoader)
    glTexStorage3D(GL_TEXTURE_2D_ARRAY, levels, GL_SRGB8_ALPHA8, size, size, count);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, size, size, count, GL_RGBA, GL_UNSIGNED_BYTE,
                    textures.pixels().data());
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    const GLenum error = glGetError();
    if (error == GL_NO_ERROR) return true;
    __android_log_print(ANDROID_LOG_WARN, "Realms", "map: %d textures of %d: GL error 0x%x", int(count), int(size),
                        unsigned(error));
    glDeleteTextures(1, &mapTextures_);
    mapTextures_ = 0;
    return false;
}

void AndroidMapMode::uploadSky(const realms::MapSky &sky) {
    if (sky.empty()) return;
    const GLsizei size = sky.size();
    glGenTextures(1, &skyTexture_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, skyTexture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // drawn as is; no mipmaps, they would seam where the faces meet
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, size, size, realms::MapSky::kFaces, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 sky.pixels().data());
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void AndroidMapMode::releaseTriangles() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (lightUvVbo_) glDeleteBuffers(1, &lightUvVbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (lightmapTexture_) glDeleteTextures(1, &lightmapTexture_);
    if (mapTextures_) glDeleteTextures(1, &mapTextures_);
    if (skyTexture_) glDeleteTextures(1, &skyTexture_);
    vbo_ = lightUvVbo_ = vao_ = lightmapTexture_ = mapTextures_ = skyTexture_ = 0;
    count_ = 0;
    ranges_.clear();
}

void AndroidMapMode::releaseGl() {
    if (prepare_.valid()) prepare_.wait();
    prepare_ = {};
    model_.release();
    releaseTriangles();
    gles_.release();
    if (mixer_) sound_.stop(*mixer_);
    reuseCity_ = false;
    cityScale_ = 1.0f;
    cityOffset_ = {};
}

void AndroidMapMode::close() {
    releaseGl();
    view_->close();
}

std::unique_ptr<mapview::Quake2View> AndroidMapMode::releaseView() {
    releaseGl();
    std::unique_ptr<mapview::Quake2View> out = std::move(view_);
    view_ = std::make_unique<mapview::Quake2View>();
    return out;
}

void AndroidMapMode::advance() {
    if (prepare_.valid() && prepare_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        std::optional<city::Scene> scene;
        try {
            scene = prepare_.get();
        } catch (const std::exception &) {
        }
        prepare_ = {};
        if (scene) model_.beginUpload(std::move(*scene));
    }
    if (!model_.uploading()) return;
    std::string log;
    const auto start = std::chrono::steady_clock::now();
    while (model_.uploading() && std::chrono::steady_clock::now() - start < std::chrono::milliseconds(4))
        model_.uploadStep(log);
    if (!log.empty()) __android_log_print(ANDROID_LOG_WARN, "Realms", "%s", log.c_str());
    if (model_.ready()) {
        releaseTriangles(); // the textured model replaces the plain triangles
        view_->disableLightmaps();
        session().summary() += ", textured";
    }
}

void AndroidMapMode::update(realms::Engine &engine, const realms::WalkInput &input, float renderYaw,
                            float renderPitch, float dt, bool attack) {
    view_->update(engine, input, renderYaw, renderPitch, dt, attack);
}

bool AndroidMapMode::respawn(realms::Engine &engine) { return view_->respawn(engine); }
void AndroidMapMode::toggleNoclip() { view_->toggleNoclip(); }
float AndroidMapMode::spawnRenderYaw() const { return view_->spawnRenderYaw(); }

realms::Quake2Module::PlayerStats AndroidMapMode::stats() const { return view_->module().stats(); }

std::vector<std::string> AndroidMapMode::takeMessages() { return view_->takeMessages(); }

physics::Vec3 AndroidMapMode::drawEye() const {
    const physics::Vec3 e = view_->eye();
    return reuseCity_ ? mapview::toLevelScene(e, cityScale_, cityOffset_) : e;
}

// The map's triangles: pass 0 the opaque faces, 1 the translucent ones
// (blended, depth not written).
void AndroidMapMode::drawTriangles(GLuint program, realms::Engine &engine, int pass) {
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "modelPass"), 0);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    // Map faces have a consistent winding, but brush-built maps and glTF
    // slabs can be seen from either side.
    glDisable(GL_CULL_FACE);
    if (lightmapTexture_) {
        realms::Quake2Module &q2 = view_->module();
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, lightmapTexture_);
        if (pass == 0 && q2.updateLightStyles()) { // animated light styles: the atlas again
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, q2.lighting().width(), q2.lighting().height(), GL_RGBA,
                            GL_UNSIGNED_BYTE, q2.lightmap().data());
        }
        glActiveTexture(GL_TEXTURE0);
        glUniform1i(glGetUniformLocation(program, "mapLightmap"), engine.cvarBool("r_lightmaps", true) ? 1 : 0);
    }
    if (mapTextures_) {
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D_ARRAY, mapTextures_);
        glActiveTexture(GL_TEXTURE0);
        glUniform1i(glGetUniformLocation(program, "mapTextured"), 1);
    }
    float pos[mapview::kMaxDynamicLights * 4] = {}, col[mapview::kMaxDynamicLights * 4] = {};
    const int lightCount = int(std::min(lights_.size(), mapview::kMaxDynamicLights));
    for (int i = 0; i < lightCount; i++) {
        const mapview::DynamicLight &l = lights_[size_t(i)];
        const float p[4] = {l.x, l.y, l.z, l.radius}, c[4] = {l.r, l.g, l.b, 1.0f};
        std::copy(p, p + 4, pos + i * 4);
        std::copy(c, c + 4, col + i * 4);
    }
    glUniform1i(glGetUniformLocation(program, "dlightCount"), lightCount);
    glUniform4fv(glGetUniformLocation(program, "dlights"), GLsizei(mapview::kMaxDynamicLights), pos);
    glUniform4fv(glGetUniformLocation(program, "dlightColors"), GLsizei(mapview::kMaxDynamicLights), col);
    const std::array<float, 4> motion = view_->surfaceMotion();
    glUniform4fv(glGetUniformLocation(program, "mapSurface"), 1, motion.data());
    glUniform1i(glGetUniformLocation(program, "mapPass"), pass);
    if (pass == 1) { // R_DrawAlphaSurfaces
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
    }
    const GLint animationLoc = glGetUniformLocation(program, "mapAnimation");
    std::vector<int> layers;
    const auto animate = [&](int model) {
        if (!view_->animationCount()) return;
        view_->animationLayers(model, layers);
        glUniform1iv(animationLoc, GLsizei(std::min<size_t>(layers.size(), mapview::kMaxMapAnimations)), layers.data());
    };
    const GLint transformLoc = glGetUniformLocation(program, "objectTransform");
    glBindVertexArray(vao_);
    if (ranges_.empty()) {
        animate(0);
        glDrawArrays(GL_TRIANGLES, 0, count_);
    } else {
        // brush models where the game has them (doors, platforms), hidden when gone
        const std::vector<mapview::ModelPose> &poses = view_->modelPoses();
        for (const mapview::ModelRange &r : ranges_) {
            const mapview::ModelPose *pose =
                r.model > 0 && size_t(r.model) < poses.size() ? &poses[size_t(r.model)] : nullptr;
            if (pose && !pose->visible) continue;
            glUniformMatrix4fv(transformLoc, 1, GL_FALSE, pose ? pose->transform.data() : kIdentity);
            animate(r.model);
            glDrawArrays(GL_TRIANGLES, r.first, r.count);
        }
        glUniformMatrix4fv(transformLoc, 1, GL_FALSE, kIdentity);
    }
    glBindVertexArray(0);
    glUniform1i(glGetUniformLocation(program, "mapPass"), 0);
    if (pass == 1) {
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }
    glUniform1i(glGetUniformLocation(program, "mapLightmap"), 0);
    glUniform1i(glGetUniformLocation(program, "mapTextured"), 0);
    glUniform1i(glGetUniformLocation(program, "dlightCount"), 0);
}

void AndroidMapMode::draw(GLuint program, const city::GltfRenderer &sandboxCity, realms::Engine &engine,
                          float renderYaw, float renderPitch, float aspect) {
    const physics::Vec3 eye = drawEye();
    if (reuseCity_) {
        sandboxCity.draw(program, eye.x, eye.z);
        return;
    }
    // effects first: their dynamic lights light the world and the entities
    view_->effects(renderYaw, renderPitch, mapview::kMaxDynamicLights, effectVertices_, lights_);
    if (model_.ready()) {
        const city::GltfRenderer::ModelTransform moved = [this](int model, std::array<float, 16> &out) {
            return view_->placedTransform(model, out);
        };
        model_.draw(program, eye.x, eye.z, false, &moved);
    } else if (count_) {
        drawTriangles(program, engine, 0);
    }
    // translucent faces (water, glass) after the entities, before the effects
    const bool translucent = !model_.ready() && count_ && view_->hasTranslucentFaces();
    if (!view_->gameRunning()) {
        if (translucent) drawTriangles(program, engine, 1);
        return;
    }
    Quake2Gles::Camera camera;
    camera.eye = eye;
    camera.yaw = renderYaw;
    camera.pitch = renderPitch;
    camera.aspect = aspect;
    view_->entities(engine, renderYaw, renderPitch, engine.cvarBool("cl_gun", true), entityVertices_, entityBatches_);
    gles_.drawEntities(engine, entityVertices_, entityBatches_, lights_, camera);
    if (translucent) {
        glUseProgram(program);
        drawTriangles(program, engine, 1);
    }
    gles_.drawEffects(effectVertices_, camera);
    glUseProgram(program);
}

void AndroidMapMode::playSounds(realms::Engine &engine, ekron::audio::Mixer &mixer, double dt, bool paused) {
    mixer_ = &mixer;
    realms::Quake2Module &q2 = view_->module();
    std::vector<realms::GameEvent> sounds = q2.takeSounds();
    if (paused || !q2.gameRunning()) {
        sound_.stop(mixer);
        return;
    }
    sound_.update(engine, mixer, sounds, q2.loopSounds(), q2.listener(), dt);
}
