// credits.cpp - see credits.h
#include "credits.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "credits_font.h"
#include "vfs.h"

namespace zengine {
namespace credits {

namespace {

constexpr float kSlotX = 250, kSlotW = 780, kSlotH = 40;   // Credits.gbs: slot groups sit at (30 + 220, 40 n), 780 x 40
constexpr float kPageH = 720;                              // pages are stacked one canvas height apart
constexpr float kScrollPerTick = 719.0f / 594.0f;          // "CreditsScroll": -719 px in 594 ticks
constexpr int kHoldTicks = 120;                            // the first page stays still for two seconds
constexpr int kFastForward = 6;
constexpr int kNoteHoldTicks = 600;                        // the closing message stays on screen for ten seconds

// The closing message (Spanish, the author's own words).
const char* const kNoteHead = "El dinero corrompe.";
const char* const kNoteBody1 =
    "Eso es lo que hace la edición Mobile. Escucha muy bien: no juegues para que gastes y alimentes a la empresa; "
    "cómprate el juego original para ver que yo te apoyo como comunidad.";
const char* const kNoteBody2 =
    "Ellos están interesados en dinero para así sacar una versión “Mobile”, pero yo, como soy tu amigo, "
    "te tengo un Runtime para que lo juegues legal.";
const char* const kNoteTail = "Gracias por comprar el juego.";

std::vector<uint32_t> decodeUtf8(const std::string& s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = c;
        int n = 1;
        if (c >= 0xF0) { cp = c & 0x07; n = 4; }
        else if (c >= 0xE0) { cp = c & 0x0F; n = 3; }
        else if (c >= 0xC0) { cp = c & 0x1F; n = 2; }
        for (int k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        out.push_back(cp);
        i += static_cast<size_t>(n);
    }
    return out;
}

std::string lowerStr(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return s;
}

std::string trimStr(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

void putUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// The .ini files mix UTF-8 with stray Windows-1252 bytes (curly quotes).  Valid UTF-8 is kept; a lone high byte is read as
// Windows-1252.  Typographic punctuation the fonts may not have becomes its ASCII twin.
std::string cleanText(const std::string& s) {
    static const uint16_t k1252[32] = {0x20AC, 0x81,   0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                                       0x2039, 0x0152, 0x8D,   0x017D, 0x8F,   0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                       0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,   0x017E, 0x0178};
    std::string out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = c;
        size_t len = 1;
        if (c >= 0xC2 && c < 0xE0 && i + 1 < s.size() && (static_cast<unsigned char>(s[i + 1]) & 0xC0) == 0x80) {
            cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
            len = 2;
        } else if (c >= 0xE0 && c < 0xF0 && i + 2 < s.size() && (static_cast<unsigned char>(s[i + 1]) & 0xC0) == 0x80 &&
                   (static_cast<unsigned char>(s[i + 2]) & 0xC0) == 0x80) {
            cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6) | (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
            len = 3;
        } else if (c >= 0xF0 && c < 0xF5 && i + 3 < s.size() && (static_cast<unsigned char>(s[i + 1]) & 0xC0) == 0x80 &&
                   (static_cast<unsigned char>(s[i + 2]) & 0xC0) == 0x80 && (static_cast<unsigned char>(s[i + 3]) & 0xC0) == 0x80) {
            cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 12) |
                 ((static_cast<unsigned char>(s[i + 2]) & 0x3Fu) << 6) | (static_cast<unsigned char>(s[i + 3]) & 0x3Fu);
            len = 4;
        } else if (c >= 0x80) {   // not UTF-8: Windows-1252
            cp = (c < 0xA0) ? k1252[c - 0x80] : c;
        }
        i += len;
        switch (cp) {
            case 0x2018: case 0x2019: case 0x201B: case 0x2032: out += '\''; break;
            case 0x201C: case 0x201D: case 0x201F: case 0x2033: out += '"'; break;
            case 0x2013: case 0x2014: out += '-'; break;
            case 0x2026: out += "..."; break;
            case 0xA0: out += ' '; break;
            default: putUtf8(out, cp); break;
        }
    }
    // the contributors' list marks its tier titles with "{star}"
    for (size_t p = out.find("{star}"); p != std::string::npos; p = out.find("{star}", p + 1)) out.replace(p, 6, "*");
    return out;
}

bool isKey(const std::string& lineLower, const char* key) {
    const size_t n = std::char_traits<char>::length(key);
    return lineLower.compare(0, n, key) == 0;
}

Page::Layout layoutOf(const std::string& word) {
    const std::string w = lowerStr(word);
    if (w == "intro") return Page::Layout::Intro;
    if (w == "leftimage") return Page::Layout::LeftImage;
    if (w == "rightimage") return Page::Layout::RightImage;
    if (w == "topimage") return Page::Layout::TopImage;
    if (w == "bottomimage") return Page::Layout::BottomImage;
    return Page::Layout::AllText;
}

float firstSlotY(Page::Layout l) {
    switch (l) {
        case Page::Layout::Intro: return 440;
        case Page::Layout::TopImage: return 240;
        default: return 0;
    }
}

}  // namespace

int slotCapacity(Page::Layout l) {
    switch (l) {
        case Page::Layout::Intro: return 11;
        case Page::Layout::TopImage:
        case Page::Layout::BottomImage: return 12;
        default: return 18;
    }
}

std::vector<Page> parseCredits(const std::string& text) {
    std::vector<std::string> lines;
    {
        size_t pos = 0;
        while (pos <= text.size()) {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos) end = text.size();
            std::string line = text.substr(pos, end - pos);
            pos = end + 1;
            if (const size_t h = line.find('#'); h != std::string::npos) line.erase(h);
            lines.push_back(trimStr(cleanText(line)));
            if (end >= text.size()) break;
        }
    }
    std::vector<Page> pages;
    int used = 0;
    auto startPage = [&](Page::Layout l, const std::string& image) {
        Page p;
        p.layout = l;
        p.image = image;
        pages.push_back(std::move(p));
        used = 0;
    };
    auto add = [&](Entry e) {
        if (pages.empty()) startPage(Page::Layout::AllText, "");
        if (used + e.slots() > slotCapacity(pages.back().layout)) {   // spill: a side picture runs on along the list, anything else becomes plain text
            const Page::Layout l = pages.back().layout;
            const bool side = l == Page::Layout::LeftImage || l == Page::Layout::RightImage;
            const std::string img = side ? pages.back().image : std::string();
            startPage(side ? l : Page::Layout::AllText, img);
        }
        used += e.slots();
        pages.back().rows.push_back(std::move(e));
    };
    // the value of "Key:" - on the same line, or on the next line that is not itself a key
    auto valueOf = [&](size_t& i, size_t keyLen) {
        std::string v = trimStr(lines[i].substr(keyLen));
        if (!v.empty()) return v;
        for (size_t j = i + 1; j < lines.size(); ++j) {
            if (lines[j].empty()) continue;
            const std::string lo = lowerStr(lines[j]);
            if (isKey(lo, "credit:") || isKey(lo, "creditspage:") || isKey(lo, "header:") || isKey(lo, "image:")) break;
            i = j;
            return lines[j];
        }
        return std::string();
    };
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& ln = lines[i];
        if (ln.empty()) continue;
        const std::string lo = lowerStr(ln);
        if (isKey(lo, "creditspage:")) {
            const std::string v = trimStr(ln.substr(12));
            const size_t sp = v.find_first_of(" \t");
            startPage(layoutOf(sp == std::string::npos ? v : v.substr(0, sp)), "");
        } else if (isKey(lo, "image:")) {
            if (pages.empty()) startPage(Page::Layout::AllText, "");
            pages.back().image = trimStr(ln.substr(6));
        } else if (isKey(lo, "header:")) {
            Entry e;
            e.kind = Entry::Kind::Header;
            e.name = valueOf(i, 7);
            add(std::move(e));
        } else if (isKey(lo, "credit:")) {
            Entry e;
            const std::string v = valueOf(i, 7);
            if (v.empty() || lowerStr(v) == "blank") {
                e.kind = Entry::Kind::Blank;
            } else {
                e.kind = Entry::Kind::Credit;
                const size_t comma = v.find(',');
                if (comma == std::string::npos) {
                    e.name = v;
                    e.centered = true;
                } else {
                    e.name = trimStr(v.substr(0, comma));
                    e.title = trimStr(v.substr(comma + 1));
                    if (e.title.empty()) e.centered = true;
                }
            }
            add(std::move(e));
        }
    }
    return pages;
}

Page developersPage(const std::string& lang) {
    Page p;
    p.layout = Page::Layout::AllText;
    auto blank = [&]() { p.rows.push_back(Entry{}); };
    auto name = [&](const char* n) {
        Entry e;
        e.kind = Entry::Kind::Credit;
        e.name = n;
        e.centered = true;
        p.rows.push_back(std::move(e));
    };
    for (int i = 0; i < 6; ++i) blank();   // 18 slots on the page: 6 + header (2) + blank + 3 names + the rest below = centred on the screen
    Entry h;
    h.kind = Entry::Kind::Header;
    h.name = lang.rfind("es", 0) == 0 ? "Desarrollado por" : "Developed by";
    p.rows.push_back(std::move(h));
    blank();
    name("Claude by Anthropic");
    name("Destroyer");
    name("Nitro");
    return p;
}

void CreditsScreen::setGameData(const GameData* gd) {
    gd_ = gd;
    loaded_ = false;
    pages_.clear();
    gfx_.reset();
    ren_ = nullptr;
    gfxOk_ = false;
    credScene_ = nullptr;
    credView_ = nullptr;
    headerTpl_.clear();
    fNames_ = fHead_ = nullptr;
    thanksBound_ = false;
}

void CreditsScreen::begin(const std::string& lang, bool contributors, bool thanks) {
    lang_ = lang.empty() ? std::string("en") : lang;
    loc_.clear();
    if (gd_) {
        std::string json;
        if (gd_->vfs().readText(lang_ + "_loc.json", json)) loc_.parseJson(json);
        else if (gd_->vfs().readText("en_loc.json", json)) loc_.parseJson(json);
    }
    if (contributors != contributors_) loaded_ = false;
    contributors_ = contributors;
    thanks_ = thanks;
    tick_ = 0;
    acc_ = 0;
    paused_ = false;
    phase_ = 0;
    thanksTick_ = 0;
    thanksBound_ = false;
    if (!loaded_ && gd_) {
        loaded_ = true;
        pages_.clear();
        std::string txt;
        const char* names[] = {"credits_contrib_2013.ini"};
        const char* normal[] = {"credits_2021.ini", "credits.ini"};
        if (contributors) {
            for (const char* n : names)
                if (pages_.empty() && gd_->vfs().readText(n, txt)) pages_ = parseCredits(txt);
        } else {
            for (const char* n : normal)
                if (pages_.empty() && gd_->vfs().readText(n, txt)) pages_ = parseCredits(txt);
        }
        if (!contributors) pages_.push_back(developersPage(lang_));   // after the game's own credits
        std::printf("[credits] %zu pages (%s)\n", pages_.size(), contributors ? "contributors" : "credits");
        if (std::getenv("ZDROID_CREDITS_DEBUG"))
            for (size_t i = 0; i < pages_.size(); ++i) {
                int used = 0;
                for (const Entry& e : pages_[i].rows) used += e.slots();
                std::printf("[credits]  page %zu layout %d rows %zu slots %d/%d %s\n", i, static_cast<int>(pages_[i].layout), pages_[i].rows.size(), used,
                            slotCapacity(pages_[i].layout), pages_[i].image.c_str());
            }
    }
    if (const char* e = std::getenv("ZDROID_CREDITS_PAGE")) {   // smoke tests: start on page N
        const int n = std::atoi(e);
        tick_ = kHoldTicks + std::max(0, n) * 594;
    }
    if (std::getenv("ZDROID_CREDITS_THANKS")) phase_ = 1;
}

void CreditsScreen::bind(SDL_Renderer* r) {
    ren_ = r;
    gfxOk_ = false;
    credScene_ = nullptr;
    credView_ = nullptr;
    headerTpl_.clear();
    thanksBound_ = false;
    noteGlyphs_.clear();   // the textures belonged to the previous renderer
    noteFontTried_ = false;
    if (!gd_ || !r) return;
    gfx_.attach(r, &gd_->vfs());
    if (!gfx_.loadScene("Credits.gbs") || !gfx_.scene()) return;
    credScene_ = gfx_.scene();
    credView_ = credScene_->view("Credits");
    if (!credView_) return;
    if (const GbsNode* h = credView_->find("HeaderGRP")) headerTpl_.push_back(*h);
    fNames_ = gfx_.fontNamed("Credits Names");
    fHead_ = gfx_.fontNamed("Big Options w Wings");
    gfxOk_ = fNames_ != nullptr;
}

void CreditsScreen::drawPage(const Page& pg, float oy) {
    // pictures
    auto pic = [&](const std::string& name, float x, float y, float w, float h) {
        if (name.empty()) return;
        if (SDL_Texture* t = gfx_.texture(name)) gfx_.drawTexture(t, x, y + oy, w, h);
    };
    switch (pg.layout) {
        case Page::Layout::LeftImage: pic(pg.image, 0, 0, 240, 720); break;
        case Page::Layout::RightImage: pic(pg.image, 1040, 0, 240, 720); break;
        case Page::Layout::TopImage: pic(pg.image, 0, 0, 1280, 240); break;
        case Page::Layout::BottomImage: pic(pg.image, 0, 480, 1280, 240); break;
        case Page::Layout::Intro: {   // the game's logo above the first names
            int tw = 0, th = 0;
            if (SDL_Texture* t = gfx_.texture("Textures/Title Screen/logo_2ec.png", &tw, &th)) {
                const float w = 539, h = tw > 0 ? w * static_cast<float>(th) / static_cast<float>(tw) : 171;
                gfx_.drawTexture(t, 370, 140 + (303 - h) / 2 + oy, w, h);
            }
            break;
        }
        default: break;
    }
    const float y0 = firstSlotY(pg.layout);
    int slot = 0;
    for (const Entry& e : pg.rows) {
        const float y = y0 + kSlotH * static_cast<float>(slot) + oy;
        slot += e.slots();
        if (y > 720 + 80 || y < -120) continue;   // off screen
        if (e.kind == Entry::Kind::Blank) continue;
        if (e.kind == Entry::Kind::Header) {
            if (headerTpl_.empty()) continue;
            const float tx = headerTpl_[0].x, ty = headerTpl_[0].y;   // the template's own place in the scene
            gfx_.drawNodes(headerTpl_, kSlotX - tx, y - ty, [&](const GbsNode& n, NodeState& st) {
                if (n.name == "HeaderTxt") {
                    st.hasText = true;
                    st.text = e.name;
                    st.textScale = 0.68f;
                }
            });
            continue;
        }
        const Col white{255, 255, 255, 255};
        auto put = [&](const std::string& s, float x, float w, int align) {
            if (s.empty() || !fNames_) return;
            float sc = 1.0f;
            const float tw = gfx_.textWidth(*fNames_, s, 1.0f);
            if (tw > w) sc = std::max(0.55f, w / tw);   // a long role is squeezed rather than wrapped
            gfx_.drawLabelText(*fNames_, s, x, y, w, kSlotH, align, true, white, sc, 1.0f);
        };
        if (e.centered) {
            put(e.name, kSlotX, kSlotW, 1);
        } else {
            put(e.title, kSlotX, 400, 0);       // CreditGRP: TitleTxt on the left (400 wide) ...
            put(e.name, kSlotX + 400, 380, 2);  // ... NameTxt right-aligned in the other 380
        }
    }
}

void CreditsScreen::ensureNoteFont() {
    if (noteFontTried_ || !ren_) return;
    noteFontTried_ = true;
    std::vector<uint8_t> raw(oswald::kRawSize, 0);
    size_t o = 0;
    for (size_t i = 0; i < sizeof(oswald::kRle);) {
        if (oswald::kRle[i] == 0 && i + 1 < sizeof(oswald::kRle)) {
            o += oswald::kRle[i + 1];
            i += 2;
        } else {
            if (o < raw.size()) raw[o] = oswald::kRle[i];
            ++o;
            ++i;
        }
    }
    for (const oswald::Glyph& g : oswald::kGlyphs) {
        NoteGlyph ng;
        ng.w = g.w;
        ng.h = g.h;
        ng.xo = g.xo;
        ng.yo = g.yo;
        ng.adv = g.adv;
        if (g.w > 0 && g.h > 0 && g.off + static_cast<size_t>(g.w) * static_cast<size_t>(g.h) <= raw.size()) {
            std::vector<uint32_t> px(static_cast<size_t>(g.w) * static_cast<size_t>(g.h));
            for (size_t k = 0; k < px.size(); ++k) px[k] = (static_cast<uint32_t>(raw[g.off + k]) << 24) | 0x00FFFFFFu;
            ng.t = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, g.w, g.h);
            if (ng.t) {
                SDL_UpdateTexture(ng.t, nullptr, px.data(), g.w * 4);
                SDL_SetTextureBlendMode(ng.t, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
                SDL_SetTextureScaleMode(ng.t, SDL_ScaleModeLinear);
#endif
            }
        }
        noteGlyphs_[g.cp] = ng;
    }
}

void CreditsScreen::drawNote(float oy) {
    ensureNoteFont();
    if (noteGlyphs_.empty()) return;
    const Col gold{199, 153, 113, 255}, white{238, 238, 238, 255};
    constexpr float kMaxW = 860, kCx = 640;
    auto advOf = [&](uint32_t cp) {
        auto it = noteGlyphs_.find(cp);
        if (it == noteGlyphs_.end()) it = noteGlyphs_.find('?');
        return it == noteGlyphs_.end() ? 0.0f : it->second.adv;
    };
    auto widthOf = [&](const std::vector<uint32_t>& cps, float sc) {
        float w = 0;
        for (uint32_t c : cps) w += advOf(c) * sc;
        return w;
    };
    auto drawRun = [&](const std::vector<uint32_t>& cps, float x, float top, float sc, Col col) {
        const float base = top + static_cast<float>(oswald::kAscent) * sc;
        for (uint32_t c : cps) {
            auto it = noteGlyphs_.find(c);
            if (it == noteGlyphs_.end()) it = noteGlyphs_.find('?');
            if (it == noteGlyphs_.end()) continue;
            const NoteGlyph& g = it->second;
            if (g.t) gfx_.drawTexture(g.t, x + static_cast<float>(g.xo) * sc, base + static_cast<float>(g.yo) * sc,
                                      static_cast<float>(g.w) * sc, static_cast<float>(g.h) * sc, 0, 0, 0, false, false, col);
            x += g.adv * sc;
        }
    };
    // a paragraph, centred, wrapped on its spaces; returns the y below it
    auto paragraph = [&](const char* utf8, float em, float y, Col col) {
        const float sc = em / static_cast<float>(oswald::kPx);
        const float lineH = em * 1.32f;
        std::vector<std::vector<uint32_t>> lines(1);
        std::vector<uint32_t> word;
        auto flush = [&]() {
            if (word.empty()) return;
            std::vector<uint32_t>& cur = lines.back();
            std::vector<uint32_t> trial = cur;
            if (!trial.empty()) trial.push_back(' ');
            trial.insert(trial.end(), word.begin(), word.end());
            if (!cur.empty() && widthOf(trial, sc) > kMaxW) lines.push_back(word);
            else cur = trial;
            word.clear();
        };
        for (uint32_t c : decodeUtf8(utf8)) {
            if (c == ' ') flush();
            else word.push_back(c);
        }
        flush();
        for (const auto& ln : lines) {
            drawRun(ln, kCx - widthOf(ln, sc) / 2, y + oy, sc, col);
            y += lineH;
        }
        return y;
    };
    auto height = [&](const char* utf8, float em) {   // dry run of the same wrapping to centre the block
        const float sc = em / static_cast<float>(oswald::kPx);
        int n = 1;
        float w = 0;
        std::vector<uint32_t> word;
        auto flush = [&]() {
            if (word.empty()) return;
            const float ww = widthOf(word, sc), sp = advOf(' ') * sc;
            if (w > 0 && w + sp + ww > kMaxW) { ++n; w = ww; }
            else w += (w > 0 ? sp : 0) + ww;
            word.clear();
        };
        for (uint32_t c : decodeUtf8(utf8)) {
            if (c == ' ') flush();
            else word.push_back(c);
        }
        flush();
        return static_cast<float>(n) * em * 1.32f;
    };
    constexpr float kHead = 60, kBody = 35, kTail = 46, kGap = 34, kRule = 44;
    const float total = height(kNoteHead, kHead) + kRule + height(kNoteBody1, kBody) + kGap + height(kNoteBody2, kBody) + kGap +
                        height(kNoteTail, kTail);
    float y = (720.0f - total) / 2.0f;
    y = paragraph(kNoteHead, kHead, y, gold);
    gfx_.fillRect(kCx - 70, y + 14 + oy, 140, 2, gold);
    y += kRule;
    y = paragraph(kNoteBody1, kBody, y, white) + kGap;
    y = paragraph(kNoteBody2, kBody, y, white) + kGap;
    paragraph(kNoteTail, kTail, y, gold);
}

void CreditsScreen::drawThanks(Ui& ui, SDL_Renderer* r, bool& done, int steps) {
    (void)r;
    bool drawn = false;
    if (gfx_.loadScene("ThanksForPlaying.gbs") && gfx_.scene()) {
        if (const GbsSceneView* v = gfx_.scene()->view("ThanksForPlaying")) {
            if (!thanksBound_) {
                thanksAnim_.bind(v);
                thanksAnim_.play("OnActivate");
                thanksAnim_.play("ThanksForPlaying");
                thanksBound_ = true;
            }
            for (int i = 0; i < steps; ++i) thanksAnim_.tick();
            const std::string txt = loc_.trOr("ThanksForPlaying", "THANK YOU FOR PLAYING!");
            gfx_.setViewport(0, 0, 0.5f);
            gfx_.setAnimPlayer(&thanksAnim_);
            gfx_.drawView(*v, [&](const GbsNode& n, NodeState& st) {
                if (n.name == "Copy of logo") st.hide = true;   // the glow's picture is not in the scene (the game sets it from code)
                else if (n.name == "logo") st.texName = "Textures/Title Screen/logo01.png";
                else if (n.name == "text") {
                    st.hasText = true;
                    st.text = txt;
                    st.dx = -97;   // the animation widens the box to 1216 px from x = 135 (a longer text in the trial): keep it centred
                }
            });
            gfx_.setAnimPlayer(nullptr);
            drawn = true;
            if (thanksTick_ > 0 && thanksAnim_.finished("ThanksForPlaying")) done = true;
        }
    }
    if (!drawn) {   // no ThanksForPlaying.gbs: a plain card for two seconds
        ui.fill(Rect{0, 0, 640, 360}, Col{0, 0, 0, 255});
        const std::string t = loc_.trOr("ThanksForPlaying", "THANK YOU FOR PLAYING!");
        ui.text(320 - Ui::textW(t, 2) / 2, 170, t, kAccent, 2);
        if (thanksTick_ > 120) done = true;
    }
    if (thanksTick_ > 90 && (ui.in.released || ui.in.enter)) done = true;
}

bool CreditsScreen::frame(Ui& ui, SDL_Renderer* r, int dtMs) {
    if (!gd_) return false;
    if (music_) {   // the roll plays the credits theme, the card after a story its ending theme (each falls back to the next best)
        const MusicLibrary* l = music_->lib();
        const int idx = l ? (phase_ == 1 ? l->ending() : l->credits()) : -1;
        if (!music_->play(idx)) music_->stop();
    }
    if (ren_ != r) bind(r);
    // the leave button works in both phases
    bool done = false;
    ui.fill(Rect{0, 0, 640, 360}, Col{0, 0, 0, 255});
    if (!gfxOk_ || pages_.empty()) {   // no Credits.gbs / credits file in the mounted data: nothing to roll
        const std::string msg = pages_.empty() ? "No credits file found in the mounted data." : "Credits.gbs is not in the mounted data.";
        ui.text(320 - Ui::textW(msg) / 2, 170, msg, kWarn);
        if (ui.button(6000, Rect{262, 200, 116, 22}, "BACK")) done = true;
        if (ui.in.enter) done = true;
        return !done;
    }

    // ---- time: 60 ticks per second; a held finger / Down fast-forwards, a tap pauses
    acc_ += std::clamp(dtMs, 0, 250);
    int steps = 0;
    while (acc_ >= 1000.0 / 60.0 && steps < 8) {
        acc_ -= 1000.0 / 60.0;
        ++steps;
    }
    if (acc_ >= 1000.0 / 60.0) acc_ = 0;
    if (ui.in.down) ++holdTicks_;
    const bool fast = (ui.in.down && holdTicks_ > 25) || ui.in.keyDown;
    if (ui.in.released) {
        if (holdTicks_ > 0 && holdTicks_ <= 25 && !Rect{598, 6, 36, 18}.contains(ui.in.pmx, ui.in.pmy)) paused_ = !paused_;
        holdTicks_ = 0;
    }
    if (ui.in.enter) paused_ = !paused_;
    if (phase_ == 0) {
        if (!paused_) tick_ += steps * (fast ? kFastForward : 1);
        float scroll = tick_ > kHoldTicks ? static_cast<float>(tick_ - kHoldTicks) * kScrollPerTick : 0.0f;
        const float noteStart = static_cast<float>(pages_.size()) * kPageH;   // the closing message is the page after the last one
        if (scroll > noteStart) scroll = noteStart + std::max(0.0f, scroll - noteStart - static_cast<float>(kNoteHoldTicks) * kScrollPerTick);
        gfx_.loadScene("Credits.gbs");
        gfx_.setViewport(0, 0, 0.5f);
        gfx_.fillRect(0, 0, 1280, 720, Col{0, 0, 0, 255});
        for (size_t p = 0; p < pages_.size(); ++p) {
            const float oy = static_cast<float>(p) * kPageH + 2.0f - scroll;
            if (oy > 720.0f || oy < -kPageH) continue;
            drawPage(pages_[p], oy);
        }
        {
            const float oy = noteStart + 2.0f - scroll;
            if (oy <= 720.0f && oy >= -kPageH) drawNote(oy);
        }
        if (scroll > noteStart + kPageH) {   // the closing message has left the screen
            if (thanks_) phase_ = 1;
            else done = true;
        }
        if (paused_) {
            const std::string t = loc_.trOr("CreditsPaused", "Credits Paused - any button resumes.");
            ui.fill(Rect{0, 326, 640, 22}, Col{0, 0, 0, 200});
            ui.text(320 - Ui::textW(t) / 2, 333, t, kWarn);
        }
    } else {
        thanksTick_ += steps;
        drawThanks(ui, r, done, steps);
    }
    if (phase_ == 0 || thanksTick_ < 60)
        if (ui.button(6000, Rect{598, 6, 36, 18}, "X")) done = true;
    return !done;
}

}  // namespace credits
}  // namespace zengine
