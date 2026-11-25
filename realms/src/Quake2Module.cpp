// Realms Engine - the Quake 2 stack as one module (see Quake2Module.h).
#include "realms/Quake2Module.h"
#include "realms/ViewFov.h"

#include "realms/q2game.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace realms {
namespace {

bool startsWithWorldspawn(const Engine &engine) {
    const auto ents = Engine::parseEntities(engine.mapEntities());
    return !ents.empty() && Engine::entityValue(ents.front(), "classname") == "worldspawn";
}

struct CameraAxes {
    std::array<float, 3> forward, right, up;
};

// AngleVectors for (pitch, yaw, roll = 0).
CameraAxes cameraAxes(const Vec3 &angles) {
    // AngleVectors: forward, right, up (roll from the view kick / death view)
    const float d2r = 3.14159265f / 180.0f;
    const float sy = std::sin(angles[1] * d2r), cy = std::cos(angles[1] * d2r);
    const float sp = std::sin(angles[0] * d2r), cp = std::cos(angles[0] * d2r);
    const float sr = std::sin(angles[2] * d2r), cr = std::cos(angles[2] * d2r);
    return {{cp * cy, cp * sy, -sp},
            {-sr * sp * cy + cr * sy, -sr * sp * sy - cr * cy, -sr * cp},
            {cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp}};
}

// The module whose game or demo runs (the last one opened or run): "record"
// and "stop" act on it.
Quake2Module *gActiveModule = nullptr;

} // namespace

Quake2Module::Quake2Module() : lightStyles_(defaultLightStyles()) {}
Quake2Module::~Quake2Module() { close(); }

void Quake2Module::open(Engine &engine, const MapGeometry &geometry, const Quake2ModuleConfig &config,
                        std::string &why) {
    close();
    why.clear();
    executableDir_ = config.executableDir;
    engine_ = &engine;
    gActiveModule = this;
    // Quake 2's demo commands (cl_main.c CL_Record_f / CL_Stop_f), on
    // whichever module runs when they are typed
    Engine *e = &engine;
    const auto active = [e]() { return gActiveModule && gActiveModule->engine_ == e ? gActiveModule : nullptr; };
    if (!engine.commandExists("record"))
        engine.addCommand("record", [e, active](const CommandArgs &args) {
            if (args.size() != 2) {
                e->print("USAGE: record <demoname>");
                return;
            }
            Quake2Module *q = active();
            std::string error = "You must be in a level to record.";
            if (q && q->startRecording(*e, args[1], error)) e->print("recording to " + gameDemoFile(args[1]) + ".");
            else e->print("record: " + error, REALMS_PRINT_WARNING);
        });
    if (!engine.commandExists("stop"))
        engine.addCommand("stop", [e, active](const CommandArgs &) {
            Quake2Module *q = active();
            if (q && q->demoPlaying()) {
                q->stopDemo();
                e->print("Stopped demo playback.");
            } else if (!q || !q->stopRecording()) {
                e->print("Not recording a demo.");
            }
        });
    if (config.lightmaps && geometry.source == MapGeometrySource::Faces) {
        std::string error;
        if (!lighting_.load(engine, error))
            engine.print("map: no lightmaps: " + error + "\n", REALMS_PRINT_DEVELOPER);
        if (!lighting_.empty()) lighting_.compose(lightStyles_, lightmapRgba_);
    }
    if (config.textures &&
        (geometry.source == MapGeometrySource::Faces || geometry.source == MapGeometrySource::Brushes)) {
        textures_.load(engine, geometry);
        if (textures_.missing())
            engine.print("map: " + std::to_string(textures_.missing()) + " textures not found\n", REALMS_PRINT_DEVELOPER);
    }
    // BSP maps fall back to unit1_ like SP_worldspawn; a glTF map has a sky
    // only when its scene extras name one ("sky", "skyrotate", "skyaxis"),
    // drawn behind the whole scene.
    const MapSkySettings sky = skySettings(engine.mapEntities());
    if (config.sky && (geometry.source == MapGeometrySource::Faces || geometry.source == MapGeometrySource::Brushes ||
                       (geometry.source == MapGeometrySource::Mesh && sky.named))) {
        std::string error;
        if (!sky_.load(engine, sky, &error))
            engine.print("map: no sky: " + error + "\n", REALMS_PRINT_DEVELOPER);
    }
    // "demomap": its demo instead of the game
    const std::string demo = engine.cvarString(kGameDemoCvar, "");
    if (!demo.empty()) {
        engine.forceSetCvar(kGameDemoCvar, "");
        std::string error;
        if (startDemo(engine, demo, error)) return;
        engine.print("demomap: " + error, REALMS_PRINT_WARNING);
    }
    // "connect": the server's game when this is its map (the console's
    // "map" loaded it for the client); another map ends the connection
    GameNetClient &net = GameNetClient::instance();
    if ((net.state() == GameNetClient::State::Loading || net.state() == GameNetClient::State::Active) &&
        net.mapPath() == engine.mapPath()) {
        std::string error;
        if (attachNet(engine, net, error)) return;
        engine.print("connect: " + error, REALMS_PRINT_WARNING);
        return;
    }
    if (net.connected()) {
        net.disconnect(GameNetClient::clock());
        engine.print("Disconnected from " + net.serverName() + ".");
    }
    if (config.startGame) startGame(engine, why);
}

void Quake2Module::close() {
    stopRecording();
    demo_.reset();
    demoHasNext_ = false;
    demoAtEnd_ = false;
    timeDemo_ = timeDemoStarted_ = false;
    timeDemoFrames_ = 0;
    timeDemoResult_ = TimeDemoResult{};
    demoNext_ = GameSnapshot{};
    // a map still loaded: the server is changing maps (its clients come
    // back for the new one); none: killserver
    stopServer(engine_ && engine_->mapLoaded() ? "reconnect" : "disconnect\nServer quit");
    if (net_) {
        // playing a server's game and another map (or none) is opened
        if (net_->state() == GameNetClient::State::Active) net_->disconnect(GameNetClient::clock());
        net_ = nullptr;
    }
    viewEntity_ = 1;
    if (gActiveModule == this) gActiveModule = nullptr;
    client_.reset();
    failure_.clear();
    poses_.clear();
    messages_.clear();
    lastPickup_ = 0;
    layout_.clear();
    lerp_.reset();
    models_.clear();
    scene_.clear();
    effects_.reset();
    sounds_.clear();
    lighting_.clear();
    textures_.clear();
    sky_.clear();
    lightStyles_ = defaultLightStyles();
    lightmapRgba_.clear();
}

void Quake2Module::stopServer(const std::string &command) {
    if (listen_) listen_->close(command);
    listen_.reset();
    listenGame_.reset();
    GameNetClient::instance().setLocalServerPort(0);
    if (game_) {
        game_->stop();
        game_.reset();
    }
}

bool Quake2Module::startGame(Engine &engine, std::string &why) {
    std::string where;
    if (!GameServer::moduleAvailable(executableDir_, &where, &why)) return false;
    if (!startsWithWorldspawn(engine)) {
        why = "the map has no worldspawn entity";
        return false;
    }
    // a restart: the listen server's clients come back for the new game
    if (listen_) {
        listen_->close("reconnect");
        listen_.reset();
        listenGame_.reset();
        GameNetClient::instance().setLocalServerPort(0);
    }
    if (!game_) game_ = std::make_unique<GameServer>();
    GameServerConfig config;
    config.executableDir = executableDir_;
    config.playerName = engine.cvarString("name", "Player");
    config.fov = clampFov(engine.cvarFloat("fov", 90.0f));
    engine.cvarModified("fov"); // sent with the connect
    config.hand = GameServer::clampHand(engine.cvarInt("hand", 0));
    engine.cvarModified("hand");
    config.skill = engine.cvarInt("skill", 1);
    // SV_InitGame: the multiplayer cvars (registerGameNetCommands)
    config.deathmatch = engine.cvarFloat("deathmatch", 0.0f) != 0.0f;
    config.coop = engine.cvarFloat("coop", 0.0f) != 0.0f;
    config.maxClients = engine.cvarInt("maxclients", 1);
    if (!game_->start(engine, config, why)) {
        game_.reset();
        return false;
    }
    if (game_->maxClients() > 1) startListening(engine);
    // a demo being recorded starts over with the new game: the player resets
    // its client too, then gets the full snapshot
    if (recorder_.isOpen() && !recorder_.write(GameSnapshot{}, 0.0f)) stopRecording();
    resetClientSide(); // the first snapshot is a full one
    std::string palette;
    if (!effects_.loadPalette(engine, &palette))
        engine.print("map: effects use the built-in palette (" + palette + ")\n", REALMS_PRINT_DEVELOPER);
    gActiveModule = this;
    sync(0.0f);
    return true;
}

void Quake2Module::startListening(Engine &engine) {
    const int port = engine.cvarInt("port", kNetDefaultPort);
    auto listen = std::make_unique<GameNetServer>();
    std::string error;
    if (port < 0 || port > 65535 || !listen->open(uint16_t(port), error)) {
        if (error.empty()) error = "bad port " + std::to_string(port);
        engine.print("No listen server, others can't join: " + error, REALMS_PRINT_WARNING);
        return;
    }
    RealmsMapInfo info{};
    engine.mapInfo(info);
    listen->setMap(engine.mapPath(), info.checksum);
    Engine *e = &engine;
    listen->print = [e](const std::string &text) { e->print(text); };
    listen_ = std::move(listen);
    listenGame_ = std::make_unique<GameServerNetGame>(*game_);
    GameNetClient::instance().setLocalServerPort(listen_->port());
    engine.print(std::string(game_->deathmatch() ? "Deathmatch" : "Coop") + " for " +
                 std::to_string(game_->maxClients()) + " players: listening on UDP port " +
                 std::to_string(listen_->port()) + ".");
}

bool Quake2Module::attachNet(Engine &engine, GameNetClient &net, std::string &why) {
    RealmsMapInfo info{};
    if (net.mapChecksum() && engine.mapInfo(info) && info.checksum && info.checksum != net.mapChecksum()) {
        why = "your " + engine.mapPath() + " differs from the server's";
        net.disconnect(GameNetClient::clock());
        return false;
    }
    stopServer("");
    resetClientSide(); // the server sends everything
    std::string palette;
    if (!effects_.loadPalette(engine, &palette))
        engine.print("map: effects use the built-in palette (" + palette + ")\n", REALMS_PRINT_DEVELOPER);
    // the connect sent the userinfo; changes from now on are sent
    engine.cvarModified("fov");
    engine.cvarModified("hand");
    net_ = &net;
    viewEntity_ = net.playerNum() >= 0 ? net.playerNum() + 1 : 1;
    gActiveModule = this;
    net.begin(GameNetClient::clock());
    return true;
}

void Quake2Module::detachNet() {
    if (!net_) return;
    failure_ = net_->takeDisconnectReason(); // "" when the server changes maps
    net_ = nullptr;
    viewEntity_ = 1;
    stopRecording();
    resetClientSide();
    poses_.clear();
    scene_.clear();
}

void Quake2Module::netFrame(const GameUserCmd &cmd, double dt) {
    gActiveModule = this;
    std::vector<GameSnapshot> snapshots;
    net_->frame(cmd, dt, GameNetClient::clock(), snapshots);
    if (net_->playerNum() >= 0) viewEntity_ = net_->playerNum() + 1;
    float seconds = float(dt);
    for (GameSnapshot &s : snapshots) {
        record(s, seconds);
        seconds = 0.0f;
        receive(std::move(s));
    }
    if (net_->state() != GameNetClient::State::Active) detachNet();
}

void Quake2Module::resetClientSide() {
    lastPickup_ = 0;
    layout_.clear();
    client_.reset();
    lerp_.reset();
    effects_.reset();
    sounds_.clear();
}

bool Quake2Module::restartGame(Engine &engine, std::string &why) {
    if (net_) {
        why = "playing on " + net_->serverName() + " (disconnect first)";
        return false;
    }
    if (demo_) stopDemo();
    poses_.clear();
    scene_.clear();
    return startGame(engine, why);
}

bool Quake2Module::gameRunning() const {
    return serverRunning() || demo_ != nullptr || (net_ && client_.running());
}
const GameServer *Quake2Module::game() const { return serverRunning() ? game_.get() : nullptr; }
GameServer *Quake2Module::game() { return serverRunning() ? game_.get() : nullptr; }

// ------------------------------------------------------------ demos

bool Quake2Module::startRecording(Engine &engine, const std::string &name, std::string &why) {
    if (demo_) {
        why = "can't record while a demo plays";
        return false;
    }
    if ((!serverRunning() && !net_) || !client_.running()) {
        why = "You must be in a level to record.";
        return false;
    }
    if (recorder_.isOpen()) {
        why = "Already recording.";
        return false;
    }
    const std::string path = gameDemoPath(engine, name);
    if (path.empty()) {
        why = engine.writableRoot().empty() ? "no writable folder for demos"
                                            : "bad demo name \"" + name + "\" (1 to 32 of A-Z a-z 0-9 _ -)";
        return false;
    }
    GameDemoHeader header;
    header.mapPath = engine.mapPath();
    RealmsMapInfo info{};
    if (engine.mapInfo(info)) header.mapChecksum = info.checksum;
    if (!recorder_.open(path, header, why)) return false;
    recordFile_ = gameDemoFile(name);
    // CL_Record_f writes the client's gamestate first: here everything the
    // client holds, plus the layout shown now
    GameSnapshot first = client_.fullSnapshot();
    if (!layout_.empty()) {
        GameEvent layout;
        layout.kind = GameEvent::Kind::Layout;
        layout.text = layout_;
        first.events.push_back(std::move(layout));
    }
    if (!recorder_.write(first, 0.0f)) {
        why = "can't write " + path;
        recorder_.close();
        return false;
    }
    return true;
}

bool Quake2Module::stopRecording() {
    if (!recorder_.isOpen()) return false;
    const std::string file = recordFile_;
    const int records = recorder_.records();
    const uint64_t bytes = recorder_.bytes();
    recorder_.close();
    if (engine_)
        engine_->print("Stopped demo: " + file + ", " + std::to_string(records) + " frames, " +
                       std::to_string((bytes + 1023) / 1024) + " KB.");
    return true;
}

bool Quake2Module::startDemo(Engine &engine, const std::string &file, std::string &why) {
    std::vector<unsigned char> bytes;
    if (!engine.loadFile(file, bytes)) {
        why = "can't find " + file;
        return false;
    }
    auto reader = std::make_unique<GameDemoReader>();
    if (!reader->open(std::vector<uint8_t>(bytes.begin(), bytes.end()), why)) {
        why = file + ": " + why;
        return false;
    }
    const GameDemoHeader &header = reader->header();
    if (header.mapPath != engine.mapPath()) {
        why = file + " was recorded on " + header.mapPath + ", not " + engine.mapPath();
        return false;
    }
    RealmsMapInfo info{};
    if (header.mapChecksum && engine.mapInfo(info) && info.checksum && info.checksum != header.mapChecksum) {
        why = file + " was recorded on another version of " + header.mapPath;
        return false;
    }
    stopServer("disconnect\nServer quit");
    resetClientSide();
    std::string palette;
    if (!effects_.loadPalette(engine, &palette))
        engine.print("map: effects use the built-in palette (" + palette + ")\n", REALMS_PRINT_DEVELOPER);
    demo_ = std::move(reader);
    demoHasNext_ = false;
    demoAtEnd_ = false;
    timeDemo_ = engine.cvarBool("timedemo", false);
    timeDemoStarted_ = false;
    timeDemoFrames_ = 0;
    demoClock_ = demoDue_ = 0.0;
    gActiveModule = this;
    engine.print(std::string(timeDemo_ ? "timing demo " : "playing demo ") + file);
    // its first record is due at once (a timedemo's frames are counted from
    // the next one)
    float seconds = 0.0f;
    GameSnapshot first;
    if (demo_->next(first, seconds)) receive(std::move(first));
    else demoAtEnd_ = true;
    return true;
}

void Quake2Module::advanceDemo(double dt) {
    if (!demo_) return;
    if (demoAtEnd_) { // the last record has been shown for a frame
        finishDemo();
        return;
    }
    if (timeDemo_) {
        // one record a frame, whatever its time (SV_Frame doesn't wait for
        // the clock with timedemo set); the clock starts at the first frame
        if (!timeDemoStarted_) {
            timeDemoStarted_ = true;
            timeDemoStart_ = std::chrono::steady_clock::now();
        }
        GameSnapshot next;
        float seconds = 0.0f;
        if (!demo_->next(next, seconds)) {
            finishDemo();
            return;
        }
        timeDemoFrames_++;
        receive(std::move(next));
        return;
    }
    demoClock_ += std::isfinite(dt) ? std::max(dt, 0.0) : 0.0;
    bool received = false;
    while (demo_) {
        if (!demoHasNext_) {
            float seconds = 0.0f;
            if (!demo_->next(demoNext_, seconds)) {
                demoAtEnd_ = true;
                if (!received) finishDemo();
                return;
            }
            demoHasNext_ = true;
            demoDue_ += double(seconds);
        }
        if (demoDue_ > demoClock_ + 1e-4) return; // due (within rounding: 0.1 ms)
        demoHasNext_ = false;
        receive(std::move(demoNext_));
        received = true;
        demoNext_ = GameSnapshot{};
    }
}

void Quake2Module::finishDemo() {
    // the end: back to walking the map
    if (!demo_) return;
    if (timeDemo_) { // CL_FinishTimeDemo
        TimeDemoResult r;
        r.frames = timeDemoFrames_;
        if (timeDemoStarted_)
            r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - timeDemoStart_).count();
        r.fps = r.frames > 0 && r.seconds > 0.0 ? r.frames / r.seconds : 0.0;
        timeDemoResult_ = r;
    }
    if (engine_) {
        if (!demo_->error().empty())
            engine_->print("demo: " + demo_->error() + ", stopped there", REALMS_PRINT_WARNING);
        char buf[96];
        if (timeDemo_)
            std::snprintf(buf, sizeof buf, "%i frames, %3.1f seconds: %3.1f fps", timeDemoResult_.frames,
                          timeDemoResult_.seconds, timeDemoResult_.fps);
        else
            std::snprintf(buf, sizeof buf, "Demo finished: %d frames, %.1f seconds.", demo_->records(),
                          demo_->time());
        engine_->print(buf);
    }
    stopDemo();
}

void Quake2Module::stopDemo() {
    if (!demo_) return;
    demo_.reset();
    demoHasNext_ = false;
    demoAtEnd_ = false;
    timeDemo_ = timeDemoStarted_ = false;
    timeDemoFrames_ = 0;
    demoNext_ = GameSnapshot{};
    resetClientSide();
    poses_.clear();
    scene_.clear();
}

std::string Quake2Module::takeGameFailure() {
    if (game_ && !game_->running()) { // the game stopped (game error): back to walking
        failure_ = game_->lastError();
        if (failure_.empty()) failure_ = "the game stopped";
        stopServer("disconnect\nServer quit");
        stopRecording();
        client_.reset();
        poses_.clear();
        scene_.clear();
    }
    std::string out;
    out.swap(failure_);
    return out;
}

GameUserCmd Quake2Module::userCmd(const WalkInput &in, bool attack) {
    GameUserCmd cmd;
    cmd.forward = std::clamp(in.forward, -1.0f, 1.0f) * 400.0f;
    cmd.side = std::clamp(in.right, -1.0f, 1.0f) * 400.0f;
    cmd.up = in.jump ? 400.0f : in.crouch ? -400.0f : std::clamp(in.up, -1.0f, 1.0f) * 400.0f;
    cmd.yaw = in.yaw;
    cmd.pitch = in.pitch;
    cmd.attack = attack;
    return cmd;
}

void Quake2Module::frame(const GameUserCmd &cmd, double dt) {
    if (demo_) {
        gActiveModule = this;
        advanceDemo(dt);
        return;
    }
    if (net_) {
        netFrame(cmd, dt);
        return;
    }
    if (!serverRunning()) return;
    gActiveModule = this;
    // a listen server: the remote clients' usercmds before the frame, their
    // snapshots after it
    const double now = GameNetClient::clock();
    if (listen_) listen_->receive(*listenGame_, now);
    game_->frame(cmd, dt);
    if (listen_ && serverRunning()) listen_->send(*listenGame_, now);
    sync(float(dt));
}

void Quake2Module::record(const GameSnapshot &snapshot, float seconds) {
    if (recorder_.isOpen() && !recorder_.write(snapshot, seconds)) {
        const std::string file = recordFile_;
        stopRecording();
        if (engine_) engine_->print("record: can't write " + file + ", stopped", REALMS_PRINT_WARNING);
    }
}

void Quake2Module::sync(float seconds) {
    if (!serverRunning()) return;
    GameSnapshot snapshot;
    game_->snapshot(snapshot, client_.needsFull());
    record(snapshot, seconds);
    receive(std::move(snapshot));
}

void Quake2Module::receive(GameSnapshot snapshot) {
    if (!snapshot.running) { // the game started over (in a demo): so does the client
        resetClientSide();
        poses_.clear();
        return;
    }
    // everything below reads the client's state, made from the snapshots
    client_.apply(std::move(snapshot));
    const GameClient &g = client_;
    if (const q2player_state_t *ps = g.playerState()) {
        const int pickup = ps->stats[Q2_STAT_PICKUP_STRING];
        if (pickup && pickup != lastPickup_) {
            const std::string &item = g.configString(pickup);
            if (!item.empty()) messages_.push_back(item);
        }
        lastPickup_ = pickup;
    }
    // Client time: the server frames plus the interpolation fraction, so
    // effects advance smoothly between game frames.
    const double now = clientTimeMs(g);
    for (const GameEvent &ev : client_.takeEvents()) {
        effects_.handleEvent(ev, now); // temp entities, muzzle flashes, entity events
        // the server prints its messages itself; a demo's and a remote
        // server's are printed here
        if ((demo_ || net_) && engine_ && ev.kind == GameEvent::Kind::Print && !ev.text.empty()) engine_->print(ev.text);
        if (ev.kind == GameEvent::Kind::Sound) sounds_.push_back(ev);
        if (ev.kind == GameEvent::Kind::Layout) layout_ = ev.text;
        if (ev.kind == GameEvent::Kind::CenterPrint && !ev.text.empty()) {
            std::string t = ev.text;
            while (!t.empty() && (t.back() == '\n' || t.back() == ' ')) t.pop_back();
            if (!t.empty()) messages_.push_back(t);
        }
    }
    if (messages_.size() > 16) messages_.erase(messages_.begin(), messages_.end() - 16);

    // Brush entity poses: each inline model where its entity is
    // (interpolated, so doors and lifts glide), hidden when no visible
    // entity uses it (func_wall off, func_explosive gone...).
    lerp_.update(g);
    const std::vector<GameLerpedEntity> lerped = lerp_.entities(g);
    int models = 0;
    for (const GameLerpedEntity &l : lerped)
        if (l.entity) models = std::max(models, l.entity->inlineModel);
    poses_.assign(size_t(std::max(models, 0) + 1), Quake2ModelPose{});
    std::vector<bool> used(poses_.size(), false);
    for (const GameLerpedEntity &l : lerped) {
        const int m = l.entity ? l.entity->inlineModel : 0;
        if (m <= 0 || used[size_t(m)]) continue;
        used[size_t(m)] = true;
        poses_[size_t(m)].origin = l.origin;
        poses_[size_t(m)].angles = l.angles;
        poses_[size_t(m)].frame = l.frame;
    }
    for (size_t m = 1; m < poses_.size(); m++) poses_[m].visible = used[m];

    // Entity effects (trails, flies, lights), laser entities, then age everything.
    effects_.addEntityEffects(lerped, g.frameNumber(), now);
    effects_.setBeamEntities(g.beamEntities());
    effects_.update(now);
    for (GameEvent &ev : effects_.takeSounds()) sounds_.push_back(std::move(ev));
    if (sounds_.size() > 256) sounds_.erase(sounds_.begin(), sounds_.end() - 256);
}

Vec3 Quake2Module::viewOrigin() const {
    if (!gameRunning()) return {0, 0, 0};
    // CL_CalcViewValues: the player's origin (moved by every usercmd) plus
    // the view offset between the last two game frames
    Vec3 o = client_.playerOrigin();
    const GameViewLerp v = lerp_.view(client_);
    for (size_t k = 0; k < 3; k++) o[k] += v.offset[k];
    return o;
}

int Quake2Module::cdTrack() const {
    if (!gameRunning()) return 0;
    return std::max(0, std::atoi(client_.configString(Q2_CS_CDTRACK).c_str()));
}

Vec3 Quake2Module::cameraAngles(const Vec3 &look) const {
    if (!gameRunning()) return look;
    // CL_CalcViewValues: the player's own look (the death view while dead)
    // plus the kick angles
    const GameViewLerp v = lerp_.view(client_);
    Vec3 own = look;
    if (demo_) { // the recorded player's look (set by every usercmd), not ours
        const q2player_state_t *ps = client_.playerState();
        own = ps ? Vec3{ps->viewangles[0], ps->viewangles[1], ps->viewangles[2]} : Vec3{};
    }
    Vec3 a = v.locked ? Vec3{v.angles[0], v.angles[1], v.angles[2]} : own;
    if (kickAngles_) // cl_kickangles
        for (size_t k = 0; k < 3; k++) a[k] += v.kick[k];
    return a;
}
Vec3 Quake2Module::viewAngles() const { return gameRunning() ? client_.viewAngles() : Vec3{0, 0, 0}; }

bool Quake2Module::noclip() const {
    if (!gameRunning()) return false;
    const q2player_state_t *ps = client_.playerState();
    return ps && ps->pmove.pm_type == Q2_PM_SPECTATOR;
}

void Quake2Module::toggleNoclip() { clientCommand("noclip"); }

void Quake2Module::mirrorPlayer(MapWalker &w) const {
    if (!gameRunning()) return;
    w.origin = client_.playerOrigin();
    if (const q2player_state_t *ps = client_.playerState()) {
        for (int i = 0; i < 3; i++) w.velocity[size_t(i)] = ps->pmove.velocity[i] * 0.125f;
        w.onGround = (ps->pmove.pm_flags & Q2_PMF_ON_GROUND) != 0;
        w.ducked = (ps->pmove.pm_flags & Q2_PMF_DUCKED) != 0;
        w.noclip = ps->pmove.pm_type == Q2_PM_SPECTATOR;
        w.waterLevel = (ps->rdflags & 1 /* RDF_UNDERWATER */) ? 3 : 0;
    }
}

Quake2Module::PlayerStats Quake2Module::stats() const {
    PlayerStats s;
    if (!gameRunning()) return s;
    if (const q2player_state_t *ps = client_.playerState()) {
        s.health = ps->stats[Q2_STAT_HEALTH];
        s.armor = ps->stats[Q2_STAT_ARMOR];
        s.ammo = ps->stats[Q2_STAT_AMMO];
        for (size_t i = 0; i < 4; i++) {
            const float v = ps->blend[i];
            s.blend[i] = std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f;
        }
        if (s.blend[3] <= 0.0f) s.blend = {};
        s.fov = clampFov(ps->fov);
    }
    return s;
}

float Quake2Module::viewFov(const Engine &engine) const {
    return gameRunning() ? stats().fov : clampFov(engine.cvarFloat("fov", 90.0f));
}

std::string Quake2Module::status() const {
    if (!gameRunning() || !client_.playerState()) return {};
    const PlayerStats s = stats();
    char buf[96];
    std::snprintf(buf, sizeof(buf), "HP %d  Armor %d  Ammo %d", s.health, s.armor, s.ammo);
    return buf;
}

namespace {
std::vector<GameLayoutItem> runLayout(const GameClient &g, const std::string &program, int width, int height) {
    const q2player_state_t *ps = g.playerState();
    if (!ps || program.empty()) return {};
    GameLayoutInput in;
    in.stats = ps->stats;
    in.configString = [&g](int index) { return g.configString(index); };
    in.width = width;
    in.height = height;
    in.serverFrame = g.frameNumber();
    return runGameLayout(program, in);
}
} // namespace

bool Quake2Module::layoutShown() const {
    if (!gameRunning()) return false;
    const q2player_state_t *ps = client_.playerState();
    return ps && (ps->stats[Q2_STAT_LAYOUTS] & 1) && !layout_.empty();
}

std::vector<GameLayoutItem> Quake2Module::layoutItems(int width, int height) const {
    if (!layoutShown()) return {};
    return runLayout(client_, layout_, width, height);
}

std::vector<GameLayoutItem> Quake2Module::statusBarItems(int width, int height) const {
    if (!gameRunning()) return {};
    return runLayout(client_, client_.configString(Q2_CS_STATUSBAR), width, height);
}

void Quake2Module::toggleHelp() { clientCommand("help"); }

bool Quake2Module::inventoryShown() const {
    if (!gameRunning()) return false;
    const q2player_state_t *ps = client_.playerState();
    return ps && (ps->stats[Q2_STAT_LAYOUTS] & 2);
}

std::vector<GameLayoutItem> Quake2Module::inventoryItems(int width, int height, bool cursor) const {
    if (!inventoryShown()) return {};
    const GameClient &g = client_;
    return gameInventoryItems(g.inventory().data(), g.playerState()->stats[Q2_STAT_SELECTED_ITEM],
                              [&g](int index) { return g.configString(index); }, width, height, cursor);
}

void Quake2Module::clientCommand(const std::string &line) {
    if (serverRunning()) game_->clientCommand(line);
    else if (net_) net_->sendCommand(line); // the server's game runs it
}

std::vector<std::string> Quake2Module::takeMessages() {
    std::vector<std::string> out;
    out.swap(messages_);
    return out;
}

const GameScene &Quake2Module::buildScene(Engine &engine, const Vec3 &cameraOrigin, const Vec3 &cameraAngles,
                                          bool viewWeapon) {
    scene_.clear();
    if (!gameRunning()) return scene_;
    // Like the Quake 2 client sending its userinfo when the fov cvar changes.
    const bool userinfo = serverRunning() || net_;
    const float fov = engine.cvarFloat("fov", 90.0f);
    const int hand = GameServer::clampHand(engine.cvarInt("hand", 0));
    const bool fovChanged = userinfo && engine.cvarModified("fov");
    const bool handChanged = userinfo && engine.cvarModified("hand");
    if (serverRunning()) {
        if (fovChanged) game_->setFov(fov);
        if (handChanged) game_->setHand(hand);
    } else if (net_ && (fovChanged || handChanged)) {
        char info[64];
        std::snprintf(info, sizeof info, "\\fov\\%g\\hand\\%d", double(fov), hand);
        net_->sendUserinfo(info);
    }
    GameSceneOptions opt;
    opt.viewWeapon = viewWeapon && !noclip();
    opt.hand = hand;
    opt.hasCamera = true;
    opt.cameraOrigin = cameraOrigin;
    opt.cameraAngles = cameraAngles;
    buildGameScene(engine, client_, lerp_, models_, opt, scene_);
    // explosions, smoke, flashes and cable segments (cl_tent.c)
    effects_.models(clientTimeMs(client_), fxModels_);
    appendEffectModels(engine, models_, fxModels_, scene_);
    for (const std::string &e : models_.takeErrors()) engine.print("map: model " + e + "\n", REALMS_PRINT_DEVELOPER);
    effects_.lights(clientTimeMs(client_), fxLights_);
    for (GameDrawBatch &b : scene_.batches)
        if (!b.fullbright) b.lit = lightAt(b.origin, fxLights_, b.light);
    return scene_;
}

void Quake2Module::buildEffects(const Vec3 &eye, const Vec3 &cameraAngles, size_t maxLights,
                                std::vector<Quake2EffectVertex> &quads, std::vector<GameDynamicLight> &lights) {
    quads.clear();
    lights.clear();
    if (!gameRunning()) return;
    const double now = clientTimeMs(client_);
    const CameraAxes axes = cameraAxes(cameraAngles);
    const auto &forward = axes.forward, &right = axes.right, &up = axes.up;
    auto quad = [&](const std::array<std::array<float, 3>, 4> &c, const std::array<float, 4> &col, float shape) {
        static const float uv[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        static const int order[6] = {0, 1, 2, 0, 2, 3};
        for (int k : order) {
            const auto &p = c[size_t(k)];
            quads.push_back({p[0], p[1], p[2], col[0], col[1], col[2], col[3], uv[k][0], uv[k][1], shape});
        }
    };

    effects_.particles(now, fxParticles_);
    quads.reserve(fxParticles_.size() * 6 + 64);
    for (const GameParticle &p : fxParticles_) {
        // gl_rpart.c: about one unit up close, growing with distance
        const float depth = (p.origin[0] - eye[0]) * forward[0] + (p.origin[1] - eye[1]) * forward[1] +
                            (p.origin[2] - eye[2]) * forward[2];
        if (depth < 1.0f) continue; // behind the camera
        const float scale = depth < 20.0f ? 1.0f : 1.0f + depth * 0.004f;
        const float h = p.size > 1.0f ? p.size : 0.8f * scale;
        std::array<std::array<float, 3>, 4> c;
        for (size_t i = 0; i < 3; i++) {
            c[0][i] = p.origin[i] - right[i] * h - up[i] * h;
            c[1][i] = p.origin[i] + right[i] * h - up[i] * h;
            c[2][i] = p.origin[i] + right[i] * h + up[i] * h;
            c[3][i] = p.origin[i] - right[i] * h + up[i] * h;
        }
        quad(c, p.color, 0.0f);
    }
    effects_.beams(now, fxBeams_);
    for (const GameBeam &b : fxBeams_) {
        const std::array<float, 3> dir{b.end[0] - b.start[0], b.end[1] - b.start[1], b.end[2] - b.start[2]};
        const std::array<float, 3> mid{(b.start[0] + b.end[0]) * 0.5f - eye[0], (b.start[1] + b.end[1]) * 0.5f - eye[1],
                                       (b.start[2] + b.end[2]) * 0.5f - eye[2]};
        std::array<float, 3> side{dir[1] * mid[2] - dir[2] * mid[1], dir[2] * mid[0] - dir[0] * mid[2],
                                  dir[0] * mid[1] - dir[1] * mid[0]};
        const float len = std::sqrt(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
        if (len < 1e-6f) continue;
        for (float &v : side) v *= 0.5f * b.width / len;
        std::array<std::array<float, 3>, 4> c;
        for (size_t i = 0; i < 3; i++) {
            c[0][i] = b.start[i] - side[i];
            c[1][i] = b.start[i] + side[i];
            c[2][i] = b.end[i] + side[i];
            c[3][i] = b.end[i] - side[i];
        }
        quad(c, b.color, 1.0f);
    }
    // lights: the closest to the camera first
    effects_.lights(now, lights);
    auto distance = [&](const GameDynamicLight &l) {
        const float x = l.origin[0] - eye[0], y = l.origin[1] - eye[1], z = l.origin[2] - eye[2];
        return x * x + y * y + z * z - l.radius * l.radius;
    };
    std::stable_sort(lights.begin(), lights.end(),
                     [&](const GameDynamicLight &a, const GameDynamicLight &b) { return distance(a) < distance(b); });
    if (lights.size() > maxLights) lights.resize(maxLights);
}

std::vector<GameEvent> Quake2Module::takeSounds() {
    std::vector<GameEvent> out;
    out.swap(sounds_);
    return out;
}

std::vector<GameLoopSound> Quake2Module::loopSounds() const {
    return gameRunning() ? client_.loopSounds() : std::vector<GameLoopSound>{};
}

GameListener Quake2Module::listener() const {
    GameListener l;
    if (!gameRunning()) return l;
    l.origin = client_.viewOrigin();
    l.right = gameRightVector(client_.viewAngles());
    l.entity = viewEntity_; // the player's edict: 1, or its slot's on a server
    return l;
}

bool Quake2Module::lightmapUvs(const MapGeometry &geometry, std::vector<float> &uvs) const {
    uvs.clear();
    if (lighting_.empty()) return false;
    uvs.reserve(geometry.triangleCount() * 6);
    for (const auto &b : geometry.batches) {
        const size_t n = b.positions.size() / 3;
        for (size_t i = 0; i < n; ++i) {
            float uv[2] = {-1.0f, -1.0f};
            const size_t tri = i / 3;
            const int32_t face = tri < b.faces.size() ? b.faces[tri] : -1;
            if (face < 0 || !lighting_.faceUv(size_t(face), &b.positions[i * 3], uv)) uv[0] = uv[1] = -1.0f;
            uvs.push_back(uv[0]);
            uvs.push_back(uv[1]);
        }
    }
    return true;
}

void Quake2Module::disableLightmaps() {
    lighting_.clear();
    lightmapRgba_.clear();
}

MapLightStyles Quake2Module::currentLightStyles() const {
    MapLightStyles styles = defaultLightStyles();
    if (!gameRunning()) return styles;
    // CL_SetLightstyle / CL_RunLightStyles: the configstrings at 10 Hz
    const double now = clientTimeMs(client_);
    for (int s : lighting_.styles()) {
        if (s >= Q2_MAX_LIGHTSTYLES) continue;
        styles[size_t(s)] = lightStyleValue(client_.configString(Q2_CS_LIGHTS + s), now);
    }
    return styles;
}

bool Quake2Module::updateLightStyles() {
    if (lighting_.empty()) return false;
    const MapLightStyles styles = currentLightStyles();
    if (!lighting_.stylesDiffer(styles, lightStyles_)) return false;
    lightStyles_ = styles;
    lighting_.compose(lightStyles_, lightmapRgba_);
    return true;
}

bool Quake2Module::lightAt(const Vec3 &p, std::array<float, 3> &rgb) const {
    std::vector<GameDynamicLight> dlights;
    if (gameRunning()) effects_.lights(clientTimeMs(client_), dlights);
    return lightAt(p, dlights, rgb);
}

bool Quake2Module::lightAt(const Vec3 &p, const std::vector<GameDynamicLight> &dlights,
                           std::array<float, 3> &rgb) const {
    rgb = {1.0f, 1.0f, 1.0f};
    if (lighting_.empty()) return false;
    lighting_.lightPoint(p.data(), lightStyles_, rgb.data());
    addDynamicLights(p, dlights, rgb);
    if (std::max({rgb[0], rgb[1], rgb[2]}) < 0.1f) rgb = {0.1f, 0.1f, 0.1f};
    return true;
}

} // namespace realms
