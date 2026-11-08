// menu_list_anim.h - decides which of the LAGG animations of the main menu view ("MainMenu_New" in Main.gbs) plays when.
//
// The scene is built so that the code has very little to do: the film strips, the glow, the header art that pops in and the
// labels that fade at the ends of the list are all moved by animations.  What is left for the code is the choice of the
// animation:
//   - the list is a window of eleven labels (MenuItemOffTop, MenuItem0..10); the highlighted entry is not a label but the
//     header art (MenuItemActive), so the labels around it are item(sel-4..sel-1) in MenuItem0..3, item(sel+1..) in MenuItem5..9;
//   - moving the highlight by one entry plays a bundle at once ("MenuFilmUp" or "MenuFilmDown" moves the group and the film by one
//     step, "Menu0Up.." / "MenuDown0.." and friends fade the labels that leave and enter, "MenuCenterSwap" pops the new header
//     art in).  The last letters of a name say whether the label that animation touches is tinted dark (L = locked entry) or
//     not (U): the 1-tick "Menu<N>" / "Menu<N>Locked" poses set that tint for a label at rest;
//   - when the move has finished the labels are rebound one entry over and "MenuReset2" brings the group, the film and the
//     end labels back to the resting pose, which looks identical because the film repeats every step;
//   - "MenuIntro" is the first appearance, "SubMenuIntro" / "SubMenuOutro" slide the band when a page opens over the list.
// This class only holds that state machine; it draws nothing.  Time moves with tick() (60 Hz), like GbsAnimPlayer.
#pragma once
#include <string>
#include <vector>

#include "gbs_anim.h"

namespace zengine {

class MenuListAnim {
public:
    enum class Mode { Off, Intro, List, Sub };

    // Binds the view ("MainMenu_New"; not owned).  Clears everything.
    void bind(const GbsSceneView* view);
    GbsAnimPlayer& player() { return pl_; }
    const GbsAnimPlayer& player() const { return pl_; }

    // The list shows up for the first time (OnActivate + MenuIntro).  `usable[i]` is false for an entry that is drawn dark.
    void activate(int sel, const std::vector<char>& usable);
    // A page opens over the list (`instant`: it was already open, e.g. coming back into the menu: no slide) / the list is back.
    void enterSub(bool instant = false);
    void leaveSub();
    // Every frame: where the highlight should be and which entries are usable (indices outside the list count as usable).
    void setTarget(int sel, const std::vector<char>& usable);
    // One 60 Hz step.
    void tick();

    Mode mode() const { return mode_; }
    bool introRunning() const { return mode_ == Mode::Intro; }
    bool moving() const { return dir_ != 0; }
    int moveDir() const { return dir_; }           // +1: the highlight goes to the next entry (list scrolls up), -1: previous
    int listSel() const { return listSel_; }       // the entry the labels are bound to (the old one while a move plays)
    bool usable(int idx) const { return idx < 0 || idx >= static_cast<int>(usable_.size()) || usable_[static_cast<size_t>(idx)] != 0; }

    // Entry shown by a label of the window when the highlight is on `sel`: slot -1 = MenuItemOffTop, 0..10 = MenuItem<slot>.
    // During a move the slots that are blank at rest (OffTop, 4, 10) carry the entries that slide in.
    static int itemOf(int slot, int sel);
    // The names of the animations a move plays, in start order (exposed for tests).
    std::vector<std::string> moveAnims(int dir, int sel) const;

private:
    void restPose();
    void startMove();
    void finishMove();
    char letter(int idx) const { return usable(idx) ? 'U' : 'L'; }

    GbsAnimPlayer pl_;
    Mode mode_ = Mode::Off;
    int listSel_ = 0, target_ = 0, dir_ = 0;
    bool outro_ = false;
    std::vector<char> usable_;
    std::string film_;   // the film animation of the move in progress (its end ends the move)
};

}  // namespace zengine
