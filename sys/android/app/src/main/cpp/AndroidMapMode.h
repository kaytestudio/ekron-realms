// "map <name>" on Android: a Quake 2 BSP or glTF map drawn with the
// sandbox's GLES program, on top of mapview::Quake2View. Runs the game when
// librealms_game.so is in the APK and the map has a worldspawn; otherwise
// the map is walked.
//
// When the map is the sandbox's own city file the already uploaded model is
// reused (level-mode scene, see Scene::sourceScale/sourceOffset). Other glTF
// maps are prepared on a worker thread; until then, and for BSPs, the map's
// own triangles are drawn, grouped by BSP model and lightmapped.
#pragma once

#include "GameSoundPlayer.h"
#include "GltfRenderer.h"
#include "Quake2Gles.h"
#include "Quake2View.h"
#include "realms/GameHudCanvas.h"

#include <GLES3/gl3.h>

#include <array>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class AndroidMapMode {
public:
    AndroidMapMode() = default;
    AndroidMapMode(const AndroidMapMode &) = delete;
    AndroidMapMode &operator=(const AndroidMapMode &) = delete;
    ~AndroidMapMode();

    // Opens the engine's loaded map (GL context current). `sandboxCity` and
    // its asset path let the sandbox city model be reused. `retained` is a
    // view kept from the previous Renderer (releaseView): when it is still
    // playing this map its game carries on instead of restarting.
    bool open(realms::Engine &engine, const city::GltfRenderer &sandboxCity, const std::string &sandboxCityPath,
              std::string &error, std::unique_ptr<mapview::Quake2View> retained = nullptr);
    void close(); // GL context current
    // The map and its running game, kept while the GL context goes away (the
    // GL objects are released here). Inactive afterwards.
    std::unique_ptr<mapview::Quake2View> releaseView();
    bool active() const { return view_ && view_->active(); }
    // The textured model is still being prepared / uploaded.
    bool texturedPending() const { return prepare_.valid() || model_.uploading(); }
    // The skybox for the sky pass (0 without one) and its direction matrix.
    GLuint skyTexture() const { return skyTexture_; }
    std::array<float, 9> skyMatrix() const { return view_ ? view_->skyMatrix() : std::array<float, 9>{}; }

    // Continues preparing / uploading the textured model (GL context current).
    void advance();
    // One frame of the game (attack fires) or the walker.
    void update(realms::Engine &engine, const realms::WalkInput &input, float renderYaw, float renderPitch, float dt,
                bool attack);
    bool respawn(realms::Engine &engine);
    void toggleNoclip();
    float spawnRenderYaw() const;

    // Camera eye in the space draw() renders in (call before setting the
    // shader's eye uniform).
    physics::Vec3 drawEye() const;
    // Draws the map with `program` bound and the per-frame uniforms set,
    // then the game's entities and effects. The vertex-colour pass expects
    // the sandbox albedo array on unit 0.
    void draw(GLuint program, const city::GltfRenderer &sandboxCity, realms::Engine &engine, float renderYaw,
              float renderPitch, float aspect);
    // The focal length map mode draws with (the "focal" uniform): from the
    // game's fov, or the fov cvar without a game (realms/ViewFov.h).
    // The camera the map is drawn with: the player's look plus the game's
    // view kick, or its death view (mapview::Quake2View::drawAngles).
    mapview::Quake2View::DrawAngles drawAngles(float renderYaw, float renderPitch) const {
        if (!view_) return {renderYaw, renderPitch, 0.0f};
        return view_->drawAngles(renderYaw, renderPitch);
    }
    float focalLength(const realms::Engine &engine) const {
        return realms::mapFocalLength(view_ ? view_->module().viewFov(engine) : 90.0f);
    }
    // The game's sounds on the sandbox mixer (silent while paused).
    void playSounds(realms::Engine &engine, ekron::audio::Mixer &mixer, double dt, bool paused);

    bool gameRunning() const { return view_ && view_->gameRunning(); }
    // Quake 2's 2D HUD (status bar, help computer / scores, inventory) as one
    // texture of width / scale x height / scale texels, uploaded again when it
    // changed (GL context current); 0 when there is nothing to draw.
    GLuint hudTexture(realms::Engine &engine, int width, int height, double seconds, int &scale);
    // Bit 0: a layout screen is shown (help computer, scores); bit 1: the inventory.
    int hudScreens() const;
    // A Quake 2 client command for the game ("help", "inven", "invuse"...).
    void gameCommand(const std::string &line) {
        if (gameRunning()) view_->module().clientCommand(line);
    }
    realms::Quake2Module::PlayerStats stats() const;
    std::vector<std::string> takeMessages();

    mapview::MapSession &session() { return view_->session(); }
    const mapview::MapSession &session() const { return view_->session(); }

private:
    void drawTriangles(GLuint program, realms::Engine &engine, int pass);
    void releaseTriangles();
    void releaseGl();

    std::unique_ptr<mapview::Quake2View> view_ = std::make_unique<mapview::Quake2View>();
    bool reuseCity_ = false;
    float cityScale_ = 1.0f;
    std::array<float, 3> cityOffset_{};
    GLuint vao_ = 0, vbo_ = 0, lightUvVbo_ = 0, lightmapTexture_ = 0;
    GLuint mapTextures_ = 0; // unit 3: the map's own textures (Quake2Module::textures())
    bool uploadMapTextures(const realms::MapTextures &textures);
    GLuint skyTexture_ = 0; // the skybox (Quake2Module::sky())
    void uploadSky(const realms::MapSky &sky);
    GLsizei count_ = 0;
    std::vector<mapview::ModelRange> ranges_;
    city::GltfRenderer model_;
    std::future<std::optional<city::Scene>> prepare_;
    Quake2Gles gles_;
    ekron::audio::GameSoundPlayer sound_;
    ekron::audio::Mixer *mixer_ = nullptr;
    std::vector<mapview::EntityVertex> entityVertices_;
    std::vector<mapview::EntityBatch> entityBatches_;
    std::vector<mapview::EffectVertex> effectVertices_;
    std::vector<mapview::DynamicLight> lights_;
    realms::GameHudCanvas hud_;
    GLuint hudTexture_ = 0;
};
