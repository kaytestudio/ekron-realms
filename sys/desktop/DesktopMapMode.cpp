// Realms map mode for the desktop client (see DesktopMapMode.h).
#include "DesktopMapMode.h"

#include "realms/MapGeometry.h"

#include <cstdio>
#include <vector>

namespace ekron::desktop {

bool DesktopMapMode::open(realms::Engine &engine, ekron::gl::Api &gl, DesktopRenderer &renderer, std::string &error,
                          const std::string &executableDir) {
    close(gl, renderer);
    engine.registerCvar("r_lightmaps", "1", REALMS_CVAR_ARCHIVE);
    engine.registerCvar("sv_game", "1", REALMS_CVAR_ARCHIVE);
    engine.registerCvar("cl_gun", "1", REALMS_CVAR_ARCHIVE);
    engine.registerCvar("gl_polyblend", "1", REALMS_CVAR_ARCHIVE);
    // The map, its lightmaps and (sv_game) the Quake 2 game.
    mapview::Quake2ViewOptions options;
    options.executableDir = executableDir;
    options.startGame = engine.cvarBool("sv_game", true);
    realms::MapGeometry geometry;
    if (!view_.open(engine, options, geometry, error)) return false;
    mapview::MapSession &session = view_.session();

    // glTF maps: draw the textured file itself, in its own space.
    if (session.mesh()) {
        auto model = std::make_unique<DesktopGltfModel>();
        const realms::Engine &reader = engine;
        city::LoadOptions loadOptions;
        loadOptions.level = false;
        loadOptions.buildPhysics = false;
        loadOptions.keepSourceSpace = true;
        loadOptions.nodeModels = view_.gltfNodeModels(); // doors and platforms move
        std::string log;
        if (model->loadWithReader(engine.mapPath(),
                                  [&reader](const std::string &p, std::vector<unsigned char> &out) {
                                      return reader.loadFile(p, out);
                                  },
                                  log, loadOptions)) {
            model->upload(gl);
            if (model->ready()) model_ = std::move(model);
        }
        if (!model_) engine.print("map: " + engine.mapPath() + ": no textured model, drawing collision triangles\n");
    }

    // Vertices grouped by BSP model (world first) so brush entities can move,
    // with the lightmap atlas coordinates in the same order.
    std::vector<sandbox::MeshVertex> vertices;
    std::vector<mapview::ModelRange> ranges;
    std::vector<float> lightUvs;
    const realms::MapTextures &textures = quake2().textures();
    if (model_ || textures.empty() ||
        !renderer.setMapTextures(gl, textures.layerSize(), int(textures.count()), textures.pixels()))
        view_.disableTextures();
    else
        session.summary() += ", " + std::to_string(textures.count()) + " textures";
    if (!model_) view_.triangles(geometry, vertices, ranges, lightUvs);
    const realms::MapSky &sky = quake2().sky();
    if (!sky.empty()) { // BSP sky faces, and the background of every map
        renderer.setMapSky(gl, sky.size(), sky.pixels());
        session.summary() += ", sky " + sky.settings().name;
    } else {
        view_.disableSky();
    }
    std::vector<DesktopRenderer::MapDrawRange> drawRanges;
    modelCount_ = 1;
    for (const mapview::ModelRange &r : ranges) {
        drawRanges.push_back({r.model, GLint(r.first), GLsizei(r.count)});
        modelCount_ = std::max(modelCount_, size_t(std::max(r.model, 0)) + 1);
    }
    renderer.setMapGeometry(gl, vertices, drawRanges);
    if (model_) session.summary() += ", textured";
    const realms::MapLighting &lighting = quake2().lighting();
    if (quake2().lightmapped() && !lightUvs.empty() && lightUvs.size() == vertices.size() * 2) {
        renderer.setMapLightmap(gl, lightUvs, lighting.width(), lighting.height(), quake2().lightmap());
        char buf[96];
        std::snprintf(buf, sizeof(buf), ", lightmaps (%zu faces, %dx%d)", lighting.litFaces(), lighting.width(),
                      lighting.height());
        session.summary() += buf;
    } else {
        view_.disableLightmaps();
    }
    syncPoses();
    return true;
}

void DesktopMapMode::close(ekron::gl::Api &gl, DesktopRenderer &renderer) {
    poses_.clear();
    surfaces_ = {};
    modelCount_ = 1;
    renderer.clearMapEffects();
    releaseSkins(gl);
    renderer.clearMapEntities();
    if (model_) {
        model_->release(gl);
        model_.reset();
    }
    if (view_.active()) renderer.clearMapGeometry(gl);
    view_.close();
}

void DesktopMapMode::updateLighting(realms::Engine &engine, ekron::gl::Api &gl, DesktopRenderer &renderer) {
    renderer.setMapLightmapEnabled(engine.cvarBool("r_lightmaps", true));
    if (quake2().updateLightStyles()) renderer.updateMapLightmap(gl, quake2().lightmap());
}

void DesktopMapMode::update(realms::Engine &engine, const realms::WalkInput &input, float renderYaw,
                            float renderPitch, float dt, bool attack) {
    view_.update(engine, input, renderYaw, renderPitch, dt, attack);
    syncPoses();
}

void DesktopMapMode::syncPoses() {
    const std::vector<mapview::ModelPose> &poses = view_.modelPoses();
    poses_.resize(poses.size());
    for (size_t m = 0; m < poses.size(); m++) poses_[m] = {poses[m].visible, poses[m].transform, poses[m].placed};
    // surface effects: motion, and the animation frames of every model's faces
    surfaces_.motion = view_.surfaceMotion();
    surfaces_.translucent = view_.hasTranslucentFaces();
    surfaces_.animations.clear();
    if (view_.animationCount()) {
        surfaces_.animations.resize(std::max(modelCount_, poses.size()));
        std::vector<int> layers;
        for (size_t m = 0; m < surfaces_.animations.size(); m++) {
            view_.animationLayers(int(m), layers);
            surfaces_.animations[m].assign(layers.begin(), layers.end());
        }
    }
}

void DesktopMapMode::buildEffects(float renderYaw, float renderPitch,
                                  std::vector<DesktopRenderer::EffectVertex> &vertices,
                                  std::vector<DesktopRenderer::DynamicLight> &lights) {
    view_.effects(renderYaw, renderPitch, DesktopRenderer::kMaxMapLights, effectVertices_, lights_);
    vertices.clear();
    lights.clear();
    vertices.reserve(effectVertices_.size());
    for (const mapview::EffectVertex &q : effectVertices_)
        vertices.push_back({q.x, q.y, q.z, q.r, q.g, q.b, q.a, q.u, q.v, q.shape});
    for (const mapview::DynamicLight &l : lights_) lights.push_back({l.x, l.y, l.z, l.radius, l.r, l.g, l.b});
}

void DesktopMapMode::releaseSkins(ekron::gl::Api &gl) {
    for (auto &kv : skins_)
        if (kv.second) gl.DeleteTextures(1, &kv.second);
    skins_.clear();
}

GLuint DesktopMapMode::skinTexture(realms::Engine &engine, ekron::gl::Api &gl, const std::string &name) {
    if (name.empty()) return 0;
    const auto it = skins_.find(name);
    if (it != skins_.end()) return it->second;
    GLuint tex = 0;
    realms::RgbaImage img;
    std::string error;
    if (realms::GameModelCache::loadSkin(engine, name, img, error)) {
        gl.GenTextures(1, &tex);
        gl.BindTexture(GL_TEXTURE_2D, tex);
        gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_SRGB8_ALPHA8), img.width, img.height, 0, GL_RGBA,
                      GL_UNSIGNED_BYTE, img.rgba.data());
        gl.GenerateMipmap(GL_TEXTURE_2D);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_LINEAR_MIPMAP_LINEAR));
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_LINEAR));
        // repeat: .md3 texture coordinates may run past 0..1, as in Quake 3
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_REPEAT));
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_REPEAT));
        gl.BindTexture(GL_TEXTURE_2D, 0);
    } else {
        engine.print("map: skin " + name + ": " + error + "\n", REALMS_PRINT_DEVELOPER);
    }
    skins_[name] = tex;
    return tex;
}

void DesktopMapMode::buildEntities(realms::Engine &engine, ekron::gl::Api &gl, float renderYaw, float renderPitch,
                                   std::vector<DesktopRenderer::EntityVertex> &vertices,
                                   std::vector<DesktopRenderer::EntityBatch> &batches) {
    view_.entities(engine, renderYaw, renderPitch, engine.cvarBool("cl_gun", true), entityVertices_, entityBatches_);
    vertices.clear();
    batches.clear();
    vertices.reserve(entityVertices_.size());
    for (const mapview::EntityVertex &v : entityVertices_) vertices.push_back({v.x, v.y, v.z, v.nx, v.ny, v.nz, v.u, v.v});
    batches.reserve(entityBatches_.size());
    for (const mapview::EntityBatch &b : entityBatches_) {
        DesktopRenderer::EntityBatch out;
        out.first = b.first;
        out.count = b.count;
        out.texture = skinTexture(engine, gl, b.skin);
        out.color = {b.color[0], b.color[1], b.color[2], b.color[3]};
        // untextured models (skin missing) get a neutral grey, boxes their colour
        if (!out.texture && !b.fallbackBox) out.color = {0.7f * b.color[0], 0.7f * b.color[1], 0.72f * b.color[2], b.color[3]};
        out.transform = b.transform;
        out.viewWeapon = b.viewWeapon;
        out.fullbright = b.fullbright;
        out.lit = b.lit;
        out.light = b.light;
        batches.push_back(out);
    }
}

bool DesktopMapMode::respawn(realms::Engine &engine) {
    const bool ok = view_.respawn(engine);
    syncPoses();
    return ok;
}

void DesktopMapMode::toggleNoclip() { view_.toggleNoclip(); }
bool DesktopMapMode::noclip() const { return view_.noclip(); }
physics::Vec3 DesktopMapMode::eye() const { return view_.eye(); }
float DesktopMapMode::spawnRenderYaw() const { return view_.spawnRenderYaw(); }

} // namespace ekron::desktop
