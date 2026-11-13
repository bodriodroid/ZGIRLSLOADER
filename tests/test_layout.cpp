// test_layout.cpp - No Limits Screen: logical size for the screen's shape (src/core/screen_layout.*), the touch pad moved to the real
// edges (touch::Layout::widened) and the option's definition.
#include <cstdio>
#include <string>

#include "dev_options.h"
#include "screen_layout.h"
#include "touch_pad.h"

using namespace zengine;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                       \
    do {                                                               \
        ++g_checks;                                                    \
        if (!(c)) {                                                    \
            ++g_fail;                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);   \
        }                                                              \
    } while (0)

int main() {
    using layout::compute;
    // off: always the base
    for (int w : {1280, 2400, 3200}) {
        const layout::Layout l = compute(w, 1080, false);
        CHECK(l.w == 640 && l.h == 360 && l.safeX == 0 && !l.wide());
    }
    // 16:9 (and a few pixels of rounding): the base
    CHECK(!compute(1920, 1080, true).wide());
    CHECK(!compute(1280, 720, true).wide());
    CHECK(!compute(1918, 1080, true).wide());
    CHECK(!compute(1921, 1080, true).wide());
    // narrower than 16:9 (tablet, 4:3, portrait): the base, bars above and below
    CHECK(!compute(1024, 768, true).wide());
    CHECK(!compute(1080, 2400, true).wide());
    // phones
    const layout::Layout a = compute(2400, 1080, true);   // 20:9
    CHECK(a.h == 360 && a.w == 800 && a.safeX == 80 && a.extra() == 160 && a.wide());
    const layout::Layout b = compute(2340, 1080, true);   // 19.5:9
    CHECK(b.w == 780 && b.safeX == 70);
    const layout::Layout c = compute(2520, 1080, true);   // 21:9
    CHECK(c.w == 840 && c.safeX == 100);
    // always even, centred, never above the cap, never below the base
    for (int pw = 1000; pw <= 6000; pw += 37) {
        const layout::Layout l = compute(pw, 1000, true);
        CHECK(l.w >= 640 && l.w <= layout::kMaxW && l.w % 2 == 0 && l.safeX * 2 == l.w - 640);
    }
    CHECK(compute(6000, 1000, true).w == layout::kMaxW);   // an absurd window: capped, the rest is bars
    CHECK(compute(0, 0, true).w == 640 && compute(-5, 10, true).w == 640);

    // touch pad: the right-hand buttons go to the real right edge, the left cluster stays
    touch::Layout base = touch::Layout::defaults();
    const touch::Layout wide = touch::Layout::widened(base, 160);
    CHECK(wide.buttons.size() == base.buttons.size());
    bool movedRight = false, keptLeft = true;
    for (size_t i = 0; i < base.buttons.size(); ++i) {
        const bool right = base.buttons[i].x + base.buttons[i].w / 2 >= 320;
        if (right) {
            movedRight = true;
            CHECK(wide.buttons[i].x == base.buttons[i].x + 160 && wide.buttons[i].y == base.buttons[i].y);
        } else if (wide.buttons[i].x != base.buttons[i].x) {
            keptLeft = false;
        }
    }
    CHECK(movedRight && keptLeft);
    CHECK(wide.stickX == base.stickX && wide.zoneRight == base.zoneRight);
    const touch::Layout same = touch::Layout::widened(base, 0);
    CHECK(same.buttons.size() == base.buttons.size() && same.buttons[0].x == base.buttons[0].x);

    // the option
    using namespace devopt;
    CHECK(std::string(def(kNoLimits).name) == "nolimits" && def(kNoLimits).nChoices == 2 && def(kNoLimits).def == 0);
    CHECK(!resolve([](const char*) { return std::string(); }).noLimits);
    CHECK(resolve([](const char* k) { return std::string(std::string(k) == def(kNoLimits).key ? "On" : ""); }).noLimits);
    CHECK(text("dev.nolimits", true) != nullptr && text("dev.nolimits.d", false) != nullptr);

    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
