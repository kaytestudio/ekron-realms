#include "Localization.h"

#include <array>
#include <atomic>
#include <cctype>
#include <cstdlib>

namespace ekron::i18n {

namespace {

struct Entry {
    Str key;
    std::array<const char *, kLanguageCount> text;
};

constexpr Entry kTable[] = {
#define EKRON_STR(key, en, ru, pt, es) {Str::key, {en, ru, pt, es}},
#include "LocalizationStrings.h"
#undef EKRON_STR
};

static_assert(sizeof(kTable) / sizeof(kTable[0]) == kStringCount,
              "Every Str key needs exactly one table entry");

constexpr bool tableMatchesEnumOrder() {
    for (size_t i = 0; i < kStringCount; ++i)
        if (static_cast<size_t>(kTable[i].key) != i) return false;
    return true;
}
static_assert(tableMatchesEnumOrder(), "String table order must match the Str enum");

std::atomic<int> gLanguage{static_cast<int>(Language::English)};

size_t clampLanguage(Language language) {
    const auto idx = static_cast<size_t>(language);
    return idx < kLanguageCount ? idx : 0;
}

std::string lowerAscii(std::string_view in) {
    std::string out(in);
    for (char &c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

} // namespace

const char *tr(Str key, Language language) {
    const auto k = static_cast<size_t>(key);
    if (k >= kStringCount) return "";
    const char *text = kTable[k].text[clampLanguage(language)];
    if (text && text[0] != '\0') return text;
    // Defensive fallback to English for an accidentally empty translation.
    return kTable[k].text[0] ? kTable[k].text[0] : "";
}

const char *tr(Str key) { return tr(key, currentLanguage()); }

Language currentLanguage() {
    return static_cast<Language>(gLanguage.load(std::memory_order_relaxed));
}

void setLanguage(Language language) {
    gLanguage.store(static_cast<int>(clampLanguage(language)), std::memory_order_relaxed);
}

Language nextLanguage(Language language) {
    return static_cast<Language>((clampLanguage(language) + 1) % kLanguageCount);
}

const char *languageCode(Language language) {
    switch (language) {
        case Language::Russian:      return "ru";
        case Language::PortuguesePT: return "pt-PT";
        case Language::SpanishES:    return "es-ES";
        case Language::English:
        default:                     return "en";
    }
}

const char *languageNativeName(Language language) {
    switch (language) {
        case Language::Russian:      return "Русский";
        case Language::PortuguesePT: return "Português (Portugal)";
        case Language::SpanishES:    return "Español (España)";
        case Language::English:
        default:                     return "English";
    }
}

std::optional<Language> parseLanguageTag(std::string_view tag) {
    // Trim whitespace.
    while (!tag.empty() && std::isspace(static_cast<unsigned char>(tag.front()))) tag.remove_prefix(1);
    while (!tag.empty() && std::isspace(static_cast<unsigned char>(tag.back()))) tag.remove_suffix(1);
    if (tag.empty()) return std::nullopt;

    const std::string lower = lowerAscii(tag);
    if (lower == "c" || lower == "posix" || lower.rfind("c.", 0) == 0) return std::nullopt;

    // Full language names (handy for EKRON_LANG=russian etc.).
    if (lower == "english") return Language::English;
    if (lower == "russian" || tag == "русский" || tag == "Русский") return Language::Russian;
    if (lower == "portuguese" || lower == "português" || lower == "portugues") return Language::PortuguesePT;
    if (lower == "spanish" || lower == "español" || lower == "espanol" || lower == "castellano")
        return Language::SpanishES;

    // Primary language subtag: up to the first '-', '_', '.' or '@'.
    const size_t end = lower.find_first_of("-_.@");
    const std::string primary = lower.substr(0, end);
    if (primary == "en" || primary == "eng") return Language::English;
    if (primary == "ru" || primary == "rus") return Language::Russian;
    if (primary == "pt" || primary == "por") return Language::PortuguesePT;
    if (primary == "es" || primary == "spa") return Language::SpanishES;
    return std::nullopt;
}

std::optional<Language> languageFromEnvironment() {
    // GNU LANGUAGE is a colon-separated priority list; it only applies when a
    // non-"C" locale is configured, but honouring it first is harmless here.
    for (const char *var : {"LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char *value = std::getenv(var);
        if (!value || value[0] == '\0') continue;
        std::string_view list(value);
        while (!list.empty()) {
            const size_t colon = list.find(':');
            const std::string_view item = list.substr(0, colon);
            if (auto lang = parseLanguageTag(item)) return lang;
            if (colon == std::string_view::npos) break;
            list.remove_prefix(colon + 1);
        }
    }
    return std::nullopt;
}

std::string formatPattern(std::string_view pattern, std::initializer_list<std::string_view> args) {
    std::string out;
    out.reserve(pattern.size() + 16);
    for (size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '{') {
            const size_t close = pattern.find('}', i + 1);
            if (close != std::string_view::npos && close > i + 1 && close - i <= 3) {
                bool digits = true;
                size_t index = 0;
                for (size_t j = i + 1; j < close; ++j) {
                    if (!std::isdigit(static_cast<unsigned char>(pattern[j]))) { digits = false; break; }
                    index = index * 10 + size_t(pattern[j] - '0');
                }
                if (digits && index < args.size()) {
                    out.append(*(args.begin() + index));
                    i = close;
                    continue;
                }
            }
        }
        out.push_back(pattern[i]);
    }
    return out;
}

std::string format(Str key, std::initializer_list<std::string_view> args) {
    return formatPattern(tr(key), args);
}

std::string localizeOnlineStatus(std::string_view status) {
    struct Mapping { std::string_view english; Str key; };
    static constexpr Mapping kMap[] = {
        {"Offline", Str::OnlineOffline},
        {"Invalid online settings", Str::OnlineInvalidSettings},
        {"Connecting", Str::OnlineConnecting},
        {"Online: players only", Str::OnlinePlayersOnly},
        {"Connection closed", Str::OnlineConnectionClosed},
        {"Connection failed (TLS or credentials)", Str::OnlineConnectionFailed},
        {"Online unavailable: OpenSSL missing", Str::OnlineOpenSslMissing},
    };
    for (const auto &m : kMap)
        if (m.english == status) return tr(m.key);
    return std::string(status);
}

std::string localizeShopError(std::string_view code) {
    if (code == "unknown") return tr(Str::ShopErrorUnknownItem);
    if (code == "owned") return tr(Str::ShopErrorAlreadyOwned);
    if (code == "insufficient") return tr(Str::ShopErrorNotEnoughGold);
    return {};
}

} // namespace ekron::i18n
