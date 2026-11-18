// menu_data.h - the data that drives the game's own menus.
//
// Skullgirls builds every menu page from a small text file (ui/config/MainMenu/*.ini,
// ui/config/Options/*.ini) and looks all visible text up in core/localization/<lang>_loc.json.
// This unit reads both, so the ZDroid menu is the game's menu definition rather than a lookalike:
//
//   TopText: Single Player          <- page title (a localisation key)
//
//   Option:
//   Type: Advance                   <- Advance (run an action) | Setting (cycle choices) |
//   Text: Arcade Mode               <-   Submenu (open another .ini) | Confirm (yes/no)
//   Description: Arcade Mode Desc   <- localisation keys
//   Keyword: ACT_ARCADE             <- action / setting key / sub-menu file
//
// Parsing is tolerant: unknown keys are skipped, never fatal.
#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace zengine {

enum class MenuOptType { Advance, Setting, Submenu, Confirm, Unknown };

struct MenuOption {
    MenuOptType type = MenuOptType::Unknown;
    std::string text;         // localisation key of the label
    std::string description;  // localisation key ("null" / "raw" / empty = no description)
    std::string keyword;      // ACT_* action, OPT_* setting key, or a sub-menu .ini name
    std::string defaultChoice;
    std::vector<std::string> choices;  // file order; DefaultChoice is one of them
    bool p1Lock = false;               // online-only: player 1 must own the controls
    bool restart = false;              // changing it needs a restart in the original
    bool defaultCursor = false;        // the cursor starts on this entry
    std::vector<std::string> allowedPlatforms;  // empty = every platform
    std::vector<std::string> blockedPlatforms;
    std::string modeLimit;  // "RetailOnly" / "BetaOnly"
    // `Move:` blocks (the command lists, movelist_<Name>.ini): `text` is the move's name key, `description` the key of its
    // command string ("{qcf} + {hp}"), `moveKind` Special / Super1..5 / Normal, `indent` the pixels the row is pushed in
    // (follow-ups of the move above).  They are rows to read, not actions.
    bool isMove = false;
    std::string moveKind;
    int indent = 0;

    bool hasDescription() const { return !description.empty() && description != "null" && description != "raw"; }
    // AllowedPlatforms / BlockedPlatforms / ModeLimit applied to `platform` (e.g. "STEAM", "ANDROID").
    bool visibleOn(const std::string& platform) const;
};

struct MenuDef {
    std::string topText;  // localisation key of the page title
    std::vector<MenuOption> options;
};

MenuDef parseMenuIni(std::string_view text);

// The command strings of the move lists carry button tokens: "{qcf} + {hp}", "{f}{plus}{mpmk}", "{dp}", "{lplk}" ...
// Plain-text reading of them ("QCF + HP", "F+MP/MK"): what the menu shows where it has no button art, and what the tests check.
std::string commandToText(const std::string& cmd);
// One token without its braces -> the sequence of icons it stands for: direction arrows ("f", "df" ...), face buttons
// ("lp", "mk" ...) or a word ("360", "+", ">", "P", "K", "PP", "KK").  Empty for an unknown token.
struct CommandGlyph {
    enum class Kind { Arrow, Button, Word } kind = Kind::Word;
    std::string s;   // Arrow: f b u d uf ub df db | Button: lp mp hp lk mk hk | Word: the text
};
std::vector<CommandGlyph> commandTokenGlyphs(const std::string& token);

// Flat {"key": "text"} JSON, as found in core/localization/*_loc.json.  Values that are `null`
// (or not strings) are treated as missing, so lookups fall back to the key itself.
class Localization {
public:
    // Replaces the table.  On malformed input returns false and keeps what was read so far.
    bool parseJson(std::string_view json, std::string* err = nullptr);
    bool has(const std::string& key) const { return map_.find(key) != map_.end(); }
    // The localised text, or `key` itself when there is none.
    std::string tr(const std::string& key) const;
    // The localised text, or `fallback` when there is none.
    std::string trOr(const std::string& key, const std::string& fallback) const;
    size_t size() const { return map_.size(); }
    void clear() { map_.clear(); }

private:
    std::unordered_map<std::string, std::string> map_;
};

}  // namespace zengine
