// mugen_foit.h - the .foit of a converted MUGEN character: its frames, boxes and one animation state per .air action.
//
// This is the animation side only.  Every .air frame becomes a foit::Frame (the sprite in image slot 0, drawn at the .air offset minus the sprite's
// axis, mirrored copies of the sprite for H / V flips; clsn2 -> hurt boxes, clsn1 -> hit boxes) and every action a state of timed nodes
// (state "Stand" is action 0, the others "Action_<id>"; an action that loops from a later frame gets a second state "<name>_Loop").  No move
// logic and no commands: MUGEN's CNS/CMD is a different language from the game's slot scripts, so states do not branch yet.
#pragma once
#include <string>
#include <vector>

#include "art_write.h"
#include "foit.h"
#include "mugen.h"

namespace zengine {
namespace mugen {

struct FoitBuild {
    foit::Character ch;
    size_t frames = 0, states = 0, mirroredImages = 0, missingSprites = 0;
};

// `images[i]` is the already encoded image of sprites[i] (its planes are in `art`); images for mirrored copies are appended to `art`.
bool buildFoit(const std::string& name, const std::vector<Sprite>& sprites, const std::vector<foit::Image>& images, std::vector<uint8_t>& art,
               const std::vector<AirAction>& actions, FoitBuild& out, std::string* err = nullptr);

}  // namespace mugen
}  // namespace zengine
