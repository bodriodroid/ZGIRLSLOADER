// confirm_dialog.h - the game's own "WARNING / Are you sure?" window (InGame.gbs "DialogBox"): the ornate frame pops in over a
// dimmed screen, the winged title, the question and two choices with the glowing selector bar under the one that is picked.
//
// One class serves every place that asks before throwing something away: the main menu's "Exit Game", the pause menu's "Exit Game",
// "Return to Main Menu", "Give Up".  The scene file only holds the frame at its parking place; its animations (OnActivate /
// ConfirmSetup / P1MenuIntro / ConfirmSelect<n>) place everything, exactly as the original does.  Every texture and font is read
// from the user's mounted UI pack - nothing of the game is bundled.
#pragma once
#include <string>

#include "game_ui.h"
#include "gbs_anim.h"
#include "ui.h"

namespace zengine {

class ConfirmDialog {
public:
    enum class Answer { Pending, Yes, No };
    struct Text {
        std::string plate;     // the small plate over the frame ("PLAYER 1")
        std::string title;     // the winged title ("WARNING")
        std::string message;   // the question
        std::string yes, no;   // the two choices
    };

    // Opens the dialog.  `defaultNo` highlights "No" first (the safe answer, as the game does).
    void open(const Text& t, bool defaultNo = true);
    void close();
    bool isOpen() const { return open_; }
    // true when the scene file of this data has the window (otherwise the caller draws its own plain panel).
    static bool available(GameUi& gfx, const std::string& restoreScene);

    // Keyboard (left / right / up / down toggle, enter picks the highlighted one, back = No) and pointer (tap a choice) input,
    // in the 640x360 logical space the screens use.  `armed` = false ignores picks (the frame is still popping in).
    Answer input(const Input& in);

    // Advances the animation one 60 Hz tick and draws the window over whatever is on screen.  The scene that was current
    // (`restoreScene`, e.g. "Main.gbs") is made current again afterwards.  Returns false when the scene has no DialogBox.
    bool draw(GameUi& gfx, const std::string& restoreScene);

    int selection() const { return sel_; }        // 0 = Yes, 1 = No
    int ticks() const { return ticks_; }
    // The two choices on the logical screen (640x360), for hit tests and smoke clicks.
    static Rect yesRect();
    static Rect noRect();

private:
    void select(int s);

    bool open_ = false, bound_ = false;
    int sel_ = 1, ticks_ = 0;
    Text text_;
    GbsAnimPlayer anim_;
};

}  // namespace zengine
