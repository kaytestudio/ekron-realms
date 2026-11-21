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
    using i18n::Str;
    static const std::vector<ShopCategory> kCats = {
        {Str::ShopCategorySkins, ShopKind::Skin, {
            {1, ShopKind::Skin, Str::ShopSkinFlame,       Str::ShopSkinFlameDesc,       150, 1.00f, 0.32f, 0.10f, ShopEffect::None, 0},
            {2, ShopKind::Skin, Str::ShopSkinFrost,       Str::ShopSkinFrostDesc,       150, 0.40f, 0.85f, 1.00f, ShopEffect::None, 0},
            {3, ShopKind::Skin, Str::ShopSkinEmerald,     Str::ShopSkinEmeraldDesc,     200, 0.25f, 0.95f, 0.40f, ShopEffect::None, 0},
            {4, ShopKind::Skin, Str::ShopSkinAzure,       Str::ShopSkinAzureDesc,       200, 0.30f, 0.55f, 1.00f, ShopEffect::None, 0},
            {5, ShopKind::Skin, Str::ShopSkinGoldenSeal,  Str::ShopSkinGoldenSealDesc,  350, 1.00f, 0.82f, 0.25f, ShopEffect::None, 0},
            {6, ShopKind::Skin, Str::ShopSkinVioletStorm, Str::ShopSkinVioletStormDesc, 350, 0.75f, 0.30f, 1.00f, ShopEffect::None, 0},
            {7, ShopKind::Skin, Str::ShopSkinToxicRose,   Str::ShopSkinToxicRoseDesc,   400, 1.00f, 0.20f, 0.55f, ShopEffect::None, 0},
            {8, ShopKind::Skin, Str::ShopSkinNeon,        Str::ShopSkinNeonDesc,        500, 0.60f, 1.00f, 0.95f, ShopEffect::None, 0},
        }},
        {Str::ShopCategoryExtras, ShopKind::Extra, {
            {101, ShopKind::Extra, Str::ShopExtraBonusWallet,     Str::ShopExtraBonusWalletDesc,     300, 1.00f, 1.00f, 1.00f, ShopEffect::CoinBonus, 500},
            {102, ShopKind::Extra, Str::ShopExtraDoubleCoins,     Str::ShopExtraDoubleCoinsDesc,     500, 1.00f, 1.00f, 1.00f, ShopEffect::DoubleCoins, 0},
            {103, ShopKind::Extra, Str::ShopExtraGoldenCrosshair, Str::ShopExtraGoldenCrosshairDesc, 250, 1.00f, 1.00f, 1.00f, ShopEffect::GoldenCrosshair, 0},
            {104, ShopKind::Extra, Str::ShopExtraCamoHud,         Str::ShopExtraCamoHudDesc,         250, 1.00f, 1.00f, 1.00f, ShopEffect::CamoHud, 0},
            {105, ShopKind::Extra, Str::ShopExtraNeonSight,       Str::ShopExtraNeonSightDesc,       400, 1.00f, 1.00f, 1.00f, ShopEffect::NeonSight, 0},
            {106, ShopKind::Extra, Str::ShopExtraStartWithFlight, Str::ShopExtraStartWithFlightDesc, 800, 1.00f, 1.00f, 1.00f, ShopEffect::StartWithFlight, 0},
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
    using i18n::Str;
    switch (effect) {
        case ShopEffect::CoinBonus:       return i18n::tr(Str::ShopEffectCoinBonus);
        case ShopEffect::DoubleCoins:     return i18n::tr(Str::ShopEffectDoubleCoins);
        case ShopEffect::GoldenCrosshair: return i18n::tr(Str::ShopExtraGoldenCrosshair);
        case ShopEffect::CamoHud:         return i18n::tr(Str::ShopExtraCamoHud);
        case ShopEffect::NeonSight:       return i18n::tr(Str::ShopExtraNeonSight);
        case ShopEffect::StartWithFlight: return i18n::tr(Str::ShopExtraStartWithFlight);
        case ShopEffect::None:
        default:                           return "";
    }
}

const char *shopItemName(const ShopItem &item) { return i18n::tr(item.name); }
const char *shopItemDescription(const ShopItem &item) { return i18n::tr(item.description); }
const char *shopCategoryName(const ShopCategory &category) { return i18n::tr(category.name); }

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
        reasonOut = kShopErrorUnknown;
        return false;
    }
    if (owned.count(itemId)) {
        reasonOut = kShopErrorOwned;
        return false;
    }
    if (coins < item->price) {
        reasonOut = kShopErrorInsufficient;
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
    return state.hasExtra(102) ? 2 : 1; // 102 == "Double Coins"
}

} // namespace ekron::desktop
