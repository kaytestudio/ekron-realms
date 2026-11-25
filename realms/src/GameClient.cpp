// Realms Engine - the client's state from server snapshots (see GameClient.h).
#include "realms/GameClient.h"

#include <cstdlib>
#include <iterator>
#include <utility>

namespace realms {

GameClient::GameClient() { reset(); }

void GameClient::reset() {
    running_ = false;
    frameNumber_ = 0;
    lerpFraction_ = 0.0;
    hasPlayer_ = false;
    player_ = q2player_state_t{};
    playerOrigin_ = Vec3{};
    entities_.clear();
    beams_.clear();
    loopSounds_.clear();
    configStrings_.assign(Q2_MAX_CONFIGSTRINGS, std::string());
    inventory_.fill(0);
    events_.clear();
    snapshots_ = 0;
    serverFrames_ = 0;
}

void GameClient::apply(GameSnapshot s) {
    if (!s.running) {
        reset();
        return;
    }
    running_ = true;
    snapshots_++;
    frameNumber_ = s.frameNumber;
    lerpFraction_ = s.lerpFraction;
    hasPlayer_ = s.hasPlayer;
    player_ = s.player;
    playerOrigin_ = s.playerOrigin;
    if (s.entitiesChanged) {
        serverFrames_++;
        entities_ = std::move(s.entities);
        beams_ = std::move(s.beams);
        loopSounds_ = std::move(s.loopSounds);
    }
    for (auto &cs : s.configStrings)
        if (cs.first >= 0 && cs.first < Q2_MAX_CONFIGSTRINGS) configStrings_[size_t(cs.first)] = std::move(cs.second);
    if (s.inventoryChanged) inventory_ = s.inventory;
    events_.insert(events_.end(), std::make_move_iterator(s.events.begin()), std::make_move_iterator(s.events.end()));
}

void GameClient::update(GameServer &server) {
    GameSnapshot s;
    server.snapshot(s, needsFull());
    apply(std::move(s));
}

GameSnapshot GameClient::fullSnapshot() const {
    GameSnapshot s;
    s.running = running_;
    if (!running_) return s;
    s.frameNumber = frameNumber_;
    s.lerpFraction = lerpFraction_;
    s.hasPlayer = hasPlayer_;
    s.player = player_;
    s.playerOrigin = playerOrigin_;
    s.entitiesChanged = true;
    s.entities = entities_;
    s.beams = beams_;
    s.loopSounds = loopSounds_;
    for (size_t i = 0; i < configStrings_.size(); i++)
        if (!configStrings_[i].empty()) s.configStrings.emplace_back(int(i), configStrings_[i]);
    s.inventoryChanged = true;
    s.inventory = inventory_;
    return s;
}

Vec3 GameClient::viewOrigin() const {
    Vec3 o = playerOrigin();
    if (const q2player_state_t *ps = playerState())
        for (int i = 0; i < 3; i++) o[size_t(i)] += ps->viewoffset[i];
    return o;
}

Vec3 GameClient::viewAngles() const {
    const q2player_state_t *ps = playerState();
    if (!ps) return {};
    return {ps->viewangles[0] + ps->kick_angles[0], ps->viewangles[1] + ps->kick_angles[1],
            ps->viewangles[2] + ps->kick_angles[2]};
}

std::vector<GameRenderEntity> GameClient::renderEntities(bool withInline) const {
    if (!running_) return {};
    if (withInline) return entities_;
    std::vector<GameRenderEntity> out;
    out.reserve(entities_.size());
    for (const GameRenderEntity &e : entities_)
        if (e.inlineModel <= 0) out.push_back(e);
    return out;
}

const std::string &GameClient::configString(int index) const {
    static const std::string empty;
    if (index < 0 || size_t(index) >= configStrings_.size()) return empty;
    return configStrings_[size_t(index)];
}

std::string GameClient::modelName(int modelindex) const {
    if (modelindex <= 0 || modelindex >= Q2_MAX_MODELS) return {};
    return configString(Q2_CS_MODELS + modelindex);
}

int GameClient::inlineModel(int modelindex) const {
    const std::string name = modelName(modelindex);
    return name.size() > 1 && name[0] == '*' ? std::atoi(name.c_str() + 1) : 0;
}

std::vector<GameEvent> GameClient::takeEvents() {
    std::vector<GameEvent> out;
    out.swap(events_);
    return out;
}

} // namespace realms
