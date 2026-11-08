// err_overlay.h - the seven-segment display in the red corner at the bottom of the screen (SDL2).
//
//   * "Err N" (N = 1..5, see errcode.h) appears there only while a code is raised; several codes take turns.  A new non-fatal code
//     blinks for a moment, a crash stays lit.  Normal players only ever see these digits.
//   * with the RAM display switched on (Developer Options > RAM display, or ZDROID_DEV_RAM=On) a panel with tall digits shows the
//     resident memory in MB, its peak, the growth per minute, the memory the fighters' sprites hold and - during a match - the
//     match's state hash, and the first detail line of the current error code is printed above it.
//
// The glyphs are DSEG7 Classic Bold rasterised into seg_font.h (tools/gen_seg_font.py, SIL Open Font License 1.1).  Unlit segments
// are drawn first as a dim "8", like a real display.
#pragma once
#include <SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include "errcode.h"
#include "ramprobe.h"
#include "ui.h"

namespace zengine {

// What the debug panel shows besides the process figures; every field is optional.
struct DebugReadout {
    bool haveWorld = false;      // a match is running: hash + object count are valid
    uint64_t worldHash = 0;
    uint32_t worldObjects = 0;
    uint64_t spriteBytes = 0;    // texture memory held by the fighters' frames (0 = nothing / unknown)
};

class ErrOverlay {
public:
    ErrOverlay() = default;
    ~ErrOverlay() { shutdown(); }
    ErrOverlay(const ErrOverlay&) = delete;
    ErrOverlay& operator=(const ErrOverlay&) = delete;

    // Decodes the glyphs into three textures (full, half and quarter size, so small digits stay crisp).
    bool init(SDL_Renderer* r);
    void shutdown();
    bool ready() const { return r_ != nullptr && levels_[0].tex != nullptr; }

    // Once per frame, after everything else.  `nowMs` = SDL_GetTicks().  Draws nothing when no code is raised and `debug` is off.
    void draw(Ui& ui, uint32_t nowMs, const err::Registry& reg, bool debug, const DebugReadout* extra = nullptr);

    // Lower level (the tests drive these directly): digits `digitH` pixels tall, top-left at (x, y); `ghost` draws the unlit
    // segments in `ghostCol` first.
    void drawText(const std::string& s, int x, int y, int digitH, Col lit, bool ghost = true, Col ghostCol = Col{0, 0, 0, 0});
    int textWidth(const std::string& s, int digitH) const;

    // The badge rectangle for the current `digitH` / number of codes (bottom right, flush to the corner).
    static Rect badgeRect(int logicalW, int logicalH, int digitH, int codes);

    static constexpr int kErrDigitH = 18;   // "Err 3"
    static constexpr int kRamBigH = 34;     // resident MB
    static constexpr int kRamSmallH = 14;   // peak, growth, hash

private:
    struct Cell { int16_t sx = 0, sy = 0, w = 0, h = 0, xo = 0, yo = 0; float adv = 0; };
    struct Level {
        SDL_Texture* tex = nullptr;
        int digitH = 0;          // height of a digit in this level's pixels
        int cellW = 0, cellH = 0, cols = 16;
        std::vector<Cell> cells; // index = codepoint - 0x20
    };
    const Level& levelFor(int digitH) const;
    void drawBadge(Ui& ui, uint32_t nowMs, const err::Registry& reg, bool debug);
    void drawRam(Ui& ui, uint32_t nowMs, const DebugReadout* extra, int rightEdge);

    SDL_Renderer* r_ = nullptr;
    Level levels_[3];
    uint32_t lastGen_ = 0, genAtMs_ = 0;
    ram::Process proc_;
    ram::Tracker tracker_;
    uint32_t procAtMs_ = 0;
    bool procSeen_ = false;
};

}  // namespace zengine
