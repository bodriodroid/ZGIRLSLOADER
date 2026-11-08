// zdroid_stages - does every stage of the Character Select reach a real .lvl and a real backdrop picture?
//
//   zdroid_stages <data dir> [<data dir> ...]
//
// Mounts the folders like the app does, then for each stage id the Character Select offers (the same list as charselect.cpp) prints the
// .lvl it resolves to, the 2D backdrop the .lvl names and whether that picture (or one of the other names the Play screen tries) is
// in the mounted data.  Run it on the phone's data folder when "the level does not load / I always get the Test Level": the line
// that says NO LEVEL or NO BACKDROP is the stage that falls back.
#include <cstdio>
#include <string>
#include <vector>

#include "gamedata.h"
#include "level.h"

using namespace zengine;

static const char* const kIds[] = {
    "new_meridian", "rooftops_day", "rooftops_night", "training_stage", "under_the_bridge", "wasteland", "maple_crest",
    "nightmare_crest", "medici_office", "class_notes", "asg_labs", "asg_labs_empty", "innsmouthday", "innsmouth_night", "casino",
    "cathedral", "cathedral_empty", "atrium", "falling_wreckage", "bathhouse", "bathhouse_sekh", "boardwalk", "gehenna",
    "sound_stage", "speakeasy", "hilgardcastle", "hilgardcastle_nightmare", "arena", "arena_empty", "arena_SGCS", "arena_SGCS2024"};

static std::string baseName(const std::string& p) {
    const size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}

static std::string lower(std::string t) {
    for (char& c : t)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return t;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: zdroid_stages <data dir> [<data dir> ...]\n");
        return 2;
    }
    GameData gd;
    for (int i = 1; i < argc; ++i) std::printf("mount %s: %zu file(s)\n", argv[i], static_cast<size_t>(gd.mount(argv[i])));
    const Inventory inv = gd.scan();
    std::printf("%zu .lvl level(s), %zu stages.ini row(s)\n", inv.levels.size(), inv.stages.size());
    if (inv.levels.empty()) std::printf("!! no .lvl at all: every stage falls back to the generated test level (the game's stages/*.lvl are not in the mounted folders)\n");
    for (const LevelInfo& l : inv.levels) std::printf("   level %-28s %s\n", l.name.c_str(), l.path.c_str());

    int noLevel = 0, noBackdrop = 0, ok = 0;
    for (const char* id : kIds) {
        const int li = resolveLevel(inv.levels, inv.stages, id);
        if (li < 0) {
            std::printf("NO LEVEL    %-24s (no .lvl matches this id)\n", id);
            ++noLevel;
            continue;
        }
        const LevelInfo& lv = inv.levels[static_cast<size_t>(li)];
        std::string stem = lv.name;
        if (const size_t p = stem.find("_3d"); p != std::string::npos) stem = stem.substr(0, p);
        std::string found;
        for (const std::string& want : {baseName(lv.background2D), stem + "_bg.dds", "stage_" + stem + ".dds", stem + ".dds"}) {
            if (want.empty()) continue;
            for (const std::string& path : gd.vfs().findByName(want)) {
                const bool portrait = lower(path).find("charselect") != std::string::npos;   // the Character Select thumbnail is not the stage
                if (!portrait && found.empty()) found = path;
            }
        }
        if (found.empty()) {
            std::printf("NO BACKDROP %-24s -> %-26s wants '%s'\n", id, lv.name.c_str(), lv.background2D.c_str());
            ++noBackdrop;
        } else {
            std::printf("ok          %-24s -> %-26s %s\n", id, lv.name.c_str(), found.c_str());
            ++ok;
        }
    }
    std::printf("\n%d ok, %d without a backdrop picture, %d without a level\n", ok, noBackdrop, noLevel);
    return (noLevel || noBackdrop) ? 1 : 0;
}
