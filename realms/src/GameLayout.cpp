// Quake 2 layout programs (see GameLayout.h). Follows SCR_ExecuteLayoutString,
// SCR_DrawField and DrawHUDString in cl_scrn.c, and COM_Parse in q_shared.c,
// of the GPL Quake 2 source (engine/client/cl_scrn.pas in this repository).
#include "realms/GameLayout.h"
#include "realms/q2game.h"

#include <cstdio>
#include <cstdlib>

namespace realms {
namespace {

constexpr size_t kMaxTokenChars = 128; // MAX_TOKEN_CHARS
constexpr size_t kMaxItems = 1024;
constexpr int kCharWidth = 24;         // CHAR_WIDTH: a status bar digit

// COM_Parse: the next word or "quoted string"; `//` comments run to the end
// of the line. Returns false at the end of the program.
bool parse(std::string_view s, size_t &pos, std::string &token) {
    token.clear();
    for (;;) {
        while (pos < s.size() && static_cast<unsigned char>(s[pos]) <= ' ') pos++;
        if (pos >= s.size()) return false;
        if (s[pos] == '/' && pos + 1 < s.size() && s[pos + 1] == '/') {
            while (pos < s.size() && s[pos] != '\n') pos++;
            continue;
        }
        break;
    }
    if (s[pos] == '"') {
        pos++;
        while (pos < s.size() && s[pos] != '"') {
            if (token.size() < kMaxTokenChars) token.push_back(s[pos]);
            pos++;
        }
        if (pos < s.size()) pos++; // closing quote
        return true;
    }
    while (pos < s.size() && static_cast<unsigned char>(s[pos]) > ' ') {
        if (token.size() < kMaxTokenChars) token.push_back(s[pos]);
        pos++;
    }
    if (token.size() == kMaxTokenChars) token.clear(); // as COM_Parse
    return true;
}

int toInt(const std::string &token) { return int(std::strtol(token.c_str(), nullptr, 10)); } // atoi

// Quake 2 characters: the high bit picks the second set. Text items carry
// plain ASCII and one `alt` flag; other control glyphs become spaces.
GameLayoutItem textItem(int x, int y, std::string_view line, bool alt) {
    GameLayoutItem item;
    item.kind = GameLayoutItem::Kind::Text;
    item.x = x;
    item.y = y;
    size_t high = 0, printable = 0;
    for (char ch : line) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (c & 0x80) high++;
        c &= 0x7f;
        if (c < ' ' || c == 0x7f) c = ' ';
        if (c != ' ') printable++;
        item.text.push_back(char(c));
    }
    item.alt = alt != (printable > 0 && high >= printable);
    return item;
}

} // namespace

std::vector<GameLayoutItem> gameInventoryItems(const int16_t *inventory, int selected,
                                               const std::function<std::string(int)> &configString, int width,
                                               int height, bool cursor,
                                               const std::function<std::string(const std::string &)> &hotkey) {
    constexpr int kDisplayItems = 17;
    std::vector<GameLayoutItem> items;
    if (!inventory) return items;
    auto name = [&](int item) { return configString ? configString(Q2_CS_ITEMS + item) : std::string(); };
    std::vector<int> owned;
    int selectedNum = 0;
    for (int i = 0; i < Q2_MAX_ITEMS; i++) {
        if (i == selected) selectedNum = int(owned.size());
        if (inventory[i] != 0) owned.push_back(i);
    }
    const int num = int(owned.size());
    int top = selectedNum - kDisplayItems / 2; // scroll to keep the selection in view
    if (num - top < kDisplayItems) top = num - kDisplayItems;
    if (top < 0) top = 0;

    int x = (width - 256) / 2, y = (height - 240) / 2;
    GameLayoutItem pic;
    pic.kind = GameLayoutItem::Kind::Pic;
    pic.x = x;
    pic.y = y + 8;
    pic.text = "inventory";
    items.push_back(pic);
    y += 24;
    x += 24;
    items.push_back(textItem(x, y, "hotkey ### item", false));
    items.push_back(textItem(x, y + 8, "------ --- ----", false));
    y += 16;
    for (int i = top; i < num && i < top + kDisplayItems; i++) {
        const int item = owned[size_t(i)];
        const std::string itemName = name(item);
        const std::string key = hotkey ? hotkey(itemName) : std::string();
        char line[96];
        std::snprintf(line, sizeof(line), "%6.6s %3i %.64s", key.c_str(), int(inventory[item]), itemName.c_str());
        if (item == selected && cursor) items.push_back(textItem(x - 8, y, ">", false));
        items.push_back(textItem(x, y, line, item != selected));
        y += 8;
    }
    return items;
}

std::string gameLayoutPicPath(const std::string &name) {
    if (!name.empty() && (name[0] == '/' || name[0] == '\\')) return name.substr(1);
    return "pics/" + name + ".pcx";
}

std::string gameLayoutDigitPic(char ch, bool alt) {
    std::string name = alt ? "anum_" : "num_";
    if (ch == '-') return name + "minus";
    return name + ch;
}

std::vector<GameLayoutItem> runGameLayout(std::string_view s, const GameLayoutInput &in) {
    std::vector<GameLayoutItem> items;
    if (!in.stats) return items;
    const int16_t *stats = in.stats;
    auto config = [&](int index) -> std::string {
        if (index < 0 || index >= Q2_MAX_CONFIGSTRINGS || !in.configString) return {};
        return in.configString(index);
    };
    auto add = [&](GameLayoutItem item) {
        if (items.size() < kMaxItems) items.push_back(std::move(item));
    };
    auto pic = [&](int x, int y, std::string name) {
        if (name.empty()) return;
        GameLayoutItem item;
        item.kind = GameLayoutItem::Kind::Pic;
        item.x = x;
        item.y = y;
        item.text = std::move(name);
        add(std::move(item));
    };
    // DrawString / DrawAltString: one line, no wrapping.
    auto drawString = [&](int x, int y, std::string_view text, bool alt) {
        if (!text.empty()) add(textItem(x, y, text, alt));
    };
    // DrawHUDString: lines split at '\n', each centred in 320 pixels.
    auto hudString = [&](int x, int y, std::string_view text, bool alt) {
        const int margin = x;
        size_t start = 0;
        while (start <= text.size()) {
            size_t end = text.find('\n', start);
            if (end == std::string_view::npos) end = text.size();
            const std::string_view line = text.substr(start, end - start);
            if (!line.empty()) add(textItem(margin + (320 - int(line.size()) * 8) / 2, y, line, alt));
            if (end >= text.size()) break;
            start = end + 1;
            y += 8;
        }
    };
    // SCR_DrawField: right-aligned in `width` digits (1..5), the first digits
    // when the number is wider.
    auto field = [&](int x, int y, bool alt, int width, int value) {
        if (width < 1) return;
        if (width > 5) width = 5;
        char num[16];
        std::snprintf(num, sizeof(num), "%d", value);
        std::string digits(num);
        if (int(digits.size()) > width) digits.resize(size_t(width));
        GameLayoutItem item;
        item.kind = GameLayoutItem::Kind::Number;
        item.x = x + 2 + kCharWidth * (width - int(digits.size()));
        item.y = y;
        item.text = std::move(digits);
        item.alt = alt;
        add(std::move(item));
    };
    auto statIndex = [](const std::string &token, int &index) {
        index = toInt(token);
        return index >= 0 && index < Q2_MAX_STATS;
    };
    const bool flash = ((in.serverFrame >> 2) & 1) != 0;

    int x = 0, y = 0;
    size_t pos = 0;
    std::string token, arg;
    auto next = [&](std::string &out) {
        if (!parse(s, pos, out)) out.clear();
        return out;
    };
    while (parse(s, pos, token)) {
        if (token == "xl") {
            x = toInt(next(arg));
        } else if (token == "xr") {
            x = in.width + toInt(next(arg));
        } else if (token == "xv") {
            x = in.width / 2 - 160 + toInt(next(arg));
        } else if (token == "yt") {
            y = toInt(next(arg));
        } else if (token == "yb") {
            y = in.height + toInt(next(arg));
        } else if (token == "yv") {
            y = in.height / 2 - 120 + toInt(next(arg));
        } else if (token == "pic") { // a pic from a stat (an image index)
            int stat = 0;
            if (!statIndex(next(arg), stat)) break;
            const int value = stats[stat];
            if (value < 0 || value >= Q2_MAX_IMAGES) break;
            pic(x, y, config(Q2_CS_IMAGES + value));
        } else if (token == "client") { // a deathmatch client block
            x = in.width / 2 - 160 + toInt(next(arg));
            y = in.height / 2 - 120 + toInt(next(arg));
            const int client = toInt(next(arg));
            if (client < 0 || client >= Q2_MAX_CLIENTS) break;
            const int score = toInt(next(arg));
            const int ping = toInt(next(arg));
            const int time = toInt(next(arg));
            // CL_ParseClientinfo: "name\model/skin"
            const std::string info = config(Q2_CS_PLAYERSKINS + client);
            const size_t slash = info.find('\\');
            const std::string name = info.substr(0, slash);
            std::string skin = slash == std::string::npos ? std::string() : info.substr(slash + 1);
            if (skin.empty() || skin.find('/') == std::string::npos) skin = "male/grunt";
            char line[64];
            drawString(x + 32, y, name, true);
            drawString(x + 32, y + 8, "Score: ", false);
            std::snprintf(line, sizeof(line), "%d", score);
            drawString(x + 32 + 7 * 8, y + 8, line, true);
            std::snprintf(line, sizeof(line), "Ping:  %d", ping);
            drawString(x + 32, y + 16, line, false);
            std::snprintf(line, sizeof(line), "Time:  %d", time);
            drawString(x + 32, y + 24, line, false);
            pic(x, y, "/players/" + skin + "_i.pcx");
        } else if (token == "ctf") { // a ctf client block
            x = in.width / 2 - 160 + toInt(next(arg));
            y = in.height / 2 - 120 + toInt(next(arg));
            const int client = toInt(next(arg));
            if (client < 0 || client >= Q2_MAX_CLIENTS) break;
            const int score = toInt(next(arg));
            int ping = toInt(next(arg));
            if (ping > 999) ping = 999;
            const std::string info = config(Q2_CS_PLAYERSKINS + client);
            char line[80];
            std::snprintf(line, sizeof(line), "%3d %3d %-12.12s", score, ping, info.substr(0, info.find('\\')).c_str());
            drawString(x, y, line, client == 0); // the one player is client 0
        } else if (token == "picn") { // a pic by name
            pic(x, y, next(arg));
        } else if (token == "num") { // a number from a stat
            const int width = toInt(next(arg));
            int stat = 0;
            if (!statIndex(next(arg), stat)) break;
            field(x, y, false, width, stats[stat]);
        } else if (token == "hnum") { // health
            const int value = stats[Q2_STAT_HEALTH];
            const bool alt = value > 25 ? false : value > 0 ? flash : true;
            if (stats[Q2_STAT_FLASHES] & 1) pic(x, y, "field_3");
            field(x, y, alt, 3, value);
        } else if (token == "anum") { // ammo; negative: not shown
            const int value = stats[Q2_STAT_AMMO];
            if (value < 0) continue;
            const bool alt = value > 5 ? false : flash;
            if (stats[Q2_STAT_FLASHES] & 4) pic(x, y, "field_3");
            field(x, y, alt, 3, value);
        } else if (token == "rnum") { // armor; none: not shown
            const int value = stats[Q2_STAT_ARMOR];
            if (value < 1) continue;
            if (stats[Q2_STAT_FLASHES] & 2) pic(x, y, "field_3");
            field(x, y, false, 3, value);
        } else if (token == "stat_string") { // the config string a stat names
            int stat = 0;
            if (!statIndex(next(arg), stat)) break;
            const int index = stats[stat];
            if (index < 0 || index >= Q2_MAX_CONFIGSTRINGS) break;
            drawString(x, y, config(index), false);
        } else if (token == "cstring" || token == "cstring2") {
            hudString(x, y, next(arg), token == "cstring2");
        } else if (token == "string" || token == "string2") {
            drawString(x, y, next(arg), token == "string2");
        } else if (token == "if") { // skip to endif when the stat is 0
            int stat = 0;
            if (!statIndex(next(arg), stat)) break;
            if (stats[stat] == 0) {
                while (parse(s, pos, token) && token != "endif") {
                }
            }
        }
    }
    return items;
}

} // namespace realms
