// test_pad_layout.cpp - the touch pad's layout editing rules (src/core/touch_pad.*): add / remove / move / resize a button, move the stick,
// mirror for the other hand, the saved text and what a hand-edited file may do.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "fight.h"
#include "touch_pad.h"

using namespace zengine;

static int g_fail = 0, g_n = 0;
#define CHECK(c)                                                \
    do {                                                        \
        ++g_n;                                                  \
        if (!(c)) {                                             \
            ++g_fail;                                           \
            std::printf("  FAIL line %d: %s\n", __LINE__, #c);  \
        }                                                       \
    } while (0)

static bool overlap(const touch::PadButton& a, const touch::PadButton& b) {
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

int main() {
    using touch::Layout;
    using touch::PadButton;

    // ---- the saved text
    const Layout d = Layout::defaults();
    Layout back;
    CHECK(Layout::parse(d.serialize(), back));
    CHECK(back.serialize() == d.serialize());
    CHECK(back.zoneLeft == 0 && back.zoneRight == 280 && back.buttons.size() == d.buttons.size());
    // a file of the older shape (no left edge on the stick line) still reads, the left edge is 0
    {
        Layout o;
        CHECK(Layout::parse("zdroid-touch 1\nstick 92 272 52 14 60 1 280 120\nbutton LP 404 226 50 46 1\n", o));
        CHECK(o.zoneLeft == 0 && o.zoneRight == 280 && o.buttons.size() == 1 && o.buttons[0].label == "LP");
    }
    CHECK(!Layout::parse("not a layout", back));
    // a hand-edited file cannot put the pad off the screen or make it huge
    {
        Layout o;
        CHECK(Layout::parse("zdroid-touch 1\nstick 5000 -30 52 14 60 1 10 120 900\nbutton LP -999 9999 1 1 1\nbutton HP 100 100 9000 9000 4\n", o));
        CHECK(o.stickX == 640 && o.stickY == 0 && o.zoneLeft == 640 && o.zoneRight >= o.zoneLeft + 40);
        CHECK(o.buttons.size() == 2 && o.buttons[0].x == -20 && o.buttons[0].y == 380 && o.buttons[0].w == 16 && o.buttons[0].h == 14);
        CHECK(o.buttons[1].w == 140 && o.buttons[1].h == 140);
    }

    // ---- adding and removing buttons
    Layout l = d;
    CHECK(!Layout::presets().empty());
    const size_t before = l.buttons.size();
    const int idx = l.addButton(Layout::presets()[4]);   // MK
    CHECK(idx == static_cast<int>(before) && l.buttons.size() == before + 1);
    if (idx >= 0) {
        const PadButton& nb = l.buttons[static_cast<size_t>(idx)];
        CHECK(nb.label == Layout::presets()[4].label && nb.bits == Layout::presets()[4].bits);
        CHECK(nb.x >= 0 && nb.y >= 0 && nb.x + nb.w <= 640 && nb.y + nb.h <= 360);
        for (size_t i = 0; i + 1 < l.buttons.size(); ++i) CHECK(!overlap(nb, l.buttons[i]));   // a free spot
    }
    while (l.buttons.size() < Layout::kMaxButtons) CHECK(l.addButton(Layout::presets()[0]) >= 0);
    CHECK(l.addButton(Layout::presets()[0]) == -1 && l.buttons.size() == Layout::kMaxButtons);   // full
    CHECK(l.removeButton(3) && l.buttons.size() == Layout::kMaxButtons - 1);
    CHECK(!l.removeButton(999));
    Layout one = d;
    one.buttons.resize(1);
    CHECK(!one.removeButton(0) && one.buttons.size() == 1);   // never an empty pad

    // ---- moving and resizing
    Layout m = d;
    m.moveButton(0, -50, -50);
    CHECK(m.buttons[0].x == 0 && m.buttons[0].y == 0);
    m.moveButton(0, 5000, 5000);
    CHECK(m.buttons[0].x == 640 - m.buttons[0].w && m.buttons[0].y == 360 - m.buttons[0].h);
    m.moveButton(1, 300, 200);
    const int cx = m.buttons[1].x + m.buttons[1].w / 2, cy = m.buttons[1].y + m.buttons[1].h / 2;
    const float aspect = static_cast<float>(m.buttons[1].w) / static_cast<float>(m.buttons[1].h);
    m.resizeButton(1, 80);
    CHECK(m.buttons[1].w == 80);
    CHECK(std::abs((m.buttons[1].x + m.buttons[1].w / 2) - cx) <= 1 && std::abs((m.buttons[1].y + m.buttons[1].h / 2) - cy) <= 1);
    CHECK(std::abs(static_cast<float>(m.buttons[1].w) / static_cast<float>(m.buttons[1].h) - aspect) < 0.08f);
    m.resizeButton(1, 5);
    CHECK(m.buttons[1].w == 24);
    m.resizeButton(1, 900);
    CHECK(m.buttons[1].w == 120);

    // ---- the stick and its zone
    Layout s = d;
    s.moveStick(92, 272);
    CHECK(s.zoneLeft == 0 && s.zoneRight == 280);                     // the default stays what it was
    s.moveStick(-100, 9999);
    CHECK(s.stickX >= s.stickRadius && s.stickY <= 360 - s.stickRadius);
    s.setStickRadius(5);
    CHECK(s.stickRadius == 30);
    s.setStickRadius(500);
    CHECK(s.stickRadius == 90);

    // ---- the other hand
    const Layout mir = d.mirrored();
    CHECK(!d.leftHanded() && mir.leftHanded());
    CHECK(mir.stickX == 640 - d.stickX && mir.zoneRight == 640 && mir.zoneLeft == 640 - 280);
    CHECK(mir.buttons[0].x == 640 - d.buttons[0].x - d.buttons[0].w && mir.buttons[0].bits == d.buttons[0].bits);
    CHECK(mir.mirrored().serialize() == d.serialize());              // twice is the original
    Layout mirBack;
    CHECK(Layout::parse(mir.serialize(), mirBack) && mirBack.serialize() == mir.serialize());
    // a wider picture: the left-handed stick goes with the right edge, the buttons on the left stay
    const Layout wide = Layout::widened(mir, 160);
    CHECK(wide.stickX == mir.stickX + 160 && wide.zoneRight == mir.zoneRight + 160 && wide.zoneLeft == mir.zoneLeft + 160);
    CHECK(wide.buttons[0].x == mir.buttons[0].x);

    // ---- the pad reads the mirrored zone
    {
        touch::TouchPad pad(mir);
        std::vector<touch::Finger> f{{1, 560, 300}};   // right side, below the zone top: the stick
        pad.update(f);
        CHECK(pad.stickActive());
        pad.reset();
        std::vector<touch::Finger> g{{2, 40, 300}};     // left of the zone and not on a button: nothing
        pad.update(g);
        CHECK(!pad.stickActive());
        const PadButton& b0 = mir.buttons[0];
        std::vector<touch::Finger> h{{3, b0.x + b0.w / 2, b0.y + b0.h / 2}};
        const uint16_t bits = pad.update(h);
        CHECK((bits & b0.bits) == b0.bits);
        // the normal layout still takes the stick on the left
        touch::TouchPad pr(d);
        std::vector<touch::Finger> k{{4, 90, 300}};
        pr.update(k);
        CHECK(pr.stickActive());
    }

    std::printf("pad layout tests: %d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
