#pragma once
// Built-in UTF-8 8x8 -> crisp scaled pixel font covering every glyph used by the
// localized desktop HUD (see Localization.h):
//   * English  -- ASCII (0x20..0x7E)
//   * Russian  -- Cyrillic U+0401 'Ё', U+0410..U+044F 'А'..'я', U+0451 'ё'
//   * Portuguese (Portugal) -- Latin-1 accented letters
//     À Á Â Ã Ç É Ê Í Ó Ô Õ Ú / à á â ã ç é ê í ó ô õ ú
//   * Punctuation -- '—', '×', '°', '•', '«', '»', '…'
// Zero external font file or SDL2_ttf dependency required. The 16x12 atlas is
// completely filled (192 slots); grow kAtlasRows before adding more glyphs.

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace ekron::font {

constexpr int kGlyphW = 8;
constexpr int kGlyphH = 8;
constexpr int kAtlasCols = 16;
constexpr int kAtlasRows = 12; // 192 glyph slots -> 128 x 96 texture
constexpr int kAtlasW = kAtlasCols * kGlyphW;
constexpr int kAtlasH = kAtlasRows * kGlyphH;

inline uint32_t nextUtf8Codepoint(std::string_view text, size_t &i) {
    if (i >= text.size()) return 0;
    const unsigned char c0 = static_cast<unsigned char>(text[i++]);
    if (c0 < 0x80) return c0;
    if ((c0 & 0xE0) == 0xC0 && i < text.size()) {
        const unsigned char c1 = static_cast<unsigned char>(text[i++]);
        return ((uint32_t(c0 & 0x1F) << 6) | uint32_t(c1 & 0x3F));
    }
    if ((c0 & 0xF0) == 0xE0 && i + 1 < text.size()) {
        const unsigned char c1 = static_cast<unsigned char>(text[i++]);
        const unsigned char c2 = static_cast<unsigned char>(text[i++]);
        return ((uint32_t(c0 & 0x0F) << 12) | (uint32_t(c1 & 0x3F) << 6) | uint32_t(c2 & 0x3F));
    }
    if ((c0 & 0xF8) == 0xF0 && i + 2 < text.size()) {
        const unsigned char c1 = static_cast<unsigned char>(text[i++]);
        const unsigned char c2 = static_cast<unsigned char>(text[i++]);
        const unsigned char c3 = static_cast<unsigned char>(text[i++]);
        return ((uint32_t(c0 & 0x07) << 18) | (uint32_t(c1 & 0x3F) << 12) |
                (uint32_t(c2 & 0x3F) << 6) | uint32_t(c3 & 0x3F));
    }
    return '?';
}

// Portuguese accented letters, in atlas order starting at slot 165.
inline constexpr std::array<uint32_t, 24> kLatinAccented = {
    0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C7, 0x00C9, 0x00CA, 0x00CD, 0x00D3, 0x00D4, 0x00D5, 0x00DA,
    0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E7, 0x00E9, 0x00EA, 0x00ED, 0x00F3, 0x00F4, 0x00F5, 0x00FA,
};

constexpr int kFallbackSlot = int('?' - 0x20);

// Maps Unicode codepoint to atlas slot [0 .. 191]:
//   0..94   : ASCII 0x20..0x7E
//   95..158 : Cyrillic U+0410..U+044F ('А'..'я')
//   159     : U+0401 ('Ё')
//   160     : U+0451 ('ё')
//   161     : U+2014 ('—') / U+2013 ('–')
//   162     : U+00D7 ('×')
//   163     : U+00B0 ('°')
//   164     : U+2022 ('•') / U+00B7 ('·')
//   165..188: Portuguese accented letters (kLatinAccented)
//   189     : U+00AB ('«')
//   190     : U+00BB ('»')
//   191     : U+2026 ('…')
// Unsupported codepoints fall back to '?' (kFallbackSlot).
inline int glyphSlot(uint32_t cp) {
    if (cp >= 0x20 && cp <= 0x7E) return int(cp - 0x20);
    if (cp >= 0x0410 && cp <= 0x044F) return 95 + int(cp - 0x0410);
    if (cp == 0x0401) return 159;
    if (cp == 0x0451) return 160;
    if (cp == 0x2014 || cp == 0x2013) return 161;
    if (cp == 0x00D7) return 162;
    if (cp == 0x00B0) return 163;
    if (cp == 0x2022 || cp == 0x00B7) return 164;
    for (size_t k = 0; k < kLatinAccented.size(); ++k)
        if (kLatinAccented[k] == cp) return 165 + int(k);
    if (cp == 0x00AB) return 189;
    if (cp == 0x00BB) return 190;
    if (cp == 0x2026) return 191;
    return kFallbackSlot;
}

inline const std::array<uint64_t, 192> &glyphBitmaps() {
    // Each uint64_t encodes 8 rows (MSB row 0 -> LSB row 7), MSB bit 7 = leftmost pixel.
    static const std::array<uint64_t, 192> kGlyphs = {
        // 0x20..0x2F:  !"#$%&'()*+,-./
        0x0000000000000000ULL, 0x183C3C1818001800ULL, 0x6C6C240000000000ULL, 0x6C6CFE6CFE6C6C00ULL,
        0x183E603C067C1800ULL, 0x00C6CC183066C600ULL, 0x386C3876DCCE7600ULL, 0x1818300000000000ULL,
        0x0C18303030180C00ULL, 0x30180C0C0C183000ULL, 0x00663CFF3C660000ULL, 0x0018187E18180000ULL,
        0x0000000000181830ULL, 0x0000007E00000000ULL, 0x0000000000181800ULL, 0x060C183060C08000ULL,
        // 0x30..0x3F: 0123456789:;<=>?
        0x3C666E7666663C00ULL, 0x1838181818187E00ULL, 0x3C66061C30667E00ULL, 0x3C66061C06663C00ULL,
        0x1C3C6CCCFE0C0C00ULL, 0x7E607C0606663C00ULL, 0x1C30607C66663C00ULL, 0x7E66060C18181800ULL,
        0x3C66663C66663C00ULL, 0x3C66663E060C3800ULL, 0x0018180000181800ULL, 0x0018180000181830ULL,
        0x0C18306030180C00ULL, 0x00007E00007E0000ULL, 0x30180C060C183000ULL, 0x3C66060C18001800ULL,
        // 0x40..0x4F: @ABCDEFGHIJKLMNO
        0x3C666E6E60623C00ULL, 0x183C66667E666600ULL, 0x7C66667C66667C00ULL, 0x3C66606060663C00ULL,
        0x786C6666666C7800ULL, 0x7E60607860607E00ULL, 0x7E60607860606000ULL, 0x3C66606E66663E00ULL,
        0x6666667E66666600ULL, 0x3C18181818183C00ULL, 0x1E0C0C0C6C6C3800ULL, 0x666C7870786C6600ULL,
        0x6060606060607E00ULL, 0xC6EEFEFED6C6C600ULL, 0xC6E6F6DECEC6C600ULL, 0x3C66666666663C00ULL,
        // 0x50..0x5F: PQRSTUVWXYZ[\]^_
        0x7C66667C60606000ULL, 0x3C6666666E3C0E00ULL, 0x7C66667C6C666600ULL, 0x3C66603C06663C00ULL,
        0x7E18181818181800ULL, 0x6666666666663C00ULL, 0x66666666663C1800ULL, 0xC6C6C6D6FE6C4400ULL,
        0xC66C38386CC6C600ULL, 0x6666663C18181800ULL, 0xFE0C183060C0FE00ULL, 0x3C30303030303C00ULL,
        0xC06030180C060200ULL, 0x3C0C0C0C0C0C3C00ULL, 0x10386CC600000000ULL, 0x00000000000000FFULL,
        // 0x60..0x6F: `abcdefghijklmno
        0x30180C0000000000ULL, 0x00003C063E663E00ULL, 0x60607C6666667C00ULL, 0x00003C6660663C00ULL,
        0x06063E6666663E00ULL, 0x00003C667E603C00ULL, 0x1C36307830303000ULL, 0x00003E66663E063CULL,
        0x60607C6666666600ULL, 0x1800381818183C00ULL, 0x06000E060666663CULL, 0x6060666C786C6600ULL,
        0x3818181818183C00ULL, 0x0000ECFED6C6C600ULL, 0x00007C6666666600ULL, 0x00003C6666663C00ULL,
        // 0x70..0x7E: pqrstuvwxyz{|}~
        0x00007C66667C6060ULL, 0x00003E66663E0606ULL, 0x00007C6660606000ULL, 0x00003E603C067C00ULL,
        0x30307C3030361C00ULL, 0x0000666666663E00ULL, 0x00006666663C1800ULL, 0x0000C6C6D6FE6C00ULL,
        0x0000C66C386CC600ULL, 0x00006666663E063CULL, 0x00007E0C18307E00ULL, 0x0E18187018180E00ULL,
        0x1818181818181800ULL, 0x7018180E18187000ULL, 0x76DC000000000000ULL,
        // 95..126: Cyrillic uppercase А..Я (U+0410..U+042F)
        0x183C66667E666600ULL, // А
        0x7E60607C66667C00ULL, // Б
        0x7C66667C66667C00ULL, // В
        0x7E66606060606000ULL, // Г
        0x1E366666667EC300ULL, // Д
        0x7E60607860607E00ULL, // Е
        0xD6D67C387CD6D600ULL, // Ж
        0x3C66061C06663C00ULL, // З
        0xC6C6CEFE76E6C600ULL, // И
        0x38C6CEFE76E6C600ULL, // Й
        0x666C7870786C6600ULL, // К
        0x1E3666666666C600ULL, // Л
        0xC6EEFEFED6C6C600ULL, // М
        0x6666667E66666600ULL, // Н
        0x3C66666666663C00ULL, // О
        0x7E66666666666600ULL, // П
        0x7C66667C60606000ULL, // Р
        0x3C66606060663C00ULL, // С
        0x7E18181818181800ULL, // Т
        0x6666663E06663C00ULL, // У
        0x187EDBDBE77E1800ULL, // Ф
        0xC66C38386CC6C600ULL, // Х
        0x66666666667E0300ULL, // Ц
        0x6666663E06060600ULL, // Ч
        0xD6D6D6D6D6D6FE00ULL, // Ш
        0xD6D6D6D6D6FE0300ULL, // Щ
        0xF030303E33333E00ULL, // Ъ
        0xC6C6C6F6C3C3F600ULL, // Ы
        0x6060607C66667C00ULL, // Ь
        0x3C66061E06663C00ULL, // Э
        0xCDDAFAFADADA8D00ULL, // Ю
        0x3E66663E3666C600ULL, // Я
        // 127..158: Cyrillic lowercase а..я (U+0430..U+044F)
        0x00003C063E663E00ULL, // а
        0x001E607C66663C00ULL, // б
        0x00007C667C667C00ULL, // в
        0x00007E6660606000ULL, // г
        0x00001E36667EC300ULL, // д
        0x00003C667E603C00ULL, // е
        0x0000D67C387CD600ULL, // ж
        0x00003C661C663C00ULL, // з
        0x0000C6CE76E6C600ULL, // и
        0x0038C6CE76E6C600ULL, // й
        0x0000666C786C6600ULL, // к
        0x00001E366666C600ULL, // л
        0x0000C6FEFED6C600ULL, // м
        0x000066667E666600ULL, // н
        0x00003C6666663C00ULL, // о
        0x00007E6666666600ULL, // п
        0x00007C66667C6060ULL, // р
        0x00003C6660663C00ULL, // с
        0x00007E1818181800ULL, // т
        0x000066663E063C00ULL, // у
        0x00187EDBDBE71800ULL, // ф
        0x0000C66C386CC600ULL, // х
        0x00006666667E0303ULL, // ц
        0x000066663E060600ULL, // ч
        0x0000D6D6D6D6FE00ULL, // ш
        0x0000D6D6D6FE0303ULL, // щ
        0x0000E0607C667C00ULL, // ъ
        0x0000C6C6F6C3F600ULL, // ы
        0x000060607C667C00ULL, // ь
        0x00003C661E663C00ULL, // э
        0x0000CDDAFADACD00ULL, // ю
        0x00003E663E366600ULL, // я
        // 159..164: Ё, ё, —, ×, °, •
        0x24007E6078607E00ULL, // Ё
        0x24003C667E603C00ULL, // ё
        0x0000007E00000000ULL, // —
        0x00663C183C660000ULL, // ×
        0x386C6C3800000000ULL, // °
        0x0000183C3C180000ULL, // •
        // 165..188: Portuguese accented letters, 189..191: «, », …
        0x303C66667E666600ULL, // À U+00C0
        0x0C3C66667E666600ULL, // Á U+00C1
        0x183C66667E666600ULL, // Â U+00C2
        0x763C66667E666600ULL, // Ã U+00C3
        0x3C66606060663C18ULL, // Ç U+00C7
        0x0C7E606078607E00ULL, // É U+00C9
        0x187E606078607E00ULL, // Ê U+00CA
        0x0C3C181818183C00ULL, // Í U+00CD
        0x0C3C666666663C00ULL, // Ó U+00D3
        0x183C666666663C00ULL, // Ô U+00D4
        0x763C666666663C00ULL, // Õ U+00D5
        0x0C66666666663C00ULL, // Ú U+00DA
        0x30183C063E663E00ULL, // à U+00E0
        0x0C183C063E663E00ULL, // á U+00E1
        0x18243C063E663E00ULL, // â U+00E2
        0x324C3C063E663E00ULL, // ã U+00E3
        0x00003C6660663C18ULL, // ç U+00E7
        0x0C183C667E603C00ULL, // é U+00E9
        0x18243C667E603C00ULL, // ê U+00EA
        0x0C18381818183C00ULL, // í U+00ED
        0x0C183C6666663C00ULL, // ó U+00F3
        0x18243C6666663C00ULL, // ô U+00F4
        0x324C3C6666663C00ULL, // õ U+00F5
        0x0C18666666663E00ULL, // ú U+00FA
        0x00003366CC663300ULL, // « U+00AB
        0x0000CC663366CC00ULL, // » U+00BB
        0x000000000000DB00ULL  // … U+2026
    };
    return kGlyphs;
}

inline std::vector<unsigned char> buildAtlasRgba() {
    std::vector<unsigned char> rgba(size_t(kAtlasW * kAtlasH * 4), 0);
    const auto &bitmaps = glyphBitmaps();
    for (size_t idx = 0; idx < bitmaps.size(); ++idx) {
        const int gx = int(idx % kAtlasCols) * kGlyphW;
        const int gy = int(idx / kAtlasCols) * kGlyphH;
        const uint64_t bits = bitmaps[idx];
        for (int row = 0; row < kGlyphH; ++row) {
            const uint8_t rowBits = static_cast<uint8_t>((bits >> ((7 - row) * 8)) & 0xFFu);
            for (int col = 0; col < kGlyphW; ++col) {
                const bool on = (rowBits & (0x80u >> col)) != 0;
                const size_t px = (size_t(gy + row) * kAtlasW + size_t(gx + col)) * 4;
                rgba[px + 0] = 255;
                rgba[px + 1] = 255;
                rgba[px + 2] = 255;
                rgba[px + 3] = on ? 255 : 0;
            }
        }
    }
    return rgba;
}

inline float measureTextWidth(std::string_view text, float scale) {
    size_t i = 0;
    int chars = 0;
    while (i < text.size()) {
        (void)nextUtf8Codepoint(text, i);
        ++chars;
    }
    return float(chars) * float(kGlyphW) * scale;
}

} // namespace ekron::font
