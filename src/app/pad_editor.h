// pad_editor.h - Extras -> Touch controls: move, resize, add and remove the on-screen buttons, move the stick, swap hands.
//
// The pad of a match is the layout in <pref dir>/touch_layout.txt (src/core/touch_pad.*).  This screen edits that layout on a dummy playfield:
// drag a button (or the stick) to move it, tap it to select it and size it, add a button from the presets (the six attacks, the 3P / 3K
// macros, P+K and the two-button chords, Start), delete one, mirror the whole pad for the other hand, and Save.  The overall size and opacity
// are the same two options "Help & Options" has in a match (ZD_TOUCH_SCALE / ZD_TOUCH_OPACITY).  The next match reads the saved file.
#pragma once
#include <SDL.h>

#include <string>

#include "touch_pad.h"
#include "ui.h"

namespace zengine {
namespace padedit {

// <pref dir>/touch_layout.txt; false = there is none (or it is not a layout) and `out` is untouched.
bool loadSaved(touch::Layout& out);
bool save(const touch::Layout& l);
// Back to the built-in layout: deletes the file.
void clearSaved();

struct Out {
    bool done = false;    // leave the screen
    bool saved = false;   // ... after saving
};

class PadEditor {
public:
    void begin(const std::string& lang);
    Out frame(Ui& ui, SDL_Renderer* r, int dtMs);

    // for tests and the smoke run
    const touch::Layout& layout() const { return base_; }
    int selected() const { return sel_; }
    void select(int i) { sel_ = i; }

private:
    bool es() const { return lang_ == "es"; }
    const char* tr(const char* en, const char* es_) const { return es() ? es_ : en; }
    void pick(int mx, int my);
    void dragTo(int mx, int my);
    void drawBackdrop(Ui& ui) const;
    void drawPanel(Ui& ui, Out& out);
    void drawPresets(Ui& ui);
    void say(const std::string& t) { toast_ = t; toastMs_ = 1800; }
    int optScale() const;
    int optOpacity() const;

    std::string lang_ = "en";
    touch::Layout base_;      // what is edited and saved: scale 1, the full opacity of the file
    touch::Layout view_;      // base_ with the user's size / opacity options: what the match will draw
    int sel_ = -1;            // a button index, padview::kStick or -1
    int drag_ = -3;           // what the pointer is moving (same values; -3 = nothing)
    int grabX_ = 0, grabY_ = 0;
    bool adding_ = false;
    int startScale_ = 100, startOpacity_ = 60;   // the options when the screen opened: Cancel puts them back
    std::string startText_;                      // the layout when the screen opened
    std::string toast_;
    int toastMs_ = 0;
};

}  // namespace padedit
}  // namespace zengine
