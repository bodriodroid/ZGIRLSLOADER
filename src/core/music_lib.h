// music_lib.h - the game's music catalogue, read from the user's own folders (nothing is bundled).
//
// Sources: core/music.ini ("Track: / Stage: / Files: <intro>, <loop>") and every other long .wav the Vfs holds outside
// the sound-effect / voice folders (the menu and character-select themes, the attract tune, anything extra).  The class
// only decides WHICH theme goes where; decoding and playing is audio.h.
//
//   fight   forFight(level name)  the theme music.ini gives that stage, else a stable pick among the themes on disk
//   story   forCue("24")          MUSIC_24 / MUSIC_NEWS / MUSIC_K ... of the story scripts, matched by file names
//   screens menu() select() credits() ending() gallery()   with fallbacks, so no screen is silent while any theme exists
#pragma once
#include <string>
#include <vector>

#include "vfs.h"

namespace zengine {

struct MusicEntry {
    enum class Kind { Menu, Select, Stage, Cutscene, Other } kind = Kind::Other;
    std::string title;                 // "Moonlit Melee"
    std::vector<std::string> stages;   // normalised world names ("newmeridian")
    std::string introStem, loopStem;   // as written in music.ini
    std::string introFile, loopFile;   // names the Vfs resolves ("skull02_1.wav"); loopFile empty = not in the data
    int rank = 0;                      // who wins when two themes claim a stage: a music.ini of the user's > the game's > the loader's own
    bool present() const { return !loopFile.empty(); }
};

class MusicLibrary {
public:
    // Call after every (re)scan of the data folder.
    void scan(const Vfs& vfs);
    const std::vector<MusicEntry>& entries() const { return entries_; }
    size_t presentCount() const;

    // Index of the theme for a stage (.lvl name such as "new_meridian_3d"); -1 when music.ini has none or its files are missing.
    int forLevel(const std::string& levelName) const;
    // forLevel, else a stable pick among the stage themes that are on disk (the same stage always gets the same one), else the
    // character-select / menu theme; -1 = the data has no music at all.
    int forFight(const std::string& levelName) const;
    // Story scripts' "MUSIC_<code>": "24", "NEWS", "K", "BOSS" ...  -1 = no file for it (the caller keeps the current music).
    int forCue(const std::string& code) const;

    // Screen themes; each falls back to the next best so that a screen is only silent when the data has no music at all.
    int menu() const;
    int select() const;
    int credits() const;
    int ending() const;
    int gallery() const;

    // Text the log / the jukebox prints.
    std::string summary() const;
    // File names (without extension) of the themes that are on disk, for the "no file for this cue" message.
    std::string stems(size_t max = 40) const;
    // Themes whose only files are .ogg / .mp3 (the mixer plays .wav only).
    size_t unsupportedCount() const { return unsupported_; }

    // Exposed for tests.
    static std::string normalise(const std::string& s);   // lower-case letters and digits only
    static std::string levelWorld(const std::string& levelName);   // "river_king_casino" -> "riverking"

private:
    int findByTokens(const std::vector<std::string>& wantAny, bool cutscenePreferred) const;
    std::vector<MusicEntry> entries_;
    size_t unsupported_ = 0;
};

}  // namespace zengine
