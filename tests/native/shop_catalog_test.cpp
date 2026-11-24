// Unit tests for the Ekron Realms in-game store catalog, wallet and purchase
// rules. SDL/GL-independent so it builds and runs without graphics deps.
#include "ShopCatalog.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::cerr << "FAIL: " #cond " @ line " << __LINE__ << "\n"; ++failures; } } while (0)

// A file in the system temp folder (/tmp, %TEMP%).
static std::string tempPath(const char *name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

static bool fileExists(const std::string &path) {
    std::ifstream in(path);
    return in.good();
}

int main() {
    using namespace ekron::desktop;

    // Catalog shape.
    const auto &cats = shopCategories();
    CHECK(cats.size() == 2);
    CHECK(cats[0].kind == ShopKind::Skin);
    CHECK(cats[1].kind == ShopKind::Extra);
    CHECK(!cats[0].items.empty());
    CHECK(!cats[1].items.empty());

    // Unique ids and valid prices/tints.
    for (const auto &cat : cats) {
        int lastId = -1;
        for (const auto &it : cat.items) {
            CHECK(it.id != lastId);
            CHECK(it.id > 0);
            CHECK(it.price > 0);
            CHECK(it.tintR >= 0.0f && it.tintR <= 1.0f);
            CHECK(it.tintG >= 0.0f && it.tintG <= 1.0f);
            CHECK(it.tintB >= 0.0f && it.tintB <= 1.0f);
            lastId = it.id;
        }
    }

    // findShopItem.
    CHECK(findShopItem(1) != nullptr);
    CHECK(findShopItem(1)->kind == ShopKind::Skin);
    CHECK(findShopItem(102) != nullptr);
    CHECK(findShopItem(102)->kind == ShopKind::Extra);
    CHECK(findShopItem(999999) == nullptr);

    // Fresh wallet cannot afford anything expensive, but can buy a cheap skin.
    ShopState state;
    state.coins = 150;
    std::string reason;
    CHECK(state.tryBuy(1, reason));       // Flame skin, 150
    CHECK(state.owns(1));
    CHECK(state.coins == 0);
    CHECK(state.equippedSkinId == 1);     // buying a skin equips it
    CHECK(!state.tryBuy(1, reason));      // already owned
    CHECK(reason == kShopErrorOwned);

    // Not enough coins.
    ShopState poor;
    poor.coins = 10;
    CHECK(!poor.tryBuy(5, reason));       // Gold skin, 350
    CHECK(reason == kShopErrorInsufficient);
    CHECK(!poor.owns(5));

    // Unknown item.
    CHECK(!poor.tryBuy(12345, reason));
    CHECK(reason == kShopErrorUnknown);

    // Extras auto-activate and grant their effects through the state.
    ShopState extra;
    extra.coins = 1000;
    CHECK(extra.tryBuy(102, reason));     // Double Coins, 500
    CHECK(extra.hasExtra(102));
    CHECK(!extra.hasExtra(101));
    CHECK(shopCoinMultiplier(extra) == 2);
    CHECK(shopCoinMultiplier(state) == 1);

    // Coin Bonus grants coins on purchase.
    ShopState bonus;
    bonus.coins = 1000;
    CHECK(bonus.tryBuy(101, reason));     // Coin Bonus, 300, +500
    CHECK(bonus.coins == 1000 - 300 + 500);
    // Passive accrual respects the multiplier and banks fractional seconds.
    ShopState acc;
    acc.accrue(0.5f, 1);
    CHECK(acc.coins == 0);                // 0.5s * 1 = 0.5 -> no whole coin yet
    acc.accrue(0.5f, 1);
    CHECK(acc.coins == 1);                // accumulated to 1.0s
    acc.accrue(1.0f, 2);
    CHECK(acc.coins == 1 + 2);            // 1s * 2 = 2 coins

    // addCoins clamps at zero.
    acc.addCoins(-100000);
    CHECK(acc.coins == 0);

    // Equip / unequip rules.
    CHECK(state.equipSkin(1));
    CHECK(state.isEquippedSkin(1));
    CHECK(!state.equipSkin(102));        // not a skin
    ShopState noSkin;
    noSkin.coins = 1000;
    CHECK(!noSkin.equipSkin(2));         // not owned
    CHECK(noSkin.tryBuy(2, reason));
    CHECK(noSkin.equipSkin(2));

    // Persistence round-trip.
    const std::string path = tempPath("ekron-shop-test-store.json");
    std::remove(path.c_str());
    ShopState saver;
    saver.coins = 777;
    CHECK(saver.tryBuy(3, reason));       // Emerald skin, 200 -> 577
    CHECK(saver.tryBuy(104, reason));     // Camo HUD, 250 -> 327
    saver.save(path);
    CHECK(fileExists(path));

    ShopState loader;
    CHECK(loader.load(path));
    CHECK(loader.coins == saver.coins);
    CHECK(loader.owns(3));
    CHECK(loader.owns(104));
    CHECK(loader.equippedSkinId == 3);
    CHECK(loader.hasExtra(104));
    CHECK(loader.coins == 327);

    // Malformed / missing store is tolerated.
    CHECK(!loader.load(tempPath("ekron-shop-does-not-exist-xyz.json")));
    const std::string badPath = tempPath("ekron-shop-bad.json");
    std::ofstream bad(badPath);
    bad << "{ not json";
    bad.close();
    ShopState afterBad;
    CHECK(!afterBad.load(badPath));
    CHECK(afterBad.coins == 0);

    // Remote sync replaces state and keeps an owned skin equipped.
    ShopState remote;
    remote.applyRemote(500, {1, 2, 102}, -1);
    CHECK(remote.coins == 500);
    CHECK(remote.owns(1) && remote.owns(2) && remote.owns(102));
    CHECK(remote.hasExtra(102));
    CHECK(remote.equippedSkinId == -1);  // server sent none and none was local

    // An explicit server-tracked equipped skin is honoured; otherwise the
    // locally equipped skin is preserved when still owned.
    ShopState remote2;
    remote2.applyRemote(500, {1, 2, 102}, 2);
    CHECK(remote2.equippedSkinId == 2);
    ShopState remote3;
    remote3.equippedSkinId = 1;
    remote3.applyRemote(500, {1, 2, 102}, -1);
    CHECK(remote3.equippedSkinId == 1);  // kept because still owned

    // Effect labels are non-null for known effects.
    CHECK(std::string(shopEffectLabel(ShopEffect::DoubleCoins)).size() > 0);
    CHECK(std::string(shopEffectLabel(ShopEffect::None)).empty());

    // Purchase failure codes match the presence relay protocol and are
    // translated for every supported language.
    namespace i18n = ekron::i18n;
    for (const char *code : {kShopErrorUnknown, kShopErrorOwned, kShopErrorInsufficient}) {
        for (size_t l = 0; l < i18n::kLanguageCount; ++l) {
            i18n::setLanguage(static_cast<i18n::Language>(l));
            CHECK(!i18n::localizeShopError(code).empty());
        }
    }

    // Item names follow the UI language.
    const ShopItem *flame = findShopItem(1);
    i18n::setLanguage(i18n::Language::English);
    CHECK(std::string(shopItemName(*flame)) == "Flame");
    CHECK(std::string(shopCategoryName(cats[1])) == "UPGRADES");
    i18n::setLanguage(i18n::Language::Russian);
    CHECK(std::string(shopItemName(*flame)) == "Пламя");
    CHECK(std::string(shopItemDescription(*flame)) == "Ярко-красный выброс грави-пушки");
    i18n::setLanguage(i18n::Language::PortuguesePT);
    CHECK(std::string(shopItemName(*flame)) == "Chama");
    CHECK(std::string(shopEffectLabel(ShopEffect::CoinBonus)) == "+500 moedas");
    i18n::setLanguage(i18n::Language::SpanishES);
    CHECK(std::string(shopItemName(*flame)) == "Llama");
    CHECK(std::string(shopCategoryName(cats[1])) == "MEJORAS");
    CHECK(std::string(shopEffectLabel(ShopEffect::CoinBonus)) == "+500 monedas");
    i18n::setLanguage(i18n::Language::English);

    if (failures == 0) {
        std::cout << "Shop catalog, wallet, purchase and persistence OK\n";
        return 0;
    }
    std::cerr << failures << " shop catalog test(s) failed\n";
    return 1;
}
