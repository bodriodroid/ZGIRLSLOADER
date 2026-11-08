// zg_progress.h - ZGirls Loader: what the player has done in Story Mode, and what that unlocks (Double / Valentine stories,
// the art gallery).  It is a thin view over the .savini save (core/savini.h):
//
//   [story]        pos.filia=7      highest TimelinePos of Filia's story that was completed
//                  done.filia=1     Filia's story was played to the end
//   [progression]  StoryFilia=C     same flag and same letters as the game's ProgressionInit.txt (I C L U N)
//   [earned]       FiliaWins=3      counters won in matches
//
// Keys given to the methods are case-insensitive ("Ms. Fortune" / "MsFortune" / "msfortune").  Nothing of the original
// (encrypted) save format is read or written.
#pragma once
#include <string>

#include "savini.h"

namespace zengine {

// "Ms. Fortune" / "MsFortune" / "msfortune" -> "msfortune": lower case letters and digits only.
std::string zgKey(const std::string& s);

// True when `word` is written on a line of zgirls_flags.txt (external storage on Android, then the pref dir).
// Words used by Story / Gallery: "story_all" (every node of every story playable), "gallery_all" (every picture unlocked).
bool zgOption(const char* word);

class ZgProgress {
public:
    // Opens <dir>/zgirls.savini (missing file = nothing done yet; the older zgirls_progress.txt of that folder is imported once).
    void load(const std::string& prefsDir);
    // The .savini is written by every setter; this only forces it.
    void save() const;
    // The text of the game's ProgressionInit.txt (read from the user's data): adds the keys the save does not have yet.
    void setInitText(const std::string& text);

    int storyPos(const std::string& character) const;             // 0 = nothing completed
    void setStoryPos(const std::string& character, int pos);      // keeps the highest value
    bool storyDone(const std::string& character) const;
    void setStoryDone(const std::string& character);
    bool anyStoryDone() const { return storiesDone() > 0; }
    int storiesDone() const;
    // Arcade ladders finished: the game's own "Arcade<Char>" flag (what the gallery's OneArcade / AllArcade count), plus the older
    // "Gallery<Char>_Arcade" this loader used to set.
    bool arcadeDone(const std::string& character) const;
    void setArcadeDone(const std::string& character);
    int arcadesDone() const;
    int addWin(const std::string& character);                     // [earned] <Char>Wins += 1, returns the new total

    bool flag(const std::string& key) const;                      // case-insensitive ("StoryDouble" == "storydouble")
    void setFlag(const std::string& key);
    // "double" -> flag StoryDouble set (used to lift the ProgressionInit.txt "L" locks).
    bool storyUnlocked(const std::string& character) const { return flag("story" + character); }

private:
    SavIni& ini() const { return SavIni::shared(); }
};

}  // namespace zengine
