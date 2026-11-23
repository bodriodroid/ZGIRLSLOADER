// chord_assist.h - lets two attack buttons that land a few ticks apart reach the fight as ONE chord (pure logic: no SDL, testable).
//
// Why: the game's own input reading counts PP / KK / PPP / KKK (the buttons of every super) only when the buttons go down within one tick of
// each other.  On a keyboard that is easy; on a phone two thumbs routinely land 2-3 ticks apart, so the super never came out and the first
// button's normal did instead.  This sits between the pad and the fight (the raw buttons of one local player, once per tick) and does not
// touch the simulation or what a netplay peer receives beyond the buttons themselves.
//
// How: while it is `active` (the caller says so when the fighter has a super bar to spend), the FIRST attack button of a group (punches
// LP/MP/HP, kicks LK/MK/HK) that goes down while no other button of that group is down is held back for up to `window` ticks.  If another
// button of the same group goes down in that time, the buttons are released to the fight together on that tick (a chord).  Otherwise the
// held-back press goes out when the window ends; a tap that ends inside the window is still delivered as a one tick press.  Everything else
// (directions, Start, the other group, a second button while the first is already out, anything while not active) passes through unchanged.
// So the only cost is `window` ticks of delay on a lone attack press, and only while a super is affordable.
#pragma once
#include <cstdint>

namespace zengine {
namespace fight {

class ChordAssist {
public:
    // `window` = ticks a lone press may be held back (0 = off).  At 60 ticks per second 3 ticks = 50 ms.
    explicit ChordAssist(int window = 3) : window_(window < 0 ? 0 : window) {}
    void setWindow(int ticks) { window_ = ticks < 0 ? 0 : ticks; }
    int window() const { return window_; }
    void reset();
    // The buttons to feed the fight this tick (fight.h bits).  Call exactly once per tick.
    uint16_t filter(uint16_t raw, bool active);

private:
    struct Group {
        uint16_t mask = 0;
        uint16_t out = 0;      // buttons currently shown to the fight
        uint16_t held = 0;     // pressed, held back
        int age = 0;           // ticks the held-back press has waited
    };
    Group g_[2];
    int window_ = 3;
    bool inited_ = false;
    void init();
    uint16_t stepGroup(Group& g, uint16_t down, bool active);
};

}  // namespace fight
}  // namespace zengine
