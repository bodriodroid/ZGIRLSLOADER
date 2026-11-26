// foit_write.h - foit::Character -> the bytes of a <char>.foit (the exact inverse of foit::parse; grammar in foit.h / docs/foit.ksy).
//
// write(parse(file)) is byte for byte the file for all 20 character files of the PC build (tests/test_foit_write.cpp checks that when
// ZDROID_TEST_CHARS points at the folder).  The colour-palette "layer object" is copied from Character::layerRaw (the parser keeps only part of it);
// a character built from scratch has hasLayer = false and takes its palettes from the <name>_Np.dds textures instead.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "foit.h"

namespace zengine {
namespace foit {

// false + *err when the structure cannot be written (an empty script chain that must have a node, a count above its field, a frame id that
// is not in the frame list ...).  Nothing about the pixels is checked: the Image records say where their planes sit in the .art.
bool write(const Character& c, std::vector<uint8_t>& out, std::string* err = nullptr);

}  // namespace foit
}  // namespace zengine
