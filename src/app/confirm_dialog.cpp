// confirm_dialog.cpp - see confirm_dialog.h.
#include "confirm_dialog.h"

#include <algorithm>

namespace zengine {

namespace {
constexpr float kCanvas = 0.5f;   // 1280x720 canvas -> 640x360 logical

// The scene parks "ConfirmGRP" (three 81-px choice slots) off the left edge; the original's code moves it into the frame's slot
// ("P1Slot1", under the question).  With two choices the pair sits side by side, centred on the frame.  Canvas pixels.
constexpr float kConfirmDx = 1034.0f, kConfirmDy = 149.0f;
// Touch-sized hit areas of the two choices (canvas pixels): the left and right half of the row under the question.
constexpr float kRowX = 520.0f, kRowY = 382.0f, kHalfW = 120.0f, kRowH = 54.0f;

std::string upperText(std::string s) {
    for (char& c : s)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
    return s;
}

// A longer translation keeps the glyph height of the English text and is squeezed sideways into the label's (animated) box; only
// what still does not fit shrinks.
void fitLabel(GameUi& gfx, const GbsNode& n, NodeState& st) {
    if (n.textFit == kFitScaleDown || n.textFit == kFitScaleToFit) return;   // the label's own fit mode squeezes it (gbs_text.h)
    const GbsFont* f = gfx.font(n.fontId);
    float bw = n.w, sc = n.scaleX;
    if (const GbsAnimPlayer* ap = gfx.animPlayer())
        if (const GbsPose* ps = ap->pose(n.id))
            if (ps->kind == 'L') {
                bw = ps->w;
                sc = ps->scaleX;
            }
    if (!f || bw <= 0 || st.text.empty()) return;
    const float tw = gfx.textWidth(*f, st.text, sc);
    if (tw <= bw) return;
    const float ratio = bw / tw;
    constexpr float kMaxSqueeze = 0.8f;
    st.textSqueezeX = std::max(ratio, kMaxSqueeze);
    if (ratio < kMaxSqueeze) st.textScale = ratio / kMaxSqueeze;
}
}  // namespace

Rect ConfirmDialog::yesRect() {
    return Rect{static_cast<int>(kRowX * kCanvas), static_cast<int>(kRowY * kCanvas), static_cast<int>(kHalfW * kCanvas),
                static_cast<int>(kRowH * kCanvas)};
}
Rect ConfirmDialog::noRect() {
    return Rect{static_cast<int>((kRowX + kHalfW) * kCanvas), static_cast<int>(kRowY * kCanvas), static_cast<int>(kHalfW * kCanvas),
                static_cast<int>(kRowH * kCanvas)};
}

bool ConfirmDialog::available(GameUi& gfx, const std::string& restoreScene) {
    const bool ok = gfx.loadScene("InGame.gbs") && gfx.scene() && gfx.scene()->view("DialogBox");
    if (!restoreScene.empty()) gfx.loadScene(restoreScene);
    return ok;
}

void ConfirmDialog::open(const Text& t, bool defaultNo) {
    text_ = t;
    open_ = true;
    bound_ = false;
    ticks_ = 0;
    sel_ = defaultNo ? 1 : 0;
}

void ConfirmDialog::close() {
    open_ = false;
    bound_ = false;
}

void ConfirmDialog::select(int s) {
    if (s == sel_) return;
    if (bound_) {
        anim_.play(sel_ == 0 ? "ConfirmDeselect1" : "ConfirmDeselect2");
        anim_.play(s == 0 ? "ConfirmSelect1" : "ConfirmSelect2");
    }
    sel_ = s;
}

ConfirmDialog::Answer ConfirmDialog::input(const Input& in) {
    if (!open_) return Answer::Pending;
    const bool armed = ticks_ > 7;   // the frame is still popping in
    if (in.left || in.right || in.keyUp || in.keyDown) select(1 - sel_);
    if (in.pressed) {
        if (yesRect().contains(in.mx, in.my)) select(0);
        else if (noRect().contains(in.mx, in.my)) select(1);
    }
    if (!armed) return Answer::Pending;
    if (in.back) return Answer::No;
    if (in.enter) return sel_ == 0 ? Answer::Yes : Answer::No;
    if (in.released) {
        if (yesRect().contains(in.pmx, in.pmy) && yesRect().contains(in.mx, in.my)) return Answer::Yes;
        if (noRect().contains(in.pmx, in.pmy) && noRect().contains(in.mx, in.my)) return Answer::No;
    }
    return Answer::Pending;
}

bool ConfirmDialog::draw(GameUi& gfx, const std::string& restoreScene) {
    if (!open_) return false;
    if (!gfx.loadScene("InGame.gbs") || !gfx.scene()) {
        if (!restoreScene.empty()) gfx.loadScene(restoreScene);
        return false;
    }
    const GbsSceneView* v = gfx.scene()->view("DialogBox");
    if (!v) {
        if (!restoreScene.empty()) gfx.loadScene(restoreScene);
        return false;
    }
    if (!bound_ || anim_.view() != v) {
        anim_.bind(v);
        anim_.play("OnActivate");
        anim_.play("ConfirmSetup");
        anim_.play("P1MenuIntro");
        anim_.play(sel_ == 0 ? "ConfirmSelect1" : "ConfirmSelect2");
        bound_ = true;
        ticks_ = 0;
    }
    anim_.tick();
    ++ticks_;
    gfx.setViewport(0, 0, kCanvas);
    gfx.setAnimPlayer(&anim_);
    gfx.drawView(*v, [&](const GbsNode& n, NodeState& st) {
        const std::string& nm = n.name;
        if (nm == "ChoiceGRP" || nm == "AdvanceGRP" || nm == "ArrowGroup" || nm == "P1Slot1" || nm == "Selector_Grp_3" ||
            nm == "ChoiceTxt3") {
            st.hide = true;
            return;
        }
        if (n.type == GbsNode::Type::Label) st.pivotAnchored = true;
        if (nm == "ConfirmGRP") {
            st.dx = kConfirmDx;
            st.dy = kConfirmDy;
        } else if (nm == "Header_Text") {
            st.hasText = true;
            st.text = text_.plate;
            fitLabel(gfx, n, st);
        } else if (nm == "P1TitleTxt") {
            st.hasText = true;
            st.text = upperText(text_.title);
            fitLabel(gfx, n, st);
        } else if (nm == "ConfirmTxt") {
            st.hasText = true;
            st.text = text_.message;
        } else if (nm == "ChoiceTxt1") {
            st.hasText = true;
            st.text = text_.yes;
            fitLabel(gfx, n, st);
        } else if (nm == "ChoiceTxt2") {
            st.hasText = true;
            st.text = text_.no;
            fitLabel(gfx, n, st);
        }
    });
    gfx.setAnimPlayer(nullptr);
    if (!restoreScene.empty()) gfx.loadScene(restoreScene);
    return true;
}

}  // namespace zengine
