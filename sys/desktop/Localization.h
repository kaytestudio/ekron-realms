#pragma once

// Desktop UI strings in en, ru and pt-PT (table in LocalizationStrings.h).
//
// Language order (main.cpp): --lang, $EKRON_LANG, the pause-menu setting,
// the OS locale (SDL_GetPreferredLocales, then LC_ALL/LC_MESSAGES/LANG),
// English.

#include <cstddef>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

namespace ekron::i18n {

enum class Language : int {
    English = 0,
    Russian,
    PortuguesePT,
    Count
};

constexpr size_t kLanguageCount = static_cast<size_t>(Language::Count);

// Translation keys, generated from LocalizationStrings.h.
enum class Str : int {
#define EKRON_STR(key, en, ru, pt) key,
#include "LocalizationStrings.h"
#undef EKRON_STR
    Count
};

constexpr size_t kStringCount = static_cast<size_t>(Str::Count);

// Translate into an explicit language. Never returns nullptr.
const char *tr(Str key, Language language);

// Translate into the process-wide current language (default: English).
const char *tr(Str key);

Language currentLanguage();
void setLanguage(Language language);

// Cycle English -> Russian -> Portuguese (Portugal) -> English.
Language nextLanguage(Language language);

// BCP-47 code: "en", "ru", "pt-PT".
const char *languageCode(Language language);

// Endonym shown in the language picker: "English", "Русский", "Português (Portugal)".
const char *languageNativeName(Language language);

// Parse a BCP-47 / POSIX locale tag ("ru", "ru_RU.UTF-8", "pt-PT", "pt_BR",
// "en-US", "english"...). Any Portuguese variant maps to PortuguesePT, the only
// Portuguese translation shipped for now. Returns nullopt for unsupported
// languages and for the neutral "C" / "POSIX" locales.
std::optional<Language> parseLanguageTag(std::string_view tag);

// First supported language found in LC_ALL, LC_MESSAGES or LANG
// (colon-separated LANGUAGE lists are honoured too).
std::optional<Language> languageFromEnvironment();

// Replace "{0}", "{1}", ... in `pattern` with `args`. Unknown or out-of-range
// placeholders are kept verbatim so a translator typo never crashes the HUD.
std::string formatPattern(std::string_view pattern, std::initializer_list<std::string_view> args);

// tr() + formatPattern() in the current language.
std::string format(Str key, std::initializer_list<std::string_view> args);

// Map an English status string reported by DesktopOnlineClient to its
// translation; unknown strings are returned unchanged.
std::string localizeOnlineStatus(std::string_view status);

// Map a store purchase failure code ("unknown", "owned", "insufficient" — the
// same codes used by ShopState::tryBuy and the presence relay) to a localized
// message. Returns an empty string for unknown codes.
std::string localizeShopError(std::string_view code);

} // namespace ekron::i18n
