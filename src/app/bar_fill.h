// bar_fill.h - the picture is always 16:9 (640x360 logical).  On a screen that is wider (a 20:9 phone) the sides used to stay black.
// BarFill draws the whole frame into an off-screen picture instead and, at the end of the frame, puts it in the middle of the window
// with the sides filled by a blurred, darkened copy of the same picture - so menus, the title, the loading screens, the art gallery,
// stage pictures, cut-ins and the fight (with "No Limits Screen" off) all fill the screen without any of them knowing.
// Nothing is stretched: the picture itself keeps its shape and size.
#pragma once
#include <SDL.h>

namespace zengine {

class BarFill {
public:
    struct Layout {
        bool wanted = false;      // the window is wider than the picture by enough to show bars
        int picW = 0, picH = 0;   // off-screen picture, in window pixels (same pixel density as the window)
        int x = 0;                // left edge of the picture inside the window
        double visible = 1.0;     // fraction of the picture's height the blurred copy shows (it is scaled to the window's width)
    };
    // Pure: what to do for a window of outW x outH pixels and a logical picture of lw x lh.
    static Layout layout(int outW, int outH, int lw, int lh);

    BarFill() = default;
    ~BarFill() { release(); }
    BarFill(const BarFill&) = delete;
    BarFill& operator=(const BarFill&) = delete;

    // Call before drawing the frame.  true = the frame goes into the off-screen picture (call end() afterwards); false = draw as usual.
    bool begin(SDL_Renderer* r, int lw, int lh);
    // Call after the frame, before presenting / reading the window.  Restores the logical size.
    void end(SDL_Renderer* r);
    void release();
    bool active() const { return active_; }

private:
    SDL_Texture* pic_ = nullptr;
    SDL_Texture* mid_ = nullptr;    // 1/4 size step of the blur
    SDL_Texture* small_ = nullptr;  // 1/16 size: the blurred copy
    int picW_ = 0, picH_ = 0;
    int lw_ = 0, lh_ = 0;
    Layout lay_;
    bool active_ = false;
    SDL_Renderer* owner_ = nullptr;
};

}  // namespace zengine
