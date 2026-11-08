// gallery.cpp - see gallery.h
#include "gallery.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstdlib>

#include "dds.h"
#include "vfs.h"

namespace zengine {
namespace gallery {

namespace {

constexpr int kCols = 5, kRows = 3, kPerPage = kCols * kRows;
constexpr int kThumbMax = 256, kFullMax = 2048;

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

std::string stemOf(const std::string& path) {
    std::string s = path;
    const size_t slash = s.find_last_of("/\\");
    if (slash != std::string::npos) s = s.substr(slash + 1);
    const size_t dot = s.find('.');
    if (dot != std::string::npos) s.resize(dot);
    return s;
}

bool endsWith(const std::string& s, const std::string& e) { return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0; }

}  // namespace

void GalleryScreen::setGameData(const GameData* gd) {
    gd_ = gd;
    scanned_ = false;
    freeAll();
    items_.clear();
    groups_.clear();
    view_ = -1;
    bgTried_ = false;
    bgView_ = nullptr;
    bgAnim_.bind(nullptr);
    gfx_.reset();
    ren_ = nullptr;
}

void GalleryScreen::freeAll() {
    for (Tex& t : thumbs_)
        if (t.t) SDL_DestroyTexture(t.t);
    thumbs_.clear();
    if (full_.t) SDL_DestroyTexture(full_.t);
    full_ = Tex{};
    fullIdx_ = -1;
}

void GalleryScreen::begin(const std::string& lang) {
    lang_ = lang.empty() ? std::string("en") : lang;
    loc_.clear();
    if (gd_) {
        std::string json;
        if (gd_->vfs().readText(lang_ + "_loc.json", json)) loc_.parseJson(json);
        else if (gd_->vfs().readText("en_loc.json", json)) loc_.parseJson(json);
    }
    prog_.load(prefsDir_);
    all_ = zgOption("gallery_all");
    if (!scanned_) scan();
    view_ = -1;
    page_ = 0;
    juke_ = false;
    jukeIdx_ = -1;
    jukeList_.clear();
    if (group_ >= static_cast<int>(groups_.size())) group_ = 0;
    bgTried_ = false;
    if (const char* e = std::getenv("ZDROID_GALLERY_JUKE")) {   // smoke tests: open the jukebox (value = row to play)
        if (music_ && music_->count() > 0) {
            openJukebox();
            jukeSel_ = std::max(0, std::atoi(e));
            if (std::atoi(e) > 0) {
                jukeList_.clear();   // built on the first frame; the row is played then
                jukePlayOnOpen_ = std::atoi(e);
            }
        }
    }
    if (const char* e = std::getenv("ZDROID_GALLERY_VIEW")) {   // smoke tests: open the viewer on picture N
        const int n = std::atoi(e);
        if (n >= 0 && n < static_cast<int>(items_.size())) view_ = n;
    }
}

std::string GalleryScreen::resolvePic(const std::string& relIn) const {
    if (!gd_) return {};
    std::string rel = trimStr(relIn);
    for (char& c : rel)
        if (c == '\\') c = '/';
    if (rel.empty()) return {};
    std::vector<std::string> bases{rel, "ui_artgallery/Assets/" + rel, "ui_artgallery/" + rel};
    std::vector<std::string> cands;
    for (const std::string& b : bases) {
        cands.push_back(b);
        const std::string lb = lowerStr(b);
        const size_t dot = b.find_last_of('.');
        const size_t slash = b.find_last_of('/');
        const bool hasExt = dot != std::string::npos && (slash == std::string::npos || dot > slash);
        if (!hasExt) cands.push_back(b + ".dds");
        else if (!endsWith(lb, ".dds")) cands.push_back(b.substr(0, dot) + ".dds");
        if (lb.find(".dxt1") != std::string::npos || lb.find(".dxt5") != std::string::npos) {
            const size_t p = lb.find(".dxt");
            cands.push_back(b.substr(0, p) + ".dds");
        }
    }
    std::string real;
    for (const std::string& c : cands)
        if (gd_->vfs().resolve(c, &real)) return real;
    return {};
}

void GalleryScreen::scanIni(const std::string& text) {
    Item cur;
    bool any = false;
    auto flush = [&]() {
        if (any && cur.path.empty()) cur.path = cur.thumb;   // a cutscene entry has no picture of its own: its thumbnail is the picture
        if (any && (!cur.path.empty() || !cur.csPath.empty())) items_.push_back(cur);
        cur = Item{};
        any = false;
    };
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        if (const size_t h = line.find('#'); h != std::string::npos) line.erase(h);
        line = trimStr(line);
        if (line.empty()) {
            if (end >= text.size()) break;
            continue;
        }
        const size_t colon = line.find(':');
        if (colon == std::string::npos) {
            if (end >= text.size()) break;
            continue;
        }
        const std::string key = lowerStr(trimStr(line.substr(0, colon)));
        const std::string val = trimStr(line.substr(colon + 1));
        if (key == "name") {
            flush();
            cur.name = loc_.trOr(val, val);
            any = true;
        } else if (key == "description") {
            cur.desc = loc_.trOr(val, val);
            any = true;
        } else if (key == "artistname") {
            cur.artist = val;
        } else if (key == "tags") {
            const size_t comma = val.find_first_of(", ");
            cur.group = trimStr(comma == std::string::npos ? val : val.substr(0, comma));
        } else if (key == "unlockcriteria") {
            cur.criteria = val;
        } else if (key == "path") {
            cur.path = resolvePic(val);
            if (cur.path.empty()) std::printf("[gallery] picture not found: %s\n", val.c_str());
            if (cur.name.empty()) cur.name = stemOf(val);
            any = true;
        } else if (key == "cspath") {   // the cutscene's script, under arcade/story/ (the exe prefixes it); not a picture
            cur.csPath = val;
            any = true;
        } else if (key == "thumbnail") {   // ui_artgallery/Assets/<value>
            cur.thumb = resolvePic(val);
            any = true;
        } else if (key == "type") {        // "Texture" (a picture) or "Cutscene" (a story cutscene, shown with the movie frame)
            cur.cutscene = lowerStr(val) == "cutscene";
            any = true;
        }
        if (end >= text.size()) break;
    }
    flush();
}

void GalleryScreen::scanFolder(const std::string& marker, bool byChar) {
    if (!gd_) return;
    for (const std::string& real : gd_->vfs().files()) {
        const std::string np = normPath(real);
        if (!endsWith(np, ".dds")) continue;
        const size_t at = np.find(marker);
        if (at == std::string::npos) continue;
        if (np.find("textures/artgallery/") != std::string::npos) continue;   // the gallery's own frames and arrows
        Item it;
        it.path = real;
        it.name = stemOf(real);
        std::string rest = real.substr(at + marker.size());
        for (char& c : rest)
            if (c == '\\') c = '/';
        const size_t slash = rest.find('/');
        it.group = slash == std::string::npos ? std::string("Art") : rest.substr(0, slash);
        if (lowerStr(it.group) == "assets" && slash != std::string::npos) {   // ui_artgallery/Assets/<group>/...
            const std::string r2 = rest.substr(slash + 1);
            const size_t s2 = r2.find('/');
            it.group = s2 == std::string::npos ? std::string("Art") : r2.substr(0, s2);
        }
        if (lowerStr(it.name).find("_cs") != std::string::npos || lowerStr(it.name).find("thumb") != std::string::npos) continue;
        if (byChar) it.criteria = "Gallery" + it.group + "_Story";
        items_.push_back(std::move(it));
    }
    std::sort(items_.begin(), items_.end(), [](const Item& a, const Item& b) {
        return a.group != b.group ? a.group < b.group : a.name < b.name;
    });
}

void GalleryScreen::scan() {
    scanned_ = true;
    items_.clear();
    storyChars_.clear();
    freeAll();
    if (!gd_) return;
    for (const std::string& real : gd_->vfs().listByExt(".stm")) {
        std::string k = zgKey(stemOf(real));
        if (k.find("_cinematicsonly") == std::string::npos) storyChars_.insert(k);
    }
    std::string ini;
    if (gd_->vfs().readText("ui_artgallery/gallery.ini", ini)) scanIni(ini);
    if (items_.empty()) scanFolder("ui_artgallery/", false);
    if (items_.empty()) scanFolder("story_art/cinematics/", true);
    if (items_.empty()) scanFolder("story_art/", true);   // story_art/<Char>/*.dds
    buildGroups();
    std::printf("[gallery] %zu pictures in %zu groups\n", items_.size(), groups_.size());
}

void GalleryScreen::buildGroups() {
    groups_.clear();
    for (Item& it : items_) {
        if (it.group.empty()) it.group = "Art";
        if (std::find(groups_.begin(), groups_.end(), it.group) == groups_.end()) groups_.push_back(it.group);
    }
    thumbs_.assign(items_.size(), Tex{});
}

bool GalleryScreen::unlocked(const Item& it) const {
    if (all_) return true;   // GalleryTroll / zgirls_flags.txt "gallery_all"
    std::string c = zgKey(it.criteria);
    if (c.empty() || c == "none" || c == "0" || c == "always") return true;
    // The exe (gallery loader, 0x61d240) counts the finished stories and arcade runs over its 14-entry character table and compares:
    // OneStory / OneArcade = at least one, AllStory / AllArcade = 14.  Any other value is the name of a progression flag.
    constexpr int kAll = 14;
    if (c == "onestory") return prog_.storiesDone() >= 1;
    if (c == "onearcade") return prog_.arcadesDone() >= 1;
    if (c == "allstory") return prog_.storiesDone() >= kAll;
    if (c == "allarcade") return prog_.arcadesDone() >= kAll;
    if (prog_.flag(it.criteria)) return true;
    if (c.compare(0, 7, "gallery") == 0) c = c.substr(7);
    bool arcade = false;
    for (const char* suf : {"_story", "_arcade"})
        if (endsWith(c, suf)) {
            arcade = std::string(suf) == "_arcade";
            c.resize(c.size() - std::string(suf).size());
        }
    if (c.compare(0, 5, "story") == 0 && storyChars_.count(c.substr(5))) c = c.substr(5);
    if (c.compare(0, 6, "arcade") == 0 && storyChars_.count(c.substr(6))) {
        c = c.substr(6);
        arcade = true;
    }
    if (storyChars_.count(c)) return arcade ? prog_.arcadeDone(c) : prog_.storyDone(c);
    return true;   // a criterion this engine does not know: shown
}

std::vector<size_t> GalleryScreen::shown() const {
    std::vector<size_t> out;
    const std::string g = group_ >= 0 && group_ < static_cast<int>(groups_.size()) ? groups_[static_cast<size_t>(group_)] : std::string();
    for (size_t i = 0; i < items_.size(); ++i)
        if (g.empty() || items_[i].group == g) out.push_back(i);
    return out;
}

GalleryScreen::Tex GalleryScreen::load(SDL_Renderer* r, const std::string& realPath, int maxSize) {
    Tex t;
    t.tried = true;
    if (!r || realPath.empty()) return t;
    std::vector<uint8_t> bytes;
    if (!readFile(realPath, bytes)) return t;
    DdsImage img;
    std::string err;
    if (!decodeDds(bytes, img, &err, maxSize) || img.empty()) {
        std::printf("[gallery] %s: %s\n", realPath.c_str(), err.c_str());
        return t;
    }
    t.t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, img.width, img.height);
    if (!t.t) return t;
    SDL_UpdateTexture(t.t, nullptr, img.rgba.data(), img.width * 4);
    SDL_SetTextureBlendMode(t.t, SDL_BLENDMODE_BLEND);
    t.w = img.width;
    t.h = img.height;
    return t;
}

bool GalleryScreen::onBack() {
    if (juke_) {
        juke_ = false;
        return true;
    }
    if (view_ >= 0) {
        view_ = -1;
        return true;
    }
    return false;
}


namespace {

void nodeXY(const GbsAnimPlayer& a, const GbsNode& n, float& x, float& y) {
    if (const GbsPose* p = a.pose(n.id)) {
        x = p->x;
        y = p->y;
    } else {
        x = n.x;
        y = n.y;
    }
}

// where the highlight sits relative to the frame group it belongs to (from the view's resting poses)
constexpr float kSelOffX = -4, kSelOffY = -6;
constexpr float kCellX = 17, kCellY = 13, kCellW = 193, kCellH = 134;   // the thumbnail's box inside a frame group

std::string fmtCount(std::string s, int a, int b) {
    auto rep = [&](const std::string& k, const std::string& v) {
        for (size_t p = s.find(k); p != std::string::npos; p = s.find(k, p + v.size())) s.replace(p, k.size(), v);
    };
    rep("{1}", std::to_string(a));
    rep("{2}", std::to_string(b));
    return s;
}

}  // namespace

void GalleryScreen::bindViews(SDL_Renderer* r) {
    bgTried_ = true;
    bgView_ = gView_ = vView_ = nullptr;
    framesNode_ = nullptr;
    role_.clear();
    for (CellNodes& c : cells_) c = CellNodes{};
    pendGroup_ = -1;
    viewerOn_ = false;
    selInit_ = false;
    bgAnim_.bind(nullptr);
    gAnim_.bind(nullptr);
    vAnim_.bind(nullptr);
    if (!gd_ || !r) return;
    if (!gfxAttached_) {
        gfx_.attach(r, &gd_->vfs());
        gfxAttached_ = true;
    }
    if (!gfx_.loadScene("ArtGallery.gbs") || !gfx_.scene()) return;
    const GbsScene* sc = gfx_.scene();
    bgView_ = sc->view("Posterpile_BG");
    gView_ = sc->view("ArtGallery");
    vView_ = sc->view("ArtGallery_Viewer");
    bgAnim_.bind(bgView_);
    if (bgView_) {
        bgAnim_.play("OnActivate");
        bgAnim_.play("Filmstrips_On");
        bgAnim_.play("Posters_In");
    }
    gAnim_.bind(gView_);
    vAnim_.bind(vView_);
    if (!gView_) return;
    gAnim_.play("OnActivate");
    gAnim_.play("ArtGallery_In");
    gAnim_.play("Art_Selector_Animate");
    framesNode_ = gView_->find("Art_Frames");
    if (!framesNode_) return;
    for (const GbsNode& col : framesNode_->kids) {
        for (const GbsNode& grp : col.kids) {
            if (grp.name.size() < 9 || grp.name[0] != 'A') continue;   // "A<n>_Frame_GRP"
            const int n = std::atoi(grp.name.c_str() + 1);
            if (n < 1 || n > 20) continue;
            cells_[n - 1] = CellNodes{&col, &grp};
            role_[grp.id] = {n - 1, 'G'};
            for (const GbsNode& k : grp.kids)
                if (endsWith(k.name, "_Thumbnail")) role_[k.id] = {n - 1, 'T'};
        }
    }
}

void GalleryScreen::stepAnims(int dtMs) {
    bgAcc_ += std::clamp(dtMs, 0, 250);
    int n = 0;
    while (bgAcc_ >= 1000.0 / 60.0 && n++ < 8) {
        bgAcc_ -= 1000.0 / 60.0;
        if (bgView_) bgAnim_.tick();
        if (gView_) gAnim_.tick();
        if (vView_ && viewerOn_) vAnim_.tick();
    }
    if (bgAcc_ >= 1000.0 / 60.0) bgAcc_ = 0;
    bgAnim_.takeCues();
    gAnim_.takeCues();
    vAnim_.takeCues();
    if (pendGroup_ >= 0 && (!gView_ || gAnim_.finished("Thumbnails_Out"))) {   // the page is faded out: switch, fade in
        group_ = pendGroup_;
        page_ = pendPage_;
        sel_ = pendSel_;
        pendGroup_ = -1;
        if (gView_) gAnim_.play("Thumbnails_In");
    }
}

void GalleryScreen::startPage(int group, int page, int sel) {
    if (pendGroup_ >= 0) return;
    pendGroup_ = group;
    pendPage_ = page;
    pendSel_ = sel;
    if (!gView_ || !gAnim_.play("Thumbnails_Out")) {   // no animation to wait for
        group_ = group;
        page_ = page;
        sel_ = sel;
        pendGroup_ = -1;
    }
}

bool GalleryScreen::cellOrigin(int cell, float& x, float& y) const {
    if (cell < 0 || cell >= 20 || !framesNode_ || !cells_[cell].col || !cells_[cell].grp) return false;
    float fx, fy, cx, cy, gx, gy;
    nodeXY(gAnim_, *framesNode_, fx, fy);
    nodeXY(gAnim_, *cells_[cell].col, cx, cy);
    nodeXY(gAnim_, *cells_[cell].grp, gx, gy);
    x = fx + cx + gx;
    y = fy + cy + gy;
    return true;
}

void GalleryScreen::drawBackdrop() {
    if (!bgView_ || !gfx_.loadScene("ArtGallery.gbs")) return;
    gfx_.setViewport(0, 0, 0.5f);
    gfx_.setAnimPlayer(&bgAnim_);
    gfx_.drawView(*bgView_, [](const GbsNode& node, NodeState& st) {
        if (node.type == GbsNode::Type::Label) st.hide = true;
    });
    gfx_.setAnimPlayer(nullptr);
}

bool GalleryScreen::frame(Ui& ui, SDL_Renderer* r, int dtMs) {
    if (!scanned_) scan();
    if (ren_ != r) {   // new renderer: textures of the old one are gone
        freeAll();
        thumbs_.assign(items_.size(), Tex{});
        ren_ = r;
        gfxAttached_ = false;
        bgTried_ = false;
    }
    if (!bgTried_) bindViews(r);
    if (music_) {   // the gallery's theme, or what the jukebox picked
        const MusicLibrary* l = music_->lib();
        const bool ok = jukeIdx_ == -2 ? false : music_->play(jukeIdx_ >= 0 ? jukeIdx_ : (l ? l->gallery() : -1));
        if (!ok) music_->stop();
    }
    ui.fill(Rect{0, 0, 640, 360}, kBg);
    stepAnims(dtMs);
    drawBackdrop();
    if (juke_) {
        bool leaveJ = false;
        drawJukebox(ui, r, leaveJ);
        if (leaveJ) juke_ = false;
        return true;
    }
    if (view_ >= 0 && view_ < static_cast<int>(items_.size())) {
        if (!viewerOn_) {   // just opened: slide the viewer in
            viewerOn_ = true;
            if (vView_) {
                vAnim_.clear();
                for (const char* a : {"OnActivate", "BG_On", "Infobar_On", "Desc_On"}) vAnim_.play(a);
            }
        }
        drawViewer(ui, r);
        return true;
    }
    viewerOn_ = false;
    bool leave = false;
    drawGrid(ui, r, leave);
    return !leave;
}

void GalleryScreen::drawGrid(Ui& ui, SDL_Renderer* r, bool& leave) {
    if (!gView_ || !framesNode_) {
        drawGridPlain(ui, r, leave);
        return;
    }
    const std::vector<size_t> list = shown();
    const int total = static_cast<int>(list.size());
    const int pages = std::max(1, (total + kPerPage - 1) / kPerPage);
    page_ = std::clamp(page_, 0, pages - 1);
    const int onPage = std::clamp(total - page_ * kPerPage, 0, kPerPage);
    sel_ = onPage > 0 ? std::clamp(sel_, 0, onPage - 1) : 0;
    const int nGroups = static_cast<int>(groups_.size());
    const bool busy = pendGroup_ >= 0 || gAnim_.running("ArtGallery_In") || gAnim_.running("Thumbnails_In");

    // two thumbnails decoded per frame: no stall when a page opens
    int loads = 0;
    for (int k = 0; k < onPage; ++k) {
        const size_t idx = list[static_cast<size_t>(page_ * kPerPage + k)];
        if (!unlocked(items_[idx])) continue;
        Tex& t = thumbs_[idx];
        if (!t.tried && loads < 2) {
            ++loads;
            t = load(r, items_[idx].thumb.empty() ? items_[idx].path : items_[idx].thumb, kThumbMax);
        }
    }

    // ---- input
    bool swiped = false;
    if (ui.in.pressed) {
        swipeX_ = ui.in.mx;
        swiping_ = true;
    }
    if (ui.in.released && swiping_) {
        swiping_ = false;
        const int dx = ui.in.mx - swipeX_;
        if (!busy && dx < -60 && page_ + 1 < pages) startPage(group_, page_ + 1, std::min(sel_, 0) + (sel_ / kCols) * kCols), swiped = true;
        else if (!busy && dx > 60 && page_ > 0) startPage(group_, page_ - 1, (sel_ / kCols) * kCols), swiped = true;
    }
    auto changeGroup = [&](int d) {
        if (nGroups > 1) startPage((group_ + d + nGroups) % nGroups, 0, 0);
    };
    auto openItem = [&](size_t idx) {
        view_ = static_cast<int>(idx);
        fullIdx_ = -1;
    };
    if (!busy) {
        if (ui.in.left) {
            if (sel_ % kCols > 0) --sel_;
            else if (page_ > 0) startPage(group_, page_ - 1, (sel_ / kCols) * kCols + kCols - 1);
        }
        if (ui.in.right) {
            if (sel_ % kCols < kCols - 1 && sel_ + 1 < onPage) ++sel_;
            else if (page_ + 1 < pages) startPage(group_, page_ + 1, (sel_ / kCols) * kCols);
        }
        if (ui.in.keyUp) {
            if (sel_ >= kCols) sel_ -= kCols;
            else changeGroup(-1);
        }
        if (ui.in.keyDown) {
            if (sel_ + kCols < onPage) sel_ += kCols;
            else changeGroup(+1);
        }
        if (ui.in.enter && onPage > 0) {
            const size_t idx = list[static_cast<size_t>(page_ * kPerPage + sel_)];
            if (unlocked(items_[idx])) openItem(idx);
        }
    }
    // group arrows: the header's left / right thirds
    bool leftHot = false, rightHot = false;
    if (const GbsNode* tg = gView_->find("Art_Type_GRP"); tg && nGroups > 1) {
        float tx, ty;
        nodeXY(gAnim_, *tg, tx, ty);
        const Rect left{static_cast<int>((tx + 44) * 0.5f), static_cast<int>((ty + 40) * 0.5f), static_cast<int>(617 * 0.34f * 0.5f), 21};
        const Rect right{static_cast<int>((tx + 44 + 617 * 0.66f) * 0.5f), left.y, left.w, 21};
        const bool down = ui.in.down && !ui.in.touchPointer ? true : ui.in.down;
        leftHot = down && left.contains(ui.in.pmx, ui.in.pmy) && left.contains(ui.in.mx, ui.in.my);
        rightHot = down && right.contains(ui.in.pmx, ui.in.pmy) && right.contains(ui.in.mx, ui.in.my);
        if (!busy && ui.in.released && !swiped) {
            if (left.contains(ui.in.mx, ui.in.my) && left.contains(ui.in.pmx, ui.in.pmy)) changeGroup(-1);
            else if (right.contains(ui.in.mx, ui.in.my) && right.contains(ui.in.pmx, ui.in.pmy)) changeGroup(+1);
        }
    }
    // thumbnails: hover moves the highlight (mouse only), a tap opens
    const bool moved = ui.in.mx != lmx_ || ui.in.my != lmy_;
    lmx_ = ui.in.mx;
    lmy_ = ui.in.my;
    for (int k = 0; k < onPage && !busy; ++k) {
        float ox, oy;
        if (!cellOrigin(k, ox, oy)) continue;
        const Rect cell{static_cast<int>((ox + kCellX) * 0.5f), static_cast<int>((oy + kCellY) * 0.5f), static_cast<int>(kCellW * 0.5f),
                        static_cast<int>(kCellH * 0.5f)};
        if (moved && !ui.in.touchPointer && cell.contains(ui.in.mx, ui.in.my)) sel_ = k;
        if (!swiped && ui.in.released && cell.contains(ui.in.mx, ui.in.my) && cell.contains(ui.in.pmx, ui.in.pmy)) {
            sel_ = k;
            const size_t idx = list[static_cast<size_t>(page_ * kPerPage + k)];
            if (unlocked(items_[idx])) openItem(idx);
        }
    }

    // ---- the highlight glides to the selected cell
    float tgtX = 0, tgtY = 0;
    const bool haveSel = onPage > 0 && cellOrigin(sel_, tgtX, tgtY);
    tgtX += kSelOffX;
    tgtY += kSelOffY;
    if (!selInit_ || !haveSel) {
        selX_ = tgtX;
        selY_ = tgtY;
        selInit_ = haveSel;
    } else {
        selX_ += (tgtX - selX_) * 0.4f;
        selY_ += (tgtY - selY_) * 0.4f;
    }

    int open = 0;
    for (const Item& it : items_) open += unlocked(it) ? 1 : 0;
    const std::string gname = group_ >= 0 && group_ < nGroups ? groups_[static_cast<size_t>(group_)] : std::string();
    std::string selName, selSub;
    if (onPage > 0) {
        const Item& si = items_[list[static_cast<size_t>(page_ * kPerPage + sel_)]];
        if (unlocked(si)) {
            selName = si.name;
            selSub = si.artist.empty() ? si.desc : si.artist;
        } else {
            selName = loc_.trOr("Locked!", "Locked!");
        }
    }
    const std::string header = loc_.trOr("Art Gallery", "Art Gallery");
    const std::string counter = fmtCount(loc_.trOr("COMPLETE: {1}/{2}", "COMPLETE: {1}/{2}"), open, static_cast<int>(items_.size()));
    const float frac = pages > 1 ? static_cast<float>(page_) / static_cast<float>(pages - 1) : 0.0f;

    gfx_.loadScene("ArtGallery.gbs");   // fonts and textures are looked up in the current scene
    gfx_.setViewport(0, 0, 0.5f);
    gfx_.setAnimPlayer(&gAnim_);
    gfx_.drawView(*gView_, [&](const GbsNode& n, NodeState& st) {
        if (const auto it = role_.find(n.id); it != role_.end()) {
            const int cell = it->second.first;
            const size_t li = static_cast<size_t>(page_ * kPerPage + cell);
            if (it->second.second == 'G') {
                if (cell >= kPerPage || li >= list.size()) st.hide = true;   // empty slot (and the spare row)
            } else if (it->second.second == 'T' && li < list.size()) {
                const size_t idx = list[li];
                if (!unlocked(items_[idx])) {
                    st.texName = "Textures/ArtGallery/thumbnail_question.png";
                } else if (thumbs_[idx].t) {
                    st.rawTex = thumbs_[idx].t;
                    st.rawW = thumbs_[idx].w;
                    st.rawH = thumbs_[idx].h;
                }
            }
            return;
        }
        const std::string& nm = n.name;
        if (n.type == GbsNode::Type::Label) {
            if (nm == "Header_TXT") st.hasText = true, st.text = header;
            else if (nm == "Top_Right_Data_TXT") st.hasText = true, st.text = counter;
            else if (nm == "Art_Desc_TXT") st.hasText = true, st.text = gname;
            return;
        }
        if (nm == "Info_Bar") {   // the scene parks it mid-screen; the ribbon goes under the last row of frames
            st.dx = 16 - n.x;
            st.dy = 618 - n.y;
        }
        if (nm == "Art_Selector_GRP") {
            if (!haveSel || pendGroup_ >= 0) {
                st.hide = true;
                return;
            }
            float cx, cy;
            nodeXY(gAnim_, n, cx, cy);
            st.dx = selX_ - cx;
            st.dy = selY_ - cy;
        } else if (nm == "Scrollbar_GRP") {
            if (pages < 2) st.hide = true;
        } else if (nm == "Scrollknob") {
            st.dy = frac * 440.0f;
        } else if (nm == "Left_Arrow_On" || nm == "Right_Arrow_On") {
            if (!(nm[0] == 'L' ? leftHot : rightHot)) st.hide = true;
        } else if (nm == "Left_Arrow_Off" || nm == "Right_Arrow_Off") {
            if (nGroups < 2 || (nm[0] == 'L' ? leftHot : rightHot)) st.hide = true;
        }
    });
    gfx_.setAnimPlayer(nullptr);
    // the ribbon's own font ("Dialogue Text") has no Latin atlas in every dump: the picture's name is set in "Online Name"
    if (!gAnim_.running("ArtGallery_In") && pendGroup_ < 0 && !selName.empty()) {
        if (const GbsFont* f = gfx_.fontNamed("Online Name"))
            gfx_.drawLabelText(*f, selName + (selSub.empty() ? std::string() : "  -  " + selSub), 40, 618, 1200, 44, 0, true,
                               Col{255, 255, 255, 255}, 0.62f, 1.0f);
    }

    // ---- touch controls that the scene does not have
    if (ui.button(5000, Rect{8, 341, 70, 17}, "BACK")) leave = true;
    if (music_ && music_->count() > 0 && ui.button(5001, Rect{84, 341, 70, 17}, "MUSIC")) openJukebox();
    if (pages > 1) {
        if (ui.button(5200, Rect{490, 341, 64, 17}, "<", page_ > 0 && !busy)) startPage(group_, page_ - 1, sel_);
        const std::string pg = std::to_string(page_ + 1) + "/" + std::to_string(pages);
        ui.text(560 + (24 - Ui::textW(pg)) / 2, 345, pg, kDim);
        if (ui.button(5201, Rect{588, 341, 44, 17}, ">", page_ + 1 < pages && !busy)) startPage(group_, page_ + 1, sel_);
    }
    if (items_.empty()) {
        ui.text(16, 60, "No gallery pictures found in the mounted data.", kWarn);
        ui.text(16, 76, "Looked for: ui_artgallery/gallery.ini, ui_artgallery/*.dds, story_art/<Char>/*.dds", kDim);
    }
}

// ---------------------------------------------------------------------------------------- jukebox

void GalleryScreen::openJukebox() {
    juke_ = true;
    jukeList_.clear();
    jukeSel_ = jukeTop_ = 0;
    jukeMsg_.clear();
    jukeMsgT_ = 0;
}

void GalleryScreen::playJuke(int entry) {
    const MusicEntry* e = music_ ? music_->entry(entry) : nullptr;
    if (!e) return;
    if (!e->present()) {
        jukeMsg_ = "No file for \"" + e->loopStem + ".wav\" in the mounted data";
        jukeMsgT_ = 150;
        return;
    }
    jukeIdx_ = entry;
    jukeMsg_.clear();
}

namespace {
constexpr int kJukeRows = 10;
constexpr float kJukeRowH = 48, kJukeX = 70, kJukeY = 124, kJukeW = 720;
}  // namespace

void GalleryScreen::drawJukebox(Ui& ui, SDL_Renderer* r, bool& leave) {
    (void)r;
    ++jukeT_;
    if (jukeMsgT_ > 0) --jukeMsgT_;
    const MusicLibrary* lib = music_ ? music_->lib() : nullptr;
    if (jukeList_.empty() && lib) {   // the themes with files first, in the catalogue's order
        for (int pass = 0; pass < 2; ++pass)
            for (size_t i = 0; i < lib->entries().size(); ++i)
                if (lib->entries()[i].present() == (pass == 0)) jukeList_.push_back(static_cast<int>(i));
    }
    const int n = static_cast<int>(jukeList_.size());
    if (jukePlayOnOpen_ > 0 && n > 0) {   // smoke tests: ZDROID_GALLERY_JUKE=<row>
        jukeSel_ = std::min(jukePlayOnOpen_, n - 1);
        playJuke(jukeList_[static_cast<size_t>(jukeSel_)]);
        jukePlayOnOpen_ = 0;
    }
    auto entryAt = [&](int row) -> const MusicEntry* { return row >= 0 && row < n ? music_->entry(jukeList_[static_cast<size_t>(row)]) : nullptr; };
    const int playing = jukeIdx_ >= 0 ? jukeIdx_ : (jukeIdx_ == -1 && lib ? lib->gallery() : -1);

    // ---- input
    if (n > 0) {
        if (ui.in.keyUp) jukeSel_ = (jukeSel_ + n - 1) % n;
        if (ui.in.keyDown) jukeSel_ = (jukeSel_ + 1) % n;
        auto step = [&](int d) {   // previous / next theme that has a file
            for (int k = 1; k <= n; ++k) {
                const int row = ((jukeSel_ + d * k) % n + n) % n;
                if (entryAt(row) && entryAt(row)->present()) {
                    jukeSel_ = row;
                    playJuke(jukeList_[static_cast<size_t>(row)]);
                    return;
                }
            }
        };
        if (ui.in.left) step(-1);
        if (ui.in.right) step(+1);
        if (ui.in.enter) playJuke(jukeList_[static_cast<size_t>(jukeSel_)]);
        // rows: a tap plays, a vertical drag scrolls
        if (ui.in.pressed) {
            jukeDragY_ = ui.in.my;
            jukeDragging_ = true;
        }
        if (ui.in.released && jukeDragging_) {
            jukeDragging_ = false;
            const int dy = ui.in.my - jukeDragY_;
            if (std::abs(dy) > 14) {
                jukeTop_ = std::clamp(jukeTop_ + (dy < 0 ? 3 : -3), 0, std::max(0, n - kJukeRows));
            } else {
                for (int k = 0; k < kJukeRows; ++k) {
                    const int row = jukeTop_ + k;
                    if (row >= n) break;
                    const Rect cell{static_cast<int>(kJukeX * 0.5f), static_cast<int>((kJukeY + kJukeRowH * static_cast<float>(k)) * 0.5f),
                                    static_cast<int>(kJukeW * 0.5f), static_cast<int>(kJukeRowH * 0.5f)};
                    if (cell.contains(ui.in.mx, ui.in.my)) {
                        jukeSel_ = row;
                        playJuke(jukeList_[static_cast<size_t>(row)]);
                    }
                }
            }
        }
        jukeSel_ = std::clamp(jukeSel_, 0, n - 1);
        if (jukeSel_ < jukeTop_) jukeTop_ = jukeSel_;
        if (jukeSel_ >= jukeTop_ + kJukeRows) jukeTop_ = jukeSel_ - kJukeRows + 1;
        jukeTop_ = std::clamp(jukeTop_, 0, std::max(0, n - kJukeRows));
    }

    // ---- drawing (1280 x 720 canvas, the gallery's own fonts)
    gfx_.loadScene("ArtGallery.gbs");
    gfx_.setViewport(0, 0, 0.5f);
    const GbsFont* head = gfx_.fontNamed("Online Header");
    const GbsFont* name = gfx_.fontNamed("Online Name");
    const Col gold{199, 153, 113, 255}, white{240, 240, 240, 255}, dim{150, 150, 160, 255};
    gfx_.fillRect(kJukeX - 14, kJukeY - 12, kJukeW + 28, kJukeRowH * kJukeRows + 24, Col{0, 0, 0, 165});
    gfx_.fillRect(kJukeX - 14, kJukeY - 12, kJukeW + 28, 2, gold);
    gfx_.fillRect(kJukeX - 14, kJukeY + kJukeRowH * kJukeRows + 10, kJukeW + 28, 2, gold);
    if (head) gfx_.drawLine(*head, "MUSIC", 76, 34, white, 0.9f);
    if (name) {
        const std::string cnt = std::to_string(lib ? lib->presentCount() : 0) + " / " + std::to_string(n) + " TRACKS";
        gfx_.drawLabelText(*name, cnt, 780, 52, 430, 40, 2, true, gold, 0.55f, 1.0f);
    }
    for (int k = 0; k < kJukeRows; ++k) {
        const int row = jukeTop_ + k;
        const MusicEntry* e = entryAt(row);
        if (!e) break;
        const float y = kJukeY + kJukeRowH * static_cast<float>(k);
        const bool sel = row == jukeSel_, isPlaying = jukeList_[static_cast<size_t>(row)] == playing;
        if (sel) {
            gfx_.fillRect(kJukeX, y + 2, kJukeW, kJukeRowH - 4, Col{120, 18, 24, 215});
            gfx_.fillRect(kJukeX, y + 2, 6, kJukeRowH - 4, gold);
        }
        if (name) {
            const std::string t = e->title + (e->present() ? "" : "   (no file)");
            gfx_.drawLabelText(*name, t, kJukeX + 24, y, kJukeW - 120, kJukeRowH, 0, true, e->present() ? (sel ? white : Col{215, 215, 220, 255}) : dim, 0.62f, 1.0f);
        }
        if (isPlaying) {   // three bars dancing
            for (int b = 0; b < 3; ++b) {
                const float h = 8.0f + 16.0f * (0.5f + 0.5f * std::sin(static_cast<float>(jukeT_) * (0.19f + 0.07f * static_cast<float>(b)) + static_cast<float>(b) * 1.7f));
                gfx_.fillRect(kJukeX + kJukeW - 62 + static_cast<float>(b) * 11, y + kJukeRowH / 2 + 12 - h, 7, h, gold);
            }
        }
    }
    if (n > kJukeRows) {   // scroll bar
        const float frac = static_cast<float>(jukeTop_) / static_cast<float>(n - kJukeRows);
        const float th = kJukeRowH * kJukeRows * static_cast<float>(kJukeRows) / static_cast<float>(n);
        gfx_.fillRect(kJukeX + kJukeW + 6, kJukeY, 4, kJukeRowH * kJukeRows, Col{255, 255, 255, 40});
        gfx_.fillRect(kJukeX + kJukeW + 6, kJukeY + frac * (kJukeRowH * kJukeRows - th), 4, th, gold);
    }
    // now playing
    const MusicEntry* np = lib && playing >= 0 ? music_->entry(playing) : nullptr;
    gfx_.fillRect(830, kJukeY - 12, 380, 330, Col{0, 0, 0, 165});
    gfx_.fillRect(830, kJukeY - 12, 380, 2, gold);
    if (name) {
        gfx_.drawLabelText(*name, jukeIdx_ == -2 ? "STOPPED" : "NOW PLAYING", 852, kJukeY + 4, 340, 34, 0, true, gold, 0.5f, 1.0f);
        if (np) gfx_.drawLabelText(*name, np->title, 852, kJukeY + 44, 340, 120, 0, false, white, 0.8f, 1.0f);
        else gfx_.drawLabelText(*name, n > 0 ? "-" : "No music files found in the mounted data.", 852, kJukeY + 44, 340, 120, 0, false, dim, 0.62f, 1.0f);
        if (np && jukeIdx_ == -1) gfx_.drawLabelText(*name, "(the gallery's theme)", 852, kJukeY + 170, 340, 30, 0, true, dim, 0.45f, 1.0f);
    }
    if (np && music_->audio()) {   // progress through the loop
        const Audio* au = music_->audio();
        const float fracp = au->musicInIntro() || au->music().loopEnd == 0
                                ? 0.0f
                                : std::clamp(static_cast<float>(au->musicPosition()) / static_cast<float>(au->music().loopEnd), 0.0f, 1.0f);
        gfx_.fillRect(852, kJukeY + 220, 336, 6, Col{255, 255, 255, 50});
        gfx_.fillRect(852, kJukeY + 220, 336 * fracp, 6, gold);
        if (name) gfx_.drawLabelText(*name, au->musicInIntro() ? "INTRO" : "LOOP", 852, kJukeY + 232, 336, 30, 0, true, dim, 0.42f, 1.0f);
    }
    if (jukeMsgT_ > 0 && name) gfx_.drawLabelText(*name, jukeMsg_, 70, 648, 1140, 36, 0, true, Col{255, 200, 120, 255}, 0.5f, 1.0f);

    // ---- touch / mouse controls
    if (ui.button(5300, Rect{8, 341, 70, 17}, "BACK")) leave = true;
    if (ui.button(5301, Rect{300, 341, 54, 17}, "PREV", n > 0)) {
        for (int k = 1; k <= n; ++k) {
            const int row = ((jukeSel_ - k) % n + n) % n;
            if (entryAt(row) && entryAt(row)->present()) {
                jukeSel_ = row;
                playJuke(jukeList_[static_cast<size_t>(row)]);
                break;
            }
        }
    }
    if (ui.button(5302, Rect{360, 341, 70, 17}, jukeIdx_ == -2 ? "PLAY" : "STOP", n > 0)) {
        if (jukeIdx_ == -2) jukeIdx_ = -1;
        else jukeIdx_ = -2;
    }
    if (ui.button(5303, Rect{436, 341, 54, 17}, "NEXT", n > 0)) {
        for (int k = 1; k <= n; ++k) {
            const int row = (jukeSel_ + k) % n;
            if (entryAt(row) && entryAt(row)->present()) {
                jukeSel_ = row;
                playJuke(jukeList_[static_cast<size_t>(row)]);
                break;
            }
        }
    }
    if (ui.button(5304, Rect{530, 341, 44, 17}, "UP", n > kJukeRows)) jukeTop_ = std::max(0, jukeTop_ - 3);
    if (ui.button(5305, Rect{580, 341, 52, 17}, "DOWN", n > kJukeRows)) jukeTop_ = std::min(std::max(0, n - kJukeRows), jukeTop_ + 3);
}

void GalleryScreen::loadFull(SDL_Renderer* r) {
    if (fullIdx_ == view_) return;
    if (full_.t) SDL_DestroyTexture(full_.t);
    full_ = load(r, items_[static_cast<size_t>(view_)].path, kFullMax);
    fullIdx_ = view_;
}

void GalleryScreen::drawViewer(Ui& ui, SDL_Renderer* r) {
    if (!vView_) {
        drawViewerPlain(ui, r);
        return;
    }
    // the unlocked pictures of the current tab, in order: arrows / swipe move through them
    std::vector<size_t> list;
    for (size_t i : shown())
        if (unlocked(items_[i])) list.push_back(i);
    int pos = 0;
    for (size_t k = 0; k < list.size(); ++k)
        if (static_cast<int>(list[k]) == view_) pos = static_cast<int>(k);
    loadFull(r);
    const Item& it = items_[static_cast<size_t>(view_)];
    std::string title = it.name;
    if (!it.artist.empty()) title += "  -  " + it.artist;
    const std::string counter = std::to_string(pos + 1) + " / " + std::to_string(list.size());

    gfx_.loadScene("ArtGallery.gbs");
    gfx_.setViewport(0, 0, 0.5f);
    gfx_.setAnimPlayer(&vAnim_);
    const float fade = std::clamp(static_cast<float>(vAnim_.tickOf("Infobar_On")) / 14.0f, 0.0f, 1.0f);
    gfx_.drawView(*vView_, [&](const GbsNode& n, NodeState& st) {
        if (n.name == "Art_Holder") {   // the scene parks it off-screen: the game's code puts it in the middle of the top part
            float cx, cy;
            nodeXY(vAnim_, n, cx, cy);
            st.dx = (1280 - 870) / 2 - cx;
            st.dy = 38 - cy;
            st.alpha = std::clamp(static_cast<float>(vAnim_.tickOf("BG_On")) / 14.0f, 0.0f, 1.0f);
            if (full_.t) {
                st.rawTex = full_.t;
                st.rawW = full_.w;
                st.rawH = full_.h;
                st.rawCover = false;
            }
        } else if (n.name == "Info_Frame") {   // the ribbon goes under the picture
            float cx, cy;
            nodeXY(vAnim_, n, cx, cy);
            st.dx = 16 - cx;
            st.dy = 560 - cy;
        }
    });
    gfx_.setAnimPlayer(nullptr);
    // text in "Online Name": the ribbon's own font ("Dialogue Text") has no Latin atlas in every dump
    if (const GbsFont* f = gfx_.fontNamed("Online Name")) {
        gfx_.drawLabelText(*f, title, 40, 560, 900, 44, 0, true, Col{255, 255, 255, 255}, 0.62f, fade);
        gfx_.drawLabelText(*f, counter, 340, 560, 900, 44, 2, true, Col{220, 220, 220, 255}, 0.55f, fade);
        if (!it.desc.empty()) gfx_.drawLabelText(*f, it.desc, 40, 610, 1200, 60, 0, false, Col{210, 210, 210, 255}, 0.5f, fade);
    }
    if (!full_.t) ui.text(320 - Ui::textW("picture could not be read") / 2, 160, "picture could not be read", kWarn);

    int step = 0;
    if (ui.button(5300, Rect{6, 150, 34, 56}, "<", pos > 0)) step = -1;
    if (ui.button(5301, Rect{600, 150, 34, 56}, ">", pos + 1 < static_cast<int>(list.size()))) step = +1;
    if (ui.button(5302, Rect{596, 6, 38, 22}, "X")) {
        view_ = -1;
        return;
    }
    if (ui.in.left) step = -1;
    if (ui.in.right) step = +1;
    if (ui.in.pressed) {
        swipeX_ = ui.in.mx;
        swiping_ = true;
    }
    if (ui.in.released && swiping_) {
        swiping_ = false;
        const int dx = ui.in.mx - swipeX_;
        if (dx < -60) step = +1;
        else if (dx > 60) step = -1;
    }
    if (step != 0 && !list.empty()) {
        const int np = std::clamp(pos + step, 0, static_cast<int>(list.size()) - 1);
        view_ = static_cast<int>(list[static_cast<size_t>(np)]);
    }
}


// ---- fallbacks without ArtGallery.gbs: plain panels
void GalleryScreen::drawGridPlain(Ui& ui, SDL_Renderer* r, bool& leave) {
    ui.fill(Rect{0, 0, 640, 360}, Col{10, 10, 16, 150});
    ui.text(16, 10, upperStr(loc_.trOr("Art Gallery", "Art Gallery")), kAccent, 2);
    if (ui.button(5000, Rect{536, 6, 96, 24}, "BACK")) leave = true;
    if (items_.empty()) {
        ui.text(16, 60, "No gallery pictures found in the mounted data.", kWarn);
        ui.text(16, 80, "Looked for: ui_artgallery/gallery.ini, ui_artgallery/*.dds,", kDim);
        ui.text(16, 94, "story_art/Cinematics/<Char>/*.dds (copy them from your game folder).", kDim);
        return;
    }
    int open = 0;
    for (const Item& it : items_) open += unlocked(it) ? 1 : 0;
    const std::string cnt = "COMPLETE: " + std::to_string(open) + "/" + std::to_string(items_.size());
    ui.text(528 - Ui::textW(cnt), 14, cnt, kDim);

    // ---- tabs
    if (groups_.size() > 1) {
        const int tabW = 100;
        const int maxTabs = 6;
        const int firstTab = std::clamp(group_ - maxTabs / 2, 0, std::max(0, static_cast<int>(groups_.size()) - maxTabs));
        for (int i = firstTab; i < static_cast<int>(groups_.size()) && i < firstTab + maxTabs; ++i) {
            const Rect b{16 + (i - firstTab) * (tabW + 4), 36, tabW, 20};
            if (ui.button(5100 + i, b, upperStr(groups_[static_cast<size_t>(i)]), true, i == group_)) {
                group_ = i;
                page_ = 0;
            }
        }
        if (ui.in.keyUp) group_ = (group_ + static_cast<int>(groups_.size()) - 1) % static_cast<int>(groups_.size()), page_ = 0;
        if (ui.in.keyDown) group_ = (group_ + 1) % static_cast<int>(groups_.size()), page_ = 0;
    }

    const std::vector<size_t> list = shown();
    const int pages = std::max(1, (static_cast<int>(list.size()) + kPerPage - 1) / kPerPage);
    // swipe left / right changes the page
    if (ui.in.pressed) {
        swipeX_ = ui.in.mx;
        swiping_ = true;
    }
    bool swiped = false;
    if (ui.in.released && swiping_) {
        swiping_ = false;
        const int dx = ui.in.mx - swipeX_;
        if (dx < -60) page_++, swiped = true;
        else if (dx > 60) page_--, swiped = true;
    }
    if (ui.in.left) page_--;
    if (ui.in.right) page_++;
    page_ = std::clamp(page_, 0, pages - 1);

    const int tw = 112, th = 63, gx = 8, gy = 26;
    const int x0 = (640 - (kCols * tw + (kCols - 1) * gx)) / 2, y0 = 64;
    int loads = 0;
    for (int k = 0; k < kPerPage; ++k) {
        const int li = page_ * kPerPage + k;
        if (li >= static_cast<int>(list.size())) break;
        const size_t idx = list[static_cast<size_t>(li)];
        const Item& it = items_[idx];
        const Rect cell{x0 + (k % kCols) * (tw + gx), y0 + (k / kCols) * (th + gy), tw, th};
        const bool open = unlocked(it);
        ui.fill(cell, kPanel);
        if (open) {
            Tex& t = thumbs_[idx];
            if (!t.tried && loads < 2) {   // two pictures decoded per frame: no stall when a page opens
                ++loads;
                t = load(r, it.thumb.empty() ? it.path : it.thumb, kThumbMax);
            }
            if (t.t) {
                int ow = 0, oh = 0;
                fitInto(t.w, t.h, cell.w, cell.h, ow, oh);
                SDL_Rect dst{cell.x + (cell.w - ow) / 2, cell.y + (cell.h - oh) / 2, ow, oh};
                SDL_RenderCopy(r, t.t, nullptr, &dst);
            } else {
                ui.textIn(cell, t.tried ? "?" : "...", kDim, true);
            }
        } else {
            ui.text(cell.x + cell.w / 2 - 8, cell.y + cell.h / 2 - 8, "?", kDim, 2);
        }
        const bool hot = cell.contains(ui.in.mx, ui.in.my) && !ui.in.touchPointer;
        ui.outline(cell, hot ? kAccent : kLine);
        ui.textIn(Rect{cell.x, cell.y + cell.h + 3, cell.w, 12}, open ? it.name : "LOCKED", open ? kText : kDim, true);
        if (!swiped && ui.in.released && cell.contains(ui.in.mx, ui.in.my) && cell.contains(ui.in.pmx, ui.in.pmy)) {
            if (open) view_ = static_cast<int>(idx);
        }
    }
    const std::string pg = std::to_string(page_ + 1) + " / " + std::to_string(pages);
    ui.text(320 - Ui::textW(pg) / 2, 334, pg, kDim);
    if (ui.button(5200, Rect{16, 326, 80, 24}, "<", page_ > 0)) page_ = std::max(0, page_ - 1);
    if (ui.button(5201, Rect{544, 326, 80, 24}, ">", page_ < pages - 1)) page_ = std::min(pages - 1, page_ + 1);
    if (all_) ui.text(110, 334, "gallery_all", kWarn);
}

void GalleryScreen::drawViewerPlain(Ui& ui, SDL_Renderer* r) {
    // the pictures of the current tab that are unlocked, in order: arrows / swipe move through them
    std::vector<size_t> list;
    for (size_t i : shown())
        if (unlocked(items_[i])) list.push_back(i);
    int pos = 0;
    for (size_t k = 0; k < list.size(); ++k)
        if (static_cast<int>(list[k]) == view_) pos = static_cast<int>(k);
    if (fullIdx_ != view_) {
        if (full_.t) SDL_DestroyTexture(full_.t);
        full_ = load(r, items_[static_cast<size_t>(view_)].path, kFullMax);
        fullIdx_ = view_;
    }
    ui.fill(Rect{0, 0, 640, 360}, Col{0, 0, 0, 255});
    if (full_.t) {
        int ow = 0, oh = 0;
        fitInto(full_.w, full_.h, 640, 330, ow, oh);
        SDL_Rect dst{(640 - ow) / 2, (330 - oh) / 2, ow, oh};
        SDL_RenderCopy(r, full_.t, nullptr, &dst);
    } else {
        ui.text(320 - Ui::textW("picture could not be read") / 2, 160, "picture could not be read", kWarn);
    }
    const Item& it = items_[static_cast<size_t>(view_)];
    ui.fill(Rect{0, 330, 640, 30}, Col{0, 0, 0, 200});
    std::string line = it.name;
    if (!it.artist.empty()) line += "  -  " + it.artist;
    ui.text(12, 336, Ui::fit(line, 420), kText);
    if (!it.desc.empty()) ui.text(12, 348, Ui::fit(it.desc, 420), kDim);
    const std::string pg = std::to_string(pos + 1) + " / " + std::to_string(list.size());
    ui.text(440, 341, pg, kDim);

    int step = 0;
    if (ui.button(5300, Rect{500, 334, 40, 22}, "<", pos > 0)) step = -1;
    if (ui.button(5301, Rect{544, 334, 40, 22}, ">", pos + 1 < static_cast<int>(list.size()))) step = +1;
    if (ui.button(5302, Rect{588, 334, 48, 22}, "X")) {
        view_ = -1;
        return;
    }
    if (ui.in.left) step = -1;
    if (ui.in.right) step = +1;
    if (ui.in.pressed) {
        swipeX_ = ui.in.mx;
        swiping_ = true;
    }
    if (ui.in.released && swiping_ && ui.in.my < 330) {
        swiping_ = false;
        const int dx = ui.in.mx - swipeX_;
        if (dx < -60) step = +1;
        else if (dx > 60) step = -1;
    }
    if (step != 0 && !list.empty()) {
        const int np = std::clamp(pos + step, 0, static_cast<int>(list.size()) - 1);
        view_ = static_cast<int>(list[static_cast<size_t>(np)]);
    }
}

}  // namespace gallery
}  // namespace zengine
