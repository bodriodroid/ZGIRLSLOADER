// super_fx.h - the picture side of a super move: how long the stage stays dark and how the flash pulses.
//
// The simulation only says WHAT is going on: while a fighter's SUPERSHADOWS slot is non-zero the stage is darkened around it, and
// while its SUPERFLASH slot is non-zero the super flash is on (the value is the 1-based look: 1 super, 2 / 3 hyper combo, 4 counter).
// Both are plain on / off switches that the character scripts flip.  This class turns them into smooth strengths for the shader
// uniforms (ScreenFx::shadowT / flashT).  It is presentation only: it is stepped once per displayed tick, never saved, never part of
// a rollback, and nothing in the simulation reads it.
//
// The ramp lengths are our own neutral numbers (they are not read from any game file).
#pragma once
#include <cstdint>

namespace zengine {
namespace fx {

struct SuperFxInput {
    int flash = 0;         // SUPERFLASH of the super's owner: 0 = off, else the 1-based flash look
    bool shadows = false;  // SUPERSHADOWS of the super's owner
};

struct SuperFxState {
    float shadowT = 0;   // 0..1  how dark the stage (and everything that is not the super's owner) is
    float flashT = 0;    // 0..1  how far the picture is remapped towards the flash look
    int look = 1;        // 1-based flash look of the pulse in progress
    float outlineMix = 0;  // 0..1  where the owner's super outline is between its two colours (0 while no pulse runs)
    bool active() const { return shadowT > 0.0f || flashT > 0.0f; }
};

class SuperFxTimeline {
public:
    // Ticks the stage needs to go fully dark / fully light again.
    static constexpr int kShadowIn = 8;
    static constexpr int kShadowOut = 14;
    // The flash is a pulse that starts when SUPERFLASH switches on: up for kFlashUp ticks, down again over kFlashDown.
    static constexpr int kFlashUp = 3;
    static constexpr int kFlashDown = 16;

    void reset() { *this = SuperFxTimeline(); }
    // One displayed tick.
    void step(const SuperFxInput& in);
    SuperFxState state() const;

    // The pieces, public so the tests can pin them down.
    // Shape of the flash pulse `age` ticks after it started (0 = the tick it started): 0..1.
    static float pulseAt(int age);
    // Where the super outline sits between its two colours `age` ticks into a pulse: a triangle wave, 0..1.
    static float outlineAt(int age);

private:
    float shadowLin_ = 0;  // linear 0..1 ramp; state() eases it
    int flashAge_ = -1;    // ticks since the flash switched on; -1 = no pulse
    int look_ = 1;
    bool flashOn_ = false;
};

// Colour of a .pal super-outline entry (0xAARRGGBB) as the linear-ish floats the outline pass takes.  A zero entry (a palette without
// one) gives `fallback`.
void outlineColour(uint32_t argb, const float fallback[3], float out[3]);

}  // namespace fx
}  // namespace zengine
