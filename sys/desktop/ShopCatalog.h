#pragma once

// In-game shop: Gravity Gun skins and coin unlocks ("extras", active once
// owned). Offline fallback and persistence; with the relay connected the
// server owns the wallet (DesktopOnlineClient, sandbox_relay.py).

#include "Localization.h"

#include <string>
#include <unordered_set>
#include <vector>

namespace ekron::desktop {

enum class ShopKind {
    Skin,
    Extra,
};

// Cosmetic/functional effects granted by Extra items. Skins use Effect::None.
enum class ShopEffect {
    None = 0,
    CoinBonus,      // instantly grants a coin bonus
    DoubleCoins,    // doubles the passive coin accrual rate
    GoldenCrosshair,// golden crosshair cosmetic
    CamoHud,        // alternate HUD accent colour
    NeonSight,      // neon crosshair cosmetic
    StartWithFlight,// spawn with noclip flight enabled
};

struct ShopItem {
    int id;                 // unique catalog id
    ShopKind kind;
    i18n::Str name;         // localized label (see LocalizationStrings.h)
    i18n::Str description;  // localized one-line description
    int price;              // cost in gold coins
    float tintR = 1.0f;     // skin beam tint (RGB, 0..1)
    float tintG = 1.0f;
    float tintB = 1.0f;
    ShopEffect effect = ShopEffect::None;
    long long effectValue = 0; // e.g. coin bonus amount
};

struct ShopCategory {
    i18n::Str name;
    ShopKind kind;
    std::vector<ShopItem> items;
};

// Stable, statically-allocated catalog. Order is preserved for UI rendering.
const std::vector<ShopCategory> &shopCategories();

// Look up a single item by id (nullptr when unknown).
const ShopItem *findShopItem(int id);

// Human-readable label for an Extra effect in the current UI language
// (empty for ShopEffect::None).
const char *shopEffectLabel(ShopEffect effect);

// Localized item name / description / category title in the current UI language.
const char *shopItemName(const ShopItem &item);
const char *shopItemDescription(const ShopItem &item);
const char *shopCategoryName(const ShopCategory &category);

// Stable purchase failure codes. They are shared with the presence relay
// (server/sandbox_relay.py "purchase" replies) and translated for display by
// ekron::i18n::localizeShopError().
inline constexpr const char *kShopErrorUnknown = "unknown";
inline constexpr const char *kShopErrorOwned = "owned";
inline constexpr const char *kShopErrorInsufficient = "insufficient";

// Persisted wallet + ownership state for one player.
class ShopState {
public:
    long long coins = 0;
    std::unordered_set<int> owned;
    int equippedSkinId = -1;
    std::unordered_set<int> activeExtras;

    // Base passive coin accrual, in coins per second of play.
    static constexpr long long kBaseCoinsPerSecond = 1;

    // Load from a JSON file. Returns true when a readable store was found.
    // Never throws: a missing or malformed file leaves the default state.
    bool load(const std::string &path);

    // Persist to a JSON file. Best-effort; failures are ignored (the in-game
    // economy is not critical state).
    void save(const std::string &path) const;

    // Attempt to purchase an item. Returns true on success and deducts the
    // price / records ownership. On failure returns false and fills
    // reasonOut with a stable, language-neutral code (kShopErrorUnknown,
    // kShopErrorOwned or kShopErrorInsufficient) for the UI to translate.
    bool tryBuy(int itemId, std::string &reasonOut);

    bool owns(int itemId) const { return owned.count(itemId) != 0; }
    bool isEquippedSkin(int itemId) const { return equippedSkinId == itemId; }

    // Equip a skin (only meaningful for Skin items). Returns false when the
    // item is not owned or is not a skin.
    bool equipSkin(int itemId);

    // Whether an owned Extra is active. Extras auto-activate on purchase, so
    // this is true for any owned Extra; exposed for symmetry and future
    // toggleable unlocks.
    bool hasExtra(int itemId) const;

    // Passive coin accrual. `multiplier` scales the base rate (e.g. 2 for the
    // Double Coins unlock). Ignored when <= 0.
    void accrue(float seconds, int multiplier = 1);

    // Grant or revoke coins directly (used by the Coin Bonus unlock and by
    // server-authoritative wallet sync).
    void addCoins(long long amount);

    // The currently equipped skin, or nullptr when none is equipped.
    const ShopItem *equippedSkin() const;

    // Replace the whole state (used when syncing from the online relay).
    void applyRemote(long long newCoins, const std::vector<int> &newOwned,
                     int newEquippedSkin);

private:
    bool extrasAutoActivate(int itemId);

    // Fractional seconds banked for passive accrual so the integer coin
    // balance does not lose the sub-second remainder between frames.
    double accruedSeconds_ = 0.0;
};

// Multiplier applied to passive accrual when the player owns the Double Coins
// unlock. Pure helper so the UI and the client agree on the rule.
int shopCoinMultiplier(const ShopState &state);

} // namespace ekron::desktop
