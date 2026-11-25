// Quake 2 HUD layout programs, as cl_scrn.c's SCR_ExecuteLayoutString runs
// them: the status bar (CS_STATUSBAR) and the svc_layout screens (the help
// computer, scores). A program is a list of words ("xv 32 yv 8 picn help
// ...") read against the player's stats and the config strings.
// runGameLayout turns one into draw items, in Quake 2 screen pixels for a
// screen of `width` x `height`: text is 8 x 8 characters, numbers are 24 x 24
// digits, pics are drawn at their own size. Bad stat or config string
// indices stop the program (Quake 2 would drop to the console).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace realms {

struct GameLayoutItem {
    enum class Kind { Pic, Text, Number };
    Kind kind = Kind::Text;
    int x = 0, y = 0;   // top left, screen pixels
    std::string text;   // Pic: image name (see gameLayoutPicPath); Text: one line (ASCII);
                        // Number: the digits to draw, 24 pixels apart ("-" for a minus)
    bool alt = false;   // Text: Quake 2's second character set (string2, cstring2: drawn green);
                        // Number: the second digit set (anum_*: red, low health / ammo flashing)
};

struct GameLayoutInput {
    const int16_t *stats = nullptr;                // Q2_MAX_STATS values (player_state_t::stats)
    std::function<std::string(int)> configString;  // "" when unset
    int width = 320, height = 240;                 // screen, in the same pixels as the items
    int serverFrame = 0;                           // flashing numbers (every 4 frames)
};

std::vector<GameLayoutItem> runGameLayout(std::string_view program, const GameLayoutInput &input);

// The inventory screen, as cl_inv.c's CL_DrawInventory draws it: the
// "inventory" pic, a header, and up to 17 owned items around the selected
// one (STAT_SELECTED_ITEM) as "hotkey count name". Items other than the
// selected one use the second character set; with `cursor` (blinking, 5 Hz
// in Quake 2) a ">" marks the selected one. `inventory` holds Q2_MAX_ITEMS
// counts; `hotkey` names the key bound to "use <item name>" ("" for none).
std::vector<GameLayoutItem> gameInventoryItems(const int16_t *inventory, int selected,
                                               const std::function<std::string(int)> &configString, int width,
                                               int height, bool cursor,
                                               const std::function<std::string(const std::string &)> &hotkey = {});

// The file of a Pic item, as ref_gl's Draw_FindPic finds it: "help" ->
// "pics/help.pcx"; a name starting with '/' or '\\' is a path from the game
// folder ("/players/male/grunt_i.pcx" -> "players/male/grunt_i.pcx").
std::string gameLayoutPicPath(const std::string &name);

// The pic names of the status bar digits (sb_nums): num_0 ... num_9,
// num_minus, and anum_* for the second set. `ch` is '0'...'9' or '-'.
std::string gameLayoutDigitPic(char ch, bool alt);

} // namespace realms
