#include "ShopCatalog.h"

#include "third_party/json.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace ekron::desktop {

namespace {

// ---------------------------------------------------------------------------
// Catalog definition
// ---------------------------------------------------------------------------

const std::vector<ShopCategory> &catalog() {
    static const std::vector<ShopCategory> kCats = {
        {"СКИНЫ ВОЙНУХИ", ShopKind::Skin, {
            {1, ShopKind::Skin, "Пламя",            "Ярко-красный выброс грави-пушки", 150, 1.00f, 0.32f, 0.10f, ShopEffect::None, 0},
            {2, ShopKind::Skin, "Ледяной",          "Холодная голубая плазма",         150, 0.40f, 0.85f, 1.00f, ShopEffect::None, 0},
            {3, ShopKind::Skin, "Изумруд",          "Ярко-зелёная энергия",            200, 0.25f, 0.95f, 0.40f, ShopEffect::None, 0},
            {4, ShopKind::Skin, "Азур",             "Спокойная синяя плазма",          200, 0.30f, 0.55f, 1.00f, ShopEffect::None, 0},
            {5, ShopKind::Skin, "Золотая печать",    "Искристое золотое свечение",      350, 1.00f, 0.82f, 0.25f, ShopEffect::None, 0},
            {6, ShopKind::Skin, "Фиолетовая буря",  "Глубокий фиолетовый импульс",     350, 0.75f, 0.30f, 1.00f, ShopEffect::None, 0},
            {7, ShopKind::Skin, "Ядовитая роза",    "Ярко-розовый ядовитый свет",      400, 1.00f, 0.20f, 0.55f, ShopEffect::None, 0},
            {8, ShopKind::Skin, "Неон",             "Острая неоновая бело-голубая",    500, 0.60f, 1.00f, 0.95f, ShopEffect::None, 0},
        }},
        {"УЛУЧШЕНИЯ", ShopKind::Extra, {
            {101, ShopKind::Extra, "Бонусный кошелёк",   "Мгновенно +500 золотых монет", 300, 1.00f, 1.00f, 1.00f, ShopEffect::CoinBonus, 500},
            {102, ShopKind::Extra, "Двойные монеты",     "Скорость накопления монет ×2",  500, 1.00f, 1.00f, 1.00f, ShopEffect::DoubleCoins, 0},
            {103, ShopKind::Extra, "Золотой прицел",      "Золотой прицел на экране",      250, 1.00f, 1.00f, 1.00f, ShopEffect::GoldenCrosshair, 0},
            {104, ShopKind::Extra, "Камуфляж HUD",        "Камуфляжная расцветка интерфейса", 250, 1.00f, 1.00f, 1.00f, ShopEffect::CamoHud, 0},
            {105, ShopKind::Extra, "Неоновый прицел",     "Неоновый прицел с свечением",   400, 1.00f, 1.00f, 1.00f, ShopEffect::NeonSight, 0},
            {106, ShopKind::Extra, "Старт с полётом",     "Возвращение на спавн с полётом", 800, 1.00f, 1.00f, 1.00f, ShopEffect::StartWithFlight, 0},
        }},
    };
    return kCats;
}

bool isKnownItem(int id) {
    for (const auto &cat : catalog())
        for (const auto &item : cat.items)
            if (item.id == id) return true;
    return false;
}

} // namespace

const std::vector<ShopCategory> &shopCategories() { return catalog(); }

const ShopItem *findShopItem(int id) {
    for (const auto &cat : catalog())
        for (const auto &item : cat.items)
            if (item.id == id) return &item;
    return nullptr;
}

const char *shopEffectLabel(ShopEffect effect) {
    switch (effect) {
        case ShopEffect::CoinBonus:       return "+500 монет";
        case ShopEffect::DoubleCoins:     return "×2 скорость монет";
        case ShopEffect::GoldenCrosshair: return "Золотой прицел";
        case ShopEffect::CamoHud:         return "Камуфляж HUD";
        case ShopEffect::NeonSight:       return "Неоновый прицел";
        case ShopEffect::StartWithFlight: return "Старт с полётом";
        case ShopEffect::None:
        default:                           return "";
    }
}

bool ShopState::extrasAutoActivate(int itemId) {
    const ShopItem *item = findShopItem(itemId);
    return item && item->kind == ShopKind::Extra;
}

bool ShopState::load(const std::string &path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;
    std::stringstream buffer;
    buffer << in.rdbuf();
    try {
        const auto j = nlohmann::json::parse(buffer.str());
        if (!j.is_object()) return false;
        if (j.contains("coins") && j["coins"].is_number_integer())
            coins = std::max<long long>(0, j["coins"].get<long long>());
        if (j.contains("owned") && j["owned"].is_array()) {
            owned.clear();
            for (const auto &v : j["owned"]) {
                if (v.is_number_integer()) {
                    const int id = v.get<int>();
                    if (isKnownItem(id)) owned.insert(id);
                }
            }
        }
        if (j.contains("equippedSkin") && j["equippedSkin"].is_number_integer()) {
            const int id = j["equippedSkin"].get<int>();
            equippedSkinId = (owned.count(id) && findShopItem(id) &&
                              findShopItem(id)->kind == ShopKind::Skin) ? id : -1;
        }
        if (j.contains("activeExtras") && j["activeExtras"].is_array()) {
            activeExtras.clear();
            for (const auto &v : j["activeExtras"]) {
                if (v.is_number_integer()) {
                    const int id = v.get<int>();
                    if (owned.count(id) && findShopItem(id) &&
                        findShopItem(id)->kind == ShopKind::Extra) activeExtras.insert(id);
                }
            }
        }
        // Extras auto-activate on purchase; make sure saved extras reflect that.
        for (int id : owned) {
            const ShopItem *it = findShopItem(id);
            if (it && it->kind == ShopKind::Extra) activeExtras.insert(id);
        }
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

void ShopState::save(const std::string &path) const {
    nlohmann::json j;
    j["coins"] = coins;
    j["owned"] = nlohmann::json::array();
    for (int id : owned) j["owned"].push_back(id);
    j["equippedSkin"] = equippedSkinId;
    j["activeExtras"] = nlohmann::json::array();
    for (int id : activeExtras) j["activeExtras"].push_back(id);

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return;
    out << j.dump(2) << '\n';
}

bool ShopState::tryBuy(int itemId, std::string &reasonOut) {
    reasonOut.clear();
    const ShopItem *item = findShopItem(itemId);
    if (!item) {
        reasonOut = "Неизвестный товар";
        return false;
    }
    if (owned.count(itemId)) {
        reasonOut = "Уже куплено";
        return false;
    }
    if (coins < item->price) {
        reasonOut = "Недостаточно золота";
        return false;
    }
    coins -= item->price;
    owned.insert(itemId);
    if (item->kind == ShopKind::Skin) {
        // Buying a skin equips it immediately so the player sees the change.
        equippedSkinId = itemId;
    } else if (item->kind == ShopKind::Extra) {
        activeExtras.insert(itemId);
        // The Coin Bonus is a wallet-internal effect; grant it immediately so
        // the balance is correct even before the client applies other effects.
        if (item->effect == ShopEffect::CoinBonus && item->effectValue > 0)
            coins += item->effectValue;
    }
    return true;
}

bool ShopState::equipSkin(int itemId) {
    const ShopItem *item = findShopItem(itemId);
    if (!item || item->kind != ShopKind::Skin) return false;
    if (!owned.count(itemId)) return false;
    equippedSkinId = itemId;
    return true;
}

bool ShopState::hasExtra(int itemId) const {
    if (!owned.count(itemId)) return false;
    const ShopItem *item = findShopItem(itemId);
    return item && item->kind == ShopKind::Extra && activeExtras.count(itemId) != 0;
}

void ShopState::accrue(float seconds, int multiplier) {
    if (seconds <= 0.0f || multiplier <= 0) return;
    const long long rate = kBaseCoinsPerSecond * std::max(1, multiplier);
    // Accumulate fractional coins across frames without floating drift on the
    // persisted integer balance.
    accruedSeconds_ += seconds;
    const long long gained = static_cast<long long>(accruedSeconds_ * rate);
    if (gained <= 0) return;
    accruedSeconds_ -= static_cast<double>(gained) / static_cast<double>(rate);
    coins += gained;
}

void ShopState::addCoins(long long amount) {
    coins = std::max<long long>(0, coins + amount);
}

const ShopItem *ShopState::equippedSkin() const {
    if (equippedSkinId < 0) return nullptr;
    const ShopItem *item = findShopItem(equippedSkinId);
    if (!item || item->kind != ShopKind::Skin) return nullptr;
    return item;
}

void ShopState::applyRemote(long long newCoins, const std::vector<int> &newOwned,
                            int newEquippedSkin) {
    coins = std::max<long long>(0, newCoins);
    owned.clear();
    activeExtras.clear();
    for (int id : newOwned) {
        const ShopItem *item = findShopItem(id);
        if (!item) continue;
        owned.insert(id);
        if (item->kind == ShopKind::Extra) activeExtras.insert(id);
    }

    // Deterministic equipped-skin choice: prefer the value the server sent,
    // otherwise keep the locally equipped skin when it is still owned.
    int equipped = -1;
    auto isOwnedSkin = [&](int id) {
        const ShopItem *item = findShopItem(id);
        return item && item->kind == ShopKind::Skin && owned.count(id) != 0;
    };
    if (newEquippedSkin >= 0 && isOwnedSkin(newEquippedSkin)) {
        equipped = newEquippedSkin;
    } else if (equippedSkinId >= 0 && isOwnedSkin(equippedSkinId)) {
        equipped = equippedSkinId;
    }
    equippedSkinId = equipped;
}

int shopCoinMultiplier(const ShopState &state) {
    return state.hasExtra(102) ? 2 : 1; // 102 == "Двойные монеты"
}

} // namespace ekron::desktop
