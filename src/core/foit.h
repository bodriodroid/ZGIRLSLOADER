// foit.h - the character body of a <char>.foit: frame list (pivots, images, hit/hurt/push boxes),
// the state machine (states -> nodes -> frame refs + durations), the per-state transition table and the
// child objects (projectiles / effects).
//
// The grammar below is the one the shipped executable reads, worked out by static analysis of the user's
// own SkullGirls.exe (no code was run).  Reader functions in the exe, for reference while reading:
//   0x493150 character body | 0x44c2f0 frame | 0x463ca0 image | 0x44ce60 state header | 0x44d1b0 node props
//   0x46db40 command header | 0x46c580 command step | 0x48c5a0 script chain | 0x4849b0 hit data | 0x48c3f0 sub list
// The parser is verified on all 20 character files of the PC build (every file is consumed to the last byte), and the
// same grammar is written down as a Kaitai Struct description in docs/foit.ksy (checked by tools/validate_foit_ksy.py).
//
// Everything is little endian.  Frame ids that appear in node `refs` are 1-based (0 = none); the frame list
// itself is 0-based.  State ids (`State::next`, `Transition::target`) are 0-based indices into `Object::states`.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "art.h"

namespace zengine {
namespace foit {

// ---- script chains (the "slot op" virtual machine) ------------------------------------------------------
// One node is `C := op(A, B)` (arithmetic) or a comparison of A and B (boolean, flags bit 0).  An operand is
// either a literal float or the id of a float "slot" of an object; `desc` selects which and on whose object.
//   desc bit0 : raw = slot id (byte)            else raw = float bits
//   desc bit1 : global slot table               desc bit2 : random number (raw = range)
//   desc bit5 : bit of the object's flag words  desc 0x08/0x10/0x40/0x80 : whose object (see docs/RE_NOTES.md)
struct Operand {
    uint8_t desc = 0;
    uint32_t raw = 0;
    bool isSlot() const { return (desc & 1) != 0; }
};
struct ChainNode {
    Operand a, b, c;
    uint8_t flags = 0;  // bit0 = boolean node (condition); bit1 = another node follows (not stored)
    uint8_t op = 0;
    bool boolean() const { return (flags & 1) != 0; }
};
using Chain = std::vector<ChainNode>;

// ---- command ("move input") tables --------------------------------------------------------------------------
// `Step` is one element of an input sequence (direction ids b0..b2, a window w, 15 per-button requirements t)
// with an optional condition chain.  `Command` = the header (a, b) + its steps.
struct Step {
    uint8_t b0 = 9, b1 = 10, b2 = 10;
    uint16_t w = 0;
    uint8_t flags = 0;
    Chain chain;
    uint8_t t[15] = {10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10};
};
struct Command {
    uint16_t a = 0, b = 0;
    uint8_t flags = 0;  // bit1 = a default step precedes the list
    bool hasDefault = false;
    Step def;
    std::vector<Step> steps;
};

// ---- hit data attached to a node --------------------------------------------------------------------------
// Field names are the offsets inside the runtime object (the meaning of most of them is not decoded yet).
struct HitData {
    uint16_t f8 = 0;
    uint32_t f4 = 0;
    uint8_t f13 = 0;
    uint16_t fc = 0, fe = 0;
    uint8_t f18 = 0;
    uint32_t f2c = 0;
    uint16_t f10 = 0;
    uint8_t f12 = 0;
    uint32_t f0 = 0;
    uint8_t f14 = 0, f15 = 0, f16 = 0, f17 = 0;
    uint8_t fa = 0, fb = 0;
    Chain chains[4];
    bool hasChain[4] = {false, false, false, false};
    uint16_t f0a = 0xFFFE;
    // The engine keeps one more byte of the hit data at +0x19 and builds it from the two flag bytes above (fa bits 5..7 -> bits 0..2,
    // fb bits 0..4 -> bits 3..7).  Bits: 0 = the hit cannot kill the point character, 1 = invert the hit condition, 2 = extra
    // re-evaluation, 3 = no counter hit (NOCOUNTERS), 4..7 = further validity rules.
    uint8_t flags19() const { return static_cast<uint8_t>(((fa >> 5) & 7) | ((fb & 0x1F) << 3)); }
};

struct SubItem {
    uint16_t a = 0;
    uint32_t b = 0, c = 0;
    uint8_t flags = 0;
    Chain chain;
};

struct NodeProps {
    uint8_t b1 = 0, b2 = 0, b3 = 0;
    Chain chain;        // b3 & 0x80
    uint16_t dur = 1;   // ticks at 60 Hz (u16 when b1 & 1, otherwise 1)
    uint8_t x1a = 0;    // b3 & 0x40
    uint8_t f2 = 0;     // bit0 hit data | bit1 sub list | bit2 u32 list
    bool hasHit = false;
    HitData hit;
    std::vector<SubItem> sub;
    std::vector<uint32_t> u32s;
    bool hasCmd = false;  // another node props record is linked behind this one (with its own command header)
    Command cmd;
};

// A node: up to three frame layers (refs[1] is the body) + one or more linked property records.
struct Node {
    uint16_t refs[3] = {0, 0, 0};
    std::vector<NodeProps> props;
    // convenience: total ticks this node lasts (duration of the first record; linked ones are extra scripts)
    int ticks() const { return props.empty() ? 1 : props[0].dur; }
    // nodes without a frame are script/command nodes (init, loop labels, resets)
    bool isScript() const { return refs[0] == 0 && refs[1] == 0 && refs[2] == 0; }
};

struct StateHeader {
    std::vector<Node> nodes;
    bool hasCmd = false;
    Command cmd;
    std::vector<NodeProps> cmdNode;
    Chain chain;  // header script (initial slot values - e.g. walk speed)
    bool hasChain = false;
};

struct Transition {
    uint16_t target = 0;  // state id
    Command cmd;          // the input / condition that triggers it
};

struct State {
    uint8_t f = 0, b14 = 0;
    bool hasV18 = false;
    uint32_t v18 = 0;
    uint8_t v1c = 0;
    std::vector<uint32_t> hashes;  // f & 2: name hashes (e.g. the state's own hash + aliases)
    uint16_t next = 0;             // state entered when the node list ends (0-based)
    StateHeader hdr;
    std::vector<Transition> transitions;
    std::string name;

    // total length of one pass through the nodes, counting frame nodes only
    int totalTicks() const {
        int t = 0;
        for (const Node& n : hdr.nodes)
            if (!n.isScript()) t += n.ticks();
        return t;
    }
};

constexpr size_t kSlotCount = 177;  // float slots per object; `consts` holds their default values

struct Object {
    std::string name;
    uint32_t v374 = 0;
    std::vector<std::pair<uint32_t, uint8_t>> list8;
    std::array<uint32_t, kSlotCount> slotDefaults{};  // float bit patterns
    bool slotDefaultsValid = false;
    uint8_t chainFlags = 0;
    Chain chains[3];
    bool hasChain[3] = {false, false, false};
    uint8_t flagX = 0;
    std::vector<State> states;
    uint16_t refBe = 0, refBf = 0, refC0 = 0;
    bool hasRefC0 = false;
    uint8_t flagsDc = 0, x373 = 0;
    int32_t box[4] = {0, 0, 0, 0};  // default push box (top, bottom, left, right)
    uint8_t childFlags = 0;         // the flag byte of this child's 13-byte header (children only)
    std::vector<Object> children;   // projectiles / effects; share the character's frame list

    int findState(const std::string& n) const {
        for (size_t i = 0; i < states.size(); ++i)
            if (states[i].name == n) return static_cast<int>(i);
        return -1;
    }
};

// ---- frames ---------------------------------------------------------------------------------------------
struct Image {
    uint16_t cells = 0, w = 0, h = 0, n = 0;  // w x h cells of 16x16 px; n cells are stored
    uint8_t flags = 0;
    std::vector<uint8_t> mask;  // bit j (LSB first, row-major) = cell j stored
    bool hasData = false;       // flag bit 4: mask + plane references are present
    bool external = false;      // flag bit 5: planes live in the .art/.art-pt (otherwise `inl`)
    uint32_t extra[2] = {0, 0};  // flag bit 2: two more words that the engine reads and does not use; kept so a file writes back unchanged
    uint32_t off = 0;
    uint32_t size[3] = {0, 0, 0};  // compressed sizes of the normal / grad / band planes
    std::vector<uint8_t> inl[3];
    // nothing to draw: no pixel data, the 0xFFFFFFFF placeholder (image slot 6) or an empty 1x1 image (n == 0)
    bool placeholder() const { return !hasData || n == 0 || (external && off == 0xFFFFFFFFu); }
    bool silhouette() const { return (flags & 0x80) != 0; }
};

struct Box {
    int32_t top, bottom, left, right;  // relative to the pivot, y grows downwards
};

constexpr int kSlots = 8;
// image slot -> index of its (x, y) pair inside Frame::xy
constexpr int slotXY(int slot) { return slot < 6 ? (slot >> 1) * 2 : (slot == 6 ? 6 : 8); }

struct Frame {
    int32_t pivotX = 0, pivotY = 0;  // A, B: canvas coordinates of the point the character stands on
    uint8_t src = 0;                 // 5 = .art, 4 = .art-pt, 0 = no image
    std::vector<std::array<uint32_t, 3>> entries;
    uint8_t slotMask = 0;
    Image img[kSlots];
    int32_t xy[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    std::vector<Box> hit, hurt, push;  // box lists 0, 1, 2

    // the image of `slot` is drawn with its top-left corner at (xy - pivot)
    int drawX(int slot) const { return xy[slotXY(slot)] - pivotX; }
    int drawY(int slot) const { return xy[slotXY(slot) + 1] - pivotY; }
};

// ---- the layer object: colour palettes ---------------------------------------------------------------------
// The character's colour variants.  `groups` names the colour groups ("hair", "skin/hands", ...); every palette holds
// `totalColors` ARGB values (group after group) plus two extra colours.  The per-palette scripted rules that follow
// them in the file are validated but not kept.
struct ColorGroup {
    std::string name;
    uint8_t count = 0, flagA = 0, flagB = 0;
};
struct Palette {
    std::vector<uint32_t> colors;
    uint32_t edge[2] = {0, 0};
};
struct LayerObject {
    std::vector<ColorGroup> groups;
    std::vector<std::pair<uint32_t, uint32_t>> ranges;  // (index of the last colour, group) - derived data stored in the file
    std::vector<Palette> palettes;
    uint32_t totalColors = 0;
};

struct Character {
    uint32_t version = 0;
    uint8_t headerFlags = 0;
    Object body;
    std::vector<Frame> frames;
    size_t frameListPos = 0;  // file offset of the u32 frame count
    bool hasLayer = false;    // body header flag: a layer object (palettes) sits between the header and the slot table
    LayerObject layer;
    size_t layerPos = 0;      // file offset where the layer object starts (right after the hasLayer byte)
    size_t tailPos = 0;       // file offset of the slot-default table block (list8); the layer object spans [layerPos, tailPos)
    std::vector<uint8_t> preamble;  // bytes between the 13-byte file header and the body (the animation summary of header flag 8); kept as they are
    std::vector<uint8_t> layerRaw;  // those bytes as they are in the file (the parser only keeps part of the layer object; foit_write.h copies them back)
    size_t fileSize = 0;
    size_t countStates() const;    // including all children
    size_t countChildren() const;  // recursive
};

// Parses a whole .foit. Returns false (and fills *err) when the data does not match the grammar; the parse is
// accepted only when it consumes the file exactly.
bool parse(const uint8_t* data, size_t size, Character& out, std::string* err = nullptr);

// ---- pixels ---------------------------------------------------------------------------------------------
// Decodes the band/grad planes of one image. `src` is the .art (frame.src == 5) or .art-pt (== 4) content.
bool decodeImage(const Image& im, const uint8_t* src, size_t srcLen, ArtPlanes& out, bool withNormal = false);
// Same, but only checks that the three planes are decodable and have the expected size (cheap validation).
bool validateImage(const Image& im, const uint8_t* src, size_t srcLen);

// Position of the u32 that follows `kids` inside the stored name table - helper for tests/tools.
std::string describeChain(const Chain& c);

}  // namespace foit
}  // namespace zengine
