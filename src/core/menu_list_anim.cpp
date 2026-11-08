#include "menu_list_anim.h"

#include <cstdlib>

namespace zengine {

void MenuListAnim::bind(const GbsSceneView* view) {
    pl_.bind(view);
    mode_ = Mode::Off;
    listSel_ = target_ = dir_ = 0;
    outro_ = false;
    usable_.clear();
    film_.clear();
}

int MenuListAnim::itemOf(int slot, int sel) {
    if (slot < 0) return sel - 5;      // MenuItemOffTop: enters from above when the highlight goes up
    if (slot < 4) return sel - 4 + slot;
    if (slot == 4) return sel;         // the centre label: the entry that was highlighted, slides in beside the list
    if (slot <= 9) return sel + 1 + (slot - 5);
    return sel + 6;
}

std::vector<std::string> MenuListAnim::moveAnims(int dir, int sel) const {
    std::vector<std::string> out;
    if (dir > 0) {
        // the highlight goes to sel+1: MenuItem0 leaves at the top, MenuItem4 (entry `sel`) joins above the gap, MenuItem5
        // (entry sel+1) turns into the header art, MenuItem9 joins at the bottom
        out.push_back("MenuFilmUp");
        out.push_back(std::string("Menu0Up") + letter(sel - 4));
        out.push_back(usable(sel + 1) ? "MenuCenterSwap" : "CenterSwapLocked");
        out.push_back(std::string("Menu4Up") + letter(sel + 1) + letter(sel));
        out.push_back(std::string("MenuUp8") + letter(sel + 4) + letter(sel + 5));
    } else {
        // the highlight goes to sel-1: MenuItemOffTop (entry sel-5) joins at the top, MenuItem3 (entry sel-1) turns into the header
        // art, MenuItem4 (entry `sel`) joins below the gap, MenuItem8 leaves at the bottom
        out.push_back("MenuFilmDown");
        out.push_back(std::string("MenuDown0") + letter(sel - 4) + letter(sel - 5));
        out.push_back(usable(sel - 1) ? "MenuCenterSwap" : "CenterSwapLocked");
        out.push_back(std::string("Menu4D") + letter(sel) + letter(sel - 1));
        out.push_back(std::string("Menu8D") + letter(sel + 4));
    }
    return out;
}

void MenuListAnim::restPose() {
    pl_.play("MenuReset2");
    for (int slot = 0; slot <= 9; ++slot) {
        if (slot == 4) continue;
        pl_.play("Menu" + std::to_string(slot) + (usable(itemOf(slot, listSel_)) ? "" : "Locked"));
    }
}

void MenuListAnim::activate(int sel, const std::vector<char>& usable) {
    pl_.clear();
    usable_ = usable;
    listSel_ = target_ = sel;
    dir_ = 0;
    outro_ = false;
    mode_ = Mode::Intro;
    pl_.play("OnActivate");
    pl_.play("MenuIntro");
}

void MenuListAnim::enterSub(bool instant) {
    if (mode_ == Mode::Off || mode_ == Mode::Sub) return;
    mode_ = Mode::Sub;
    dir_ = 0;
    outro_ = false;
    listSel_ = target_;   // whatever was still queued is settled
    if (instant) {
        pl_.clear();
        pl_.play("OnActivate");
        pl_.play("SubMenuReset");
    } else {
        pl_.play("SubMenuIntro");
    }
}

void MenuListAnim::leaveSub() {
    if (mode_ != Mode::Sub) return;
    mode_ = Mode::List;
    outro_ = true;
    pl_.play("SubMenuOutro");
}

void MenuListAnim::setTarget(int sel, const std::vector<char>& usable) {
    usable_ = usable;
    target_ = sel;
}

void MenuListAnim::startMove() {
    dir_ = target_ > listSel_ ? 1 : -1;
    film_ = dir_ > 0 ? "MenuFilmUp" : "MenuFilmDown";
    for (const std::string& n : moveAnims(dir_, listSel_)) pl_.play(n);
}

void MenuListAnim::finishMove() {
    listSel_ += dir_;
    dir_ = 0;
    restPose();
    if (target_ != listSel_) {
        if (std::abs(target_ - listSel_) > 2) {   // a far jump (a tap on a distant entry): no point in scrolling through them all
            listSel_ = target_;
            restPose();
        } else {
            startMove();
        }
    }
}

void MenuListAnim::tick() {
    pl_.tick();
    switch (mode_) {
        case Mode::Off:
        case Mode::Sub:
            break;
        case Mode::Intro:
            if (pl_.finished("MenuIntro")) {
                mode_ = Mode::List;
                restPose();
                pl_.play("News_Frame_Anim_On");
            }
            break;
        case Mode::List:
            if (outro_) {
                if (pl_.finished("SubMenuOutro")) {
                    outro_ = false;
                    restPose();
                    pl_.play("News_Frame_Anim_On");
                }
            } else if (dir_ != 0) {
                if (pl_.finished(film_)) finishMove();
            } else if (target_ != listSel_) {
                if (std::abs(target_ - listSel_) > 2) {
                    listSel_ = target_;
                    restPose();
                } else {
                    startMove();
                }
            }
            break;
    }
}

}  // namespace zengine
