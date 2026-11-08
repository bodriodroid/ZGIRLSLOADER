// attract.cpp - see attract.h
#include "attract.h"

#include <algorithm>
#include <cstdio>

namespace zengine {

namespace {

// The labels the code fills in, with the sentence each one shows (the localisation keys are the English sentences).
struct Line {
    const char* node;
    const char* key;
};
constexpr Line kLines[] = {
    {"text1", "Every seven years a powerful artifact capable of granting a young woman's wish arises..."},
    {"text3", "However, if the woman is impure of heart,"},
    {"text4", "her wish will be corrupted and she will be turned into"},
    {"text5", "a twisted supernatural being of destruction..."},
    {"text_skullheart", "THE SKULL HEART"},
    {"text_skullgirl", "THE SKULLGIRL"},
    {"text7", "Who will defeat the Skullgirl and claim the Skull Heart now?"},
};

}  // namespace

bool AttractScreen::load(GameUi& ui) {
    order_.clear();
    names_.clear();
    scene_ = nullptr;
    if (!ui.loadScene(file_)) return false;
    scene_ = ui.scene();
    for (int i = 1; i <= 17; ++i) {
        const std::string n = "shot" + std::to_string(i);
        const GbsSceneView* v = scene_->view(n);
        if (!v || !v->anim(n)) continue;
        order_.push_back(v);
        names_.push_back(n);
    }
    if (order_.empty()) scene_ = nullptr;
    std::printf("[attract] %s: %zu shots\n", file_.c_str(), order_.size());
    return !order_.empty();
}

void AttractScreen::start() {
    finished_ = order_.empty();
    acc_ = 0;
    cur_ = -1;
    next_ = prev_ = -1;
    sounds_.clear();
    if (!finished_) enter(0, 0);
}

void AttractScreen::enter(int i, int startTick) {
    prev_ = cur_;
    cur_ = i;
    player_.bind(order_[static_cast<size_t>(i)]);
    player_.play("OnActivate");   // the resting pose of the shot (everything hidden / in place)
    player_.playFrom(names_[static_cast<size_t>(i)], startTick);
    next_ = i + 1 < static_cast<int>(order_.size()) ? i + 1 : -1;
}

void AttractScreen::jump(int i, int tick) {
    if (order_.empty()) return;
    finished_ = false;
    acc_ = 0;
    cur_ = -1;
    enter(std::clamp(i, 0, static_cast<int>(order_.size()) - 1), tick);
}

void AttractScreen::tick() {
    player_.tick();
    for (const GbsSoundCue& c : player_.takeCues()) {
        const std::string& nm = scene_ ? scene_->soundName(c.soundId) : std::string();
        if (!nm.empty()) sounds_.emplace_back(nm, c.volume);
    }
    if (player_.finished(names_[static_cast<size_t>(cur_)])) {
        if (cur_ + 1 < static_cast<int>(order_.size())) enter(cur_ + 1, 0);
        else finished_ = true;
    }
}

bool AttractScreen::update(int dtMs) {
    if (finished_ || order_.empty()) return false;
    constexpr double kTickMs = 1000.0 / 60.0;
    acc_ += std::clamp(dtMs, 0, 250);
    int n = 0;
    while (acc_ >= kTickMs && n < 8 && !finished_) {
        acc_ -= kTickMs;
        ++n;
        tick();
    }
    if (acc_ >= kTickMs) acc_ = 0;
    return !finished_;
}

std::vector<std::pair<std::string, float>> AttractScreen::takeSounds() {
    std::vector<std::pair<std::string, float>> out;
    out.swap(sounds_);
    return out;
}

void AttractScreen::warm(GameUi& ui) {
    // the shot that is coming is decoded a couple of textures per frame (no stall at the cut); the one that just ended is freed
    if (prev_ >= 0) {
        ui.unload(*order_[static_cast<size_t>(prev_)]);
        prev_ = -1;
    }
    if (next_ >= 0) ui.preload(*order_[static_cast<size_t>(next_)], 2);
}

void AttractScreen::draw(GameUi& ui) {
    ui.fillRect(0, 0, 1280, 720, Col{0, 0, 0, 255});
    if (order_.empty() || cur_ < 0 || !ui.loadScene(file_)) return;
    ui.setAnimPlayer(&player_);
    const GbsSceneView& v = *order_[static_cast<size_t>(cur_)];
    ui.drawView(v, [&](const GbsNode& n, NodeState& st) {
        if (n.type == GbsNode::Type::Label) {
            for (const Line& l : kLines)
                if (n.name == l.node) {
                    st.hasText = true;
                    st.text = text_ ? text_(l.key, l.key) : l.key;
                    return;
                }
            st.hide = true;   // the "TXT_HELPER_..." label of the editor
        }
    });
    ui.setAnimPlayer(nullptr);
    warm(ui);
}

}  // namespace zengine
