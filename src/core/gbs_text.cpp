// gbs_text.cpp - see gbs_text.h.
#include "gbs_text.h"

#include <algorithm>
#include <cmath>

namespace zengine {

namespace {

// UTF-8 -> code point; bad bytes give U+FFFD and advance by one.
uint32_t nextCp(const std::string& s, size_t& i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    auto cont = [&](size_t k) { return i + k < s.size() && (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80; };
    auto b = [&](size_t k) { return static_cast<uint32_t>(static_cast<unsigned char>(s[i + k]) & 0x3F); };
    if (c < 0x80) {
        ++i;
        return c;
    }
    if (c >= 0xC2 && c < 0xE0 && cont(1)) {
        const uint32_t cp = ((c & 0x1Fu) << 6) | b(1);
        i += 2;
        return cp;
    }
    if (c >= 0xE0 && c < 0xF0 && cont(1) && cont(2)) {
        const uint32_t cp = ((c & 0x0Fu) << 12) | (b(1) << 6) | b(2);
        i += 3;
        return cp;
    }
    if (c >= 0xF0 && c < 0xF5 && cont(1) && cont(2) && cont(3)) {
        const uint32_t cp = ((c & 0x07u) << 18) | (b(1) << 12) | (b(2) << 6) | b(3);
        i += 4;
        return cp;
    }
    ++i;
    return 0xFFFD;
}

// One character of the text with the glyph that draws it (null for white space and line feeds).
struct Item {
    uint32_t cp = 0;
    const GbsGlyph* g = nullptr;
};

bool isSpaceLike(uint32_t cp) { return cp == ' ' || cp == '\t' || cp == '\n'; }

std::vector<Item> itemsOf(const GbsFont& font, const std::string& s) {
    std::vector<Item> out;
    out.reserve(s.size());
    const GbsGlyph* q = font.find('?');
    bool escaped = false;
    for (size_t i = 0; i < s.size();) {
        const uint32_t cp = nextCp(s, i);
        if (cp == 0) break;
        if (!escaped && cp == '\\') {
            escaped = true;
            continue;
        }
        const bool literal = escaped;
        escaped = false;
        if (cp == '\r') continue;
        Item it;
        it.cp = cp;
        if (!literal && isSpaceLike(cp)) {
            out.push_back(it);
            continue;
        }
        it.g = font.find(cp);
        if (!it.g) it.g = q;
        if (!it.g) continue;
        out.push_back(it);
    }
    return out;
}

// Characters of the word starting at `i` (a run up to the next white space; white space on its own is one character).
size_t wordLength(const std::vector<Item>& items, size_t i) {
    size_t n = 0;
    for (size_t k = i; k < items.size(); ++k) {
        if (!items[k].g && (items[k].cp == ' ' || items[k].cp == '\t' || items[k].cp == '\n')) {
            if (k == i) n = 1;
            break;
        }
        ++n;
    }
    return n;
}

// Width of `len` glyphs from `start`: first bearing + width, then every following glyph adds (advance - width) + tracking
// before its own bearing + width.
int wordWidth(const std::vector<Item>& items, size_t start, size_t len, int tracking) {
    int w = 0;
    const GbsGlyph* prev = nullptr;
    for (size_t c = start; c < start + len && c < items.size(); ++c) {
        if (prev) w += (prev->advance - prev->w) + tracking;
        prev = items[c].g;
        if (prev) w += prev->bearingX + prev->w;
    }
    return w;
}

struct Laid {
    std::vector<GbsTextLine> lines;
    int maxRight = 0;
};

Laid layoutLines(const GbsFont& font, const std::vector<Item>& items, float maxW, float maxH, const GbsTextStyle& st) {
    Laid out;
    const int fontSize = font.lineHeight;
    const int lineAdvance = fontSize + static_cast<int>(static_cast<float>(fontSize) * st.leading);
    GbsTextLine line;
    int curX = 0, curY = 0;
    size_t i = 0;
    auto flush = [&]() {
        if (!line.quads.empty()) out.lines.push_back(std::move(line));
        line = GbsTextLine{};
        line.offsetY = curY;
    };
    while (i < items.size()) {
        const size_t len = std::max<size_t>(1, wordLength(items, i));
        const Item& first = items[i];
        if (!first.g && first.cp == '\n') {
            curX = 0;
            curY += lineAdvance;
            flush();
            if (static_cast<float>(curY + fontSize) >= maxH) break;
            i += len;
            continue;
        }
        int strW = 0;
        bool gap = false;
        if (!first.g && first.cp == '\t') {
            strW = fontSize * 2 - (fontSize > 0 ? curX % (fontSize * 2) : 0) + st.tracking;
            gap = true;
        } else if (!first.g && first.cp == ' ') {
            strW = fontSize / 3 + st.tracking;
            gap = true;
        } else {
            strW = wordWidth(items, i, len, st.tracking);
        }
        if (st.fit == kFitWrap && curX != 0 && static_cast<float>(curX + strW) > maxW) {
            curX = 0;
            curY += lineAdvance;
            flush();
            if (static_cast<float>(curY + fontSize) >= maxH) break;
            if (gap) strW = 0;
        }
        if (!gap) {
            for (size_t c = 0; c < len; ++c) {
                const GbsGlyph* g = items[i + c].g;
                GbsTextQuad q;
                q.glyph = g;
                q.left = curX + g->bearingX;
                q.top = curY + font.baseline - (g->bearingY + 1);
                q.right = q.left + g->w;
                q.bottom = q.top + g->h;
                line.quads.push_back(q);
                curX += g->advance + st.tracking;
                out.maxRight = std::max(out.maxRight, q.right);
            }
        } else {
            curX += strW;
        }
        i += len;
    }
    if (!line.quads.empty()) out.lines.push_back(std::move(line));
    return out;
}

}  // namespace

int GbsTextLine::width() const {
    int lo = 0, hi = 0;
    for (const GbsTextQuad& q : quads) {
        lo = std::min(lo, q.left);
        hi = std::max(hi, q.right);
    }
    return hi - lo;
}

GbsTextBlock layoutGbsText(const GbsFont& font, const std::string& utf8, float boxW, float boxH, float scaleX, float scaleY,
                           const GbsTextStyle& style) {
    GbsTextBlock b;
    if (scaleX <= 0) scaleX = 0.01f;
    if (scaleY <= 0) scaleY = 0.01f;
    b.scaleX = scaleX;
    b.scaleY = scaleY;
    b.boxW = boxW;
    b.boxH = boxH;
    if (utf8.empty() || font.lineHeight <= 0) return b;

    std::vector<Item> items = itemsOf(font, utf8);
    Laid laid = layoutLines(font, items, boxW / scaleX, boxH / scaleY, style);
    const float textW = scaleX * static_cast<float>(laid.maxRight);

    if (style.fit == kFitScaleToFit || (style.fit == kFitScaleDown && textW > boxW)) {
        // the whole text is squeezed sideways into the box; every line then starts at the box's left edge
        const float k = textW > 0 ? boxW / textW : 1.0f;
        for (GbsTextLine& l : laid.lines) {
            if (l.quads.empty()) continue;
            const int start = l.quads.front().left;
            for (GbsTextQuad& q : l.quads) {
                q.left = static_cast<int>(static_cast<float>(q.left - start) * k);
                q.right = static_cast<int>(static_cast<float>(q.right - start) * k);
            }
        }
    } else if (style.fit == kFitTruncate && textW > boxW && laid.lines.size() == 1) {
        // keep the glyphs that leave room for "...", then lay that out as one line
        GbsTextStyle one = style;
        one.fit = kFitNoWrap;
        std::vector<Item> dots;
        const GbsGlyph* dot = font.find('.');
        if (dot) dots.assign(3, Item{'.', dot});
        const int dotsW = dots.empty() ? 0 : wordWidth(dots, 0, dots.size(), 0);
        // glyph rows of the full line map 1:1 to the glyph items (white space has no quad), so walk the items back
        std::vector<Item> kept;
        size_t quadIdx = laid.lines[0].quads.size();
        size_t cut = items.size();
        size_t removed = 0;
        for (size_t k = items.size(); k > 0; --k) {
            const Item& it = items[k - 1];
            if (it.g) {
                if (quadIdx == 0) break;
                --quadIdx;
                const int right = laid.lines[0].quads[quadIdx].right;
                if (static_cast<float>(right + dotsW) < boxW / scaleX && removed >= 3) {
                    cut = k;
                    break;
                }
            }
            ++removed;
            cut = k - 1;
        }
        kept.assign(items.begin(), items.begin() + static_cast<std::ptrdiff_t>(std::min(cut, items.size())));
        kept.insert(kept.end(), dots.begin(), dots.end());
        laid = layoutLines(font, kept, boxW / scaleX, boxH / scaleY, one);
    }

    int maxOff = 0;
    for (const GbsTextLine& l : laid.lines) maxOff = std::max(maxOff, l.offsetY);
    b.textH = static_cast<float>(maxOff + font.lineHeight) * scaleY;
    b.lines = std::move(laid.lines);
    return b;
}

void gbsLineOffset(const GbsTextBlock& b, size_t line, int hAlign, int vAlign, float& dx, float& dy) {
    dx = dy = 0;
    if (line >= b.lines.size()) return;
    const float lw = static_cast<float>(b.lines[line].width()) * b.scaleX;
    if (hAlign == 1) dx = (b.boxW - lw) / 2;
    else if (hAlign == 2) dx = b.boxW - lw;
    if (vAlign == 1) dy = (b.boxH - b.textH) / 2;
    else if (vAlign == 2) dy = b.boxH - b.textH;
}

}  // namespace zengine
