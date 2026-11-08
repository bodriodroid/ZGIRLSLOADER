// charselect.cpp - see charselect.h
#include "charselect.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace zengine {
namespace menu {

namespace {

constexpr float kCanvasW = 1280.0f, kCanvasH = 720.0f;
constexpr float kScale = 0.5f;  // canvas -> logical

// roster index -> character id.  Order recovered from the icon grid of a real screenshot and the cell table of the exe.
const char* const kNames[CharSelect::kRoster] = {"filia",   "cerebella", "peacock", "parasoul",    "msfortune", "painwheel",
                                                 "valentine", "double",   "squigly", "bigband",     "fukua",     "eliza",
                                                 "beowulf", "robofortune", "annie",  "umbrella", "blackdahlia", "marie"};

// cell (row * 3 + column) -> roster index, -1 = "Random!", >= kRoster = slot the game reserves but has no character for.
// Copied from the select screen's constructor (SkullGirls.exe 0x5e6dce..0x5e6eae).
const int kCellTable[CharSelect::kCells] = {18, 17, 19,   //
                                            8,  9,  11,   //
                                            4,  2,  5,    //
                                            0,  -1, 1,    //
                                            6,  3,  7,    //
                                            10, 12, 13,   //
                                            15, 14, 16,   //
                                            22, 20, 21};

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
float easeOutCubic(float t) {
    t = clamp01(t);
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}

bool findAbs(const std::vector<GbsNode>& nodes, const std::string& name, float ox, float oy, float& x, float& y, float& w, float& h) {
    for (const GbsNode& n : nodes) {
        const float ax = ox + n.x, ay = oy + n.y;
        if (n.name == name) {
            x = ax;
            y = ay;
            w = n.w;
            h = n.h;
            return true;
        }
        if (findAbs(n.kids, name, ax, ay, x, y, w, h)) return true;
    }
    return false;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string upper(std::string s) {
    for (char& c : s)
        if (static_cast<unsigned char>(c) < 128) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// "Press {selg} to cancel" -> "Press B to cancel": the {tokens} are button glyphs the original draws from its icon font.
std::string plainTokens(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '{') {
            const size_t e = s.find('}', i);
            if (e == std::string::npos) break;
            const std::string tok = s.substr(i + 1, e - i - 1);
            if (tok == "selg" || tok == "bkXB") out += "B";
            else if (tok == "bkPS") out += "O";
            else if (tok == "bkSW") out += "B";
            i = e;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string charTex(const std::string& what, const std::string& name, const char* tail = "01") {
    return "Textures/charselect/" + what + name + tail + ".png";
}

// stage thumbnail (Textures/charselect/stages/stage_<id>.dds) -> loc key of its name.  The order is the order of the game's own
// stage names; thumbnails that are not in the user's data are left out.
struct StageRow {
    const char* id;
    const char* key;
};
constexpr StageRow kStageRows[] = {
    {"new_meridian", "New_Meridian"},
    {"rooftops_day", "Meridian_Rooftops"},
    {"rooftops_night", "Rooftops_Night"},
    {"training_stage", "Training_Stage"},
    {"under_the_bridge", "Under_the_Bridge"},
    {"wasteland", "Wasteland"},
    {"maple_crest", "Maple_Crest"},
    {"nightmare_crest", "Maple_Crest_Mare"},
    {"medici_office", "Medici_Tower"},
    {"class_notes", "Class_Notes"},
    {"asg_labs", "ASG_Lab"},
    {"asg_labs_empty", "_Empty_Lab8"},
    {"innsmouthday", "Innsmouth_Day"},
    {"innsmouth_night", "Innsmouth_Night"},
    {"casino", "River_King"},
    {"cathedral", "Cathedral"},
    {"cathedral_empty", "_Empty_Cathedral"},
    {"atrium", "Glass_Canopy"},
    {"falling_wreckage", "Falling_Wreckage"},
    {"bathhouse", "Bathhouse"},
    {"bathhouse_sekh", "Bathhouse_Sekh"},
    {"boardwalk", "Boardwalk"},
    {"gehenna", "Gehenna"},
    {"sound_stage", "Sound_Stage"},
    {"speakeasy", "Speakeasy"},
    {"hilgardcastle", "HilgardCastle"},
    {"hilgardcastle_nightmare", "HilgardCastle_Nightmare"},
    {"arena", "Arena"},
    {"arena_empty", "_Empty_Arena"},
    {"arena_SGCS", "Arena_SGCS"},
    {"arena_SGCS2024", "Arena_SGCS2024"},
};

// "Track: Moonlit Melee / Stage: New_Meridian, ..." of core/music.ini -> lower-case stage key -> track name
std::map<std::string, std::string> parseStageSongs(const std::string& text) {
    std::map<std::string, std::string> out;
    std::string track;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(pos, e - pos);
        pos = e + 1;
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
        if (line.rfind("Track:", 0) == 0) {
            track = line.substr(6);
            while (!track.empty() && track.front() == ' ') track.erase(track.begin());
        } else if (line.rfind("Stage:", 0) == 0 && !track.empty()) {
            std::string rest = line.substr(6);
            size_t a = 0;
            while (a <= rest.size()) {
                size_t b = rest.find(',', a);
                if (b == std::string::npos) b = rest.size();
                std::string k = rest.substr(a, b - a);
                while (!k.empty() && std::isspace(static_cast<unsigned char>(k.front()))) k.erase(k.begin());
                while (!k.empty() && std::isspace(static_cast<unsigned char>(k.back()))) k.pop_back();
                if (!k.empty()) {
                    for (char& c : k) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    out.emplace(k, track);
                }
                a = b + 1;
            }
        }
    }
    return out;
}

}  // namespace

const char* CharSelect::rosterName(int idx) { return (idx >= 0 && idx < kRoster) ? kNames[idx] : ""; }
int CharSelect::cellRoster(int cell) { return (cell >= 0 && cell < kCells) ? kCellTable[cell] : -2; }

bool CharSelect::cellOk(int cell) const {
    const int idx = cellRoster(cell);
    if (idx == -1) return true;  // Random!
    return idx >= 0 && idx < kRoster && art_[idx];
}

std::string CharSelect::nameAt(int cell) const {
    const int idx = cellRoster(cell);
    return idx == -1 ? "random" : rosterName(idx);
}

bool CharSelect::lockedAt(int cell) const {
    const int idx = cellRoster(cell);
    return idx >= 0 && idx < kRoster && locked_.count(kNames[idx]) > 0;
}

int CharSelect::enabledCount() const {
    int n = 0;
    for (int c = 0; c < kCells; ++c)
        if (cellOk(c)) ++n;
    return n;
}

std::string CharSelect::cursorName() const { return nameAt(cur_); }

std::string CharSelect::paletteLabel() const {
    const auto it = pals_.find(nameAt(cur_));
    if (it == pals_.end() || palIdx_ < 0 || palIdx_ >= static_cast<int>(it->second.size())) return std::string();
    return it->second[static_cast<size_t>(palIdx_)].label;
}

std::string CharSelect::palPath() const {
    if (palIdx_ == 0) return std::string();   // the game's own 1P through the Vfs
    const auto it = pals_.find(nameAt(cur_));
    if (it == pals_.end() || palIdx_ >= static_cast<int>(it->second.size())) return std::string();
    return it->second[static_cast<size_t>(palIdx_)].path;
}

void CharSelect::setPaletteIndex(int i) {
    const auto it = pals_.find(nameAt(cur_));
    palIdx_ = (it == pals_.end() || it->second.empty()) ? 0 : std::clamp(i, 0, static_cast<int>(it->second.size()) - 1);
}

bool CharSelect::stepPalette(int dir) {
    const auto it = pals_.find(nameAt(cur_));
    if (it == pals_.end() || it->second.size() < 2 || lockedAt(cur_)) return false;
    const int n = static_cast<int>(it->second.size());
    palIdx_ = ((palIdx_ + dir) % n + n) % n;
    return true;
}
bool CharSelect::cursorLocked() const { return lockedAt(cur_); }

std::set<std::string> CharSelect::parseLocked(const std::string& text) {
    std::set<std::string> out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(pos, e - pos);
        pos = e + 1;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto trim = [](std::string s) {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
            size_t i = 0;
            while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
            return s.substr(i);
        };
        const std::string key = lower(trim(line.substr(0, eq))), val = trim(line.substr(eq + 1));
        if (key.rfind("story", 0) == 0 && key.size() > 5 && val == "L") out.insert(key.substr(5));
    }
    return out;
}

// The scene's confirm cue names the announcer's sound event ("UISound/charconfirm.a.filia.txt": slot AnnouncerFilia_P1, pan left);
// the recording itself is "UISound/charconfirm.filia.p1.wav" ("charconfirm.b." = player 2), with its own folder for Japanese.
std::string voiceFile(const std::string& cue, bool jp) {
    const std::string pre = "UISound/charconfirm.";
    if (cue.rfind(pre, 0) != 0 || cue.size() < 4 || cue.compare(cue.size() - 4, 4, ".txt") != 0) return cue;
    std::string rest = cue.substr(pre.size(), cue.size() - pre.size() - 4);   // "a.filia", "b.marie.playable"
    if (rest.size() < 3 || rest[1] != '.') return cue;
    const char side = rest[0];
    rest = rest.substr(2);
    for (const char* tag : {".playable", ".boss"}) {
        const std::string t = tag;
        if (rest.size() > t.size() && rest.compare(rest.size() - t.size(), t.size(), t) == 0) rest.resize(rest.size() - t.size());
    }
    return std::string(jp ? "UISound/JP_chars/" : "UISound/") + "charconfirm." + rest + (side == 'b' ? ".p2.wav" : ".p1.wav");
}

bool CharSelect::load(GameUi& gfx, const std::string& restore) {
    ready_ = false;
    sprites_.clear();
    bg_ = Layer{};
    fade_ = Layer{};
    fight_ = Layer{};
    p1_ = Layer{};
    p1View_ = p2View_ = nullptr;
    size1_ = Layer{};
    size2_ = Layer{};
    stage_ = Layer{};
    stages_.clear();
    scene_ = nullptr;
    if (!gfx.loadScene("Charselect.gbs")) {
        gfx.loadScene(restore);
        return false;
    }
    const GbsScene* sc = gfx.scene();
    scene_ = sc;
    bg_.v = sc->view("CharSelectBackground_NEW");
    fight_.v = sc->view("CharSelectFighters");
    p1_.v = sc->view("CharSelectPickFighterP1");
    p1View_ = p1_.v;
    p2View_ = sc->view("CharSelectPickFighterP2");
    if (player_ == 1 && p2View_) p1_.v = p2View_;
    size1_.v = sc->view("CharSelectTeamSizeP1");
    size2_.v = sc->view("CharSelectTeamSizeP2");
    stage_.v = sc->view("CharSelectStage");
    fade_.v = sc->view("Fade");
    if (bg_.v && fight_.v && p1_.v) {
        float x = 0, y = 0, w = 0, h = 0, x2 = 0, y2 = 0, w2 = 0, h2 = 0, x3 = 0, y3 = 0, w3 = 0, h3 = 0;
        const auto& fn = fight_.v->nodes;
        if (findAbs(fn, "TopRowFirst", 0, 0, x, y, w, h) && findAbs(fn, "TopRowSecond", 0, 0, x2, y2, w2, h2) &&
            findAbs(fn, "BottomRowFirst", 0, 0, x3, y3, w3, h3)) {
            gridX_ = x;
            gridY_ = y;
            stepX_ = x2 - x;
            stepY_ = y3 - y;
            cellW_ = w;
            cellH_ = h;
        }
        if (findAbs(fn, "TopRowFirstSelect", 0, 0, x, y, w, h)) {
            selX_ = x;
            selY_ = y;
            selW_ = w;
        }
        if (findAbs(fn, "P1Selector", 0, 0, x, y, w, h)) {
            selRestX_ = x;
            selRestY_ = y;
        }
        for (int i = 0; i < kRoster; ++i) art_[i] = gfx.hasTexture(charTex("charselect_select_icons_", kNames[i]));
        cur_ = -1;
        for (int c = 0; c < kCells && cur_ < 0; ++c)
            if (kCellTable[c] == 0 && cellOk(c)) cur_ = c;
        for (int c = 0; c < kCells && cur_ < 0; ++c)
            if (cellOk(c)) cur_ = c;
        prev_ = cur_;
        ready_ = cur_ >= 0;
        buildStages(gfx);
    }
    gfx.loadScene(restore);
    return ready_;
}

void CharSelect::buildStages(GameUi& gfx) {
    stages_.clear();
    if (!stage_.v) return;
    std::map<std::string, std::string> songs;
    std::string ini;
    if (gfx.vfs() && gfx.vfs()->readText("music.ini", ini)) songs = parseStageSongs(ini);
    auto lowerKey = [](std::string k) {
        for (char& c : k) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return k;
    };
    if (gfx.hasTexture("Textures/charselect/stages/stage_random.png"))
        stages_.push_back({"random", "Textures/charselect/stages/stage_random.png", "Random", ""});
    for (const StageRow& r : kStageRows) {
        const std::string tex = std::string("Textures/charselect/stages/stage_") + r.id + ".png";
        if (!gfx.hasTexture(tex)) continue;
        const auto it = songs.find(lowerKey(r.key));
        stages_.push_back({r.id, tex, r.key, it == songs.end() ? std::string() : it->second});
    }
    for (const CustomStage& cs : custom_) {   // mods/stages/<id>/
        std::string tex = "stage_" + cs.id + ".png";
        if (!gfx.hasTexture(tex)) tex = "Textures/charselect/stages/stage_random.png";   // no thumbnail: the dice picture
        stages_.push_back({cs.id, tex, cs.name.empty() ? cs.id : cs.name, ""});
    }
    stagesDirty_ = false;
    if (!custom_.empty()) std::printf("[charselect] %zu stage(s), %zu from mods/stages\n", stages_.size(), custom_.size());
    if (stages_.size() < 2) stages_.clear();   // nothing to choose from: the step is skipped
}

std::string CharSelect::stageId() const {
    return (stageIdx_ >= 0 && stageIdx_ < static_cast<int>(stages_.size())) ? stages_[static_cast<size_t>(stageIdx_)].id : std::string();
}

Rect CharSelect::chipRect(int player, int n) {
    // the 1 / 2 / 3 medallions of the size panels (canvas centres measured on the PC edition: y 492; x 170 / 260 / 350 and 928 / 1018 / 1106)
    static const int cx[2][3] = {{170, 260, 350}, {928, 1018, 1106}};
    const int c = cx[player & 1][std::clamp(n, 1, 3) - 1] / 2;
    return Rect{c - 24, 246 - 24, 48, 48};
}

void CharSelect::enter(int nowMs, bool story) { enterAt(nowMs, story, Step::Size); }

void CharSelect::enterAt(int nowMs, bool story, Step first) {
    story_ = story;
    enterMs_ = moveMs_ = nowMs;
    lastMs_ = nowMs;
    acc_ = 0;
    prev_ = cur_;
    sounds_.clear();
    teamSize_ = 1;
    stageIdx_ = prevStageIdx_ = 0;
    stageDir_ = 0;
    stageMoveMs_ = -10000;
    const bool hasSize = size1_.v && size2_.v;
    step_ = Step::Fighter;
    if (!story && hasSize && first == Step::Size && !sizeLocked_) step_ = Step::Size;
    if (!story && !stages_.empty() && first == Step::Stage) step_ = Step::Stage;
    startLayers(true);
    if (player_ == 1) fight_.p.play("P1_F1_NameIntro");   // player 1's name plate stays up while player 2 picks
}

void CharSelect::startLayers(bool) {
    auto init = [](Layer& l) {
        if (!l.v) return;
        l.p.bind(l.v);
        l.p.play("OnActivate");   // the resting pose of the view: everything where it starts / hidden
    };
    init(bg_);
    init(fight_);
    init(p1_);
    init(size1_);
    init(size2_);
    init(stage_);
    init(fade_);
    fade_.p.play("FadeIn");
    bg_.p.play("Lights");
    bg_.p.play("BG");
    bg_.p.play("Stage_Off");
    fight_.p.play("Intro");
    switch (step_) {
        case Step::Size:
            size1_.p.play("Intro");
            size2_.p.play("Intro");
            size1_.p.play("TeamSize" + std::to_string(teamSize_) + "Select");
            size2_.p.play("TeamSize" + std::to_string(teamSize_) + "Select");
            break;
        case Step::Fighter:
            p1_.p.play("Intro");
            p1_.p.play("PortraitIntro");
            fight_.p.play(pfx() + "F1_NameIntro");
            break;
        case Step::Stage:
            fight_.p.playFrom("Outro", 14);
            fight_.p.play(pfx() + "F1_NameIntro");
            showStage(false);
            stage_.p.play("OnActivate");
            break;
    }
}

// the stage picture goes into the background window (Stage_01 / Stage_02 of CharSelectBackground_NEW cross-fade)
void CharSelect::showStage(bool switching) {
    if (stages_.empty()) return;
    const std::string& tex = stages_[static_cast<size_t>(std::clamp(stageIdx_, 0, static_cast<int>(stages_.size()) - 1))].tex;
    if (!switching) {
        slotTex_[0] = tex;
        slotUp_ = 0;
        bg_.p.play("Stage_Fade_Up");
    } else if (slotUp_ == 0) {   // Stage_02 fades in over Stage_01
        slotTex_[1] = tex;
        slotUp_ = 1;
        bg_.p.play("Stage_Switch_01");
    } else {                      // Stage_02 fades out and shows the new Stage_01 below it
        slotTex_[0] = tex;
        slotUp_ = 0;
        bg_.p.play("Stage_Switch_02");
    }
}

void CharSelect::setSize(int n, bool announce) {
    n = std::clamp(n, 1, 3);
    if (announce && n != teamSize_) {
        size1_.p.play("TeamSize" + std::to_string(teamSize_) + "Deselect");
        size2_.p.play("TeamSize" + std::to_string(teamSize_) + "Deselect");
    }
    teamSize_ = n;
    size1_.p.play("TeamSize" + std::to_string(n) + "Select");
    size2_.p.play("TeamSize" + std::to_string(n) + "Select");
}

void CharSelect::toSize() {
    step_ = Step::Size;
    p1_.p.play("Outro");
    fight_.p.play(pfx() + "F1_NameOutro");
    size1_.p.play("Intro");
    size2_.p.play("Intro");
}

void CharSelect::toFighter() {
    const Step from = step_;
    step_ = Step::Fighter;
    if (from == Step::Size) {
        size1_.p.play("Outro");
        size2_.p.play("Outro");
    }
    if (from == Step::Stage) {
        stage_.p.play("StageSelectOutro");
        bg_.p.play("Stage_Off");
        fight_.p.play("Intro");
    }
    p1_.p.play("Intro");
    p1_.p.play("PortraitIntro");
    fight_.p.play(pfx() + "F1_NameIntro");
}

void CharSelect::toStage() {
    step_ = Step::Stage;
    p1_.p.play("Outro");
    fight_.p.play("Outro");
    showStage(false);
    stage_.p.play("OnActivate");
}

void CharSelect::moveStage(int dir, int nowMs) {
    const int n = static_cast<int>(stages_.size());
    if (n < 2) return;
    prevStageIdx_ = stageIdx_;
    stageIdx_ = ((stageIdx_ + dir) % n + n) % n;
    stageDir_ = dir;
    stageMoveMs_ = nowMs;
    showStage(true);
    stage_.p.play(dir < 0 ? "LeftPressed" : "RightPressed");
}

void CharSelect::tickClock(int nowMs) {
    if (lastMs_ < 0 || nowMs < lastMs_) lastMs_ = nowMs;
    acc_ += std::clamp(nowMs - lastMs_, 0, 250);
    lastMs_ = nowMs;
    constexpr double kTickMs = 1000.0 / 60.0;
    int n = 0;
    Layer* const layers[] = {&bg_, &fight_, &p1_, &size1_, &size2_, &stage_, &fade_};
    while (acc_ >= kTickMs && n < 8) {
        acc_ -= kTickMs;
        ++n;
        for (Layer* l : layers)
            if (l->v) l->p.tick();
    }
    if (acc_ >= kTickMs) acc_ = 0;
    for (Layer* l : layers) {
        if (!l->v) continue;
        for (const GbsSoundCue& c : l->p.takeCues()) {
            const std::string nm = scene_ ? voiceFile(scene_->soundName(c.soundId), jpVoice_) : std::string();
            if (!nm.empty()) sounds_.emplace_back(nm, c.volume);
        }
    }
}

std::vector<std::pair<std::string, float>> CharSelect::takeSounds() {
    std::vector<std::pair<std::string, float>> out;
    out.swap(sounds_);
    return out;
}

bool CharSelect::stepCursor(int dCol, int dRow) {
    const int col = cur_ % 3, row = cur_ / 3;
    if (dCol != 0) {
        for (int s = 1; s < 3; ++s) {
            const int c2 = ((col + dCol * s) % 3 + 3) % 3;
            if (cellOk(row * 3 + c2)) {
                cur_ = row * 3 + c2;
                return true;
            }
        }
    }
    if (dRow != 0) {
        const int rows = kCells / 3;
        for (int s = 1; s < rows; ++s) {
            const int r2 = ((row + dRow * s) % rows + rows) % rows;
            if (cellOk(r2 * 3 + col)) {
                cur_ = r2 * 3 + col;
                return true;
            }
        }
        // nothing in this column: take the nearest filled cell of the next occupied row
        for (int s = 1; s < rows; ++s) {
            const int r2 = ((row + dRow * s) % rows + rows) % rows;
            for (int k = 0; k < 3; ++k) {
                const int c2 = (col + k) % 3;
                if (cellOk(r2 * 3 + c2)) {
                    cur_ = r2 * 3 + c2;
                    return true;
                }
            }
        }
    }
    return false;
}

Rect CharSelect::cellRect(int cell) const {
    const int col = cell % 3, row = cell / 3;
    const float x = (gridX_ + stepX_ * col) * kScale, y = (gridY_ + stepY_ * row) * kScale;
    return Rect{static_cast<int>(std::lround(x - 0.5f)), static_cast<int>(std::lround(y - 0.5f)),
                static_cast<int>(std::lround(stepX_ * kScale)), static_cast<int>(std::lround(stepY_ * kScale))};
}

CharSelect::Out CharSelect::update(const Input& in, int nowMs) {
    Out o;
    if (!ready_) {
        if (in.back || in.enter || in.released) o.kind = Out::Kind::Back;
        return o;
    }
    tickClock(nowMs);
    const bool tap = in.released && true;
    // ---------------------------------------------------------------- 1. SELECT SIZE
    if (step_ == Step::Size) {
        if (in.back) {
            o.kind = Out::Kind::Back;
            return o;
        }
        if (in.left || in.right) {
            const int n = std::clamp(teamSize_ + (in.right ? 1 : -1), 1, 3);
            if (n != teamSize_) {
                setSize(n, true);
                o.kind = Out::Kind::Moved;
            }
            return o;
        }
        if (in.enter) {
            toFighter();
            o.kind = Out::Kind::Step;
            return o;
        }
        if (tap) {
            for (int n = 1; n <= 3; ++n) {
                const Rect r = chipRect(0, n);
                if (!r.contains(in.mx, in.my) || !r.contains(in.pmx, in.pmy)) continue;
                if (n == teamSize_) {
                    toFighter();
                    o.kind = Out::Kind::Step;
                } else {
                    setSize(n, true);
                    o.kind = Out::Kind::Moved;
                }
                return o;
            }
            const Rect cancel{70, 286, 130, 22};   // "PRESS B TO EXIT" under the panel (logical pixels)
            if (cancel.contains(in.mx, in.my) && cancel.contains(in.pmx, in.pmy)) o.kind = Out::Kind::Back;
        }
        return o;
    }
    // ---------------------------------------------------------------- 3. SELECT STAGE
    if (step_ == Step::Stage) {
        if (in.back) {
            toFighter();
            o.kind = Out::Kind::Step;
            return o;
        }
        int dir = 0;
        if (in.left || in.keyUp) dir = -1;
        if (in.right || in.keyDown) dir = 1;
        if (dir) {
            moveStage(dir, nowMs);
            o.kind = Out::Kind::Moved;
            return o;
        }
        auto confirmStage = [&] {
            o.kind = Out::Kind::Confirm;
            o.name = nameAt(cur_);
            o.stage = stageId();
            o.teamSize = teamSize_;
            o.palette = palIdx_;
            o.paletteLabel = paletteLabel();
        };
        if (in.enter) {
            confirmStage();
            return o;
        }
        if (tap) {
            // logical pixels: the arrows and the name bar run along the bottom, the window above them is the picture,
            // the left pillar (player 1's pedestal) is "back"
            const Rect leftArrow{150, 300, 80, 56}, rightArrow{410, 300, 80, 56}, image{200, 70, 240, 220}, rnd{240, 300, 160, 56};
            const Rect backBtn{30, 90, 110, 200};
            auto hit = [&](const Rect& r) { return r.contains(in.mx, in.my) && r.contains(in.pmx, in.pmy); };
            if (hit(backBtn)) {
                toFighter();
                o.kind = Out::Kind::Step;
            } else if (hit(leftArrow)) {
                moveStage(-1, nowMs);
                o.kind = Out::Kind::Moved;
            } else if (hit(rightArrow)) {
                moveStage(1, nowMs);
                o.kind = Out::Kind::Moved;
            } else if (hit(image) || hit(rnd)) {
                confirmStage();
            }
        }
        return o;
    }
    // ---------------------------------------------------------------- 2. the fighter grid
    if (in.back) {
        if (story_ || sizeLocked_ || !(size1_.v && size2_.v)) {
            o.kind = Out::Kind::Back;
        } else {
            toSize();
            o.kind = Out::Kind::Step;
        }
        return o;
    }
    int dc = 0, dr = 0;
    if (in.left) dc = -1;
    if (in.right) dc = 1;
    if (in.keyUp) dr = -1;
    if (in.keyDown) dr = 1;
    auto moved = [&](int before) {
        palIdx_ = 0;
        prev_ = before;
        moveMs_ = nowMs;
        p1_.p.play("PortraitIntro");
        fight_.p.play(pfx() + "F1_NameIntro");
        o.kind = Out::Kind::Moved;
    };
    if (dc || dr) {
        const int before = cur_;
        if (stepCursor(dc, dr)) moved(before);
        return o;
    }
    auto confirm = [&] {
        o.name = nameAt(cur_);
        if (lockedAt(cur_)) {
            o.kind = Out::Kind::Locked;
            return;
        }
        // the confirm sound + the announcer saying the name (the scene's own CharSelectSnd_<name>_1 animation)
        if (o.name != "random") fight_.p.play("CharSelectSnd_" + o.name + "_1");
        o.palette = palIdx_;
        o.paletteLabel = paletteLabel();
        if (story_ || stages_.empty() || skipStage_) {
            o.kind = Out::Kind::Confirm;
            o.teamSize = story_ ? 1 : teamSize_;
            return;
        }
        toStage();
        o.kind = Out::Kind::Step;
    };
    if (in.enter) {
        confirm();
        return o;
    }
    if (in.tab && stepPalette(1)) {
        o.kind = Out::Kind::Moved;
        return o;
    }
    if (tap) {
        if (spriteHit_.w > 0 && spriteHit_.contains(in.mx, in.my) && spriteHit_.contains(in.pmx, in.pmy) && stepPalette(1)) {
            o.kind = Out::Kind::Moved;
            return o;
        }
        const Rect cancel{10, 324, 210, 34};  // "PRESS B TO CANCEL" under the data bar
        if (cancel.contains(in.mx, in.my) && cancel.contains(in.pmx, in.pmy)) {
            if (story_ || sizeLocked_ || !(size1_.v && size2_.v)) {
                o.kind = Out::Kind::Back;
            } else {
                toSize();
                o.kind = Out::Kind::Step;
            }
            return o;
        }
        for (int c = 0; c < kCells; ++c) {
            if (!cellOk(c)) continue;
            const Rect r = cellRect(c);
            if (!r.contains(in.mx, in.my) || !r.contains(in.pmx, in.pmy)) continue;
            if (c == cur_) {
                confirm();
            } else {
                const int before = cur_;
                cur_ = c;
                moved(before);
            }
            break;
        }
    }
    return o;
}

void CharSelect::draw(GameUi& gfx, const Localization& loc, const std::string& lang, int nowMs, const std::string& restore) {
    if (!ready_ || !gfx.loadScene("Charselect.gbs")) {
        gfx.loadScene(restore);
        return;
    }
    gfx.setViewport(0, 0, kScale);
    gfx.setJapanese(lang == "jp");
    if (stagesDirty_) buildStages(gfx);   // mods/stages changed since the scene was loaded
    const std::string cname = nameAt(cur_);
    const bool random = cname == "random", locked = lockedAt(cur_);
    const std::string pre = (lang == "en" || lang.empty()) ? "" : lang + ".";

    auto localised = [&](const std::string& base) {
        // "Textures/charselect/x.png" -> "Textures/charselect/<lang>.x.png" when the user's data has it
        if (pre.empty()) return base;
        const size_t slash = base.rfind('/');
        const std::string alt = base.substr(0, slash + 1) + pre + base.substr(slash + 1);
        return gfx.hasTexture(alt) ? alt : base;
    };
    // draws a view with the pose its player gives each node
    auto drawLayer = [&](Layer& l, const GameUi::Hook& hook) {
        if (!l.v) return;
        gfx.setAnimPlayer(&l.p);
        gfx.drawView(*l.v, hook);
        gfx.setAnimPlayer(nullptr);
    };
    // the original squeezes a label's text into the label's box (a longer language never spills out of the frame)
    // The scale is the one the animation gives the label *right now* (the intro shrinks the plate's text to 0.9), and the
    // width is the narrower of the resting and animated boxes (the "1" next to "PLAYER" starts right after the resting box).  A longer word first gets squeezed sideways, so it keeps the glyph height of
    // the original ("PLAYER" -> "JUGADOR"); only what still does not fit past a 20% squeeze shrinks uniformly.
    auto fit = [&](const GbsNode& n, NodeState& st) {
        if (n.textFit == kFitScaleDown || n.textFit == kFitScaleToFit) return;   // the label's own fit mode squeezes it (gbs_text.h)
        const GbsFont* f = gfx.font(n.fontId);
        float bw = n.w, sc = n.scaleX;
        if (const GbsAnimPlayer* ap = gfx.animPlayer())
            if (const GbsPose* ps = ap->pose(n.id))
                if (ps->kind == 'L') {
                    bw = std::min(n.w, ps->w);   // the animation widens the text box, but the neighbouring number sits at the resting edge
                    sc = ps->scaleX;
                }
        if (!f || bw <= 0) return;
        const float tw = gfx.textWidth(*f, st.text, sc);
        if (tw <= bw) return;
        const float ratio = bw / tw;
        constexpr float kMaxSqueeze = 0.8f;
        st.textSqueezeX = std::max(ratio, kMaxSqueeze);
        if (ratio < kMaxSqueeze) st.textScale = ratio / kMaxSqueeze;
    };
    const float t = nowMs / 1000.0f;

    // ---- 1. the moving city and the art deco window (the BG / Lights animations move them)
    drawLayer(bg_, [&](const GbsNode& n, NodeState& st) {
        const std::string& nm = n.name;
        if (nm == "Blue" || nm == "Fade Up") {
            st.hide = true;
        } else if (nm == "Stage_01" || nm == "Stage_02") {
            const std::string& tx = slotTex_[nm == "Stage_01" ? 0 : 1];
            const bool rnd = tx.find("stage_random") != std::string::npos;
            if (tx.empty() || rnd) st.hide = true;
            else st.texName = tx;
        }
    });

    // ---- 2. pedestals, filmstrip, icon grid, selector, header
    const std::string pedestal = random ? "Textures/charselect/charselect_pedestal_name_unknown01.png"
                                        : charTex("charselect_pedestal_name_", cname);
    const GbsPose* frameOver = nullptr;   // the strip fades out in the stage step; the icons (drawn here) follow its alpha
    if (fight_.v) {
        if (const GbsNode* fo = fight_.v->find("Main Filmstrip Frame (Over Icons)")) frameOver = fight_.p.pose(fo->id);
    }
    const float stripAlpha = frameOver ? frameOver->alpha() : 1.0f;
    drawLayer(fight_, [&](const GbsNode& n, NodeState& st) {
        const std::string& nm = n.name;
        if (nm == pfx() + "F1_Name") {
            st.texName = pedestal;
        } else if (player_ == 1 && nm == "P1_F1_Name") {   // player 1's pick stays on its pedestal while player 2 chooses
            st.texName = charTex("charselect_pedestal_name_", firstPick_);
        } else if (nm == "P1_F2_Name" || nm == "P1_F3_Name" || (nm.size() > 4 && nm.rfind("P2_F", 0) == 0 && nm.find("_Name") != std::string::npos)) {
            st.hide = true;
        } else if (nm == "TopRowFirstSelect" || nm == "TopRowSecondSelect" || nm == "BottomRowFirstSelect" || nm == "P2Selector") {
            st.hide = true;
        } else if (nm == "P1Selector") {
            const int col = cur_ % 3, row = cur_ / 3;
            const GbsPose* ps = fight_.p.pose(n.id);
            const float bx = selRestX_ + (ps ? ps->x - n.x : 0.0f), by = selRestY_ + (ps ? ps->y - n.y : 0.0f);
            st.dx = (selX_ + stepX_ * col) - bx;
            st.dy = (selY_ + stepY_ * row) - by;
            st.alpha = 0.72f + 0.28f * std::sin(t * 6.0f);
            if (step_ == Step::Size) st.hide = true;   // nobody is picking yet
        } else if (nm == "Character Select Header") {
            st.texName = localised("Textures/charselect/charselect_winged_frame01.png");
        } else if (nm == "Character Select Icons (Glow Off)") {
            if (stripAlpha < 0.02f) return;
            for (int c = 0; c < kCells; ++c) {
                if (!cellOk(c)) continue;
                const std::string id = nameAt(c);
                const bool sel = c == cur_ && step_ == Step::Fighter, lk = lockedAt(c);
                std::string tex;
                if (id == "random") tex = localised(std::string("Textures/charselect/charselect_select_icons_random0") + (sel ? "2" : "1") + ".png");
                else tex = charTex("charselect_select_icons_", id, lk ? "03" : (sel ? "02" : "01"));
                if (SDL_Texture* tx = gfx.texture(tex)) {
                    const float x = gridX_ + stepX_ * (c % 3), y = gridY_ + stepY_ * (c / 3);
                    gfx.drawTexture(tx, x, y, cellW_, cellH_, 0, 0, 0, false, false, Col{255, 255, 255, 255}, stripAlpha);
                }
            }
        }
    });

    // ---- 2b. the character standing on player 1's pedestal (character_select_<name>.spr.msb, idle loop)
    if (fight_.v && !random && !locked && step_ != Step::Size) {
        if (const GbsNode* pil = fight_.v->find(pfx() + "L1_PillarGrp")) {
            const GbsPose* pp = fight_.p.pose(pil->id);
            const float px = pp ? pp->x : pil->x, py = pp ? pp->y : pil->y;
            spriteHit_ = Rect{0, 0, 0, 0};
            const bool flip = player_ == 1;                       // player 2 faces left
            const float hx = flip ? px + 42.0f : px + 58.0f;      // the pedestal is 100 wide: mirror the hot spot
            if (flip && !firstPick_.empty()) {                    // player 1's pick, standing still on the left pedestal
                if (const GbsNode* p1pil = fight_.v->find("P1_L1_PillarGrp")) {
                    const GbsPose* p1pp = fight_.p.pose(p1pil->id);
                    if (const CharSprite* fs = sprites_.get(gfx, firstPick_, firstPal_))
                        CharSprites::draw(gfx, *fs, nowMs - enterMs_, (p1pp ? p1pp->x : p1pil->x) + 58.0f, (p1pp ? p1pp->y : p1pil->y) + 23.0f, false, 1.0f);
                }
            }
            if (const CharSprite* cs = sprites_.get(gfx, cname, palPath())) {
                const int since = nowMs - moveMs_;
                CharSprites::draw(gfx, *cs, since, hx, py + 23.0f, flip, clamp01(static_cast<float>(since) / 140.0f) * 1.0f);
                if (!cs->idle.empty()) {
                    const CharSpriteFrame& f = cs->idle[0];
                    const float w = f.w * CharSprites::kScale, h = f.h * CharSprites::kScale;
                    spriteHit_ = Rect{static_cast<int>((hx - f.hotX * CharSprites::kScale) * kScale), static_cast<int>((py + 23.0f - f.hotY * CharSprites::kScale) * kScale),
                                      static_cast<int>(w * kScale), static_cast<int>(h * kScale)};
                }
            }
        }
    }

    // ---- 3. SELECT SIZE panels (both players; the second one mirrors the first until a real second player exists)
    if (!story_) {
        const std::string sizeKey[3] = {"One Super-Powerful Character", "Two Medium-Powered Characters", "Three Regular-Powered Characters"};
        auto sizePanel = [&](Layer& l, int player) {
            // the labels of the file sit to the right of what they belong to; the original centres each text on its frame / header
            auto centreOn = [&](const GbsNode& n, NodeState& st, const char* ref) {
                float rx, ry, rw, rh, lx, ly, lw, lh;
                if (!findAbs(l.v->nodes, ref, 0, 0, rx, ry, rw, rh) || !findAbs(l.v->nodes, n.name, 0, 0, lx, ly, lw, lh)) return;
                const GbsPose* ps = l.p.pose(n.id);
                const float left = lx + (ps ? ps->x - n.x : 0.0f), w = ps ? ps->w : n.w;
                st.dx = (rx + rw / 2) - (left + w / 2);
                st.alignOverride = 1;
            };
            drawLayer(l, [&](const GbsNode& n, NodeState& st) {
                const std::string& nm = n.name;
                if (nm == "Demo_Grp" || nm.rfind("TXT_HELPER", 0) == 0) {
                    st.hide = true;
                } else if (nm == "Color Select") {
                    st.dy = -n.y;   // the scene keeps the frame group parked above the screen (the colour page); the size page puts it at 0
                } else if (nm == "Player Name") {
                    st.hasText = true;
                    st.text = upper(loc.trOr("PLAYER", "PLAYER")) + " " + std::to_string(player + 1);
                    centreOn(n, st, "Header");
                    fit(n, st);
                } else if (nm == "Size Header") {
                    st.hasText = true;
                    st.text = upper(loc.trOr("SELECT TEAM SIZE", "SELECT SIZE"));
                    fit(n, st);
                } else if (nm == "Team Select Size Body Text") {
                    st.hasText = true;
                    st.text = loc.trOr(sizeKey[std::clamp(teamSize_, 1, 3) - 1], sizeKey[std::clamp(teamSize_, 1, 3) - 1]);
                    centreOn(n, st, "Frame");
                    fit(n, st);
                } else if (nm == "ButtonConfig") {
                    st.hasText = player == 0;
                    st.hide = player != 0;
                    st.text = upper(plainTokens(loc.trOr("ExitMessage", "Press {selg} to exit")));
                    centreOn(n, st, "Frame");
                    fit(n, st);
                }
            });
        };
        sizePanel(size1_, 0);
        sizePanel(size2_, 1);
    }

    // ---- 4. player 1: portrait, name plate, data bar (the fighter step)
    const std::string title = random ? std::string("Textures/charselect/charselect_portrait_name_unknown01.png")
                                     : charTex("charselect_portrait_name_", cname);
    const std::string lockedText = upper(loc.trOr("Locked!", "Locked!"));
    if (step_ == Step::Fighter || p1_.p.running("Outro") || p1_.p.running("Intro")) {
        drawLayer(p1_, [&](const GbsNode& n, NodeState& st) {
            const std::string& nm = n.name;
            if (nm == "Portrait") {
                if (random) st.hide = true;
                else st.texName = charTex("charselect_portraits_", cname);
            } else if (nm == "PortraitName") {
                st.texName = title;
            } else if (nm == "Highlight") {
                st.hide = true;
            } else if (nm == "Demo_GRP") {
                st.hide = !locked;
            } else if (nm == "Demo_Text") {
                st.hasText = true;
                st.text = lockedText;
                st.textColor = Col{255, 244, 214, 255};
                fit(n, st);
            } else if (nm == "Player Number") {
                st.hasText = true;
                st.text = upper(loc.trOr("PLAYER", "PLAYER"));
                fit(n, st);
            } else if (nm == "Player Number Text") {
                st.hasText = true;
                st.text = player_ == 1 ? "2" : "1";
            } else if (nm == "F1_Token") {
                st.texName = (random || locked) ? "Textures/charselect/data_charcoin_unknown_glow01.png"
                                                : charTex("data_charcoin_", cname, "_glow01");
            }
        });
    }

    // ---- 5. SELECT STAGE
    if (stage_.v && !stages_.empty() && (step_ == Step::Stage || stage_.p.running("StageSelectOutro"))) {
        const StageInfo& si = stages_[static_cast<size_t>(std::clamp(stageIdx_, 0, static_cast<int>(stages_.size()) - 1))];
        auto centreOn = [&](const GbsNode& n, NodeState& st, const char* ref) {
            float rx, ry, rw, rh, lx, ly, lw, lh;
            if (!findAbs(stage_.v->nodes, ref, 0, 0, rx, ry, rw, rh) || !findAbs(stage_.v->nodes, n.name, 0, 0, lx, ly, lw, lh)) return;
            const GbsPose* ps = stage_.p.pose(n.id);
            const float left = lx + (ps ? ps->x - n.x : 0.0f), w = ps ? ps->w : n.w;
            st.dx += (rx + rw / 2) - (left + w / 2);
            st.alignOverride = 1;
        };
        drawLayer(stage_, [&](const GbsNode& n, NodeState& st) {
            const std::string& nm = n.name;
            if (nm == "Character Select") {
                // the name bar, its arrows and the "Random" tag sit along the bottom of the screen (the scene parks them at the top)
                st.dx = -n.x;
                st.dy = 440.0f;
            } else if (nm == "LevelImage" || nm == "LevelImageDupe") {
                st.hide = true;   // this scene's own picture slots are unused: the picture lives in the background window
            } else if (nm == "StageNameLabel") {
                st.hasText = true;
                st.text = si.id == "random" ? loc.trOr("Random", "Random") : loc.trOr(si.key, si.key);
                centreOn(n, st, "Stage Name Frame");
                fit(n, st);
            } else if (nm == "Random Text") {
                st.hasText = true;
                st.text = upper(loc.trOr("Random", "Random"));
                fit(n, st);
            } else if (nm == "Player Name") {
                st.hasText = true;
                st.text = upper(loc.trOr("STAGE SELECT", "SELECT STAGE"));
                centreOn(n, st, "Header");
                fit(n, st);
            } else if (nm == "Buttons_GRP" || nm == "Glows") {
                st.hide = true;   // the six LP/MP/HP/LK/MK/HK buttons of the PC screen: dropped on purpose, the arrows next to the name bar are what picks a stage here
            } else if (nm == "Song Name" || nm == "Demo_Grp" || nm == "Demo_Txt" ||
                       nm == "Reserve_Label_P1" || nm == "Reserve_Label_P2" || nm == "Arrow Up Stage Select" ||
                       nm == "Arrow Down Stage Select" || nm.rfind("TXT_HELPER", 0) == 0) {
                st.hide = true;
            }
        });
    }

    // ---- 6. "PRESS B TO CANCEL" under the data bar (the fighter step)
    if (step_ == Step::Fighter) {
        std::string msg = upper(plainTokens(loc.trOr("CancelMessage", "Press {selg} to cancel")));
        const GbsFont* f = gfx.fontNamed("Subheader w Wings");   // the slim art deco face of the original's cancel line
        if (f) {
            const char* dfName = player_ == 1 ? "Player Data Frame Right" : "Player Data Frame Left";
            const GbsNode* df = p1_.v ? p1_.v->find(dfName) : nullptr;
            const GbsPose* pose = df ? p1_.p.pose(df->id) : nullptr;
            const float slideX = (pose && df) ? (pose->x - df->x) : 0.0f;
            const float bx = (player_ == 1 ? 770.0f : 26.0f) + slideX, by = 668.0f, bw = 480.0f, bh = 34.0f, sc = 1.08f;
            const Col dark{12, 6, 4, 255};
            const int off[4][2] = {{-2, 0}, {2, 0}, {0, -2}, {0, 2}};
            for (auto& o : off) gfx.drawLabelText(*f, msg, bx + o[0], by + o[1], bw, bh, 1, true, dark, sc, 0.9f);
            gfx.drawLabelText(*f, msg, bx, by, bw, bh, 1, true, Col{234, 230, 224, 255}, sc, 1.0f);
        }
    }

    // ---- 6b. the colour of the character on the pedestal ("COLOR 3 / 31" + the file name of a mods/pal palette) when there is a choice
    if (step_ == Step::Fighter && !random && !locked) {
        const auto pit = pals_.find(cname);
        if (pit != pals_.end() && pit->second.size() > 1) {
            const std::string label = paletteLabel();
            std::string msg = "COLOR " + std::to_string(palIdx_ + 1) + " / " + std::to_string(pit->second.size());
            const std::string file = label.rfind("mod:", 0) == 0 ? upper(label.substr(4)) : std::string();
            if (const GbsFont* f = gfx.fontNamed("Subheader w Wings")) {
                const float bx = player_ == 1 ? 810.0f : 20.0f, bw = 450.0f, bh = 30.0f;
                const Col dark{12, 6, 4, 255};
                const int off[4][2] = {{-2, 0}, {2, 0}, {0, -2}, {0, 2}};
                auto line = [&](const std::string& t, float by, float sc, Col col) {
                    for (auto& o : off) gfx.drawLabelText(*f, t, bx + o[0], by + o[1], bw, bh, 1, true, dark, sc, 0.9f);
                    gfx.drawLabelText(*f, t, bx, by, bw, bh, 1, true, col, sc, 1.0f);
                };
                line(msg, 82.0f, 0.85f, Col{255, 214, 120, 255});                       // in the dark above the pedestals
                if (!file.empty()) line(file, 112.0f, 0.55f, Col{235, 225, 210, 255});   // mods/pal file name
            }
        }
    }

    // ---- 7. fade in
    if (fade_.v) {
        drawLayer(fade_, nullptr);   // FadeIn: black to clear over 5 ticks
    } else {
        const float fade = 1.0f - clamp01(static_cast<float>(nowMs - enterMs_) / 260.0f);
        if (fade > 0.01f) gfx.fillRect(0, 0, kCanvasW, kCanvasH, Col{0, 0, 0, static_cast<uint8_t>(255.0f * fade)});
    }

    gfx.loadScene(restore);
}

}  // namespace menu
}  // namespace zengine
