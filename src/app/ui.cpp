// ui.cpp - glyph atlas construction (the only place font8x8_basic.h is included) and the UTF-8 helpers.
#include <algorithm>
#include "ui.h"

#include "font8x8_basic.h"  // public domain font by Daniel Hepper (char -> unsigned char so it compiles as C++)

namespace zengine {

namespace {

// Letters outside ASCII that the game's localisations need, synthesised from the ASCII glyphs.
// accent: a acute, g grave, c circumflex, d diaeresis, t tilde, z cedilla, q inverted '?', e inverted '!'
struct ExtGlyph {
    uint32_t cp;
    char base;
    char accent;
};
const ExtGlyph kExt[] = {
    {0xE1, 'a', 'a'}, {0xE0, 'a', 'g'}, {0xE2, 'a', 'c'}, {0xE4, 'a', 'd'}, {0xE3, 'a', 't'},
    {0xE9, 'e', 'a'}, {0xE8, 'e', 'g'}, {0xEA, 'e', 'c'}, {0xEB, 'e', 'd'},
    {0xED, 'i', 'a'}, {0xEC, 'i', 'g'}, {0xEE, 'i', 'c'}, {0xEF, 'i', 'd'},
    {0xF3, 'o', 'a'}, {0xF2, 'o', 'g'}, {0xF4, 'o', 'c'}, {0xF6, 'o', 'd'}, {0xF5, 'o', 't'},
    {0xFA, 'u', 'a'}, {0xF9, 'u', 'g'}, {0xFB, 'u', 'c'}, {0xFC, 'u', 'd'},
    {0xF1, 'n', 't'}, {0xE7, 'c', 'z'}, {0xFD, 'y', 'a'}, {0xFF, 'y', 'd'},
    {0xBF, '?', 'q'}, {0xA1, '!', 'e'},
};
constexpr int kExtCount = static_cast<int>(sizeof(kExt) / sizeof(kExt[0]));

// Hand-drawn symbols (bit 0 = leftmost pixel).
struct RawGlyph {
    uint32_t cp;
    uint8_t rows[8];
};
const RawGlyph kRaw[] = {
    {0xA9, {0x3C, 0x42, 0x99, 0xA5, 0xA5, 0x99, 0x42, 0x3C}},    // (c)
    {0x2122, {0x97, 0xF2, 0x92, 0x00, 0x00, 0x00, 0x00, 0x00}},  // TM
};
constexpr int kRawCount = static_cast<int>(sizeof(kRaw) / sizeof(kRaw[0]));

// Accented capitals and typographic punctuation fold to ASCII (there is no room above a capital).
struct Fold {
    uint32_t cp;
    char to;
};
const Fold kFold[] = {
    {0xC1, 'A'}, {0xC0, 'A'}, {0xC2, 'A'}, {0xC4, 'A'}, {0xC3, 'A'}, {0xC9, 'E'}, {0xC8, 'E'}, {0xCA, 'E'}, {0xCB, 'E'},
    {0xCD, 'I'}, {0xCC, 'I'}, {0xCE, 'I'}, {0xCF, 'I'}, {0xD3, 'O'}, {0xD2, 'O'}, {0xD4, 'O'}, {0xD6, 'O'}, {0xD5, 'O'},
    {0xDA, 'U'}, {0xD9, 'U'}, {0xDB, 'U'}, {0xDC, 'U'}, {0xD1, 'N'}, {0xC7, 'C'}, {0xDF, 's'},
    {0x2018, '\''}, {0x2019, '\''}, {0x201C, '"'}, {0x201D, '"'}, {0x2013, '-'}, {0x2014, '-'}, {0x2026, '.'},
    {0xA0, ' '},
};

void accentRows(uint8_t g[8], char a) {
    // bit 0 is the leftmost pixel; lowercase letters in this font start on row 2, so rows 0-1 are free.
    g[0] = g[1] = 0;
    switch (a) {
        case 'a': g[0] = 0x08; g[1] = 0x04; break;  // /
        case 'g': g[0] = 0x04; g[1] = 0x08; break;  // \  (mirror of acute)
        case 'c': g[0] = 0x08; g[1] = 0x14; break;  // ^
        case 'd': g[1] = 0x14; break;               // two dots
        case 't': g[0] = 0x16; break;               // ~
        case 'z': g[7] = 0x0C; break;               // cedilla hangs below the baseline (row 7 is free on 'c')
        default: break;
    }
}

}  // namespace

uint32_t Ui::nextCodepoint(const std::string& s, size_t& i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    auto cont = [&](size_t k) { return i + k < s.size() && (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80; };
    if (c < 0x80) {
        ++i;
        return c;
    }
    if (c >= 0xC2 && c < 0xE0 && cont(1)) {
        const uint32_t cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
        i += 2;
        return cp;
    }
    if (c >= 0xE0 && c < 0xF0 && cont(1) && cont(2)) {
        const uint32_t cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6) |
                            (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
        i += 3;
        return cp;
    }
    if (c >= 0xF0 && c < 0xF5 && cont(1) && cont(2) && cont(3)) {
        const uint32_t cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 12) |
                            ((static_cast<unsigned char>(s[i + 2]) & 0x3Fu) << 6) | (static_cast<unsigned char>(s[i + 3]) & 0x3Fu);
        i += 4;
        return cp;
    }
    ++i;
    return 0xFFFD;
}

int Ui::glyphIndex(uint32_t cp) {
    if (cp < 128) return static_cast<int>(cp);
    for (int k = 0; k < kExtCount; ++k)
        if (kExt[k].cp == cp) return 128 + k;
    for (int k = 0; k < kRawCount; ++k)
        if (kRaw[k].cp == cp) return 128 + kExtCount + k;
    for (const Fold& f : kFold)
        if (f.cp == cp) return static_cast<unsigned char>(f.to);
    return '?';
}

int Ui::glyphCount(const std::string& s) {
    int n = 0;
    for (size_t i = 0; i < s.size();) {
        nextCodepoint(s, i);
        ++n;
    }
    return n;
}

std::string Ui::fit(const std::string& s, int widthPx) {
    const int maxc = std::max(widthPx / 8, 0);
    if (glyphCount(s) <= maxc) return s;
    const int keep = std::max(maxc - 2, 0);
    size_t i = 0;
    for (int n = 0; n < keep && i < s.size(); ++n) nextCodepoint(s, i);
    std::string out = s.substr(0, i);
    if (maxc > 2) out += "..";
    return out;
}

bool Ui::init(SDL_Renderer* renderer) {
    r = renderer;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    // 16x16 glyph atlas (256 slots): 0-127 straight from the public-domain font8x8, 128+ synthesised
    // Latin letters (see kExt).  Bit 0 of each row byte is the leftmost pixel.
    constexpr int kAtlas = 128;
    std::vector<uint32_t> px(static_cast<size_t>(kAtlas) * kAtlas, 0);
    auto put = [&](int slot, const uint8_t g[8]) {
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                if (g[y] & (1 << x)) px[static_cast<size_t>((slot / 16) * 8 + y) * kAtlas + (slot % 16) * 8 + x] = 0xFFFFFFFFu;
    };
    for (int c = 0; c < 128; ++c) {
        uint8_t g[8];
        for (int y = 0; y < 8; ++y) g[y] = static_cast<uint8_t>(font8x8_basic[c][y]);
        put(c, g);
    }
    for (int k = 0; k < kExtCount; ++k) {
        uint8_t g[8];
        for (int y = 0; y < 8; ++y) g[y] = static_cast<uint8_t>(font8x8_basic[static_cast<int>(kExt[k].base)][y]);
        if (kExt[k].accent == 'q' || kExt[k].accent == 'e') {  // flip rows 0..6 vertically
            uint8_t f[8] = {g[6], g[5], g[4], g[3], g[2], g[1], g[0], 0};
            for (int y = 0; y < 8; ++y) g[y] = f[y];
        } else {
            accentRows(g, kExt[k].accent);
        }
        put(128 + k, g);
    }
    for (int k = 0; k < kRawCount; ++k) put(128 + kExtCount + k, kRaw[k].rows);
    font = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, kAtlas, kAtlas);
    if (!font) return false;
    SDL_UpdateTexture(font, nullptr, px.data(), kAtlas * 4);
    SDL_SetTextureBlendMode(font, SDL_BLENDMODE_BLEND);
    return true;
}

}  // namespace zengine
