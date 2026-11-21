import Foundation

// Localized UI text for the iOS sandbox.
// Keys and their English, Russian and Portuguese (Portugal) values live in Localizable.xcstrings;
// iOS picks the language from the system or the per-app language in Settings.
// tools/validate_i18n.py checks that every key used here has all three translations.

/// Returns the localized string for `key` (falls back to the key itself if it is missing).
func loc(_ key: String) -> String {
    NSLocalizedString(key, tableName: nil, bundle: .main, value: key, comment: "")
}

/// Returns the localized format string for `key` filled with `arguments` (`%1$@`-style placeholders).
func locF(_ key: String, _ arguments: CVarArg...) -> String {
    String(format: loc(key), locale: Locale.current, arguments: arguments)
}
