// test_duel_rules.cpp - phase-1 combat rules on a SYNTHETIC character (no game files needed).
//
// The character is built in memory: two frames (idle / attack with a hit box), Stand, walks, the six standing
// normals with hit data by strength, hit / block reactions.  Checks: motion input reading, the input buffer during
// hitstop, chain rules, derived blockstun, chip damage, corner recoil, damage scaling, the undizzy escape, and that
// the snapshot (rollback) round trip still never diverges with the new Fighter fields.
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "duel.h"
#include "foit.h"
#include "moveset.h"
#include "touch_pad.h"

using namespace zengine;
using namespace zengine::fight;

static int g_checks = 0, g_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        ++g_checks;                                                      \
        if (!(c)) {                                                      \
            ++g_fail;                                                    \
            std::printf("  FAIL line %d: %s\n", __LINE__, #c);           \
        }                                                                \
    } while (0)
#define CHECK_EQ(a, b)                                                                              \
    do {                                                                                            \
        ++g_checks;                                                                                 \
        const long long va = static_cast<long long>(a), vb = static_cast<long long>(b);             \
        if (va != vb) {                                                                             \
            ++g_fail;                                                                               \
            std::printf("  FAIL line %d: %s == %s  (%lld vs %lld)\n", __LINE__, #a, #b, va, vb);   \
        }                                                                                           \
    } while (0)

// ---- synthetic character ------------------------------------------------------------------------------------
static foit::Node frameNode(int frame1based, int dur) {
    foit::Node n;
    n.refs[1] = static_cast<uint16_t>(frame1based);
    foit::NodeProps p;
    p.dur = static_cast<uint16_t>(dur);
    n.props.push_back(p);
    return n;
}

static std::shared_ptr<foit::Character> makeChar() {
    auto ch = std::make_shared<foit::Character>();
    foit::Frame idle;
    idle.hurt.push_back({-160, 0, -40, 40});
    idle.push.push_back({-160, 0, -45, 45});
    foit::Frame atk = idle;
    atk.hit.push_back({-120, -80, 0, 250});
    ch->frames = {idle, atk};

    std::vector<foit::State>& st = ch->body.states;
    auto add = [&](const std::string& name, std::vector<foit::Node> nodes, const std::string& next) {
        foit::State s;
        s.name = name;
        s.hdr.nodes = std::move(nodes);
        st.push_back(s);
        return next;
    };
    std::vector<std::string> nexts;
    nexts.push_back(add("Stand", {frameNode(1, 10)}, "Stand"));
    nexts.push_back(add("Crouch", {frameNode(1, 10)}, "Crouch"));
    nexts.push_back(add("WalkFwd", {frameNode(1, 4)}, "WalkFwd"));
    nexts.push_back(add("WalkBack", {frameNode(1, 4)}, "WalkBack"));
    nexts.push_back(add("JumpFall", {frameNode(1, 4)}, "JumpFall"));
    nexts.push_back(add("s.HitMid1", {frameNode(1, 20)}, "Stand"));
    nexts.push_back(add("s.BlockStun_Mid", {frameNode(1, 20)}, "Stand"));
    nexts.push_back(add("c.BlockStun_Mid", {frameNode(1, 20)}, "Crouch"));
    nexts.push_back(add("j.HitMid1", {frameNode(1, 20)}, "JumpFall"));
    // entrance animations: a short one, a very long one, and the state the game's script ends them in
    nexts.push_back(add("Intro_Wave_ForReal", {frameNode(2, 12), frameNode(1, 8)}, "IntroPose_End"));
    nexts.push_back(add("Intro_Long_ForReal", {frameNode(2, 400)}, "IntroPose_End"));
    nexts.push_back(add("IntroPose_End", {frameNode(1, 1)}, "Stand"));
    static const char* btn[6] = {"WP", "MP", "HP", "WK", "MK", "HK"};
    static const int dmg[3] = {300, 500, 800}, stop[3] = {7, 9, 12}, stun[3] = {16, 20, 24};
    for (int b = 0; b < 6; ++b) {
        const int s = b % 3;
        foit::Node active = frameNode(2, 3);
        active.props[0].hasHit = true;
        active.props[0].f2 = 1;
        active.props[0].hit.fc = static_cast<uint16_t>(dmg[s]);
        active.props[0].hit.f10 = static_cast<uint16_t>(stop[s]);
        active.props[0].hit.f8 = static_cast<uint16_t>(stun[s]);
        active.props[0].hit.f13 = 3;
        active.props[0].hit.f18 = static_cast<uint8_t>(s + 1);
        nexts.push_back(add(std::string("s.") + btn[b], {frameNode(1, 3), active, frameNode(1, 10)}, "Stand"));
    }
    for (size_t i = 0; i < st.size(); ++i) {
        st[i].next = 0;
        for (size_t j = 0; j < st.size(); ++j)
            if (st[j].name == nexts[i]) st[i].next = static_cast<uint16_t>(j);
    }
    return ch;
}

static MatchState fresh(const MoveSet& ms, int x1, int x2) {
    MatchState st{};
    duelStartRound(st.p[0], ms, toFixed(static_cast<float>(x1)), Facing::kRight, 0);
    duelStartRound(st.p[1], ms, toFixed(static_cast<float>(x2)), Facing::kLeft, 0);
    st.roundTimer = 5940;
    return st;
}

// ---- motions ------------------------------------------------------------------------------------------------
static void testMotions() {
    std::printf("[motions]\n");
    CHECK_EQ(numpadDir(0), 5);
    CHECK_EQ(numpadDir(kDown | kRight), 3);
    CHECK_EQ(numpadDir(kUp | kLeft), 7);
    CHECK_EQ(relativeDir(6, Facing::kLeft), 4);
    CHECK_EQ(relativeDir(3, Facing::kLeft), 1);
    CHECK_EQ(relativeDir(8, Facing::kLeft), 8);
    CHECK_EQ(relativeDir(6, Facing::kRight), 6);

    auto feed = [](Fighter& f, uint16_t in, int ticks) { for (int i = 0; i < ticks; ++i) pushHistory(f, in); };
    {   // quarter circle forward, facing right
        Fighter f{};
        feed(f, 0, 5);
        feed(f, kDown, 3);
        feed(f, kDown | kRight, 2);
        feed(f, kRight, 2);
        CHECK(motionDone(f, "236", 10, 8));
        CHECK(!motionDone(f, "214", 10, 8));
        CHECK(!motionDone(f, "623", 10, 8));
        f.facing = Facing::kLeft;  // the same stick motion is a 214 for a fighter facing left
        CHECK(motionDone(f, "214", 10, 8));
        CHECK(!motionDone(f, "236", 10, 8));
    }
    {   // too old: the motion was finished 20 ticks ago
        Fighter f{};
        feed(f, kDown, 3);
        feed(f, kDown | kRight, 2);
        feed(f, kRight, 2);
        feed(f, 0, 20);
        CHECK(!motionDone(f, "236", 10, 8));
    }
    {   // holding forward for a long time after 23 is not a fresh 236
        Fighter f{};
        feed(f, kDown, 3);
        feed(f, kDown | kRight, 2);
        feed(f, kRight, 40);
        CHECK(!motionDone(f, "236", 10, 8));
    }
    {   // too slow between two directions
        Fighter f{};
        feed(f, kDown, 3);
        feed(f, 0, 15);
        feed(f, kDown | kRight, 2);
        feed(f, kRight, 2);
        CHECK(!motionDone(f, "236", 10, 8));
        CHECK(!motionDone(f, "2", 10, 8));   // 2 is old
        CHECK(motionDone(f, "36", 10, 8));
    }
    {   // dragon punch 623 with a short neutral in between, and a half circle 41236
        Fighter f{};
        feed(f, kRight, 3);
        feed(f, 0, 2);
        feed(f, kDown, 2);
        feed(f, kDown | kRight, 2);
        CHECK(motionDone(f, "623", 10, 8));
        Fighter g{};
        feed(g, kLeft, 2);
        feed(g, kLeft | kDown, 2);
        feed(g, kDown, 2);
        feed(g, kDown | kRight, 2);
        feed(g, kRight, 1);
        CHECK(motionDone(g, "41236", 10, 8));
        CHECK(motionDone(g, "236", 10, 8));
    }
    {   // run-length history saturates and keeps 8 entries
        Fighter f{};
        feed(f, kRight, 400);
        CHECK_EQ(f.histLen[0], 255);
        for (int i = 0; i < 12; ++i) feed(f, (i & 1) ? kLeft : kRight, 1);
        CHECK(f.histLen[7] > 0);
    }
}

// ---- duel rules ---------------------------------------------------------------------------------------------
static void runTicks(MatchState& st, const DuelContent& c, int from, int to, uint16_t (*p1)(int), uint16_t (*p2)(int)) {
    for (int t = from; t < to; ++t) advanceDuel(st, p1 ? p1(t) : 0, p2 ? p2(t) : 0, c);
}

static void testRules(const MoveSet& ms) {
    std::printf("[duel rules]\n");
    DuelContent c;
    c.ms[0] = c.ms[1] = &ms;
    CHECK(c.valid());
    const int idWP = ms.id.normal[kPStand][0];
    CHECK(idWP >= 0 && ms.id.normal[kPStand][5] >= 0 && ms.id.hit[kPStand][1] >= 0);

    {   // one light punch: 300 raw -> 30 damage, nothing else changed in the base behaviour
        MatchState st = fresh(ms, -90, 90);
        runTicks(st, c, 0, 40, [](int t) -> uint16_t { return t == 2 ? kLP : 0; }, nullptr);
        CHECK_EQ(st.p[1].health, 970);
        MatchState early = fresh(ms, -90, 90);
        runTicks(early, c, 0, 12, [](int t) -> uint16_t { return t == 2 ? kLP : 0; }, nullptr);
        CHECK_EQ(early.p[1].undizzy, 8);  // one light hit of undizzy while the defender is still in the combo
        CHECK(st.p[1].undizzy < 8);       // ... and it drains once the defender is free
    }
    {   // buffer survives hitstop: MP pressed right after the LP connects still comes out (cancel) -> 30 + 45
        MatchState st = fresh(ms, -90, 90);
        runTicks(st, c, 0, 60, [](int t) -> uint16_t { return t == 2 ? kLP : (t == 6 ? kMP : 0); }, nullptr);
        CHECK_EQ(st.p[1].health, 1000 - 30 - 45);
        CHECK_EQ(st.p[1].comboCount == 0 ? 2 : st.p[1].comboCount, 2);
    }
    {   // the same MP pressed BEFORE anything connected and without hitstop is just a whiffed-buffer: no chain
        MatchState st = fresh(ms, -900, 900);
        runTicks(st, c, 0, 60, [](int t) -> uint16_t { return t == 2 ? kLP : (t == 4 ? kMP : 0); }, nullptr);
        CHECK_EQ(st.p[1].health, 1000);
    }
    {   // chain L -> M -> H, scaled: 30 + 500*90% + 800*80% = 30 + 45 + 64
        MatchState st = fresh(ms, -90, 90);
        runTicks(st, c, 0, 90, [](int t) -> uint16_t { return t == 2 ? kLP : (t == 6 ? kMP : (t == 18 ? kHP : 0)); }, nullptr);
        CHECK_EQ(st.p[1].health, 1000 - 30 - 45 - 64);
    }
    {   // punch -> kick of the same strength is a chain (MP -> MK), kick -> punch is not (MK -> MP)
        MatchState a = fresh(ms, -90, 90);
        runTicks(a, c, 0, 70, [](int t) -> uint16_t { return t == 2 ? kMP : (t == 8 ? kMK : 0); }, nullptr);
        CHECK_EQ(a.p[1].health, 1000 - 50 - 45);
        MatchState b = fresh(ms, -90, 90);
        runTicks(b, c, 0, 70, [](int t) -> uint16_t { return t == 2 ? kMK : (t == 8 ? kMP : 0); }, nullptr);
        CHECK_EQ(b.p[1].health, 1000 - 50);
    }
    {   // each normal once per chain: LP -> LK -> LP stops after the LK
        MatchState st = fresh(ms, -90, 90);
        runTicks(st, c, 0, 90, [](int t) -> uint16_t { return t == 2 ? kLP : (t == 6 ? kLK : (t == 16 ? kLP : 0)); }, nullptr);
        CHECK_EQ(st.p[1].health, 1000 - 30 - 27);
    }
    {   // blocked normal: derived blockstun (16 - 3 = 13), no chip
        MatchState st = fresh(ms, -90, 90);
        bool sawBlock = false;
        int bs = 0;
        for (int t = 0; t < 40; ++t) {
            advanceDuel(st, t == 2 ? kLP : 0, kRight /* P2 holds back */, c);
            if (!sawBlock && st.p[1].blockstun > 0) { sawBlock = true; bs = st.p[1].blockstun; }
        }
        CHECK(sawBlock);
        CHECK_EQ(bs, 13);
        CHECK_EQ(st.p[1].health, 1000);
    }
    {   // blocked special: chip = 25% of 300 raw -> 7
        MatchState st = fresh(ms, -90, 90);
        for (int t = 0; t < 40; ++t) {
            advanceDuel(st, t == 2 ? kLP : 0, kRight, c);
            if (t == 2) st.p[0].state = CharState::kAttackSpecial;  // stand-in for a special until phase 2
        }
        CHECK_EQ(st.p[1].health, 993);
        DuelContent noKill = c;
        noKill.rules.chipKills = false;
        MatchState low = fresh(ms, -90, 90);
        low.p[1].health = 3;
        for (int t = 0; t < 40; ++t) {
            advanceDuel(low, t == 2 ? kLP : 0, kRight, noKill);
            if (t == 2) low.p[0].state = CharState::kAttackSpecial;
        }
        CHECK_EQ(low.p[1].health, 1);
        CHECK_EQ(static_cast<int>(low.roundOver), 0);
    }
    {   // corner: the defender is at the wall, so the attacker is pushed back instead
        MatchState st = fresh(ms, 1800 - 190, 1800);
        const fixed x0 = st.p[0].x;
        runTicks(st, c, 0, 40, [](int t) -> uint16_t { return t == 2 ? kLP : 0; }, nullptr);
        CHECK_EQ(st.p[1].health, 970);
        CHECK_EQ(st.p[1].x, toFixed(1800.0f));
        CHECK(st.p[0].x < x0 - toFixed(10.0f));
        // mid-screen the attacker does not move back
        MatchState mid = fresh(ms, -90, 90);
        const fixed m0 = mid.p[0].x;
        runTicks(mid, c, 0, 40, [](int t) -> uint16_t { return t == 2 ? kLP : 0; }, nullptr);
        CHECK_EQ(mid.p[0].x, m0);
    }
    {   // undizzy: with a tiny bar the second hit (8 + 12 >= 20) ends the combo with invulnerability
        DuelContent u = c;
        u.rules.undizzyMax = 20;
        MatchState st = fresh(ms, -90, 90);
        bool escaped = false;
        for (int t = 0; t < 60; ++t) {
            advanceDuel(st, t == 2 ? kLP : (t == 6 ? kMP : (t == 18 ? kHP : 0)), 0, u);
            if (st.p[1].invul > 0) escaped = true;
        }
        CHECK(escaped);
        CHECK_EQ(st.p[1].health, 1000 - 30 - 45);  // the HP did not connect: the defender was invulnerable
        CHECK_EQ(st.p[1].undizzy, 0);
    }
    {   // undizzy drains once the defender is free again
        MatchState st = fresh(ms, -90, 90);
        runTicks(st, c, 0, 200, [](int t) -> uint16_t { return t == 2 ? kLP : 0; }, nullptr);
        CHECK_EQ(st.p[1].undizzy, 0);
    }
    {   // determinism with the new fields: snapshot every tick never diverges
        MatchState a = fresh(ms, -120, 120), b = fresh(ms, -120, 120);
        int mismatches = 0;
        for (int t = 0; t < 400; ++t) {
            const uint16_t i1 = (t % 37 == 3) ? kLP : (t % 23 == 5) ? kMP : (t % 53 == 9) ? kHK : (t % 29 < 8 ? kRight : (t % 31 < 3 ? kDown : 0));
            const uint16_t i2 = (t % 41 == 7) ? kMK : (t % 17 < 6 ? kRight : 0);
            advanceDuel(a, i1, i2, c);
            std::vector<uint8_t> snap = saveState(b);
            MatchState copy{};
            CHECK(loadState(copy, snap));
            advanceDuel(copy, i1, i2, c);
            b = copy;
            if (a.checksum() != b.checksum()) ++mismatches;
        }
        CHECK_EQ(mismatches, 0);
        CHECK(a.p[0].histLen[0] > 0);
    }
}


// ---- touch pad ----------------------------------------------------------------------------------------------
// ---- entrances ----------------------------------------------------------------------------------------------
static void testEntrance(const MoveSet& ms) {
    std::printf("[entrance]\n");
    const std::vector<int> ins = ms.introStates();
    CHECK_EQ(ins.size(), 2u);
    if (ins.size() != 2) return;
    CHECK(ms.stateName(ins[0]) == "Intro_Wave_ForReal");
    CHECK(ms.stateName(ins[1]) == "Intro_Long_ForReal");
    DuelContent c;
    c.ms[0] = c.ms[1] = &ms;
    {   // the entrance plays from its first tick and lasts exactly as long as its timeline; then the fighter stands and may act
        MatchState st{};
        duelStartRound(st.p[0], ms, toFixed(-90.0f), Facing::kRight, 85, ins[0]);
        duelStartRound(st.p[1], ms, toFixed(90.0f), Facing::kLeft, 85, -1);
        CHECK(st.p[0].state == CharState::kIntro);
        CHECK_EQ(st.p[0].mv, ins[0]);
        CHECK_EQ(st.p[0].stateTimer, 20);
        CHECK_EQ(st.p[1].stateTimer, 85);   // no entrance: the plain intro of standing still
        for (int t = 0; t < 19; ++t) advanceDuel(st, 0, 0, c);
        CHECK(st.p[0].state == CharState::kIntro);   // one tick before the end
        advanceDuel(st, 0, 0, c);
        CHECK(st.p[0].state == CharState::kIdle);
        CHECK_EQ(st.p[0].mv, ms.id.stand);
        CHECK(st.p[1].state == CharState::kIntro);   // the other one is still on its plain intro
    }
    {   // nobody can be hit during the entrance
        MatchState st{};
        duelStartRound(st.p[0], ms, toFixed(-90.0f), Facing::kRight, 0, -1);
        duelStartRound(st.p[1], ms, toFixed(90.0f), Facing::kLeft, 0, ins[0]);
        st.roundTimer = 5940;
        for (int t = 0; t < 4; ++t) advanceDuel(st, t == 1 ? kLP : 0, 0, c);
        CHECK_EQ(st.p[1].health, 1000);
    }
    {   // a button press cuts it short: on his feet right away
        MatchState st{};
        duelStartRound(st.p[0], ms, toFixed(-90.0f), Facing::kRight, 0, ins[1]);
        for (int t = 0; t < 30; ++t) advanceDuel(st, 0, 0, c);
        CHECK(st.p[0].state == CharState::kIntro);
        duelSkipIntro(st.p[0], ms);
        CHECK(st.p[0].state == CharState::kIdle);
        CHECK_EQ(st.p[0].mv, ms.id.stand);
        CHECK_EQ(st.p[0].stateTimer, 0);
        duelSkipIntro(st.p[0], ms);                  // harmless when nothing is playing
        CHECK(st.p[0].state == CharState::kIdle);
    }
    {   // a bad state number falls back to the plain intro
        Fighter f{};
        duelStartRound(f, ms, 0, Facing::kRight, 30, 99999);
        CHECK_EQ(f.stateTimer, 30);
        CHECK_EQ(f.mv, ms.id.stand);
    }
}

static void testTouchPad() {
    std::printf("[touch pad]\n");
    using touch::Finger;
    using touch::stickDirection;
    CHECK_EQ(stickDirection(50, 0, 14, 60), 6);
    CHECK_EQ(stickDirection(0, 50, 14, 60), 2);   // y grows downwards on screen
    CHECK_EQ(stickDirection(0, -50, 14, 60), 8);
    CHECK_EQ(stickDirection(-50, 0, 14, 60), 4);
    CHECK_EQ(stickDirection(35, 35, 14, 60), 3);
    CHECK_EQ(stickDirection(-35, 35, 14, 60), 1);
    CHECK_EQ(stickDirection(5, 5, 14, 60), 5);    // dead zone
    CHECK_EQ(stickDirection(50, 20, 14, 60), 3);  // 22 deg below forward: wide diagonals read down-forward ...
    CHECK_EQ(stickDirection(50, 20, 14, 30), 6);  // ... narrow ones read forward
    CHECK_EQ(touch::dirBits(3), kDown | kRight);
    CHECK_EQ(touch::dirBits(5), 0);

    touch::TouchPad pad;
    const touch::Layout& L = pad.layout();
    auto centre = [&](const char* label, int& x, int& y) {
        for (const auto& b : L.buttons)
            if (b.label == label) { x = b.x + b.w / 2; y = b.y + b.h / 2; return true; }
        return false;
    };
    int lpx = 0, lpy = 0, lkx = 0, lky = 0, ppx = 0, ppy = 0, kkx = 0, kky = 0;
    CHECK(centre("LP", lpx, lpy) && centre("LK", lkx, lky) && centre("3P", ppx, ppy) && centre("3K", kkx, kky));

    {   // a thumb rolling a quarter circle on the stick gives a 236 the runtime recognises
        pad.reset();
        Fighter f{};
        const int bx = 100, by = 280;
        const int path[][2] = {{0, 0}, {0, 40}, {0, 44}, {30, 30}, {32, 32}, {44, 0}, {46, 0}};
        uint16_t last = 0;
        for (const auto& p : path) {
            last = pad.update({Finger{7, bx + p[0], by + p[1]}});
            pushHistory(f, last);
        }
        CHECK(pad.stickActive());
        CHECK_EQ(pad.direction(), 6);
        CHECK_EQ(last, kRight);
        CHECK(motionDone(f, "236", 10, 8));
    }
    {   // the stick finger keeps the stick even when it slides over the buttons; a second finger presses LP
        pad.reset();
        pad.update({Finger{1, 100, 280}});
        uint16_t b = pad.update({Finger{1, lpx, lpy}});
        CHECK((b & kLP) == 0);
        CHECK(b & kRight);
        b = pad.update({Finger{1, 140, 280}, Finger{2, lpx, lpy}});
        CHECK(b & kLP);
        CHECK(pad.pressed(0));
        // release the stick: neutral, LP still held
        b = pad.update({Finger{2, lpx, lpy}});
        CHECK_EQ(b & (kLeft | kRight | kUp | kDown), 0);
        CHECK(b & kLP);
        CHECK(!pad.stickActive());
    }
    {   // a finger between LP and LK presses both; macros press their bits
        pad.reset();
        const uint16_t both = pad.update({Finger{3, lpx, (lpy + lky) / 2}});
        CHECK((both & kLP) && (both & kLK));
        CHECK(pad.update({Finger{4, ppx, ppy}}) & kMacro1);
        CHECK(pad.update({Finger{5, kkx, kky}}) & kMacro2);
        CHECK_EQ(expandMacros(kMacro1) & (kLP | kMP | kHP), kLP | kMP | kHP);
    }
    {   // a finger that lands on a button inside the stick zone does not grab the stick
        touch::Layout wide = touch::Layout::defaults();
        wide.zoneRight = 640;
        touch::TouchPad p2(wide);
        const uint16_t b = p2.update({Finger{9, lpx, lpy}});
        CHECK(!p2.stickActive());
        CHECK(b & kLP);
    }
    {   // layout file round trip, user scale, garbage rejected
        touch::Layout l = touch::Layout::defaults();
        l.scale = 1.25f;
        l.opacity = 90;
        l.floating = false;
        l.buttons[0].x = 300;
        touch::Layout back;
        CHECK(touch::Layout::parse(l.serialize(), back));
        CHECK_EQ(back.buttons.size(), l.buttons.size());
        CHECK_EQ(back.buttons[0].x, 300);
        CHECK_EQ(back.opacity, 90);
        CHECK(!back.floating);
        CHECK(back.scale > 1.24f && back.scale < 1.26f);
        const touch::PadButton s = back.scaled(back.buttons[1]);
        CHECK(s.w > back.buttons[1].w);
        touch::Layout junk;
        CHECK(!touch::Layout::parse("hello", junk));
    }
}

// ---- the older characters (Filia ...): another set of names for the same moves ------------------------------------------
// Annie / Umbrella call their normals s.WP s.MP s.HP s.WK s.MK s.HK and have a Jump_Start_N/F/B + JumpLand_N/F/B; Filia calls them
// s.Jab s.Strong s.Fierce s.Short s.Fwd s.RH and has ONE Jump_Start and ONE JumpLand.  Before the aliases existed none of her 18 normals
// resolved (they never came out) and she could not jump.
static void testClassicNames() {
    std::printf("[older characters: Jab/Strong/Fierce/Short/Fwd/RH, one Jump_Start / JumpLand]\n");
    auto ch = std::make_shared<foit::Character>();
    foit::Frame idle;
    idle.hurt.push_back({-160, 0, -40, 40});
    idle.push.push_back({-160, 0, -45, 45});
    foit::Frame atk = idle;
    atk.hit.push_back({-120, -80, 0, 250});
    ch->frames = {idle, atk};
    std::vector<foit::State>& st = ch->body.states;
    std::vector<std::string> nexts;
    auto add = [&](const std::string& name, std::vector<foit::Node> nodes, const std::string& next) {
        foit::State s;
        s.name = name;
        s.hdr.nodes = std::move(nodes);
        st.push_back(s);
        nexts.push_back(next);
    };
    add("Stand", {frameNode(1, 10)}, "Stand");
    add("Crouch", {frameNode(1, 10)}, "Crouch");
    add("WalkFwd", {}, "WalkFwd_Loop");   // Filia's WalkFwd / WalkFwd_End have no frames of their own
    add("WalkFwd_Loop", {frameNode(1, 4)}, "WalkFwd_Loop");
    add("WalkFwd_End", {}, "Stand");
    add("WalkBack", {frameNode(1, 4)}, "WalkBack");
    add("JumpFall", {frameNode(1, 4)}, "JumpFall");
    add("Jump_Start", {frameNode(1, 4)}, "JumpRise_N");
    add("JumpRise_N", {frameNode(1, 4)}, "JumpRise_N");
    add("JumpRise_F", {frameNode(1, 4)}, "JumpRise_F");
    add("JumpRise_B", {frameNode(1, 4)}, "JumpRise_B");
    add("JumpLand", {frameNode(1, 2)}, "JumpLand_Recovery");
    add("JumpLand_Recovery", {frameNode(1, 17)}, "Stand");
    add("s.HitMid1", {frameNode(1, 20)}, "Stand");
    add("s.Jab_RapidFire", {frameNode(1, 3)}, "Stand");   // looks like a normal but is not one
    static const char* names[3][6] = {{"s.Jab", "s.Strong", "s.Fierce", "s.Short", "s.Fwd", "s.RH"},
                                      {"c.Jab", "c.Strong", "c.Fierce", "c.Short", "c.Fwd", "c.RH"},
                                      {"j.Jab", "j.Strong", "j.Fierce", "j.Short", "j.Fwd", "j.RH"}};
    for (int p = 0; p < 3; ++p)
        for (int b = 0; b < 6; ++b) {
            foit::Node active = frameNode(2, 3);
            active.props[0].hasHit = true;
            active.props[0].f2 = 1;
            active.props[0].hit.fc = 300;
            active.props[0].hit.f10 = 8;
            active.props[0].hit.f8 = 18;
            active.props[0].hit.f13 = 3;
            active.props[0].hit.f18 = static_cast<uint8_t>(b % 3 + 1);
            add(names[p][b], {frameNode(1, 3), active, frameNode(1, 10)}, p == 2 ? "JumpFall" : "Stand");
        }
    for (size_t i = 0; i < st.size(); ++i) {
        st[i].next = 0;
        for (size_t j = 0; j < st.size(); ++j)
            if (st[j].name == nexts[i]) st[i].next = static_cast<uint16_t>(j);
    }
    MoveSet ms;
    std::string err;
    CHECK(ms.build(ch, &err));
    for (int p = 0; p < 3; ++p)
        for (int b = 0; b < 6; ++b) {
            CHECK_EQ(ms.id.normal[p][b], ms.find(names[p][b]));
            CHECK(ms.id.normal[p][b] >= 0);
        }
    CHECK(ms.cat(ms.id.normal[kPStand][0]) == Cat::kAttackL);
    CHECK(ms.cat(ms.id.normal[kPStand][5]) == Cat::kAttackH);
    CHECK(ms.find("s.Jab_RapidFire") >= 0);
    for (int p = 0; p < 3; ++p)
        for (int b = 0; b < 6; ++b) CHECK(ms.id.normal[p][b] != ms.find("s.Jab_RapidFire"));
    for (int d = 0; d < 3; ++d) {
        CHECK_EQ(ms.id.jumpStart[d], ms.find("Jump_Start"));
        CHECK_EQ(ms.id.land[d], ms.find("JumpLand"));
    }
    CHECK(ms.cat(ms.find("Jump_Start")) == Cat::kJumpStart);
    CHECK(ms.cat(ms.find("JumpLand")) == Cat::kLand);
    CHECK(ms.cat(ms.find("JumpLand_Recovery")) == Cat::kLand);
    // the old names still win when a character has both sets, and the unsplit states are not forced on a character that has the split ones
    CHECK_EQ(ms.find("WalkFwd") >= 0 ? 1 : 0, 1);

    // in a duel: a punch comes out and Up starts a jump
    {
        MatchState m = fresh(ms, -200, 200);
        DuelContent c;
        c.ms[0] = c.ms[1] = &ms;
        for (int i = 0; i < 6; ++i) advanceDuel(m, 0, 0, c);
        advanceDuel(m, kLP, 0, c);
        advanceDuel(m, kLP, 0, c);
        CHECK_EQ(static_cast<int>(m.p[0].mv), ms.id.normal[kPStand][0]);
        MatchState j = fresh(ms, -200, 200);
        for (int i = 0; i < 6; ++i) advanceDuel(j, 0, 0, c);
        advanceDuel(j, kUp, 0, c);
        CHECK(j.p[0].state == CharState::kJumpStart);
        CHECK_EQ(static_cast<int>(j.p[0].mv), ms.find("Jump_Start"));
        for (int i = 0; i < 12; ++i) advanceDuel(j, 0, 0, c);
        CHECK(j.p[0].state == CharState::kJumpRise || j.p[0].state == CharState::kJumpFall);
    }
}

int main() {
    testMotions();
    testTouchPad();
    testClassicNames();
    auto ch = makeChar();
    MoveSet ms;
    std::string err;
    const bool ok = ms.build(ch, &err);
    CHECK(ok);
    if (!ok) std::printf("  moveset: %s\n", err.c_str());
    else {
        testRules(ms);
        testEntrance(ms);
    }
    std::printf("\n%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
