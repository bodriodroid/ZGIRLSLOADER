// story_mode.cpp - see story_mode.h
#include "story_mode.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

#include "dds.h"
#include "story.h"
#include "text_formats.h"
#include "vfs.h"

namespace zengine {
namespace story {

namespace {

constexpr double kTickMs = 1000.0 / 60.0;
constexpr float kDialogueLift = 56.0f;   // the original frame sits at y = 520 with its text box at 650..755 of 720: raised so the whole text shows
constexpr int kMaxAnimWait = 240;   // a plain "Anim:" line holds the script at most this long (ticks)

std::string lowerStr(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return s;
}

std::string upperStr(std::string s) {
    for (char& c : s)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
    return s;
}

std::string trimStr(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

std::string normPath(std::string s) {
    for (char& c : s) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    }
    return s;
}

std::string baseStem(const std::string& path) {
    std::string s = path;
    const size_t slash = s.find_last_of("/\\");
    if (slash != std::string::npos) s = s.substr(slash + 1);
    const size_t dot = s.find_last_of('.');
    if (dot != std::string::npos) s.resize(dot);
    return s;
}

// "Filia_Legacy_Intro" -> "Filia Legacy Intro", "FiliaPremiddle_R" -> "FiliaPremiddle"
std::string pretty(std::string s) {
    if (s.size() > 2 && (s.compare(s.size() - 2, 2, "_R") == 0 || s.compare(s.size() - 2, 2, "_r") == 0)) s.resize(s.size() - 2);
    for (char& c : s)
        if (c == '_') c = ' ';
    return trimStr(s);
}

// Word wrap for the 8x8 font: at most `maxChars` code points per line.
std::vector<std::string> wrapChars(const std::string& s, int maxChars) {
    std::vector<std::string> out;
    std::string line, word;
    auto flushWord = [&]() {
        if (word.empty()) return;
        const int lw = Ui::glyphCount(line), ww = Ui::glyphCount(word);
        if (!line.empty() && lw + 1 + ww > maxChars) {
            out.push_back(line);
            line.clear();
        }
        if (!line.empty()) line += ' ';
        line += word;
        word.clear();
    };
    for (char c : s) {
        if (c == '\n') {
            flushWord();
            out.push_back(line);
            line.clear();
        } else if (c == ' ' || c == '\t') {
            flushWord();
        } else {
            word += c;
        }
    }
    flushWord();
    if (!line.empty()) out.push_back(line);
    return out;
}

bool nameIs(const std::string& n, std::initializer_list<const char*> names) {
    for (const char* s : names)
        if (n == s) return true;
    return false;
}

}  // namespace

// ============================================================================================ data

void StoryScreen::setGameData(const GameData* gd) {
    gd_ = gd;
    credits_.setGameData(gd);
    voCache_.clear();   // a new Vfs: earlier misses may be found now
    voIndex_.clear();
    voIndexBuilt_ = false;
    voMisses_ = 0;
    scanned_ = false;
    stories_.clear();
    cur_ = -1;
    state_ = State::Pick;
    gfx_.reset();
    gfxRen_ = nullptr;   // attach again on the next frame (new Vfs)
    shotView_ = dlgView_ = nullptr;
    shotSc_ = dlgSc_ = nullptr;
    shotAnim_.bind(nullptr);
    dlgAnim_.bind(nullptr);
}

void StoryScreen::loadLocalization(const std::string& lang) {
    lang_ = lang.empty() ? std::string("en") : lang;
    loc_.clear();
    if (!gd_) return;
    std::string json;
    if (gd_->vfs().readText(lang_ + "_loc.json", json)) {
        std::string err;
        if (!loc_.parseJson(json, &err)) std::printf("[story] %s_loc.json: %s (using what was read)\n", lang_.c_str(), err.c_str());
    } else if (lang_ != "en" && gd_->vfs().readText("en_loc.json", json)) {
        loc_.parseJson(json);
    }
}

void StoryScreen::scanStories() {
    scanned_ = true;
    stories_.clear();
    stages_.clear();
    initLocked_.clear();
    if (!gd_) return;
    const Vfs& vfs = gd_->vfs();
    for (const std::string& real : vfs.listByExt(".stm")) {
        const std::string np = normPath(real);
        if (np.find("arcade/story/") == std::string::npos) continue;
        const std::string stem = baseStem(real);
        const std::string ls = lowerStr(stem);
        if (ls.find("_cinematicsonly") != std::string::npos || ls.find("troll") != std::string::npos) continue;
        std::vector<uint8_t> bytes;
        if (!readFile(real, bytes)) continue;
        const std::string text(bytes.begin(), bytes.end());
        const StmFile stm = parseStm(text);
        Entry e;
        e.charName = stm.charName.empty() ? stem : stm.charName;
        e.key = zgKey(e.charName);
        e.nodes = storyNodes(stm);
        if (e.nodes.empty() || e.key.empty()) continue;
        std::stable_sort(e.nodes.begin(), e.nodes.end(), [](const StoryNode& a, const StoryNode& b) { return a.pos < b.pos; });
        bool dup = false;
        for (const Entry& o : stories_) dup = dup || o.key == e.key;
        if (dup) continue;   // the same .stm mounted twice
        stories_.push_back(std::move(e));
    }
    std::sort(stories_.begin(), stories_.end(), [](const Entry& a, const Entry& b) { return a.charName < b.charName; });

    std::string txt;
    if (vfs.readText("stages.ini", txt)) stages_ = parseStages(txt);
    for (Entry& e : stories_) {
        e.labels.clear();
        for (const StoryNode& n : e.nodes) e.labels.push_back(nodeLabel(n));
    }
    if (vfs.readText("ProgressionInit.txt", txt)) {   // "StoryDouble = L"
        prog_.setInitText(txt);                       // the .savini gets every key the game starts with
        size_t pos = 0;
        while (pos < txt.size()) {
            size_t end = txt.find('\n', pos);
            if (end == std::string::npos) end = txt.size();
            const std::string line = txt.substr(pos, end - pos);
            pos = end + 1;
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = zgKey(line.substr(0, eq)), v = trimStr(line.substr(eq + 1));
            if (k.size() > 5 && k.compare(0, 5, "story") == 0 && v == "L") initLocked_.insert(k.substr(5));
        }
    }
    std::printf("[story] %zu stories, %zu stages, %zu locked at start\n", stories_.size(), stages_.size(), initLocked_.size());
}

std::string StoryScreen::nodeLabel(const StoryNode& n) const {
    const std::string num = std::to_string(n.pos) + ". ";
    switch (n.type) {
        case StoryNode::Type::Scene: return num + "SCENE  " + pretty(baseStem(n.file));
        case StoryNode::Type::Credits: return num + "CREDITS";
        case StoryNode::Type::Fight: {
            std::string fighter, world;
            std::string txt;
            if (gd_ && gd_->vfs().readText(n.file, txt)) {
                size_t pos = 0;
                while (pos < txt.size()) {
                    size_t end = txt.find('\n', pos);
                    if (end == std::string::npos) end = txt.size();
                    std::string line = txt.substr(pos, end - pos);
                    pos = end + 1;
                    if (const size_t h = line.find('#'); h != std::string::npos) line.erase(h);
                    const std::vector<std::string> w = splitWords(line);
                    if (w.size() < 2) continue;
                    if (w[0] == "Fighter" && fighter.empty()) fighter = w[1];
                    else if (w[0] == "World" && world.empty()) world = w[1];
                }
            }
            std::string s = num + "FIGHT  VS " + upperStr(fighter.empty() ? pretty(baseStem(n.file)) : fighter);
            if (!world.empty()) s += "  @ " + pretty(world);
            return s;
        }
        case StoryNode::Type::Other: break;
    }
    return num + pretty(baseStem(n.file));
}

int StoryScreen::findStory(const std::string& who) const {
    if (stories_.empty()) return -1;
    const std::string k = zgKey(who);
    if (k.empty()) return -1;
    if (k == "random") {
        std::vector<int> open;
        for (size_t i = 0; i < stories_.size(); ++i)
            if (!storyLocked(stories_[i])) open.push_back(static_cast<int>(i));
        return open.empty() ? -1 : open[static_cast<size_t>(std::rand()) % open.size()];
    }
    for (size_t i = 0; i < stories_.size(); ++i)
        if (stories_[i].key == k) return static_cast<int>(i);
    for (size_t i = 0; i < stories_.size(); ++i)   // "bossmarie" / "marie", "robo" / "robofortune" ...
        if (stories_[i].key.find(k) != std::string::npos || k.find(stories_[i].key) != std::string::npos) return static_cast<int>(i);
    return -1;
}

bool StoryScreen::storyLocked(const Entry& e) const {
    if (storyAll_) return false;
    return initLocked_.count(e.key) != 0 && !prog_.storyUnlocked(e.key);
}

bool StoryScreen::nodeUnlocked(const Entry& e, size_t i) const {
    if (storyAll_) return true;
    if (i >= e.nodes.size()) return false;
    const StoryNode& n = e.nodes[i];
    if (n.unlockChar.empty() || n.unlockPos <= 0) return true;
    return prog_.storyPos(n.unlockChar) >= n.unlockPos;
}

size_t StoryScreen::nextNode(const Entry& e) const {
    const int done = prog_.storyPos(e.key);
    for (size_t i = 0; i < e.nodes.size(); ++i)
        if (e.nodes[i].pos > done) return i;
    return 0;   // everything seen: start over
}

std::string StoryScreen::levelForWorld(const std::string& world) const {
    const std::string w = zgKey(world);
    if (w.empty() || w == "random") return {};
    for (const StageEntry& s : stages_)
        if (zgKey(s.locId) == w) return s.level;
    return lowerStr(world);
}

FightRequest StoryScreen::fightFor(const Entry& e, const StoryNode& n) const {
    FightRequest f;
    f.p1 = e.charName;
    std::string txt;
    if (gd_ && gd_->vfs().readText(n.file, txt)) {
        size_t pos = 0;
        while (pos < txt.size()) {
            size_t end = txt.find('\n', pos);
            if (end == std::string::npos) end = txt.size();
            std::string line = txt.substr(pos, end - pos);
            pos = end + 1;
            if (const size_t h = line.find('#'); h != std::string::npos) line.erase(h);
            const std::vector<std::string> w = splitWords(line);
            if (w.size() < 2) continue;
            if (w[0] == "Fighter" && f.p2.empty()) f.p2 = w[1];   // a team fight lists more: the engine plays 1 vs 1
            else if (w[0] == "World" && f.world.empty()) f.world = w[1];
            else if (w[0] == "NumRounds") f.rounds = std::max(1, std::atoi(w[1].c_str()));
            else if (w[0] == "UseAI") f.ai = std::atoi(w[1].c_str()) != 0;
        }
    } else {
        std::printf("[story] fight script %s not found\n", n.file.c_str());
    }
    if (f.p2.empty()) f.p2 = "Random";
    f.level = levelForWorld(f.world);
    return f;
}

// ============================================================================================ flow

void StoryScreen::begin(const std::string& who, const std::string& lang) {
    loadLocalization(lang);
    prog_.load(prefsDir_);
    storyAll_ = zgOption("story_all");
    if (!scanned_) scanStories();
    notices_.clear();
    fightPending_ = false;
    pendingPlay_ = false;
    cueIdx_ = -1;
    const int idx = who.empty() ? -1 : findStory(who);
    if (idx >= 0 && !storyLocked(stories_[static_cast<size_t>(idx)])) {
        fromPicker_ = false;
        openStory(idx);
    } else {
        if (!who.empty()) std::printf("[story] no playable story for '%s'\n", who.c_str());
        fromPicker_ = true;
        state_ = State::Pick;
    }
}

void StoryScreen::openStory(int idx) {
    cur_ = idx;
    state_ = State::Timeline;
    scroll_ = 0;
    if (cur_ >= 0) {
        const Entry& e = stories_[static_cast<size_t>(cur_)];
        node_ = nextNode(e);
        scroll_ = static_cast<float>(std::max(0, static_cast<int>(node_) - 4));
        if (std::getenv("ZDROID_STORY_AUTOPLAY")) pendingPlay_ = true;   // smoke tests: start the next node at once
    }
}

void StoryScreen::playNode(size_t i, Out& out) {
    if (cur_ < 0) return;
    const Entry& e = stories_[static_cast<size_t>(cur_)];
    if (i >= e.nodes.size()) {
        state_ = State::Timeline;
        return;
    }
    node_ = i;
    const StoryNode& n = e.nodes[i];
    switch (n.type) {
        case StoryNode::Type::Scene:
            if (startCutscene(n.file)) state_ = State::Cutscene;
            else nodeDone();   // script not in the mounted data: the story goes on
            break;
        case StoryNode::Type::Fight:
            out.kind = Out::Kind::Fight;
            out.fight = fightFor(e, n);
            fightPending_ = true;
            state_ = State::Timeline;   // drawn again when the fight comes back
            break;
        case StoryNode::Type::Credits:
            creditsMs_ = 0;
            credits_.begin(lang_, false, true);   // the roll, then the Thanks for playing card
            state_ = State::Credits;
            break;
        case StoryNode::Type::Other:
            nodeDone();
            break;
    }
}

void StoryScreen::nodeDone() {
    if (cur_ < 0) return;
    const Entry& e = stories_[static_cast<size_t>(cur_)];
    if (node_ >= e.nodes.size()) return;
    const StoryNode& n = e.nodes[node_];
    prog_.setStoryPos(e.key, n.pos);
    if (!n.progressionUnlock.empty()) {
        prog_.setFlag(n.progressionUnlock);   // "StoryFilia": the last fight of Filia's story was won
        if (zgKey(n.progressionUnlock) == "story" + e.key && !prog_.storyDone(e.key)) finishStory();
    }
    if (node_ + 1 < e.nodes.size()) {
        ++node_;
        pendingPlay_ = true;   // the story goes straight on to the next node
    } else {
        if (!prog_.storyDone(e.key)) finishStory();
        state_ = State::Timeline;
    }
}

void StoryScreen::finishStory() {
    if (cur_ < 0) return;
    const Entry& e = stories_[static_cast<size_t>(cur_)];
    prog_.setStoryDone(e.key);
    prog_.setFlag("Gallery" + e.charName + "_Story");
    notices_.push_back(tr("GalleryUnlocked", "You just unlocked some art in the art gallery!"));
    // Double's and Valentine's stories start locked (ProgressionInit.txt): finishing any other story opens them
    for (const char* who : {"Double", "Valentine"}) {
        const std::string k = zgKey(who);
        if (initLocked_.count(k) && !prog_.storyUnlocked(k) && e.key != k) {
            prog_.setFlag(std::string("Story") + who);
            notices_.push_back(tr(std::string(who) + "Unlocked", upperStr(who) + " STORY UNLOCKED!"));
        }
    }
    noticeMs_ = 0;
}

void StoryScreen::fightResult(int result) {
    if (!fightPending_) return;
    fightPending_ = false;
    state_ = State::Timeline;
    if (result > 0) {
        if (cur_ >= 0) prog_.addWin(stories_[static_cast<size_t>(cur_)].key);   // [earned] <Char>Wins
        nodeDone();
    }
}

bool StoryScreen::onBack() {
    switch (state_) {
        case State::Cutscene:
            endCutscene();
            state_ = State::Timeline;
            return true;
        case State::Credits:
            state_ = State::Timeline;
            return true;
        case State::Timeline:
            pendingPlay_ = false;
            if (fromPicker_) {
                state_ = State::Pick;
                return true;
            }
            return false;
        case State::Pick: return false;
    }
    return false;
}

// ============================================================================================ frame

void StoryScreen::ensureGfx(SDL_Renderer* r) {
    if (!gd_ || !r) return;
    if (gfxRen_ != r) {
        gfx_.attach(r, &gd_->vfs());
        gfxRen_ = r;
    }
}

Out StoryScreen::frame(Ui& ui, SDL_Renderer* r, int dtMs) {
    Out out;
    timeMs_ += std::max(0, dtMs);
    ensureGfx(r);
    if (!scanned_) scanStories();
    if (music_ && state_ != State::Credits) {   // (the credits roll chooses its own theme)
        // a cutscene plays its cue (silence before the first one / after MUSIC_STOP); the story map and picker play the menu theme
        const int idx = cueIdx_ >= 0 ? cueIdx_ : (state_ == State::Cutscene || !music_->lib() ? -1 : music_->lib()->menu());
        if (!music_->play(idx)) music_->stop();
    }
    if (pendingPlay_ && state_ != State::Cutscene) {
        pendingPlay_ = false;
        playNode(node_, out);
        if (out.kind != Out::Kind::None) return out;
    }
    switch (state_) {
        case State::Pick: drawPick(ui, out); break;
        case State::Timeline: drawTimeline(ui, out); break;
        case State::Credits: drawCredits(ui, r, dtMs); break;
        case State::Cutscene: {
            acc_ += std::clamp(dtMs, 0, 250);
            int n = 0;
            while (acc_ >= kTickMs && n < 8 && !cutDone_) {
                acc_ -= kTickMs;
                ++n;
                cutTick();
            }
            if (acc_ >= kTickMs) acc_ = 0;
            cutInput(ui);
            drawCutscene(ui);
            if (cutDone_) {
                endCutscene();
                state_ = State::Timeline;
                nodeDone();
            }
            break;
        }
    }
    if (state_ == State::Timeline && !notices_.empty()) {
        noticeMs_ += std::max(0, dtMs);
        drawNotice(ui);
    }
    return out;
}

void StoryScreen::drawCharArt(const std::string& key, int x, int y, int w, int h) {
    if (!gfxRen_) return;
    // the Character Select portrait of the game (Textures/charselect/charselect_portraits_<id>01.png), when it is mounted
    int tw = 0, th = 0;
    SDL_Texture* t = gfx_.texture("Textures/charselect/charselect_portraits_" + key + "01.png", &tw, &th);
    if (!t || tw <= 0 || th <= 0) return;
    int ow = 0, oh = 0;
    fitInto(tw, th, w * 2, h * 2, ow, oh);
    gfx_.setViewport(0, 0, 0.5f);
    gfx_.drawTexture(t, static_cast<float>(x * 2 + (w * 2 - ow) / 2), static_cast<float>(y * 2 + (h * 2 - oh)), static_cast<float>(ow),
                     static_cast<float>(oh));
}

namespace {
// "Beowulf  Post-Intro 400" (two spaces in the script) is the key "Beowulf Post-Intro 400" of the localisation
std::string collapseKey(const std::string& arg) {
    std::string o;
    bool sp = false;
    for (char ch : arg) {
        if (ch == ' ' || ch == '\t' || ch == '\r') {
            sp = !o.empty();
            continue;
        }
        if (sp) o += ' ';
        sp = false;
        o += ch;
    }
    return o;
}

// Umbrella's hat speaks in "{hn05}" tokens that no localisation file defines (the .exe expands them).  Placeholder noises, one per
// number, so the box does not show the raw token; the voice file (if the data has one) is looked up by the same token.
std::string hatNoise(const std::string& key) {
    static const char* const kNoise[13] = {"Grr!", "Hrrn...", "Mrrgh!", "Gnnn!", "Rraah!", "Hrmph.", "Nnngh!", "Grahh!", "Hnnng...", "Brrr!", "Mwah!", "Grrrah!", "Hungn!"};
    std::string out;
    size_t i = 0;
    while (i < key.size()) {
        if (key.compare(i, 3, "{hn") != 0) return {};
        const size_t close = key.find('}', i);
        if (close == std::string::npos) return {};
        const int n = std::atoi(key.substr(i + 3, close - i - 3).c_str());
        out += (out.empty() ? "" : " ") + std::string(kNoise[(n > 0 ? n - 1 : 0) % 13]);
        i = close + 1;
    }
    return out;
}

bool hasLetterOrDigit(const std::string& s) {
    for (char ch : s)
        if (std::isalnum(static_cast<unsigned char>(ch))) return true;
    return false;
}
}  // namespace

int StoryScreen::audit(SDL_Renderer* r, const std::string& lang) {
    if (!gd_) return 0;
    if (!lang.empty()) lang_ = lang;
    if (r) ensureGfx(r);
    if (!scanned_) scanStories();
    loadLocalization(lang_);
    const Vfs& vfs = gd_->vfs();
    std::string report;
    int problems = 0, scenes = 0, fights = 0, lines = 0, voiced = 0, nodes = 0;
    auto bad = [&](const std::string& who, const std::string& what) {
        ++problems;
        report += who + ": " + what + "\n";
    };
    std::string silent;   // text keys that found no voice file
    for (const Entry& e : stories_) {
        for (const StoryNode& n : e.nodes) {
            ++nodes;
            const std::string who = e.charName + " #" + std::to_string(n.pos) + " " + baseStem(n.file);
            if (n.type == StoryNode::Type::Fight) {
                ++fights;
                std::string txt;
                if (!vfs.readText(n.file, txt)) {
                    bad(who, "fight script not found");
                    continue;
                }
                const FightRequest f = fightFor(e, n);
                if (f.p2.empty()) bad(who, "no Fighter in the fight script");
                if (!f.world.empty() && f.level.empty()) bad(who, "World '" + f.world + "' is not a stage of stages.ini");
                continue;
            }
            if (n.type != StoryNode::Type::Scene) continue;
            ++scenes;
            std::string text;
            if (!vfs.readText(n.file, text)) {
                bad(who, "script " + n.file + " not found (the story skips it)");
                continue;
            }
            const CutScript cs = parseCutscene(text);
            const std::string sceneFile = (cs.scene.empty() ? std::string("CutsceneCommon") : cs.scene) + ".gbs";
            const GbsSceneView* view = nullptr;
            if (gfxRen_ || r) {
                if (!gfx_.loadScene(sceneFile)) bad(who, "scene " + sceneFile + " does not load");
                else if (!cs.view.empty()) {
                    view = gfx_.scene() ? gfx_.scene()->view(cs.view) : nullptr;
                    if (!view) bad(who, "view '" + cs.view + "' is not in " + sceneFile);
                }
            }
            const bool portraits = lowerStr(sceneFile) == "cutscenecommon.gbs";
            for (const CutCmd& c : cs.cmds) {
                const std::string at = who + ":" + std::to_string(c.line);
                const std::vector<std::string> w = splitWords(c.arg);
                if (c.key == "Anim" && !w.empty() && view) {
                    const std::string name = w[0] == "_LEGACY" ? (w.size() > 1 ? w[1] : "") : w[0] == "_PERSIST" ? w.back() : w[0];
                    if (!name.empty() && !view->anim(name)) bad(at, "animation '" + name + "' is not in view '" + cs.view + "'");
                } else if (c.key == "ShotTex" && w.size() >= 3) {
                    if (gfxRen_ && !gfx_.hasTexture(w[1] + "/" + w[2])) bad(at, "picture " + w[1] + "/" + w[2] + " not found");
                } else if (c.key == "Portrait" && portraits && w.size() >= 3 && w[1] != "_EMPTY") {
                    if (gfxRen_ && !gfx_.hasTexture("THs/" + w[1] + "/" + w[2])) bad(at, "portrait THs/" + w[1] + "/" + w[2] + " not found");
                } else if (c.key == "Text") {
                    ++lines;
                    const std::string key = collapseKey(c.arg);
                    if (loc_.trOr(key, "\x01") == "\x01" && key.compare(0, 5, "_RAW_") != 0 && hatNoise(key).empty() && hasLetterOrDigit(key)) bad(at, "text key '" + key + "' is not in the localisation");
                    std::string voice;
                    for (size_t j = 0; j < cs.cmds.size(); ++j)
                        if (&cs.cmds[j] == &c) {
                            for (size_t k = j + 1; k < cs.cmds.size() && (cs.cmds[k].key == "Header" || cs.cmds[k].key == "Portrait" || cs.cmds[k].key == "Voice" || cs.cmds[k].key == "AltVO"); ++k)
                                if (cs.cmds[k].key == "Voice") voice = trimStr(cs.cmds[k].arg);
                            break;
                        }
                    if (key.compare(0, 5, "_RAW_") == 0) continue;
                    if (!voiceFileFor(key, voice, lang_ == "jp" ? "jp" : "en").empty()) ++voiced;
                    else silent += at + "  " + key + "  (expects " + (storyVoiceFiles(key, lang_ == "jp" ? "jp" : "en").empty() ? std::string("-") : storyVoiceFiles(key, lang_ == "jp" ? "jp" : "en")[0]) + ")\n";
                }
            }
        }
    }
    gfx_.reset();
    gfxRen_ = nullptr;   // reattached on the next frame
    char sum[200];
    std::snprintf(sum, sizeof sum, "%zu stories, %d nodes (%d scenes, %d fights), %d lines: %d with voice, %d silent, %d problem(s)", stories_.size(), nodes,
                  scenes, fights, lines, voiced, lines - voiced, problems);
    std::printf("[audit] %s\n%s", sum, report.c_str());
    auditMsg_ = std::string(problems ? "CHECK: " : "CHECK OK: ") + std::to_string(problems) + " problem(s), " + std::to_string(voiced) + "/" + std::to_string(lines) + " voices";
    if (!prefsDir_.empty()) {
        if (std::FILE* f = std::fopen((prefsDir_ + "/story_audit.txt").c_str(), "wb")) {
            const std::string all = std::string(sum) + "\n\n== problems ==\n" + report + "\n== lines without a voice file ==\n" + silent;
            std::fwrite(all.data(), 1, all.size(), f);
            std::fclose(f);
        }
    }
    return problems;
}

void StoryScreen::drawPick(Ui& ui, Out& out) {
    ui.fill(Rect{0, 0, 640, 360}, kBg);
    ui.text(16, 12, upperStr(tr("Story Mode", "Story Mode")), kAccent, 2);
    if (stories_.empty()) {
        ui.text(16, 60, "No story found in the mounted data.", kWarn);
        ui.text(16, 78, "Story Mode reads core/arcade/story/*.stm and *.ini of your own copy", kDim);
        ui.text(16, 92, "(Home -> data folder).", kDim);
        if (ui.button(4100, Rect{16, 320, 120, 28}, "BACK")) out.kind = Out::Kind::Back;
        return;
    }
    const int cols = 4, bw = 148, bh = 30;
    for (size_t i = 0; i < stories_.size(); ++i) {
        const Entry& e = stories_[i];
        const int c = static_cast<int>(i) % cols, row = static_cast<int>(i) / cols;
        const Rect b{16 + c * (bw + 6), 46 + row * (bh + 6), bw, bh};
        if (b.y + b.h > 352) break;
        const bool locked = storyLocked(e);
        std::string label = upperStr(tr(e.charName, e.charName));
        if (locked) label += " (LOCKED)";
        else if (prog_.storyDone(e.key)) label += " *";
        if (ui.button(4000 + static_cast<int>(i), b, label, !locked, prog_.storyDone(e.key))) {
            fromPicker_ = true;
            openStory(static_cast<int>(i));
        }
    }
    ui.text(16, 344, "* = story completed", kDim);
    if (ui.button(4102, Rect{384, 8, 120, 26}, "CHECK")) audit(nullptr);   // every story, no playing: report in <prefs>/story_audit.txt
    if (!auditMsg_.empty()) ui.text(200, 344, Ui::fit(auditMsg_, 430), kWarn);
    if (ui.button(4101, Rect{512, 8, 120, 26}, "BACK")) out.kind = Out::Kind::Back;
}

void StoryScreen::drawTimeline(Ui& ui, Out& out) {
    ui.fill(Rect{0, 0, 640, 360}, kBg);
    if (cur_ < 0 || cur_ >= static_cast<int>(stories_.size())) {
        state_ = State::Pick;
        return;
    }
    const Entry& e = stories_[static_cast<size_t>(cur_)];
    ui.fill(Rect{12, 40, 180, 268}, kPanel);
    drawCharArt(e.key, 12, 40, 180, 268);
    ui.text(16, 12, upperStr(tr(e.charName, e.charName)) + " - " + upperStr(tr("Story Mode", "Story Mode")), kAccent, 2);
    const int done = prog_.storyPos(e.key);
    const std::string prog = prog_.storyDone(e.key) ? "COMPLETED" : "PROGRESS " + std::to_string(done) + " / " + std::to_string(e.nodes.empty() ? 0 : e.nodes.back().pos);
    ui.text(16, 314, prog, prog_.storyDone(e.key) ? kGood : kDim);

    // ---- node list (drag / wheel to scroll)
    const int rowH = 24, top = 40, visible = 11;
    const int n = static_cast<int>(e.nodes.size());
    const float maxScroll = static_cast<float>(std::max(0, n - visible));
    if (ui.in.down && Rect{200, top, 432, visible * rowH}.contains(ui.in.pmx, ui.in.pmy)) scroll_ -= static_cast<float>(ui.in.dragDy) / rowH;
    if (ui.in.wheel) scroll_ -= static_cast<float>(ui.in.wheel);
    scroll_ = std::clamp(scroll_, 0.0f, maxScroll);
    const int first = static_cast<int>(scroll_);
    const size_t next = nextNode(e);
    const bool dragging = ui.in.down && std::abs(ui.in.my - ui.in.pmy) > 8;
    for (int i = first; i < n && i < first + visible; ++i) {
        const size_t si = static_cast<size_t>(i);
        const Rect b{200, top + (i - first) * rowH, 432, rowH - 3};
        const bool open = nodeUnlocked(e, si);
        const bool seen = e.nodes[si].pos <= done;
        std::string label = si < e.labels.size() ? e.labels[si] : std::string("?");
        if (!open) label += "  (LOCKED)";
        else if (seen) label += "  OK";
        if (ui.button(4200 + i, b, label, open, si == next) && !dragging) {
            playNode(si, out);
            if (out.kind != Out::Kind::None) return;
        }
    }
    if (n > visible) {
        const int barH = visible * rowH - 3;
        const int knobH = std::max(16, barH * visible / n);
        const int knobY = top + static_cast<int>((barH - knobH) * (maxScroll > 0 ? scroll_ / maxScroll : 0));
        ui.fill(Rect{634, top, 4, barH}, kPanel);
        ui.fill(Rect{634, knobY, 4, knobH}, kLine);
    }

    // ---- actions
    const std::string go = (done == 0 ? "START" : prog_.storyDone(e.key) ? "PLAY AGAIN" : "CONTINUE");
    if (ui.button(4300, Rect{200, 318, 140, 30}, go, true, true) || ui.in.enter) {
        playNode(next, out);
        if (out.kind != Out::Kind::None) return;
    }
    if (ui.button(4301, Rect{348, 318, 140, 30}, "FROM THE START")) {
        playNode(0, out);
        if (out.kind != Out::Kind::None) return;
    }
    if (ui.button(4302, Rect{496, 318, 136, 30}, "BACK")) {
        if (fromPicker_) state_ = State::Pick;
        else out.kind = Out::Kind::Back;
    }
}

// The credits node of a story: the roll of Credits.gbs / credits_2021.ini, then ThanksForPlaying.gbs (credits.cpp).
void StoryScreen::drawCredits(Ui& ui, SDL_Renderer* r, int dtMs) {
    if (!credits_.frame(ui, r, dtMs)) {
        state_ = State::Timeline;
        nodeDone();
    }
}

void StoryScreen::drawNotice(Ui& ui) {
    if (notices_.empty()) return;
    const std::string& s = notices_.front();
    const std::vector<std::string> lines = wrapChars(s, 56);
    const int h = 16 + static_cast<int>(lines.size()) * 12;
    const Rect b{90, 140, 460, h};
    ui.fill(b, Col{20, 10, 24, 235});
    ui.outline(b, kAccent);
    for (size_t i = 0; i < lines.size(); ++i)
        ui.text(320 - Ui::textW(lines[i]) / 2, b.y + 8 + static_cast<int>(i) * 12, lines[i], kWarn);
    if (noticeMs_ > 2800 || (ui.in.released && noticeMs_ > 300)) {
        notices_.erase(notices_.begin());
        noticeMs_ = 0;
    }
}

// ============================================================================================ cutscene runner

bool StoryScreen::startCutscene(const std::string& file) {
    std::string text;
    if (!gd_ || !gd_->vfs().readText(file, text)) {
        std::printf("[story] cutscene %s not found - skipped\n", file.c_str());
        return false;
    }
    cut_ = parseCutscene(text);
    pc_ = 0;
    cutDone_ = false;
    wait_ = Wait::None;
    waitTicks_ = 0;
    waitAnim_.clear();
    shotTex_.clear();
    lastShot_.clear();
    portrait_[0].clear();
    portrait_[1].clear();
    header_[0].clear();
    header_[1].clear();
    speaker_ = -1;
    dlgOn_ = cineOn_ = false;
    text_.clear();
    textTicks_ = 0;
    legacyAnim_.clear();
    legacyCues_.clear();
    legacyNext_ = 0;
    legacyHeld_ = false;
    choices_.clear();
    acc_ = 0;

    shotScene_ = (cut_.scene.empty() ? std::string("CutsceneCommon") : cut_.scene) + ".gbs";
    dlgScene_ = "CutsceneCommon.gbs";
    shotView_ = dlgView_ = nullptr;
    shotSc_ = dlgSc_ = nullptr;
    if (gfxRen_) {
        if (gfx_.loadScene(shotScene_)) {
            shotSc_ = gfx_.scene();
            if (shotSc_ && !cut_.view.empty()) shotView_ = shotSc_->view(cut_.view);
        }
        if (gfx_.loadScene(dlgScene_)) {
            dlgSc_ = gfx_.scene();
            if (dlgSc_) {
                dlgView_ = dlgSc_->view("CutsceneGeneral2013");
                if (!dlgView_) dlgView_ = dlgSc_->view("CutsceneGeneral");
            }
        }
    }
    shotAnim_.bind(shotView_);
    dlgAnim_.bind(dlgView_);
    if (shotView_) shotAnim_.play("OnActivate");
    if (dlgView_) dlgAnim_.play("OnActivate");
    // the real dialogue box needs its frame pictures; without them the plain box is drawn instead
    dlgUi_ = dlgView_ && gfx_.hasTexture("Textures/CutsceneCommon/dialogue_frame01.png");
    // in the 2013 scripts a Portrait line names a THs picture; the 2012 <Char>ArcadeMode scenes name animations instead
    portraitsOk_ = lowerStr(shotScene_) == "cutscenecommon.gbs";
    std::printf("[story] %s: scene %s%s, view %s%s, %zu lines\n", file.c_str(), shotScene_.c_str(), shotSc_ ? "" : " (missing)",
                cut_.view.c_str(), shotView_ ? "" : " (missing)", cut_.cmds.size());
    cutRun();
    return true;
}

void StoryScreen::endCutscene() {
    voStop();
    cueIdx_ = -1;
    // the shot pictures are big: free everything this cutscene decoded (the scene file is read again next time)
    shotAnim_.bind(nullptr);
    dlgAnim_.bind(nullptr);
    shotView_ = dlgView_ = nullptr;
    shotSc_ = dlgSc_ = nullptr;
    gfx_.reset();
    cutDone_ = true;
    wait_ = Wait::None;
}

void StoryScreen::playCues(GbsAnimPlayer& p, const GbsScene* sc) {
    std::vector<GbsSoundCue> cues = p.takeCues();
    if (!audio_ || !gd_ || !sc) return;
    for (const GbsSoundCue& c : cues) {
        const std::string& nm = sc->soundName(c.soundId);
        if (nm.empty()) continue;
        auto it = sfx_.find(nm);
        if (it == sfx_.end()) {
            const int id = audio_->loadSfx(gd_->vfs(), nm);
            it = sfx_.emplace(nm, id >= 0 ? id : -2).first;
        }
        if (it->second >= 0) audio_->playSfx(it->second, c.volume);
    }
}

void StoryScreen::cutTick() {
    voTick();
    shotAnim_.tick();
    dlgAnim_.tick();
    playCues(shotAnim_, shotSc_);
    playCues(dlgAnim_, dlgSc_);
    // a _LEGACY animation stops at each of its message cues until the script says DIALOGUE_LEGACY_PLAY
    if (!legacyAnim_.empty() && !legacyHeld_ && legacyNext_ < legacyCues_.size() &&
        shotAnim_.tickOf(legacyAnim_) >= legacyCues_[legacyNext_]) {
        shotAnim_.hold(legacyAnim_, true);
        legacyHeld_ = true;
        ++legacyNext_;
    }
    switch (wait_) {
        case Wait::Anim:
            if (--waitTicks_ <= 0 || shotAnim_.finished(waitAnim_)) wait_ = Wait::None;
            break;
        case Wait::Legacy:
            if (legacyAnim_.empty() || legacyHeld_ || shotAnim_.finished(legacyAnim_)) wait_ = Wait::None;
            break;
        case Wait::Tap: ++textTicks_; break;
        case Wait::End:
            if (legacyAnim_.empty() || shotAnim_.finished(legacyAnim_) || --waitTicks_ <= 0) cutDone_ = true;
            break;
        case Wait::Choice:
        case Wait::None: break;
    }
    if (wait_ == Wait::None) cutRun();
}

void StoryScreen::cutRun() {
    int guard = 0;
    while (wait_ == Wait::None && !cutDone_ && guard++ < 4096) {
        if (pc_ >= cut_.cmds.size()) {
            if (!legacyAnim_.empty() && !shotAnim_.finished(legacyAnim_)) {
                releaseLegacy(true);   // the montage plays out to its end
                wait_ = Wait::End;
                waitTicks_ = 60 * 90;
            } else {
                cutDone_ = true;
            }
            break;
        }
        const CutCmd c = cut_.cmds[pc_++];
        cutExec(c);
    }
}

void StoryScreen::cutExec(const CutCmd& c) {
    const std::string& k = c.key;
    if (k == "ShotTex") {
        const std::vector<std::string> w = splitWords(c.arg);
        if (w.size() >= 3) {
            // story_art/Cinematics/<dir>/<name>: the Vfs finds it by its last two path parts (".dxt1" names are handled by GameUi)
            const std::string tex = w[1] + "/" + w[2];
            shotTex_[w[0]] = tex;
            lastShot_ = tex;
        }
    } else if (k == "Anim") {
        const std::vector<std::string> w = splitWords(c.arg);
        if (w.empty()) return;
        if (w[0] == "_LEGACY") {
            if (w.size() >= 2) startLegacy(w[1]);
            // the montage plays first: the dialogue (and its voice) only starts once it reaches its first message cue,
            // exactly like the game, instead of speaking over the opening shots from tick 0
            if (!legacyAnim_.empty() && !legacyCues_.empty() && !legacyHeld_) wait_ = Wait::Legacy;
        } else if (w[0] == "_PERSIST") {
            if (w.size() >= 2) shotAnim_.play(w.back());   // "_PERSIST <n> <name>": runs on, the script does not wait
        } else if (shotAnim_.view() && shotAnim_.play(w[0])) {
            const GbsAnim* a = shotAnim_.view()->anim(w[0]);
            if (a && !a->loops()) {
                wait_ = Wait::Anim;
                waitAnim_ = w[0];
                waitTicks_ = std::min(a->length, kMaxAnimWait);
            }
        }
    } else if (k == "Msg") {
        cutMsg(c.arg);
    } else if (k == "Text") {
        // the speaker's name tag and talking head are written after the line they belong to
        std::string voice;
        while (pc_ < cut_.cmds.size()) {
            const CutCmd& nx = cut_.cmds[pc_];
            if (nx.key == "Header") applyHeader(nx.arg);
            else if (nx.key == "Portrait") applyPortrait(nx.arg);
            else if (nx.key == "Voice") voice = trimStr(nx.arg);
            else if (nx.key != "AltVO") break;
            ++pc_;
        }
        const std::string key = collapseKey(c.arg);
        std::string shown = loc_.trOr(key, "\x01");
        if (shown == "\x01") {
            shown = hatNoise(key);
            if (shown.empty()) shown = key;
        }
        showText(shown);
        playVoice(key, voice);
    } else if (k == "Header") {
        applyHeader(c.arg);
    } else if (k == "Portrait") {
        applyPortrait(c.arg);
    } else if (k == "Choice") {
        choices_.clear();
        for (size_t j = pc_; j < cut_.cmds.size(); ++j)
            if (cut_.cmds[j].key == "Decision") {
                const std::string t = trimStr(cut_.cmds[j].arg);
                choices_.emplace_back(tr(t, t), j);
            }
        if (!choices_.empty()) wait_ = Wait::Choice;
    } else if (k == "Decision") {
        pc_ = cut_.cmds.size();   // the branch that was picked ends where the next one starts
    }
    // Scene / View / ResetAnim / AnimSpeed / Voice / AltVO / File / ChoiceIdle*: nothing to do here
}

void StoryScreen::cutMsg(const std::string& arg) {
    const std::vector<std::string> w = splitWords(arg);
    if (w.empty()) return;
    const std::string& m = w[0];
    if (m == "DIALOGUE_ON") {
        if (!dlgOn_) dlgPlay("DialogueIntro");
        dlgOn_ = true;
    } else if (m == "DIALOGUE_OFF") {
        if (dlgOn_) dlgPlay("DialogueOutro");
        dlgOn_ = false;
        header_[0].clear();
        header_[1].clear();
        speaker_ = -1;
    } else if (m == "DIALOGUE_CINE_ON") {
        if (!cineOn_) dlgPlay("CineDialogueIntro");
        cineOn_ = true;
    } else if (m == "DIALOGUE_CINE_OFF") {
        if (cineOn_) dlgPlay("CineDialogueOutro");
        cineOn_ = false;
        header_[0].clear();
        header_[1].clear();
        speaker_ = -1;
    } else if (m == "DIALOGUE_FADE_ALL") {
        if (dlgOn_) dlgPlay("DialogueOutro");
        if (cineOn_) dlgPlay("CineDialogueOutro");
        if (!portrait_[0].empty()) dlgPlay("DisableLeftCharacter1");
        if (!portrait_[1].empty()) dlgPlay("DisableRightCharacter1");
        dlgOn_ = cineOn_ = false;
        portrait_[0].clear();
        portrait_[1].clear();
        header_[0].clear();
        header_[1].clear();
        speaker_ = -1;
        text_.clear();
    } else if (m == "DIALOGUE_CLEAR") {
        text_.clear();
        header_[0].clear();
        header_[1].clear();
        speaker_ = -1;
    } else if (m == "DIALOGUE_LEGACY_PLAY") {
        if (!legacyAnim_.empty()) {
            releaseLegacy(false);
            wait_ = Wait::Legacy;
        }
    }
    else if (m.rfind("MUSIC_", 0) == 0) {   // MUSIC_24, MUSIC_NEWS, MUSIC_K, MUSIC_STOP ...: matched to the files by their names
        const std::string code = m.substr(6);
        if (code == "STOP" || code == "OFF") {
            cueIdx_ = -1;
        } else if (music_ && music_->lib()) {
            const int idx = music_->lib()->forCue(code);
            if (idx >= 0) {
                cueIdx_ = idx;
                std::printf("[music] story cue MUSIC_%s -> %s\n", code.c_str(), music_->lib()->entries()[static_cast<size_t>(idx)].loopStem.c_str());
            } else if (cueWarned_.insert(code).second) {
                std::printf("[music] story cue MUSIC_%s: no file named for it (looked for sg%s_*, sg_%s*, skull%s_*; themes on disk: %s) - the current music goes on\n",
                            code.c_str(), code.c_str(), code.c_str(), code.c_str(), music_->lib()->stems().c_str());
            }
        }
    }
    // P_END: nothing to do here
}

int StoryScreen::loadVoice(const std::vector<std::string>& candidates) {
    if (!audio_ || !gd_) return -1;
    for (const std::string& path : candidates) {
        auto it = voCache_.find(path);
        if (it == voCache_.end()) {
            int id = audio_->loadSfx(gd_->vfs(), path);
            if (id < 0) {   // a folder mounted straight at lang_en / story_vo: the Vfs matches by path suffix, so try the bare file name too
                const size_t slash = path.find_last_of('/');
                if (slash != std::string::npos) id = audio_->loadSfx(gd_->vfs(), path.substr(slash + 1));
            }
            it = voCache_.emplace(path, id >= 0 ? id : -2).first;
        }
        if (it->second >= 0) return it->second;
    }
    return -1;
}

namespace {
std::string alnumKey(const std::string& s) {
    std::string o;
    for (char ch : s)
        if (std::isalnum(static_cast<unsigned char>(ch))) o += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return o;
}
}  // namespace

std::string StoryScreen::findVoiceFuzzy(const std::string& textKey, int part, const std::string& lang, const std::string& explicitName) {
    if (!gd_) return {};
    if (!voIndexBuilt_) {
        voIndexBuilt_ = true;
        for (const std::string& f : gd_->vfs().files()) {
            const size_t dot = f.find_last_of('.');
            if (dot == std::string::npos || alnumKey(f.substr(dot + 1)) != "wav") continue;
            const size_t slash = f.find_last_of("/\\");
            const std::string stem = f.substr(slash == std::string::npos ? 0 : slash + 1, dot - (slash == std::string::npos ? 0 : slash + 1));
            voIndex_[alnumKey(stem)] = f;   // later mounts override earlier ones
        }
        std::printf("[story] voice index: %zu wav name(s)\n", voIndex_.size());
    }
    std::string base = explicitName.empty() ? textKey : explicitName;
    if (const size_t dot = base.find_last_of('.'); dot != std::string::npos) base.erase(dot);
    const std::string k = alnumKey(base);
    if (k.empty()) return {};
    const std::string tag = lang == "jp" ? "jp" : "en";
    const std::string n = part > 0 ? std::to_string(part) : std::string();
    for (const std::string& want : {k + n + tag, k + n}) {
        const auto it = voIndex_.find(want);
        if (it != voIndex_.end()) return it->second;
    }
    return {};
}

int StoryScreen::loadVoiceFuzzy(const std::string& textKey, int part, const std::string& lang, const std::string& explicitName) {
    if (!audio_) return -1;
    const std::string path = findVoiceFuzzy(textKey, part, lang, explicitName);
    return path.empty() ? -1 : loadVoice({path});
}

// The file a line would play (any of the ways playVoice looks), or "" when it has none.  Used by the audit.
std::string StoryScreen::voiceFileFor(const std::string& textKey, const std::string& explicitName, const std::string& lang) {
    if (!gd_) return {};
    const Vfs& vfs = gd_->vfs();
    std::vector<std::string> cands;
    if (!explicitName.empty()) {
        const bool hasExt = explicitName.find('.') != std::string::npos;
        const std::string dir = "story_vo/lang_" + lang + "/";
        cands.push_back(dir + explicitName + (hasExt ? "" : ".wav"));
        if (!hasExt) cands.push_back(dir + explicitName + (lang == "jp" ? "_JP.wav" : "_EN.wav"));
    } else {
        cands = storyVoiceFiles(textKey, lang, 0);
    }
    for (const std::string& p : cands)
        if (vfs.exists(p)) return p;
    for (int part = 1; part <= 3 && explicitName.empty(); ++part)
        for (const std::string& p : storyVoiceFiles(textKey, lang, part))
            if (vfs.exists(p)) return p;
    std::string f = findVoiceFuzzy(textKey, 0, lang, explicitName);
    if (f.empty() && explicitName.empty()) f = findVoiceFuzzy(textKey, 1, lang, explicitName);
    return f;
}

void StoryScreen::playVoice(const std::string& textKey, const std::string& explicitName) {
    voStop();
    if (!audio_ || !gd_) return;
    const std::string lang = lang_ == "jp" ? "jp" : "en";
    std::vector<std::string> cands;
    if (!explicitName.empty()) {   // "Voice: <name>": the script names the file itself
        const std::string dir = "story_vo/lang_" + lang + "/";
        const std::string tag = lang == "jp" ? "_JP.wav" : "_EN.wav";
        const bool hasExt = explicitName.find('.') != std::string::npos;
        cands.push_back(dir + explicitName + (hasExt ? "" : ".wav"));
        if (!hasExt) cands.push_back(dir + explicitName + tag);
    } else {
        cands = storyVoiceFiles(textKey, lang, 0);
    }
    if (cands.empty()) return;   // "_RAW_..." keys have no voice
    int id = loadVoice(cands);
    if (id < 0) id = loadVoiceFuzzy(textKey, 0, lang, explicitName);
    if (id >= 0) {
        audio_->playSfx(id);
        std::printf("[story] voice: %s\n", cands.front().c_str());
        return;
    }
    std::vector<int> parts;   // a Cine line spread over up to three files: ..._1_EN.wav, _2_, _3_
    for (int p = 1; p <= 3 && explicitName.empty(); ++p) {
        int pid = loadVoice(storyVoiceFiles(textKey, lang, p));
        if (pid < 0) pid = loadVoiceFuzzy(textKey, p, lang, std::string());
        if (pid < 0) break;
        parts.push_back(pid);
    }
    if (!parts.empty()) {
        audio_->playSfx(parts[0]);
        std::printf("[story] voice: %zu parts for '%s'\n", parts.size(), textKey.c_str());
        voQueue_.assign(parts.begin() + 1, parts.end());
        voWait_ = static_cast<int>(audio_->sfxSeconds(parts[0]) * 60.0f) + 6;
        return;
    }
    if (voMisses_ < 3) {
        ++voMisses_;
        std::printf("[story] no voice file for '%s' (looked for %s)\n", textKey.c_str(), cands.front().c_str());
    }
}

void StoryScreen::voTick() {
    if (voQueue_.empty() || !audio_) return;
    if (--voWait_ > 0) return;
    const int id = voQueue_.front();
    voQueue_.erase(voQueue_.begin());
    audio_->playSfx(id);
    voWait_ = static_cast<int>(audio_->sfxSeconds(id) * 60.0f) + 6;
}

void StoryScreen::showText(const std::string& text) {
    text_ = text;
    textTicks_ = 0;
    wait_ = Wait::Tap;
    if (!dlgOn_ && !cineOn_) {   // an old script that never switched the box on
        dlgOn_ = true;
        dlgPlay("DialogueIntro");
    }
    dlgPlay(cineOn_ ? "CineTextOn" : "TextOn");
}

void StoryScreen::applyHeader(const std::string& arg) {
    std::vector<std::string> w = splitWords(arg);
    if (w.empty()) return;
    const int side = lowerStr(w[0]) == "right" ? 1 : 0;
    std::string name;
    for (size_t i = 1; i < w.size(); ++i) name += (name.empty() ? "" : " ") + w[i];
    std::string shown = tr(name, name);
    for (char& ch : shown)
        if (ch == '_') ch = ' ';
    if (speaker_ != side) {
        const bool cine = cineOn_;
        dlgPlay(side == 0 ? (cine ? "CineEnableLeftHeader" : "EnableLeftHeader") : (cine ? "CineEnableRightHeader" : "EnableRightHeader"));
        if (speaker_ >= 0)
            dlgPlay(speaker_ == 0 ? (cine ? "CineDisableLeftHeader" : "DisableLeftHeader") : (cine ? "CineDisableRightHeader" : "DisableRightHeader"));
    }
    header_[side] = shown;
    header_[1 - side].clear();
    speaker_ = side;
}

void StoryScreen::applyPortrait(const std::string& arg) {
    if (!portraitsOk_) return;
    const std::vector<std::string> w = splitWords(arg);
    if (w.size() < 2) return;
    const int side = lowerStr(w[0]) == "right" ? 1 : 0;
    const bool had = !portrait_[side].empty();
    if (w[1] == "_EMPTY" || w.size() < 3) {
        portrait_[side].clear();
        if (had) dlgPlay(side == 0 ? "DisableLeftCharacter1" : "DisableRightCharacter1");
        return;
    }
    portrait_[side] = "THs/" + w[1] + "/" + w[2];   // story_art/THs/<dir>/<name>
    if (!had) dlgPlay(side == 0 ? "EnableLeftCharacter1" : "EnableRightCharacter1");
}

void StoryScreen::startLegacy(const std::string& name) {
    legacyAnim_.clear();
    legacyCues_.clear();
    legacyNext_ = 0;
    legacyHeld_ = false;
    if (!shotAnim_.view() || !shotAnim_.play(name)) return;
    legacyAnim_ = name;
    if (const GbsAnim* a = shotAnim_.view()->anim(name)) {
        for (const GbsSoundCue& m : a->messages)
            if (m.t > 0 && m.t < a->length - 1) legacyCues_.push_back(m.t);
        std::sort(legacyCues_.begin(), legacyCues_.end());
        legacyCues_.erase(std::unique(legacyCues_.begin(), legacyCues_.end()), legacyCues_.end());
    }
}

void StoryScreen::releaseLegacy(bool toEnd) {
    if (legacyAnim_.empty()) return;
    shotAnim_.hold(legacyAnim_, false);
    legacyHeld_ = false;
    if (toEnd) legacyNext_ = legacyCues_.size();
}

void StoryScreen::dlgPlay(const char* name) {
    if (dlgAnim_.view()) dlgAnim_.play(name);
}

std::string StoryScreen::revealed() const {
    // one code point per tick (60 per second)
    size_t i = 0;
    int n = 0;
    while (i < text_.size() && n < textTicks_) {
        Ui::nextCodepoint(text_, i);
        ++n;
    }
    return text_.substr(0, i);
}

bool StoryScreen::fullyRevealed() const { return textTicks_ >= Ui::glyphCount(text_); }

void StoryScreen::cutInput(Ui& ui) {
    const Rect skip{560, 6, 72, 20};
    const bool tap = (ui.in.released && !skip.contains(ui.in.mx, ui.in.my) && !skip.contains(ui.in.pmx, ui.in.pmy)) || ui.in.enter || ui.in.right;
    if (!tap) return;
    if (wait_ == Wait::Tap) {
        if (!fullyRevealed()) textTicks_ = 1 << 20;
        else {
            wait_ = Wait::None;
            cutRun();
        }
    } else if (wait_ == Wait::Anim) {
        waitTicks_ = 0;   // the next tick goes on
    }
}

void StoryScreen::drawCutscene(Ui& ui) {
    gfx_.setViewport(0, 0, 0.5f);
    gfx_.setJapanese(lang_ == "jp");
    gfx_.fillRect(0, 0, 1280, 720, Col{0, 0, 0, 255});
    bool drewShot = false;
    if (shotView_ && gfx_.loadScene(shotScene_)) {
        gfx_.setAnimPlayer(&shotAnim_);
        gfx_.drawView(*shotView_, [&](const GbsNode& n, NodeState& st) {
            if (n.type == GbsNode::Type::Label) {
                st.hide = true;   // editor helper texts
                return;
            }
            const auto it = shotTex_.find(n.name);
            if (it != shotTex_.end()) st.texName = it->second;
        });
        gfx_.setAnimPlayer(nullptr);
        drewShot = true;
    }
    if (!drewShot && !lastShot_.empty()) {   // no view to place the shots: the last picture, full screen
        int tw = 0, th = 0;
        if (SDL_Texture* t = gfx_.texture(lastShot_, &tw, &th)) {
            int ow = 0, oh = 0;
            fitInto(tw, th, 1280, 720, ow, oh);
            gfx_.drawTexture(t, (1280.0f - ow) / 2, (720.0f - oh) / 2, static_cast<float>(ow), static_cast<float>(oh));
        }
    }
    if (dlgUi_ && dlgView_ && gfx_.loadScene(dlgScene_)) {
        const bool dlgVis = dlgOn_ || dlgAnim_.running("DialogueOutro");
        const bool cineVis = cineOn_ || dlgAnim_.running("CineDialogueOutro");
        const std::string shown = revealed();
        const bool arrow = wait_ == Wait::Tap && fullyRevealed();
        gfx_.setAnimPlayer(&dlgAnim_);
        gfx_.drawView(*dlgView_, [&](const GbsNode& n, NodeState& st) {
            const std::string& nm = n.name;
            if (nameIs(nm, {"Background", "Character_Left_02", "Character_Right_02", "Character_Left_01_1280", "Character_Left_02_1280",
                            "Character_Right_01_1280", "Character_Right_02_1280", "ChoiceGroup", "CharactersGroup_1280"})) {
                st.hide = true;
                return;
            }
            if (nm == "Character_Left_01" || nm == "Character_Right_01") {
                const int s = nm == "Character_Left_01" ? 0 : 1;
                if (portrait_[s].empty()) st.hide = true;
                else st.texName = portrait_[s];
                return;
            }
            if (nameIs(nm, {"DialogueGroup", "Base"})) {
                st.hide = !dlgVis || cineOn_;
                if (nm == "DialogueGroup") st.dy = -kDialogueLift;   // children follow
                return;
            }
            if (nameIs(nm, {"Dialogue_Arrow", "Dialogue_Arrow_Down", "Dialogue_Arrow_Up"})) {
                st.hide = !(dlgVis && !cineOn_ && arrow);
                if (nm == "Dialogue_Arrow") st.dy = -kDialogueLift;
                return;
            }
            if (nameIs(nm, {"CineDialogueGroup", "CineBase"})) {
                st.hide = !cineVis;
                return;
            }
            if (nameIs(nm, {"LeftHeaderGroup", "LeftHeader"})) {
                st.hide = !(dlgVis && !cineOn_ && speaker_ == 0 && !header_[0].empty());
                return;
            }
            if (nameIs(nm, {"RightHeaderGroup", "RightHeader"})) {
                st.hide = !(dlgVis && !cineOn_ && speaker_ == 1 && !header_[1].empty());
                return;
            }
            if (nameIs(nm, {"CineLeftHeaderGroup", "CineLeftHeader"})) {
                st.hide = !(cineVis && speaker_ == 0 && !header_[0].empty());
                return;
            }
            if (nameIs(nm, {"CineRightHeaderGroup", "CineRightHeader"})) {
                st.hide = !(cineVis && speaker_ == 1 && !header_[1].empty());
                return;
            }
            if (n.type != GbsNode::Type::Label) return;
            std::string t;
            if (nm == "DialogueLabel" && dlgVis && !cineOn_) t = shown;
            else if (nm == "CineDialogueLabel" && cineVis) t = shown;
            else if ((nm == "LeftHeaderLabel" && !cineOn_) || (nm == "CineLeftHeaderLabel" && cineOn_)) t = header_[0];
            else if ((nm == "RightHeaderLabel" && !cineOn_) || (nm == "CineRightHeaderLabel" && cineOn_)) t = header_[1];
            if (t.empty()) {
                st.hide = true;
                return;
            }
            st.hasText = true;
            st.text = t;
            if (nm == "DialogueLabel") {
                st.dx = 125.0f;   // the box starts at x = 5 in the scene, left of the frame's inner edge: keep the text inside the frame
                // the whole line must fit the box: a long (Spanish, German...) text gets a smaller letter instead of running off the screen
                if (const GbsFont* f = gfx_.font(n.fontId)) {
                    float sc = 1.0f;
                    for (; sc > 0.5f; sc -= 0.05f)
                        if (static_cast<float>(gfx_.wrap(*f, text_, n.w, sc).size()) * f->lineHeight * sc <= n.h + 2.0f) break;
                    st.textScale = sc;
                }
            }
            if (nm == "CineDialogueLabel") {   // its box starts at x = -69 in the scene: centre the caption in the black bar instead
                st.alignOverride = 1;
                st.dx = 640.0f - (n.x + n.w / 2);
            }
        });
        gfx_.setAnimPlayer(nullptr);
    } else {
        drawFlatDialogue(ui);
    }

    // ---- choices (Choice: / Decision:)
    if (wait_ == Wait::Choice) {
        for (size_t i = 0; i < choices_.size(); ++i) {
            const Rect b{120, 120 + static_cast<int>(i) * 36, 400, 30};
            if (ui.button(4400 + static_cast<int>(i), b, choices_[i].first, true, i == 0)) {
                pc_ = choices_[i].second + 1;
                choices_.clear();
                wait_ = Wait::None;
                cutRun();
                break;
            }
        }
    }
    if (ui.button(4499, Rect{560, 6, 72, 20}, "SKIP")) cutDone_ = true;
}

void StoryScreen::drawFlatDialogue(Ui& ui) {
    // talking heads (canvas coordinates, viewport 0.5) when the pictures are mounted
    for (int s = 0; s < 2; ++s) {
        if (portrait_[s].empty()) continue;
        int tw = 0, th = 0;
        SDL_Texture* t = gfx_.texture(portrait_[s], &tw, &th);
        if (!t) continue;
        int ow = 0, oh = 0;
        fitInto(tw, th, 520, 600, ow, oh);
        const float x = s == 0 ? 20.0f : 1260.0f - ow;
        gfx_.drawTexture(t, x, 720.0f - oh, static_cast<float>(ow), static_cast<float>(oh));
    }
    if (text_.empty() || (!dlgOn_ && !cineOn_)) return;
    const Rect box{16, 266, 608, 86};
    ui.fill(box, Col{0, 0, 0, 205});
    ui.outline(box, cineOn_ ? kLine : kAccent);
    if (speaker_ >= 0 && !header_[speaker_].empty()) {
        const std::string& h = header_[speaker_];
        const int w = Ui::textW(h) + 16;
        const Rect tag{speaker_ == 0 ? 24 : 616 - w, 252, w, 16};
        ui.fill(tag, kAccent);
        ui.text(tag.x + 8, tag.y + 4, h, kText);
    }
    const std::vector<std::string> lines = wrapChars(revealed(), 72);
    for (size_t i = 0; i < lines.size() && i < 6; ++i) ui.text(box.x + 12, box.y + 10 + static_cast<int>(i) * 12, lines[i], kText);
    if (wait_ == Wait::Tap && fullyRevealed() && (timeMs_ / 400) % 2 == 0) ui.text(box.x + box.w - 20, box.y + box.h - 14, ">", kAccent);
}

}  // namespace story
}  // namespace zengine
