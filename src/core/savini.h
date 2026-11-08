// savini.h - the ".savini" save file of ZGirls Loader (C++17, no platform dependencies).
//
// One small INI-like text file that keeps everything the player owns, split into sections so it is easy to read and to edit:
//
//   [options]       the settings of Help & Options (OPT_STR_SPDIFFICULTY=Normal ...)
//   [progression]   what is open or done, one flag per line, same names and letters as the game's ProgressionInit.txt:
//                   I incomplete, C complete, L locked, U unlocked, N no          StoryFilia=C   StoryDouble=U   Cerebella_Pal7=L
//   [earned]        what was won while playing: counters and digit strings        FiliaWins=3    FiliaPals=0000100000...
//   [story]         where each story is up to                                      pos.filia=7    done.filia=1
//
// Keys are case-insensitive when looked up and keep the case they were first written with.  Lines starting with ';' or '#' are
// comments.  The file is written as a whole (temp file + rename) after every change, so a crash never leaves half a save.
//
// ProgressionInit.txt is the game's own table of defaults; it is NOT bundled.  When the user's data folder has it, applyDefaults()
// reads its text and adds every key the save does not have yet (letters go to [progression], numbers to [earned]).
#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace zengine {

class SavIni {
public:
    struct Entry {
        std::string key;     // as written (display case)
        std::string value;
    };

    // The one save of the running app.  open() is idempotent for the same folder; it loads <dir>/zgirls.savini (or imports the
    // older zgirls_progress.txt / menu_settings.txt files of that folder when the .savini does not exist yet).
    static SavIni& shared();
    void open(const std::string& prefsDir);
    bool isOpen() const { return !path_.empty(); }
    const std::string& path() const { return path_; }

    // File access.  load() of a missing file leaves the save empty and returns false.
    bool load(const std::string& path);
    bool save() const;                       // atomic; false when there is no path or the write failed
    std::string toText() const;
    void parse(std::string_view text);       // replaces the content (used by load and by tests)

    bool has(const std::string& section, const std::string& key) const;
    std::string get(const std::string& section, const std::string& key, const std::string& fallback = {}) const;
    int getInt(const std::string& section, const std::string& key, int fallback = 0) const;
    // Setters save immediately unless autosave is off (tests, batch edits).
    void set(const std::string& section, const std::string& key, const std::string& value);
    void setInt(const std::string& section, const std::string& key, int value) { set(section, key, std::to_string(value)); }
    bool erase(const std::string& section, const std::string& key);
    const std::vector<Entry>& entries(const std::string& section) const;   // in file order (empty vector when the section is unknown)
    void clear();
    void setAutosave(bool on) { autosave_ = on; }

    // ProgressionInit.txt text: adds the missing keys.  Returns how many were added.  Existing values are never touched.
    int applyDefaults(std::string_view progressionInitText);

    // Progression flags (letters I C L U N).  isDone(): C or U.  unlock(): L -> U, I -> C, absent -> C.
    char flagLetter(const std::string& key) const;     // 0 when the key is not in [progression]
    bool isDone(const std::string& key) const;
    void unlock(const std::string& key);

    // Counters of [earned].
    int counter(const std::string& key) const { return getInt("earned", key, 0); }
    int addCounter(const std::string& key, int by = 1);

private:
    struct Section {
        std::string name;                                   // display case
        std::vector<Entry> items;
        std::unordered_map<std::string, size_t> index;      // lower-case key -> position in items
    };
    Section* find(const std::string& section);
    const Section* find(const std::string& section) const;
    Section& make(const std::string& section);
    void importLegacy(const std::string& dir);

    std::string path_, dir_;
    std::vector<Section> sections_;
    bool autosave_ = true;
};

}  // namespace zengine
