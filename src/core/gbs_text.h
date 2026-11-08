// gbs_text.h - text layout of a label of a .gbs scene (no drawing here, so it can be tested).
//
// A label node carries, next to its font and alignment, three layout settings (read by gbs_scene.h): the line spacing
// ("leading", a fraction of the font size), the extra advance after every glyph ("tracking", pixels) and a fit mode that
// says what happens when the text does not fit the box:
//
//   0 wrap       break between words at the box width; text that would start below the box is dropped
//   1 no wrap    one long line (only '\n' breaks)
//   2 scale      the text is squeezed (or stretched) horizontally to exactly the box width
//   3 scale down the same, but only when the text is wider than the box
//   4 truncate   a single line that is too wide is cut and ends in "..."
//
// The numbers follow the scene format's runtime (the OtterUI scene library the game is built on; its MIT-licensed
// source was used as the reference for the rules, this file is an independent implementation):
//   - a glyph is placed at pen + leftBearing, its top at (line top + font baseline - (glyph top + 1));
//   - a space advances fontSize / 3 + tracking, a tab runs to the next multiple of 2 * fontSize;
//   - the next line starts fontSize + (int)(fontSize * leading) below;
//   - all numbers are in the font's own pixels; the block is scaled when drawn.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "gbs_scene.h"

namespace zengine {

enum GbsTextFit : uint32_t { kFitWrap = 0, kFitNoWrap = 1, kFitScaleToFit = 2, kFitScaleDown = 3, kFitTruncate = 4 };

struct GbsTextStyle {
    float leading = 0;
    int tracking = 0;
    uint32_t fit = kFitWrap;
};

struct GbsTextQuad {
    const GbsGlyph* glyph = nullptr;
    int left = 0, top = 0, right = 0, bottom = 0;  // font pixels, relative to the line's origin
};

struct GbsTextLine {
    std::vector<GbsTextQuad> quads;
    int offsetY = 0;  // top of the line inside the block
    int width() const;  // widest right edge (the origin counts as the left edge when the text starts to its right)
};

struct GbsTextBlock {
    std::vector<GbsTextLine> lines;
    float scaleX = 1, scaleY = 1;  // what the block is drawn at
    float boxW = 0, boxH = 0;      // the label's box, canvas pixels
    float textH = 0;               // height of all lines, canvas pixels (for the vertical alignment)
    bool empty() const { return lines.empty(); }
};

// Lays `utf8` out for a label box of boxW x boxH canvas pixels drawn at (scaleX, scaleY).  Characters the font lacks
// show as '?'; '\r' is ignored; a backslash makes the next character literal.
GbsTextBlock layoutGbsText(const GbsFont& font, const std::string& utf8, float boxW, float boxH, float scaleX, float scaleY,
                           const GbsTextStyle& style);

// Offset of line `line` of the block inside its box (canvas pixels): hAlign 0 left, 1 centre, 2 right; vAlign 0 top,
// 1 centre, 2 bottom.
void gbsLineOffset(const GbsTextBlock& b, size_t line, int hAlign, int vAlign, float& dx, float& dy);

}  // namespace zengine
