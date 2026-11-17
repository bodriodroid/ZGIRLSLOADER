// colour_editor.h - the player's own colour for a fighter: opens from the Character Select (hold the standing character), edits the colours
// of the body region by region on a live preview, and saves them as a favourite (core/fav_colours.h) that then shows in the colour list.
//
// It is not the mod layer: nothing here touches mods/, the mod loader or the game's files.  Tap a part of the character (or step through the
// parts) to pick it, then move hue / saturation / brightness - the part's shading is kept - or pick one single shade of its ramp.  Whole-character
// moves (a global hue shift, pastel / vivid / dark / light, a random recolour) and "back to the original" are one tap.
// Drawn with the app's plain panel UI over the dimmed selector, in the 640x360 logical space the screens use.
#pragma once
#include <SDL.h>

#include <string>

#include "char_sprite.h"
#include "colour_edit.h"
#include "fav_colours.h"
#include "game_ui.h"
#include "ui.h"

namespace zengine {
namespace menu {

class ColourEditor {
public:
    struct Setup {
        std::string character;                   // lower-case id ("filia")
        std::string title;                       // shown in the title bar ("FILIA")
        coledit::Scheme scheme;                  // the colours to start from (what the selector shows now)
        std::string name;                        // suggested name of the favourite
        coledit::Fav editing;                    // an existing favourite (file set): Save overwrites it and Delete is offered
        std::string lang;                        // "es" or anything else (English)
    };
    struct Result {
        enum class Kind { None, Saved, Cancelled, Deleted } kind = Kind::None;
        coledit::Fav fav;                        // Saved: the files written; Deleted: the files removed
    };

    // false when the data has no standing sprite for the character (the editor then does not open).
    bool open(GameUi& gfx, const coledit::Favourites& favs, Setup s);
    bool isOpen() const { return open_; }
    // Android back / Escape: closes without saving (the next frame() reports Cancelled).
    void cancel() { cancelled_ = true; }
    // One frame: input and drawing.  Needs the renderer alive; frees the sprite when it closes.
    Result frame(Ui& ui, int nowMs);

    // ---- introspection / tests
    const coledit::Scheme& scheme() const { return cur_; }
    int region() const { return region_; }   // 0-based
    int keyIndex() const { return keyIdx_; } // -1 = the whole part

private:
    bool open_ = false, cancelled_ = false;
    bool armed_ = false;           // the pointer that opened the editor (the hold) has been released: only then does it count
    const coledit::Favourites* favs_ = nullptr;
    Setup setup_;
    coledit::Scheme cur_, orig_, snap_;
    CharSpriteEdit sprite_;
    Palette pal_;
    bool dirty_ = true;
    int region_ = 0, keyIdx_ = -1;
    int drag_ = 0;                 // 0 none, 1 hue, 2 saturation, 3 brightness, 4 global hue
    coledit::Region dragRegion_;   // the part as it was when the drag started
    coledit::Hsv hsv_;             // the slider values (kept while dragging so rounding never drifts the hue)
    float globalDeg_ = 0;
    int pulseT0_ = 0, nowMs_ = 0, seed_ = 1;
    std::string name_, msg_;
    int msgMs_ = 0;
    int armDeleteMs_ = -10000;

    bool es() const { return setup_.lang == "es"; }
    const char* tr(const char* en, const char* esp) const { return es() ? esp : en; }
    coledit::Rgb target() const;
    void select(int region, int key);
    void loadHsv();
    void applyHsv();
    void say(const std::string& m);
    void closeNow();
};

}  // namespace menu
}  // namespace zengine
