// test_mugen_moves.cpp - the gameplay template of a converted MUGEN character: the moves file, the states it adds, and that they survive foit::write.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "art_write.h"
#include "foit_names.h"
#include "foit_write.h"
#include "mugen.h"
#include "mugen_foit.h"
#include "mugen_moves.h"

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

static const char* kMoves =
    "; a comment\n"
    "[Moves]\n"
    "stand = 0\nwalkfwd = 20  ; trailing comment\nwalkback = 21\ncrouch = 11\njumpstart = 40\njump = 41, 42, 43\nland = 47\n"
    "hit = 5000\ncrouchhit = 5010\nairhit = 5020\n"
    "walkspeed = 5\nwalkbackspeed = 4\njumpspeed = 30\njumpforward = 7\njumpback = 6\nlife = 9000\npush = 38, 40\n"
    "[Attack s.Jab]\nposture = stand\nbutton = LP MP HP\nanim = 200, 201\ndamage = 350\nhitstop = 6\nhitstun = 17\nlevel = 2\npushback = 9\n"
    "[Attack c.Low]\nposture = crouch\nbutton = LK\nanim = 220\ndamage = 400\n"
    "[Attack j.Air]\nposture = air\nbutton = LP LK\nanim = 610\n"
    "[Attack broken]\nposture = stand\nbutton = HK\nanim = 9999\n";

int main() {
    // ---- the moves file ----------------------------------------------------------------------------------------------------------------------------------
    {
        mugen::MoveSet m;
        std::vector<std::string> warn;
        CHECK(mugen::parseMoves(kMoves, m, &warn));
        CHECK(warn.empty());
        CHECK(m.stand == 0 && m.walkFwd == 20 && m.walkBack == 21 && m.crouch == 11 && m.jumpStart == 40 && m.land == 47);
        CHECK(m.jump[0] == 41 && m.jump[1] == 42 && m.jump[2] == 43 && m.hit == 5000 && m.crouchHit == 5010 && m.airHit == 5020);
        CHECK(m.walkSpeed == 5.0f && m.walkBackSpeed == 4.0f && m.jumpSpeed == 30.0f && m.jumpForward == 7.0f && m.jumpBack == 6.0f && m.life == 9000.0f);
        CHECK(m.pushBack == 38 && m.pushFront == 40);
        CHECK(m.attacks.size() == 4);
        if (m.attacks.size() == 4) {
            const mugen::Attack& a = m.attacks[0];
            CHECK(a.name == "s.Jab" && a.posture == "stand" && a.buttons == std::vector<int>({0, 1, 2}) && a.anims == std::vector<int>({200, 201}));
            CHECK(a.damage == 350 && a.hitstop == 6 && a.hitstun == 17 && a.level == 2 && a.pushback == 9);
            CHECK(m.attacks[1].posture == "crouch" && m.attacks[1].buttons == std::vector<int>({3}) && m.attacks[1].damage == 400 && m.attacks[1].hitstop == 7);   // defaults stay
            CHECK(m.attacks[2].posture == "air" && m.attacks[2].buttons == std::vector<int>({0, 3}));
        }
        // lines it does not understand are reported, the rest still counts
        mugen::MoveSet w;
        warn.clear();
        CHECK(mugen::parseMoves("[Moves]\nstand = 3\nbogus = 1\nnot a pair\npush = 5\n[Mystery]\n[Attack x]\nbutton = LP ZZ\nwhat = 1\n", w, &warn));
        CHECK(w.stand == 3 && w.pushBack == 0 && w.pushFront == 0);
        CHECK(warn.size() == 6);
        // an empty file is a valid, empty move set
        mugen::MoveSet e;
        CHECK(mugen::parseMoves("", e, nullptr) && e.attacks.empty() && e.stand == 0 && e.walkFwd == -1);
    }

    // ---- a small character -------------------------------------------------------------------------------------------------------------------------------
    std::vector<mugen::Sprite> spr(1);
    spr[0].group = 0; spr[0].image = 0; spr[0].w = 20; spr[0].h = 18; spr[0].axisX = 10; spr[0].axisY = 18;
    spr[0].px.assign(20 * 18, 0);
    for (int y = 2; y < 18; ++y)
        for (int x = 3; x < 17; ++x) spr[0].px[static_cast<size_t>(y) * 20 + x] = static_cast<uint8_t>(1 + (x + y) % 9);
    std::vector<uint8_t> art;
    std::vector<foit::Image> imgs(1);
    CHECK(encodeArtImage(spr[0].w, spr[0].h, spr[0].px, std::vector<uint8_t>(spr[0].px.size(), 0), art, imgs[0]) && imgs[0].n > 0);
    std::string air;
    auto idle = [&](int id, int ticks, bool hit) {
        air += "[Begin Action " + std::to_string(id) + "]\nClsn2: 1\n Clsn2[0] = -6,-18, 6, 0\n";
        if (hit) air += "Clsn1: 1\n Clsn1[0] = 4,-14, 16,-8\n";
        air += "0,0, 0,0, " + std::to_string(ticks) + "\n0,0, 0,0, " + std::to_string(ticks) + "\n";
    };
    for (int id : {0, 20, 21, 11, 40, 41, 42, 43, 47, 5000, 5010, 5020}) idle(id, 3, false);
    idle(200, 3, true);
    idle(201, 3, false);
    idle(220, 3, true);
    idle(610, 3, true);
    mugen::FoitBuild fb;
    std::string err;
    CHECK(mugen::buildFoit("t", spr, imgs, art, mugen::parseAir(air), fb, &err));

    mugen::MoveSet m;
    CHECK(mugen::parseMoves(kMoves, m, nullptr));
    foit::Character c = fb.ch;
    std::vector<std::string> notes;
    CHECK(mugen::addMoves(c, m, &notes, &err));
    bool sawBroken = false;
    for (const std::string& n : notes)
        if (n.find("broken") != std::string::npos) sawBroken = true;
    CHECK(sawBroken);   // the attack whose action is not in the .air is reported, not silently built

    for (const char* name : {"Stand", "WalkFwd", "WalkBack", "Crouch", "JumpStart_N", "JumpStart_F", "JumpStart_B", "Jump_N", "Jump_F", "Jump_B", "JumpAir_N", "JumpAir_F",
                             "JumpAir_B", "JumpLand", "s.Hit", "s.HitStun", "c.Hit", "c.HitStun", "j.Hit", "j.HitStun", "s.Jab", "c.Low", "j.Air"})
        CHECK(c.body.findState(name) >= 0);
    CHECK(c.body.findState("broken") < 0);

    // slot defaults, push box
    float life = 0;
    std::memcpy(&life, &c.body.slotDefaults[foit::slot::LIFEBAR], 4);
    CHECK(life == 9000.0f);
    CHECK(c.body.box[2] == -38 && c.body.box[3] == 40);

    const int stand = c.body.findState("Stand"), jab = c.body.findState("s.Jab"), low = c.body.findState("c.Low"), crouch = c.body.findState("Crouch");
    const int air1 = c.body.findState("j.Air");
    CHECK(stand >= 0 && jab >= 0 && low >= 0 && crouch >= 0 && air1 >= 0);
    if (stand < 0 || jab < 0 || low < 0 || crouch < 0 || air1 < 0) {
        std::printf("mugen moves tests: %d checks, %d failed (stopped: states are missing)\n", g_n, g_fail + 1);
        return 1;
    }

    // the held direction picks the posture: from Stand a standing attack needs "not down" (negated step), a crouching one needs down
    auto firstTo = [&](const foit::State& s, int target) -> const foit::Transition* {
        for (const foit::Transition& t : s.transitions)
            if (t.target == target) return &t;
        return nullptr;
    };
    const foit::Transition* tj = firstTo(c.body.states[static_cast<size_t>(stand)], jab);
    const foit::Transition* tl = firstTo(c.body.states[static_cast<size_t>(stand)], low);
    CHECK(tj && tl);
    if (tj && tl) {
        CHECK(!tj->cmd.steps.empty() && (tj->cmd.steps[0].flags & 1) && tj->cmd.steps[0].b0 == 2 && tj->cmd.steps[0].b1 == 1 && tj->cmd.steps[0].b2 == 3);
        CHECK(!tl->cmd.steps.empty() && !(tl->cmd.steps[0].flags & 1) && tl->cmd.steps[0].b0 == 2 && tl->cmd.steps[0].t[3] == 0);
        CHECK(tj->cmd.steps[0].t[0] == 0);   // LP pressed
    }
    // inside Crouch the crouching attack starts from any direction
    const foit::Transition* tc = firstTo(c.body.states[static_cast<size_t>(crouch)], low);
    CHECK(tc && !tc->cmd.steps.empty() && tc->cmd.steps[0].b0 == 9);
    // the stand attack is not offered from Crouch, the air one only in the air
    CHECK(firstTo(c.body.states[static_cast<size_t>(crouch)], jab) == nullptr);
    CHECK(firstTo(c.body.states[static_cast<size_t>(stand)], air1) == nullptr);
    CHECK(firstTo(c.body.states[static_cast<size_t>(c.body.findState("JumpAir_N"))], air1) != nullptr);
    // an air attack that ends before the ground keeps falling instead of "landing" in mid-air
    CHECK(c.body.states[static_cast<size_t>(air1)].next == c.body.findState("JumpAir_N"));
    CHECK(c.body.states[static_cast<size_t>(jab)].next == stand && c.body.states[static_cast<size_t>(low)].next == crouch);

    // the hit data of the first frame with a hit box
    bool hitFound = false;
    for (const foit::Node& n : c.body.states[static_cast<size_t>(jab)].hdr.nodes)
        for (const foit::NodeProps& p : n.props)
            if (p.hasHit) {
                hitFound = true;
                CHECK(p.hit.fc == 350 && p.hit.f10 == 6 && p.hit.f8 == 17 && p.hit.f13 == 2 && p.hit.hasChain[0] && p.hit.hasChain[1]);
            }
    CHECK(hitFound);

    // ---- it survives the writer --------------------------------------------------------------------------------------------------------------------------
    std::vector<uint8_t> bytes;
    CHECK(foit::write(c, bytes, &err));
    foit::Character back;
    CHECK(foit::parse(bytes.data(), bytes.size(), back, &err));
    CHECK(back.body.states.size() == c.body.states.size() && back.frames.size() == c.frames.size());
    std::vector<uint8_t> again;
    CHECK(foit::write(back, again, &err) && again == bytes);   // a second trip changes nothing
    const int bj = back.body.findState("s.Jab");
    CHECK(bj == jab && back.body.states[static_cast<size_t>(bj)].transitions.size() == c.body.states[static_cast<size_t>(jab)].transitions.size());
    CHECK(back.body.box[2] == -38 && back.body.box[3] == 40);

    // ---- what is not there is not built ------------------------------------------------------------------------------------------------------------------
    {
        mugen::MoveSet lean;
        CHECK(mugen::parseMoves("[Moves]\nstand = 0\n", lean, nullptr));
        foit::Character l = fb.ch;
        std::vector<std::string> ln;
        CHECK(mugen::addMoves(l, lean, &ln, &err));
        CHECK(l.body.findState("WalkFwd") < 0 && l.body.findState("Crouch") < 0 && l.body.findState("JumpLand") < 0 && l.body.findState("s.Hit") < 0);
        CHECK(l.body.findState("Stand") == 0);
        mugen::MoveSet nostand;
        CHECK(mugen::parseMoves("[Moves]\nstand = 7777\n", nostand, nullptr));
        foit::Character n = fb.ch;
        CHECK(!mugen::addMoves(n, nostand, nullptr, &err) && !err.empty());
    }

    std::printf("mugen moves tests: %d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
