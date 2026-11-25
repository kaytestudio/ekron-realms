// GameLayout.cpp: Quake 2 layout programs (cl_scrn.c SCR_ExecuteLayoutString)
// run against stats and config strings: the help computer as HelpComputer
// (p_hud.c) sends it, the single-player status bar (g_spawn.c), number
// fields, client blocks, pic paths, and bad or random programs.
#include "realms/GameLayout.h"
#include "realms/q2game.h"

#include <array>
#include <cstdio>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace {

int gFailures = 0;
int gChecks = 0;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        ++gChecks;                                                                      \
        if (!(cond)) {                                                                  \
            ++gFailures;                                                                \
            std::fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                               \
    } while (0)

using Item = realms::GameLayoutItem;
using Kind = realms::GameLayoutItem::Kind;

struct Setup {
    std::array<int16_t, Q2_MAX_STATS> stats{};
    std::map<int, std::string> config;
    int width = 640, height = 480, frame = 0;

    std::vector<Item> run(const std::string &program) const {
        realms::GameLayoutInput in;
        in.stats = stats.data();
        in.configString = [this](int i) {
            auto it = config.find(i);
            return it == config.end() ? std::string() : it->second;
        };
        in.width = width;
        in.height = height;
        in.serverFrame = frame;
        return realms::runGameLayout(program, in);
    }
};

const Item *find(const std::vector<Item> &items, Kind kind, const std::string &text) {
    for (const Item &i : items)
        if (i.kind == kind && i.text == text) return &i;
    return nullptr;
}

bool at(const Item *item, int x, int y) { return item && item->x == x && item->y == y; }

// p_hud.c HelpComputer, filled in as Com_sprintf would.
std::string helpComputer(const char *skill, const char *level, const char *help1, const char *help2, int kills,
                         int monsters, int goals, int totalGoals, int secrets, int totalSecrets) {
    char buf[1024];
    std::snprintf(buf, sizeof(buf),
                  "xv 32 yv 8 picn help "
                  "xv 202 yv 12 string2 \"%s\" "
                  "xv 0 yv 24 cstring2 \"%s\" "
                  "xv 0 yv 54 cstring2 \"%s\" "
                  "xv 0 yv 110 cstring2 \"%s\" "
                  "xv 50 yv 164 string2 \" kills     goals    secrets\" "
                  "xv 50 yv 172 string2 \"%3i/%3i     %i/%i       %i/%i\" ",
                  skill, level, help1, help2, kills, monsters, goals, totalGoals, secrets, totalSecrets);
    return buf;
}

// g_spawn.c single_statusbar
const char *kSingleStatusbar =
    "yb -24 "
    "xv 0 hnum xv 50 pic 0 "
    "if 2 xv 100 anum xv 150 pic 2 endif "
    "if 4 xv 200 rnum xv 250 pic 4 endif "
    "if 6 xv 296 pic 6 endif "
    "yb -50 "
    "if 7 xv 0 pic 7 xv 26 yb -42 stat_string 8 yb -50 endif "
    "if 9 xv 262 num 2 10 xv 296 pic 9 endif "
    "if 11 xv 148 pic 11 endif ";

void helpComputerChecks() {
    Setup s; // 640 x 480: xv 0 is x 160, yv 0 is y 120
    const auto items = s.run(helpComputer("medium", "Outer Base", "Find the\nexit", "", 3, 12, 0, 1, 1, 2));
    CHECK(at(find(items, Kind::Pic, "help"), 192, 128));
    const Item *skill = find(items, Kind::Text, "medium");
    CHECK(at(skill, 362, 132) && skill->alt);
    // cstring2: centred in 320 pixels from xv 0
    const Item *level = find(items, Kind::Text, "Outer Base");
    CHECK(at(level, 160 + (320 - 10 * 8) / 2, 144) && level->alt);
    // a two-line help message: one line each, each centred, 8 pixels apart
    CHECK(at(find(items, Kind::Text, "Find the"), 160 + (320 - 8 * 8) / 2, 174));
    CHECK(at(find(items, Kind::Text, "exit"), 160 + (320 - 4 * 8) / 2, 182));
    CHECK(at(find(items, Kind::Text, " kills     goals    secrets"), 210, 284));
    const Item *counts = find(items, Kind::Text, "  3/ 12     0/1       1/2");
    CHECK(at(counts, 210, 292) && counts->alt);
    // the empty second help message draws nothing
    size_t texts = 0;
    for (const Item &i : items) texts += i.kind == Kind::Text;
    CHECK(texts == 6);
}

void statusBarChecks() {
    Setup s;
    s.stats[Q2_STAT_HEALTH] = 100;
    s.stats[Q2_STAT_HEALTH_ICON] = 3;
    s.stats[Q2_STAT_AMMO] = 50;
    s.stats[Q2_STAT_AMMO_ICON] = 4;
    s.config[Q2_CS_IMAGES + 3] = "i_health";
    s.config[Q2_CS_IMAGES + 4] = "a_shells";
    auto items = s.run(kSingleStatusbar);
    // hnum: 3 digits right-aligned, 2 pixels in; y = height - 24
    const Item *health = find(items, Kind::Number, "100");
    CHECK(at(health, 162, 456) && !health->alt);
    CHECK(at(find(items, Kind::Pic, "i_health"), 210, 456));
    const Item *ammo = find(items, Kind::Number, "50");
    CHECK(at(ammo, 260 + 2 + 24, 456) && !ammo->alt);
    CHECK(at(find(items, Kind::Pic, "a_shells"), 310, 456));
    // no armor icon: the armor block is skipped; no pickup, timer or help icon
    CHECK(items.size() == 4);

    // low health flashes with the server frame (every 4 frames); 0 stays red
    s.stats[Q2_STAT_HEALTH] = 20;
    s.frame = 0;
    CHECK(!find(s.run(kSingleStatusbar), Kind::Number, "20")->alt);
    s.frame = 4;
    CHECK(find(s.run(kSingleStatusbar), Kind::Number, "20")->alt);
    s.stats[Q2_STAT_HEALTH] = 0;
    s.frame = 0;
    CHECK(find(s.run(kSingleStatusbar), Kind::Number, "0")->alt);
    // negative ammo is not drawn; armor with its icon is; STAT_FLASHES puts field_3 behind
    s.stats[Q2_STAT_HEALTH] = 80;
    s.stats[Q2_STAT_AMMO] = -1;
    s.stats[Q2_STAT_ARMOR] = 25;
    s.stats[Q2_STAT_ARMOR_ICON] = 5;
    s.stats[Q2_STAT_FLASHES] = 2;
    s.config[Q2_CS_IMAGES + 5] = "i_jacketarmor";
    items = s.run(kSingleStatusbar);
    CHECK(!find(items, Kind::Number, "-1"));
    CHECK(at(find(items, Kind::Number, "25"), 360 + 2 + 24, 456));
    CHECK(at(find(items, Kind::Pic, "field_3"), 360, 456));
    // the pickup: its icon, then the item name from the config string stat 8 names
    s.stats[Q2_STAT_PICKUP_ICON] = 5;
    s.stats[Q2_STAT_PICKUP_STRING] = Q2_CS_ITEMS + 7;
    s.config[Q2_CS_ITEMS + 7] = "Jacket Armor";
    items = s.run(kSingleStatusbar);
    CHECK(at(find(items, Kind::Text, "Jacket Armor"), 186, 438));
    // the timer: num 2 10
    s.stats[Q2_STAT_TIMER] = 30;
    items = s.run(kSingleStatusbar);
    CHECK(!find(items, Kind::Number, "30")); // the timer block needs its icon (if 9)
    s.stats[Q2_STAT_TIMER_ICON] = 3;
    items = s.run(kSingleStatusbar);
    CHECK(at(find(items, Kind::Number, "30"), 422 + 2, 430));
}

void fieldAndTextChecks() {
    Setup s;
    s.stats[3] = int16_t(-5);
    s.stats[4] = 12345;
    auto items = s.run("xl 10 yt 20 num 3 3 xl 100 num 9 4 xl 300 num 2 4 xl 400 num 0 4");
    CHECK(at(find(items, Kind::Number, "-5"), 10 + 2 + 24, 20));
    CHECK(at(find(items, Kind::Number, "12345"), 102, 20)); // width 9 -> 5
    CHECK(at(find(items, Kind::Number, "12"), 302, 20));    // the first digits when too wide
    CHECK(items.size() == 3);                               // width 0: nothing
    // xr / yb from the right and bottom; comments; high-bit characters are the alt set
    items = s.run("xr -48 yb -8 string \"\xC1\xC2\xC3\" // a comment\n xl 0 yt 0 string2 \"Hi\"");
    const Item *alt = find(items, Kind::Text, "ABC");
    CHECK(at(alt, 592, 472) && alt->alt);
    const Item *hi = find(items, Kind::Text, "Hi");
    CHECK(at(hi, 0, 0) && hi->alt);
    // if ... endif skips when the stat is 0, nests nothing, and runs on after
    items = s.run("if 7 string \"no\" endif string \"yes\"");
    CHECK(!find(items, Kind::Text, "no") && find(items, Kind::Text, "yes"));
    // client block: name, score, ping, time and the skin icon (male/grunt without a skin)
    s.config[Q2_CS_PLAYERSKINS + 0] = "Bob\\female/athena";
    items = s.run("client 0 32 0 7 50 3 client 160 32 1 2 0 0");
    CHECK(at(find(items, Kind::Text, "Bob"), 192, 152) && find(items, Kind::Text, "Bob")->alt);
    CHECK(at(find(items, Kind::Text, "7"), 192 + 56, 160));
    CHECK(find(items, Kind::Text, "Ping:  50") && find(items, Kind::Text, "Time:  3"));
    CHECK(at(find(items, Kind::Pic, "/players/female/athena_i.pcx"), 160, 152));
    CHECK(at(find(items, Kind::Pic, "/players/male/grunt_i.pcx"), 320, 152));
    items = s.run("ctf 0 0 0 12 1500");
    CHECK(find(items, Kind::Text, " 12 999 Bob         ") && items[0].alt);
    // pic paths and digit pics
    CHECK(realms::gameLayoutPicPath("help") == "pics/help.pcx");
    CHECK(realms::gameLayoutPicPath("/players/male/grunt_i.pcx") == "players/male/grunt_i.pcx");
    CHECK(realms::gameLayoutPicPath("\\x.pcx") == "x.pcx");
    CHECK(realms::gameLayoutDigitPic('7', false) == "num_7");
    CHECK(realms::gameLayoutDigitPic('-', true) == "anum_minus");
}

void inventoryChecks() {
    std::array<int16_t, Q2_MAX_ITEMS> inv{};
    std::map<int, std::string> names;
    auto config = [&](int i) {
        auto it = names.find(i);
        return it == names.end() ? std::string() : it->second;
    };
    inv[3] = 1;
    inv[5] = 50;
    inv[7] = 2;
    names[Q2_CS_ITEMS + 3] = "Blaster";
    names[Q2_CS_ITEMS + 5] = "Shells";
    names[Q2_CS_ITEMS + 7] = "Medkit";
    auto items = realms::gameInventoryItems(inv.data(), 5, config, 640, 480, true);
    // 640 x 480: the pic at ((640 - 256) / 2, (480 - 240) / 2 + 8), text 24 in and 16 down
    CHECK(at(find(items, Kind::Pic, "inventory"), 192, 128));
    CHECK(at(find(items, Kind::Text, "hotkey ### item"), 216, 144));
    CHECK(at(find(items, Kind::Text, "------ --- ----"), 216, 152));
    const Item *blaster = find(items, Kind::Text, "         1 Blaster");
    const Item *shells = find(items, Kind::Text, "        50 Shells");
    const Item *medkit = find(items, Kind::Text, "         2 Medkit");
    CHECK(at(blaster, 216, 160) && blaster->alt);
    CHECK(at(shells, 216, 168) && !shells->alt); // the selected item: first character set
    CHECK(at(medkit, 216, 176) && medkit->alt);
    CHECK(at(find(items, Kind::Text, ">"), 208, 168));
    CHECK(!find(realms::gameInventoryItems(inv.data(), 5, config, 640, 480, false), Kind::Text, ">"));
    // hotkeys: the key bound to "use <name>"
    items = realms::gameInventoryItems(inv.data(), 5, config, 640, 480, false,
                                       [](const std::string &n) { return n == "Medkit" ? "m" : ""; });
    CHECK(find(items, Kind::Text, "     m   2 Medkit"));
    // 30 items, the 25th selected: 17 shown, scrolled to the end (items 13...29)
    inv.fill(0);
    for (int i = 0; i < 30; i++) inv[size_t(100 + i)] = 1;
    items = realms::gameInventoryItems(inv.data(), 124, config, 640, 480, false);
    CHECK(items.size() == 3 + 17);
    CHECK(items.size() > 3 && items[3].y == 160 && items.back().y == 160 + 16 * 8);
    CHECK(items.back().alt); // item 29, not selected
    size_t plain = 0;
    for (const Item &i : items) plain += i.kind == Kind::Text && i.y >= 160 && !i.alt;
    CHECK(plain == 1); // only the selected line
    // the selection near the top: no scrolling
    items = realms::gameInventoryItems(inv.data(), 101, config, 640, 480, false);
    CHECK(items.size() == 20 && !items[4].alt && items[3].alt);
    // nothing owned: the frame only; no inventory: nothing
    inv.fill(0);
    CHECK(realms::gameInventoryItems(inv.data(), 0, config, 640, 480, true).size() == 3);
    CHECK(realms::gameInventoryItems(nullptr, 0, config, 640, 480, true).empty());
}

void badProgramChecks() {
    Setup s;
    s.stats[0] = 9999; // not an image index
    CHECK(s.run("").empty());
    CHECK(s.run("   \n\t ").empty());
    CHECK(s.run("pic 99 string \"x\"").empty());          // bad stat: stop
    CHECK(s.run("pic 0 string \"x\"").empty());           // bad image index: stop
    CHECK(s.run("if -1 string \"x\"").empty());
    CHECK(s.run("stat_string 0 string \"x\"").empty());   // stat 0 is not a config string
    CHECK(s.run("client 0 0 300 0 0 0 string \"x\"").empty());
    CHECK(s.run("xl").empty() && s.run("num").empty() && s.run("string").empty());
    CHECK(s.run("string \"unterminated").size() == 1);
    CHECK(s.run("picn " + std::string(200, 'a')).empty()); // a word of 128+ characters is dropped
    CHECK(s.run("unknown words 1 2 3 string \"ok\"").size() == 1);
    realms::GameLayoutInput none;
    CHECK(realms::runGameLayout("string \"x\"", none).empty()); // no stats
    // random programs: no crash, at most 1024 items
    std::mt19937 rng(7);
    const char *words[] = {"xl", "xr", "xv", "yt", "yb", "yv", "pic", "picn", "num", "hnum", "anum", "rnum",
                           "if", "endif", "string", "string2", "cstring", "cstring2", "stat_string", "client",
                           "ctf", "\"", "-3", "0", "15", "40", "\n", "//"};
    for (int round = 0; round < 2000; round++) {
        for (auto &v : s.stats) v = int16_t(rng() % 64) - 8;
        std::string program;
        const int n = int(rng() % 64);
        for (int i = 0; i < n; i++) {
            if (rng() % 8 == 0) program.push_back(char(rng() % 256));
            else program += words[rng() % (sizeof(words) / sizeof(words[0]))];
            program.push_back(' ');
        }
        CHECK(s.run(program).size() <= 1024);
    }
    std::string many;
    for (int i = 0; i < 3000; i++) many += "string x ";
    CHECK(s.run(many).size() == 1024);
}

} // namespace

int main() {
    helpComputerChecks();
    statusBarChecks();
    fieldAndTextChecks();
    inventoryChecks();
    badProgramChecks();
    std::printf("[game_layout] %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
