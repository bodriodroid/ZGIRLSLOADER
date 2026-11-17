// hud_portrait.cpp - palettized HUD portraits (see hud_art.h)
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#include "colour_edit.h"
#include "hud_art.h"

namespace zengine {
namespace hudart {

bool decodePcx(const uint8_t* d, size_t size, Pcx& out, std::string* err) {
    auto fail = [&](const char* m) {
        if (err) *err = m;
        return false;
    };
    if (size < 128 || d[0] != 0x0A) return fail("not a .pcx file");
    if (d[2] != 1) return fail("uncompressed .pcx is not supported");
    const int bpp = d[3], planes = d[65];
    if (bpp != 8 || planes != 1) return fail("only 8-bit single-plane .pcx is supported");
    const int x0 = d[4] | d[5] << 8, y0 = d[6] | d[7] << 8, x1 = d[8] | d[9] << 8, y1 = d[10] | d[11] << 8;
    const int w = x1 - x0 + 1, h = y1 - y0 + 1;
    const int bpl = d[66] | d[67] << 8;
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096 || bpl < w) return fail("bad .pcx size");
    out.w = w;
    out.h = h;
    out.idx.assign(static_cast<size_t>(w) * h, 0);
    size_t p = 128;
    std::vector<uint8_t> line(static_cast<size_t>(bpl));
    for (int y = 0; y < h; ++y) {
        int n = 0;
        while (n < bpl) {
            if (p >= size) return fail("truncated .pcx");
            uint8_t b = d[p++];
            int run = 1;
            if (b >= 0xC0) {
                run = b & 0x3F;
                if (p >= size) return fail("truncated .pcx");
                b = d[p++];
            }
            for (int k = 0; k < run && n < bpl; ++k) line[static_cast<size_t>(n++)] = b;
        }
        std::memcpy(&out.idx[static_cast<size_t>(y) * w], line.data(), static_cast<size_t>(w));
    }
    return true;
}

namespace {
// One pass of the search below.  `tab` = the game build's key counts when they are to be trusted (one per region), else nullptr.
// Returns the first key position of every region (start[1..N]) or an empty vector when no counts fit the 8-bit layer.
std::vector<int> searchRowStarts(const std::vector<double>& share, const std::vector<std::vector<int>>& fits, const std::vector<int>* tab) {
    const int N = static_cast<int>(fits.size()) - 1;
    constexpr int kS = 256;
    constexpr double kNone = -1e18;
    // Staying with the table costs nothing and leaving it costs a lot, so a few pixels never argue it out of a row the palette confirms; a row
    // whose ramp the table's count does not reproduce (another build) is free to take whichever count the palette allows and the layer needs.
    constexpr double kOffTable = 8.0, kOffStart = 0.5;
    struct Cand { int n; double cost; };
    std::vector<std::vector<Cand>> cand(static_cast<size_t>(N) + 1);
    for (int r = 1; r <= N; ++r) {
        const std::vector<int>& f = fits[static_cast<size_t>(r)];
        const int t = tab ? (*tab)[static_cast<size_t>(r - 1)] : 0;
        const bool confirmed = t >= 1 && std::find(f.begin(), f.end(), t) != f.end();
        if (confirmed) {
            for (int n : f) cand[static_cast<size_t>(r)].push_back({n, n == t ? 0.0 : kOffTable});
        } else {
            for (int n : f) cand[static_cast<size_t>(r)].push_back({n, 0.001 * n});
        }
    }
    std::vector<double> dp(static_cast<size_t>(N + 1) * kS, kNone);
    std::vector<int16_t> from(static_cast<size_t>(N + 1) * kS, -1);   // the previous start; the count is the difference
    for (int c0 = 1; c0 <= 8; ++c0)   // row 0 (the outline) takes its own few keys: 3 in every complete table
        dp[static_cast<size_t>(1) * kS + c0] = share[static_cast<size_t>(c0)] - (c0 == 3 ? 0.0 : kOffStart);
    for (int r = 1; r < N; ++r)
        for (int sp = 0; sp < kS; ++sp) {
            const double base = dp[static_cast<size_t>(r) * kS + sp];
            if (base <= kNone / 2) continue;
            for (const Cand& c : cand[static_cast<size_t>(r)]) {
                const int sn = sp + c.n;
                if (sn >= kS) continue;
                const double v = base + share[static_cast<size_t>(sn)] - c.cost;
                double& slot = dp[static_cast<size_t>(r + 1) * kS + sn];
                if (v > slot) {
                    slot = v;
                    from[static_cast<size_t>(r + 1) * kS + sn] = static_cast<int16_t>(sp);
                }
            }
        }
    int bestS = -1;
    for (int sv = 0; sv < kS; ++sv)
        if (dp[static_cast<size_t>(N) * kS + sv] > kNone / 2 && (bestS < 0 || dp[static_cast<size_t>(N) * kS + sv] > dp[static_cast<size_t>(N) * kS + bestS])) bestS = sv;
    if (bestS < 0) return {};
    std::vector<int> start(static_cast<size_t>(N) + 1, 0);
    int sv = bestS;
    for (int r = N; r >= 1; --r) {
        start[static_cast<size_t>(r)] = sv;
        if (r > 1) sv = from[static_cast<size_t>(r) * kS + sv];
    }
    return start;
}
}  // namespace

std::vector<uint8_t> portraitRows(const Pcx& colors, const Palette& pal, const std::vector<int>* tableCounts) {
    std::vector<uint8_t> rows(256, 0);
    const int N = pal.bands - 1;   // regions; row 0 is the outline layer
    if (N < 1) return rows;
    // how much of the picture each value covers (percent); 0 is the outline layer, which is always row 0
    std::vector<double> share(256, 0.0);
    if (!colors.idx.empty()) {
        for (uint8_t v : colors.idx) share[v] += 1.0;
        for (double& x : share) x *= 100.0 / static_cast<double>(colors.idx.size());
    }
    share[0] = 0.0;
    std::vector<std::vector<int>> fits(static_cast<size_t>(N) + 1);
    for (int r = 1; r <= N; ++r) fits[static_cast<size_t>(r)] = coledit::keyCountCandidates(pal, r);
    const bool table = tableCounts && static_cast<int>(tableCounts->size()) == N;
    std::vector<int> start;
    if (table) start = searchRowStarts(share, fits, tableCounts);
    if (start.empty()) start = searchRowStarts(share, fits, nullptr);   // no table, a table of another build, or one the 8-bit layer cannot hold
    if (start.empty()) {
        // not even the smallest counts fit 255 positions: the values can only be region numbers
        start.assign(static_cast<size_t>(N) + 1, 0);
        for (int r = 1; r <= N; ++r) start[static_cast<size_t>(r)] = std::min(r, 255);
    }
    for (int v = 0; v < 256; ++v) {
        int row = 0;
        for (int r = 1; r <= N; ++r)
            if (start[static_cast<size_t>(r)] <= v) row = r;
        rows[static_cast<size_t>(v)] = static_cast<uint8_t>(std::min(row, 255));
    }
    return rows;
}

bool composePortrait(const Pcx& colors, const Pcx& lines, const Pcx& shade, const Palette& pal, Image& out, const std::vector<int>* tableCounts,
                     const Palette* shape) {
    if (colors.w <= 0 || colors.w != lines.w || colors.w != shade.w || colors.h != lines.h || colors.h != shade.h) return false;
    if (pal.tones <= 0 || pal.bands <= 0) return false;
    out.w = colors.w;
    out.h = colors.h;
    out.rgba.assign(static_cast<size_t>(out.w) * out.h * 4, 0);
    const std::vector<uint8_t> rows = portraitRows(colors, shape && shape->bands == pal.bands && shape->tones > 1 ? *shape : pal, tableCounts);
    const int tones = pal.tones;
    const uint8_t* lineCol = pal.at(0, 0);   // the outline colour (black in the stock palettes)
    for (size_t i = 0; i < colors.idx.size(); ++i) {
        const int idx = colors.idx[i], sh = shade.idx[i], ln = lines.idx[i];
        if (idx == 0 && sh == 255 && ln == 255) continue;   // background
        // the outline strength in sixteenths, as the game's shader quantises it
        const double line = std::floor((1.0 - ln / 255.0) * 16.0) / 16.0;
        uint8_t* o = &out.rgba[i * 4];
        if (idx == 0) {   // the outline layer has no colour of its own: the outline colour, as opaque as the line is strong
            for (int c = 0; c < 3; ++c) o[c] = lineCol[c];
            o[3] = static_cast<uint8_t>(std::clamp(line * 255.0 + 0.5, 0.0, 255.0));
            continue;
        }
        const int band = std::min<int>(rows[static_cast<size_t>(idx)], pal.bands - 1);
        // tone: the game maps the shade 100..255 onto tones 0.5..14.5 and blends the two nearest, so the lightest tone is never reached
        const double s = (static_cast<double>(sh) - 100.0) / 155.0;
        const double pos = std::clamp(s * (tones - 2) + 0.5, 0.0, static_cast<double>(tones - 1));
        const int t0 = std::min(static_cast<int>(pos), tones - 1);
        const int t1 = std::min(t0 + 1, tones - 1);
        const double fr = pos - t0;
        const uint8_t* a = pal.at(band, t0);
        const uint8_t* b = pal.at(band, t1);
        for (int c = 0; c < 3; ++c) {
            const double col = a[c] * (1.0 - fr) + b[c] * fr;
            o[c] = static_cast<uint8_t>(std::clamp(col * (1.0 - line) + lineCol[c] * line + 0.5, 0.0, 255.0));
        }
        o[3] = 255;
    }
    return true;
}

std::string displayName(const std::string& keyIn) {
    std::string key;
    for (char c : keyIn)
        if (std::isalnum(static_cast<unsigned char>(c))) key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    struct Pair { const char* key; const char* name; };
    static const Pair names[] = {
        {"bossmarie", "BLOODY MARIE"}, {"blackdahlia", "BLACK DAHLIA"}, {"msfortune", "MS. FORTUNE"}, {"robofortune", "ROBO-FORTUNE"},
        {"bigband", "BIG BAND"},       {"painwheel", "PAINWHEEL"},      {"parasoul", "PARASOUL"},      {"squigly", "SQUIGLY"},
        {"cerebella", "CEREBELLA"},    {"beowulf", "BEOWULF"},          {"eliza", "ELIZA"},            {"fukua", "FUKUA"},
        {"double", "DOUBLE"},          {"valentine", "VALENTINE"},      {"umbrella", "UMBRELLA"},      {"peacock", "PEACOCK"},
        {"filia", "FILIA"},            {"annie", "ANNIE"},              {"marie", "MARIE"},
    };
    for (const Pair& p : names)
        if (key == p.key) return p.name;
    std::string up = keyIn;
    for (char& c : up) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
        if (c == '_') c = ' ';
    }
    return up;
}

}  // namespace hudart
}  // namespace zengine
