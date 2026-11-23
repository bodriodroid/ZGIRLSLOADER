#include "chord_assist.h"

#include "fight.h"

namespace zengine {
namespace fight {

void ChordAssist::init() {
    g_[0] = Group();
    g_[1] = Group();
    g_[0].mask = static_cast<uint16_t>(kLP | kMP | kHP);
    g_[1].mask = static_cast<uint16_t>(kLK | kMK | kHK);
    inited_ = true;
}

void ChordAssist::reset() { init(); }

uint16_t ChordAssist::stepGroup(Group& g, uint16_t down, bool active) {
    // buttons that were let go: nothing of them stays shown / held back (a held-back press that ended is a tap: one tick of press)
    uint16_t tap = 0;
    tap = static_cast<uint16_t>(g.held & ~down);
    g.held = static_cast<uint16_t>(g.held & down);
    g.out = static_cast<uint16_t>(g.out & down);
    if (!active || window_ == 0) {   // not affordable (or off): everything goes straight through, and what was held back is let out now
        g.held = 0;
        g.age = 0;
        g.out = down;
        return down;
    }
    const uint16_t fresh = static_cast<uint16_t>(down & ~(g.out | g.held | tap));
    if (fresh) {
        if (g.out == 0 && g.held == 0 && (fresh & (fresh - 1)) == 0) {
            g.held = fresh;   // the first press of the group: wait for a partner
            g.age = 0;
        } else if (g.held != 0) {
            g.out = static_cast<uint16_t>(g.out | g.held | fresh);   // the partner arrived inside the window: both go out together
            g.held = 0;
            g.age = 0;
        } else {
            // a button of the group is already out, or two landed on the same tick (a chord already): no wait; the fight's own 1 tick leniency applies
            g.out = static_cast<uint16_t>(g.out | fresh);
        }
    } else if (g.held != 0) {
        if (++g.age >= window_) {   // nobody came: the lone press goes out now
            g.out = static_cast<uint16_t>(g.out | g.held);
            g.held = 0;
            g.age = 0;
        }
    }
    if (g.held == 0) g.age = 0;
    return static_cast<uint16_t>(g.out | tap);
}

uint16_t ChordAssist::filter(uint16_t raw, bool active) {
    if (!inited_) init();
    uint16_t out = static_cast<uint16_t>(raw & ~(g_[0].mask | g_[1].mask));
    for (Group& g : g_) out = static_cast<uint16_t>(out | stepGroup(g, static_cast<uint16_t>(raw & g.mask), active));
    return out;
}

}  // namespace fight
}  // namespace zengine
