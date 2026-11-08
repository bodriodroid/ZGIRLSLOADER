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

    bool hasDescription() const { return !description.empty() && description != "null" && description != "raw"; }
    // AllowedPlatforms / BlockedPlatforms / ModeLimit applied to `platform` (e.g. "STEAM", "ANDROID").
    bool visibleOn(const std::string& platform) const;
};

struct MenuDef {
    std::string topText;  // localisation key of the page title
    std::vector<MenuOption> options;
};

MenuDef parseMenuIni(std::string_view text);

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
