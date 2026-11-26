// mugen_moves.h - playable states for a converted MUGEN character: walking, crouching, jumping, attacks and being hit.
//
// MUGEN's CNS / CMD are a different language from the game's slot scripts, so the move logic is NOT translated.  What is done instead is a small
// template: the character's own animations (already states of the .foit, see mugen_foit.h) are put into a fixed set of gameplay states --
//   Stand, Crouch, WalkFwd, WalkBack, JumpStart_N/F/B, Jump_N/F/B, JumpLand, s.Hit / c.Hit / j.Hit (+ their stun loops), one state per attack --
// whose scripts, transitions and hit data are written here with the slots and ops the engine's own runtime uses (foit_rt.cpp).  Which animation
// plays in each place, and the numbers (speeds, damage, stun), come from a small text file the person converting the character writes:
//
//   [Moves]                       animation (action) numbers of the .air
//   stand = 0                     walkfwd = 20      walkback = 21
//   crouch = 11                   jumpstart = 40    jump = 41, 42, 43   (neutral, forward, back)     land = 47
//   hit = 5000                    crouchhit = 5010  airhit = 5020
//   walkspeed = 6   walkbackspeed = 4.5   jumpspeed = 32   jumpforward = 8   jumpback = 7   life = 14300
//   push = 38, 40                 how far the body pushes the opponent: back and front, from the pivot (default: the stand hurt box, which is
//                                 usually wider than the body because it includes the arms)
//
//   [Attack s.Jab]                one section per attack; the name decides nothing, the posture does
//   posture = stand               stand | crouch | air
//   button  = LP MP HP            any of LP MP HP LK MK HK starts it (a held direction does not matter)
//   anim    = 200, 201            the actions played one after the other (their .air clsn1 boxes are the hit boxes)
//   damage = 300  hitstop = 7  hitstun = 18  level = 3  pushback = 8   (the game's units; level 3 = mid, hits standing and crouching)
//
// Everything is optional (a missing action is skipped, and so are the states that need it).  The result animates, walks, jumps, attacks and
// takes hits; it has no blocking, knock-down, specials, supers or KO handling yet.
#pragma once
#include <string>
#include <vector>

#include "foit.h"

namespace zengine {
namespace mugen {

struct Attack {
    std::string name;
    std::string posture = "stand";
    std::vector<int> buttons;   // 0..5 = LP MP HP LK MK HK
    std::vector<int> anims;
    int damage = 300, hitstop = 7, hitstun = 18, level = 3, pushback = 8;
};

struct MoveSet {
    int stand = 0, walkFwd = -1, walkBack = -1, crouch = -1, jumpStart = -1, land = -1, hit = -1, crouchHit = -1, airHit = -1;
    int jump[3] = {-1, -1, -1};
    int pushBack = 0, pushFront = 0;   // 0 = keep the box buildFoit derived
    float walkSpeed = 6.0f, walkBackSpeed = 4.5f, jumpSpeed = 32.0f, jumpForward = 8.0f, jumpBack = 7.0f, life = 14300.0f;
    std::vector<Attack> attacks;
};

// false (and *err) only for a file that cannot be read at all; lines it does not understand go to *warnings.
bool parseMoves(const std::string& text, MoveSet& out, std::vector<std::string>* warnings = nullptr);

// Adds the gameplay states to a character made by buildFoit (its states "Stand" / "Action_<id>" are the animations).  Returns false + *err when a
// needed animation is missing.  `notes` receives what was and was not built.
bool addMoves(foit::Character& c, const MoveSet& m, std::vector<std::string>* notes, std::string* err);

}  // namespace mugen
}  // namespace zengine
