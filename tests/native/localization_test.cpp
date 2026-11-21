// Unit tests for the desktop localization layer (English, Russian and
// Portuguese (Portugal)). SDL/GL-independent: it checks the string table,
// placeholder consistency, locale parsing and that the built-in pixel font
// can render every translated character.
#include "DesktopFont.h"
#include "Localization.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::cerr << "FAIL: " #cond " @ line " << __LINE__ << "\n"; ++failures; } } while (0)

namespace i18n = ekron::i18n;

namespace {

std::set<std::string> placeholders(std::string_view text) {
    std::set<std::string> out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '{') continue;
        const size_t close = text.find('}', i);
        if (close == std::string_view::npos) break;
        out.insert(std::string(text.substr(i, close - i + 1)));
        i = close;
    }
    return out;
}

// Returns the first codepoint the pixel font cannot draw, or 0.
uint32_t firstMissingGlyph(std::string_view text) {
    size_t i = 0;
    while (i < text.size()) {
        const uint32_t cp = ekron::font::nextUtf8Codepoint(text, i);
        if (cp != '?' && ekron::font::glyphSlot(cp) == ekron::font::kFallbackSlot) return cp;
    }
    return 0;
}

const char *languageLabel(size_t l) { return i18n::languageCode(static_cast<i18n::Language>(l)); }

} // namespace

int main() {
    // --- Font atlas -------------------------------------------------------
    const auto &glyphs = ekron::font::glyphBitmaps();
    CHECK(glyphs.size() <= size_t(ekron::font::kAtlasCols * ekron::font::kAtlasRows));
    for (size_t g = 1; g < glyphs.size(); ++g) {
        if (glyphs[g] == 0) { std::cerr << "empty glyph slot " << g << "\n"; ++failures; }
    }
    CHECK(ekron::font::glyphSlot(0x00E3) != ekron::font::kFallbackSlot); // ã
    CHECK(ekron::font::glyphSlot(0x00C7) != ekron::font::kFallbackSlot); // Ç
    CHECK(ekron::font::glyphSlot(0x0451) != ekron::font::kFallbackSlot); // ё
    CHECK(ekron::font::glyphSlot(0x2026) != ekron::font::kFallbackSlot); // …
    CHECK(ekron::font::glyphSlot(0x4E2D) == ekron::font::kFallbackSlot); // unsupported CJK

    // --- String table -----------------------------------------------------
    const std::vector<std::string_view> universalKeys = {
        "[Q]", "[G]", "[1-3]", "[Esc]", "[E]", "[R]", "[V]", "[H]", "[2]", "[W/S]", "[A/D]", "[WASD]", "spawn.json"};
    for (size_t k = 0; k < i18n::kStringCount; ++k) {
        const auto key = static_cast<i18n::Str>(k);
        const std::string english = i18n::tr(key, i18n::Language::English);
        const auto englishPlaceholders = placeholders(english);
        for (size_t l = 0; l < i18n::kLanguageCount; ++l) {
            const std::string text = i18n::tr(key, static_cast<i18n::Language>(l));
            if (text.empty()) {
                std::cerr << "empty translation: key #" << k << " [" << languageLabel(l) << "]\n";
                ++failures;
                continue;
            }
            if (placeholders(text) != englishPlaceholders) {
                std::cerr << "placeholder mismatch: \"" << text << "\" vs \"" << english << "\"\n";
                ++failures;
            }
            if (const uint32_t cp = firstMissingGlyph(text)) {
                std::cerr << "font has no glyph for U+" << std::hex << cp << std::dec << " in \"" << text << "\"\n";
                ++failures;
            }
            for (const auto token : universalKeys) {
                if (english.find(token) != std::string::npos && text.find(token) == std::string::npos) {
                    std::cerr << "missing key hint " << token << " in \"" << text << "\"\n";
                    ++failures;
                }
            }
        }
    }

    // Spot-check a few translations.
    CHECK(std::string(i18n::tr(i18n::Str::PauseResume, i18n::Language::English)) == "Resume game");
    CHECK(std::string(i18n::tr(i18n::Str::PauseResume, i18n::Language::Russian)) == "Продолжить игру");
    CHECK(std::string(i18n::tr(i18n::Str::PauseResume, i18n::Language::PortuguesePT)) == "Continuar o jogo");
    CHECK(std::string(i18n::tr(i18n::Str::ItemConcrete, i18n::Language::PortuguesePT)) == "Betão"); // pt-PT, not pt-BR "Concreto"

    // --- Language metadata -----------------------------------------------
    for (size_t l = 0; l < i18n::kLanguageCount; ++l) {
        const auto lang = static_cast<i18n::Language>(l);
        CHECK(firstMissingGlyph(i18n::languageNativeName(lang)) == 0);
        CHECK(i18n::parseLanguageTag(i18n::languageCode(lang)) == lang);
    }
    CHECK(i18n::nextLanguage(i18n::Language::English) == i18n::Language::Russian);
    CHECK(i18n::nextLanguage(i18n::Language::Russian) == i18n::Language::PortuguesePT);
    CHECK(i18n::nextLanguage(i18n::Language::PortuguesePT) == i18n::Language::English);

    // --- Locale tag parsing -----------------------------------------------
    CHECK(i18n::parseLanguageTag("ru") == i18n::Language::Russian);
    CHECK(i18n::parseLanguageTag("ru_RU.UTF-8") == i18n::Language::Russian);
    CHECK(i18n::parseLanguageTag("RU-ru") == i18n::Language::Russian);
    CHECK(i18n::parseLanguageTag("pt-PT") == i18n::Language::PortuguesePT);
    CHECK(i18n::parseLanguageTag("pt_PT@euro") == i18n::Language::PortuguesePT);
    CHECK(i18n::parseLanguageTag("pt_BR.UTF-8") == i18n::Language::PortuguesePT);
    CHECK(i18n::parseLanguageTag("en_GB.UTF-8") == i18n::Language::English);
    CHECK(i18n::parseLanguageTag(" english ") == i18n::Language::English);
    CHECK(i18n::parseLanguageTag("Русский") == i18n::Language::Russian);
    CHECK(!i18n::parseLanguageTag("C").has_value());
    CHECK(!i18n::parseLanguageTag("C.UTF-8").has_value());
    CHECK(!i18n::parseLanguageTag("POSIX").has_value());
    CHECK(!i18n::parseLanguageTag("de_DE").has_value());
    CHECK(!i18n::parseLanguageTag("").has_value());

#if !defined(_WIN32)
    setenv("LANGUAGE", "", 1);
    setenv("LC_ALL", "", 1);
    setenv("LC_MESSAGES", "", 1);
    setenv("LANG", "pt_PT.UTF-8", 1);
    CHECK(i18n::languageFromEnvironment() == i18n::Language::PortuguesePT);
    setenv("LANGUAGE", "de:ru:en", 1);
    CHECK(i18n::languageFromEnvironment() == i18n::Language::Russian);
    setenv("LANGUAGE", "", 1);
    setenv("LANG", "C", 1);
    CHECK(!i18n::languageFromEnvironment().has_value());
#endif

    // --- Formatting -------------------------------------------------------
    CHECK(i18n::formatPattern("BUY {0}", {"150"}) == "BUY 150");
    CHECK(i18n::formatPattern("{1} / {0}", {"a", "b"}) == "b / a");
    CHECK(i18n::formatPattern("{0} {5} {x} {", {"ok"}) == "ok {5} {x} {");
    i18n::setLanguage(i18n::Language::PortuguesePT);
    CHECK(i18n::currentLanguage() == i18n::Language::PortuguesePT);
    CHECK(i18n::format(i18n::Str::ShopButtonBuy, {"150"}) == "COMPRAR 150");
    CHECK(i18n::format(i18n::Str::ToastTool, {i18n::tr(i18n::Str::ToolGravityGun)}) == "Ferramenta: Arma de Gravidade");
    i18n::setLanguage(i18n::Language::Russian);
    CHECK(i18n::format(i18n::Str::ShopGold, {"42"}) == "ЗОЛОТО: 42");

    // --- Online status + shop error mapping --------------------------------
    for (const char *status : {"Offline", "Invalid online settings", "Connecting", "Online: players only",
                               "Connection closed", "Connection failed (TLS or credentials)",
                               "Online unavailable: OpenSSL missing"}) {
        for (size_t l = 0; l < i18n::kLanguageCount; ++l) {
            i18n::setLanguage(static_cast<i18n::Language>(l));
            const std::string localized = i18n::localizeOnlineStatus(status);
            CHECK(!localized.empty());
            if (l != 0) CHECK(localized != status || std::string(status) == "Offline");
        }
    }
    i18n::setLanguage(i18n::Language::Russian);
    CHECK(i18n::localizeOnlineStatus("Some future status") == "Some future status");
    CHECK(i18n::localizeShopError("owned") == "Уже куплено");
    CHECK(i18n::localizeShopError("insufficient") == "Недостаточно золота");
    CHECK(i18n::localizeShopError("bogus").empty());
    i18n::setLanguage(i18n::Language::English);
    CHECK(i18n::localizeShopError("unknown") == "Unknown item");

    if (failures == 0) {
        std::cout << "Localization (" << i18n::kStringCount << " strings x " << i18n::kLanguageCount
                  << " languages) and font coverage OK\n";
        return 0;
    }
    std::cerr << failures << " localization check(s) failed\n";
    return 1;
}
