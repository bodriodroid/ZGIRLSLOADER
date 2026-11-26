// mugen.h - readers for the files of a MUGEN character (the format is public, documented by the MUGEN community):
//   .sff v1 sprites (PCX images, 256-colour palette indices; index 0 is transparent)   .act palettes   .air animations   .def file list
// Used by zdroid_mugen, which turns such a character into <name>.art + palette textures (art_write.h).
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace zengine {
namespace mugen {

struct Sprite {
    int group = 0, image = 0;
    int axisX = 0, axisY = 0;       // the point of the picture that is the sprite's origin
    int w = 0, h = 0;
    std::vector<uint8_t> px;        // w*h palette indices (0 = transparent)
    std::array<uint8_t, 768> ownPal{};
    bool hasOwnPal = false;         // the PCX carried its own palette (R,G,B per index)
    int linkedTo = -1;              // this sprite shares the picture of that one (its axis is its own)
};

// SFF version 1.  Linked sprites get a copy of the picture they point at.  false + *err when the file is not a readable v1 file.
bool readSff1(const uint8_t* data, size_t size, std::vector<Sprite>& out, std::string* err = nullptr);

// .act: 256 RGB triples stored last colour first (the file may carry a trailing count).  rgb[index*3 + c].
bool readAct(const uint8_t* data, size_t size, std::array<uint8_t, 768>& rgb);

struct Box {
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
};
struct AirFrame {
    int group = 0, image = 0, x = 0, y = 0;
    int time = 1;                   // ticks at 60 Hz; -1 = lasts forever
    bool flipH = false, flipV = false;
    std::vector<Box> clsn1, clsn2;  // attack boxes / body boxes
};
struct AirAction {
    int id = 0;
    int loopStart = 0;              // index of the frame the action returns to
    std::vector<AirFrame> frames;
};
std::vector<AirAction> parseAir(const std::string& text);

// The part of a .def the converter needs: [Info] name / displayname and the [Files] entries (sprite, anim, pal1.. ...), as written.
struct Def {
    std::string name, displayName;
    std::map<std::string, std::string> files;   // lowercase key -> value (quotes and comments removed)
};
Def parseDef(const std::string& text);

}  // namespace mugen
}  // namespace zengine
