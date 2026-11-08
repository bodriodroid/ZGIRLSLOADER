// level.h - Skullgirls stage definitions (*.lvl).
//
// The .lvl files next to stages.ini are plain text and carry everything a 2D stage needs:
//
//   StageSize: 3750 2000       stage bounds in stage units (x: 0..3750, centre 1875)
//   BottomClearance: 64        how far the camera may drop below the floor line
//   Start1: 1675  Start2: 2075 the two fighters' spawn points (that is why they are not symmetric)
//   ShadowDist: 400            how far above the floor a shadow still shows
//   2D stages/textures/2D/stage_asg_labs.dds   the pre-rendered backdrop (low-end / 2D mode)
//   Light: Amb 85 96 122       ambient light, used here to tint the fallback sky
//   CAMERA 66                  field of view, irrelevant for a 2D renderer
//
// Parsing is tolerant: unknown lines are skipped, never fatal.
#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace zengine {

struct LevelLight {
    std::string kind;  // Amb / VitaAmb / Dir / Pt
    int r = 0, g = 0, b = 0;
    double x = 0, y = 0, z = 0, range = 0;
};

struct LevelInfo {
    std::string path;   // real path of the .lvl
    std::string name;   // stem, e.g. "asg_labs_3d"
    int width = 3750, height = 2000;
    int bottomClearance = 64;
    int start1 = 0, start2 = 0;   // spawn points in stage units
    int shadowDist = 400;
    double cameraFov = 66;
    int ambient[3] = {85, 96, 122};
    std::string background2D;     // texture named by the "2D <path>" line (may be empty)
    std::vector<LevelLight> lights;
    std::vector<std::string> warnings;
    std::string error;            // non-empty if the file could not be read at all

    // Spawn points converted to the fight runtime's centred coordinate space
    // (0 = stage centre, +/-1400 = the edges).  Scale is width / 2800.
    double centre() const { return width * 0.5; }
    double toFightX(double stageX) const { return (stageX - centre()) * 2800.0 / (width > 0 ? width : 1); }
    double startFightX(int which) const { return toFightX(which == 0 ? start1 : start2); }
};

LevelInfo parseLevel(std::string_view text, const std::string& name, const std::string& path = {});

struct StageEntry;   // text_formats.h

// Which of `levels` a stage name from the menus means.  The Character Select thumbnails ("rooftops_day", "casino", "training_stage"),
// the localisation ids ("River_King", "Maple_Crest_Mare") and the .lvl files ("rooftops_day_3d", "river_king_casino", "training_3d")
// name the same stage three different ways; stages.ini (`table`) is the game's own list tying them together.  Tried in order:
// the .lvl name itself; a stages.ini row whose id, thumbnail ("stage_<id>") or file matches; the same names without the "_3d" suffix;
// finally a name that is contained in exactly one level.  Case, spaces, '_' and '-' do not matter.  -1 = no such stage.
int resolveLevel(const std::vector<LevelInfo>& levels, const std::vector<StageEntry>& table, std::string_view want);

// Levels a "Random" stage may pick: the ones stages.ini lists without ExcludeFromRandom / BetaOnly (test1, test2 and the like are
// not stages).  Without a table: every level except the ones whose name starts with "test".  Never empty when `levels` is not.
std::vector<int> randomLevels(const std::vector<LevelInfo>& levels, const std::vector<StageEntry>& table);

}  // namespace zengine
