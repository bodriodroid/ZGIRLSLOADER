// target_switch.h - drawing into a texture for a while and coming back to whatever was being drawn into before.
//
// SDL resets the scale to 1 whenever the render target becomes a texture, and only a switch back to the WINDOW (target null) restores the
// window's own.  Going back to another texture - the picture BarFill draws the match into, the smaller render target of "Render resolution",
// the overlay of a wide screen - leaves the scale at 1: the HUD, the touch buttons and the text drawn after it come out three times too small
// and in the corner of a picture that is itself at full size.  This keeps the scale of the target it leaves and gives it back.
#pragma once
#include <SDL.h>

namespace zengine {

class TargetSwitch {
public:
    TargetSwitch() = default;
    TargetSwitch(const TargetSwitch&) = delete;
    TargetSwitch& operator=(const TargetSwitch&) = delete;
    ~TargetSwitch() { leave(); }

    // Makes `to` the render target (its scale starts at 1, as SDL does).  false = SDL refused; nothing changed and leave() does nothing.
    bool enter(SDL_Renderer* r, SDL_Texture* to) {
        leave();
        if (!r) return false;
        SDL_Texture* const prev = SDL_GetRenderTarget(r);
        float sx = 1.0f, sy = 1.0f;
        SDL_RenderGetScale(r, &sx, &sy);
        if (SDL_SetRenderTarget(r, to) != 0) return false;
        r_ = r;
        prev_ = prev;
        sx_ = sx;
        sy_ = sy;
        in_ = true;
        return true;
    }
    bool active() const { return in_; }
    // Back to the target that was active at enter(), with its scale.
    void leave() {
        if (!in_) return;
        in_ = false;
        SDL_SetRenderTarget(r_, prev_);
        if (prev_) SDL_RenderSetScale(r_, sx_, sy_);   // the window brings back its own
    }

private:
    SDL_Renderer* r_ = nullptr;
    SDL_Texture* prev_ = nullptr;
    float sx_ = 1.0f, sy_ = 1.0f;
    bool in_ = false;
};

}  // namespace zengine
