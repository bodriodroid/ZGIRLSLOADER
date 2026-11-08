// gbs_scene.h - the scene graph inside the game's compiled UI files (*.gbs).
//
// gbs.h reads the file header and the list of views.  This unit reads what is inside them, as
// worked out from the user's own Win/*.gbs (all 23 files: every view's node list and every group's
// children tile their parent exactly, so the layout below is checked, not guessed):
//
//   payload   [0x38, 0x38+off0)          count0 font chunks            "TNFG"
//             [0x38+off0, 0x38+off1)     textureCount x 284-byte rows  [id u32][name char[260]][atlas id][u0][v0][u1][v1]
//                                        (plain rows: [0][0][0][1.0f][1.0f]; atlas members: the atlas' id and their rectangle, see GbsTexture)
//             [0x38+off1, 0x38+off2)     count2 opaque rows
//             [0x38+off2, ...)           views                          "WVGG", back to back
//
//   font      [TNFG][size][id][name char[64]] then 6 u32 of header and `glyph count` rows of 10 u32
//             (size - 76 == 4 * (6 + 10 * glyphs) on every font of the 23 files):
//               header  line height, atlas width, atlas height (the space the metrics are written in, 512),
//                       baseline, page count, glyph count
//               row     {codepoint, 0, x, y, w, h, bearingY (baseline -> top of the glyph), advance,
//                        bearingX (signed), page}   (the page is the LAST word: AttractText has 5 pages, word 1 is 0 on every font)
//             The shipped atlas pages (Fonts/<name>_<page>.dds) are larger than the metrics' space
//             (768 vs 512 here), so the source rectangles must be scaled by pageWidth / atlasW.
//
//   view      [WVGG][size][-1][name char[64]] [x y px py w h rot? ...] then at +112 {textures, anims, nodes}
//             counts, +140 the texture ids and animation ids, then `nodes` node chunks, then one "LAGG"
//             chunk that holds the animations (decoded below).  A view is a 1280x720 canvas.
//
//   node      [tag][size][id][name char[64]] then ten u32 at +76:
//               x y (relative to the parent's top-left)  pivot x/y  w h  rotation(deg, clockwise)  0  -1
//             "RPSG" sprite  +112 texture id (0xE801FFFF = none), +124 mirror flags (bit0 flip X, bit1 flip Y)
//             "LBLG" label   +112 font id, +120 +124 scale x/y, +136 horizontal alignment (0 left 1 centre 2 right),
//                            +140 vertical-centre flag.  The text itself is not stored: the game sets it.
//             "PRGG" group   +112 child count, then that many child nodes inline.
//             "CCRA" arc     +112 segment count, +116 centre-line radius, +120 thickness, +124 colour 0xAARRGGBB, +128 sweep (degrees),
//                            +132 start angle (degrees), +136 direction flag (1 = clockwise, otherwise the other way).  A ring of the
//                            HUD's super meter is one of these; see gbs_arc.h for the geometry.
//             other tags ("KSMG") keep the generic header and are skipped as opaque nodes.
//
//   section 2 (count2 rows of 264 bytes: [id u32][name char[260]]) is the scene's SOUND table: "UISound/menu.select.wav".
//
//   LAGG      [LAGG][size][count] then `count` "NAGG" animations:
//     NAGG    [NAGG][size][name char[64]] then six u32: length (ticks), loop start, loop end (0xffffffff = no loop),
//             channel count, sound-cue count, bytes of channels; then the channels, then the sound cues.
//     CAGG    [CAGG][size][node id][key count] then `key count` "MRFK" keys (the node is looked up by id in the view).
//     MRFK    [MRFK][size] [tick][ease type][ease amount] + 8 floats of layout anchors (not needed to play) + a block
//               [kind tag][block size][payload]:   payload words (pivotX pivotY x y w h rotation ...)
//               TLPG group   7 words    TLPS sprite  9 words (+ colour 0xAARRGGBB, skew)
//               TLBL label  12 words    (+ colour, scale x, scale y, skew, drop shadow)
//               TLRA arc    12 words    (then radius, thickness, colour, sweep, start angle: the arc's animated values)
//             Values are the node's complete pose at that tick; ticks run at 60 per second.  x/y are relative to the
//             parent exactly like the node's resting x/y.
//     RFCM    [RFCM][size][name char[64]][tick][count] then `count` sub-blocks: the file spells them "ADNS"
//             [16][sound id][volume 0..100] (play a sound of the scene's table) and "AGSM" [12][message id] (a
//             message for the game code).
//
// Not decoded: the per-key layout anchors.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zengine {

struct GbsGlyph {
    uint32_t cp = 0;
    int page = 0, x = 0, y = 0, w = 0, h = 0;
    int bearingY = 0;  // baseline -> top edge of the glyph
    int advance = 0;
    int bearingX = 0;  // signed
};

struct GbsFont {
    uint32_t id = 0;
    std::string name;  // also the stem of the atlas files: Fonts/<name>_<page>.dds
    int lineHeight = 0;
    int atlasW = 512, atlasH = 512;
    int baseline = 0;
    int pages = 1;
    std::vector<GbsGlyph> glyphs;  // sorted by cp
    const GbsGlyph* find(uint32_t cp) const;
};

struct GbsTexture {
    uint32_t id = 0;
    std::string name;  // "Textures/MainMenu/icon01.png" (the files on disk are .dds)
    // The table row ends with [atlas id][u0][v0][u1][v1]: a texture that lives inside a texture atlas ("Textures/TA_1001_1.png")
    // names that atlas' row and the rectangle (0..1 of the atlas) it occupies.  Plain rows carry [0][0][0][1.0][1.0] or their own id
    // with the whole image; for those `atlasId` stays 0.  (InGame.gbs: 244 of its 246 rows, i.e. the whole HUD, are atlas regions.)
    uint32_t atlasId = 0;
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    bool inAtlas() const { return atlasId != 0 && atlasId != id && u1 > u0 && v1 > v0; }
};

constexpr uint32_t kGbsNoTexture = 0xE801FFFFu;

struct GbsNode {
    enum class Type { Sprite, Label, Group, Arc, Other };
    Type type = Type::Other;
    std::string tag;  // "RPSG" ...
    uint32_t id = 0;
    std::string name;
    float x = 0, y = 0;          // relative to the parent's top-left
    float pivotX = 0, pivotY = 0;
    float w = 0, h = 0;
    float rotation = 0;          // degrees, clockwise, about (x + pivotX, y + pivotY)
    uint32_t anchorFlags = 0;    // how the node follows its parent's size (see the scene tool's anchors); 0 = fixed
    int32_t maskId = -1;         // id of the mask node that clips this node, -1 = none
    uint32_t colour = 0xFFFFFFFFu;  // resting tint 0xAARRGGBB (sprites, labels)
    float skew = 0;              // degrees of horizontal shear (sprites, labels)
    // sprite
    uint32_t texId = kGbsNoTexture;
    bool flipX = false, flipY = false;
    // label
    uint32_t fontId = 0;
    float scaleX = 1, scaleY = 1;
    int hAlign = 0;       // 0 left, 1 centre, 2 right
    bool vCentre = false;
    int vAlign = 0;       // 0 top, 1 centre, 2 bottom
    int32_t dropShadow = 0;  // px offset of the black copy drawn behind the text, 0 = none
    float leading = 0;    // extra line spacing
    int32_t tracking = 0; // extra advance per glyph
    uint32_t textFit = 0; // 0 none, otherwise how the text is squeezed into the box
    // arc ("CCRA"): a band drawn around the middle of the node's box; `colour` above is its colour
    int arcSegments = 0;         // flat pieces the sweep is cut into
    float arcRadius = 0;         // of the centre line; the band reaches radius +- thickness / 2
    float arcThickness = 0;
    float arcSweep = 0;          // degrees
    float arcStart = 0;          // degrees
    bool arcClockwise = false;   // the node's direction flag (see gbs_arc.h)
    std::vector<GbsNode> kids;
};

// ---- animations (LAGG)
struct GbsKey {
    int t = 0;                  // tick, 60 per second
    int modeA = 0, modeB = 0;   // easing of the segment that starts on this key: type 0 none / 1 in / 2 out / 3 in-out, amount 0..100 (see gbs_anim.h)
    char kind = 'G';            // 'G' group, 'S' sprite, 'L' label, 'R' ring (arc node)
    float pivotX = 0, pivotY = 0, x = 0, y = 0, w = 0, h = 0, rot = 0;
    uint32_t argb = 0xFFFFFFFFu;  // sprite / label / ring colour, 0xAARRGGBB
    float scaleX = 1, scaleY = 1; // label
    float skew = 0;             // label / sprite: degrees of horizontal shear
    int dropShadow = 0;         // label: px offset of the black copy behind the text
    float arcRadius = 0, arcThickness = 0, arcSweep = 0, arcStart = 0;  // 'R' keys: the arc's animated values (angles in degrees)
    uint32_t raw[12] = {};      // the payload words as stored
    int rawCount = 0;
    bool hasColour() const { return kind != 'G'; }
};

struct GbsChannel {
    uint32_t nodeId = 0;
    std::vector<GbsKey> keys;   // ascending ticks
};

struct GbsSoundCue {
    int t = 0;
    uint32_t soundId = 0;       // row of GbsScene::sounds
    float volume = 1;           // 0..1
};

struct GbsAnim {
    std::string name;
    int length = 1;             // ticks
    int loopStart = -1, loopEnd = -1;  // inclusive; -1 = plays once
    std::vector<GbsChannel> channels;
    std::vector<GbsSoundCue> sounds;
    std::vector<GbsSoundCue> messages;  // "MSGA" cues: soundId is a message id for the game code (volume = -1)
    bool loops() const { return loopStart >= 0 && loopEnd >= loopStart; }
};

struct GbsSound {
    uint32_t id = 0;
    std::string name;           // "UISound/menu.select.wav"
};

struct GbsSceneView {
    std::string name;
    float width = 1280, height = 720;
    std::vector<uint32_t> textureIds, animationIds;
    std::vector<GbsNode> nodes;
    uint32_t animOffset = 0, animSize = 0;  // the raw LAGG chunk
    std::vector<GbsAnim> anims;             // decoded LAGG, in file order

    const GbsNode* find(const std::string& nodeName) const;  // depth-first, first match
    const GbsAnim* anim(const std::string& animName) const;  // nullptr when the view has no such animation
};

struct GbsScene {
    uint32_t sceneId = 0;
    std::vector<GbsFont> fonts;
    std::vector<GbsTexture> textures;
    std::vector<GbsSound> sounds;
    std::vector<GbsSceneView> views;

    const GbsFont* fontById(uint32_t id) const;
    const GbsFont* fontByName(const std::string& name) const;
    const GbsSceneView* view(const std::string& name) const;
    // "" when the id is unknown or kGbsNoTexture.
    const std::string& textureName(uint32_t id) const;
    // The table row of a texture by its scene-style name (any case, ".png" / ".dds" / no extension); nullptr when the scene lacks it.
    const GbsTexture* textureByName(const std::string& name) const;
    const GbsTexture* textureById(uint32_t id) const;
    // "" when the id is unknown.
    const std::string& soundName(uint32_t id) const;
};

// Parses fonts, texture table and the node trees.  Returns false (with *err) when the file is not a scene
// or a size field does not add up; everything read before the problem stays in `out`.
bool parseGbsScene(const uint8_t* data, size_t size, GbsScene& out, std::string* err = nullptr);

// Recursive count (tests / diagnostics).
size_t countNodes(const GbsNode& n);

}  // namespace zengine
