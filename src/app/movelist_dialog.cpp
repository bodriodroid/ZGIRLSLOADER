// movelist_dialog.cpp - see movelist_dialog.h.
#include "movelist_dialog.h"

#include <algorithm>
#include <cmath>

#include "menu_data.h"

namespace zengine {

namespace {

// text with a dark outline: the spotlights of the backdrop wash plain text out
void shadowLabel(GameUi& g, const GbsFont& f, const std::string& s, float x, float y, float w, float h, int align, Col c, float scale,
                 float alpha) {
    const Col shade{0, 0, 0, 255};
    for (const float o : {-2.0f, 2.0f}) {
        g.drawLabelText(f, s, x + o, y, w, h, align, true, shade, scale, 0.85f * alpha);
        g.drawLabelText(f, s, x, y + o, w, h, align, true, shade, scale, 0.85f * alpha);
    }
    g.drawLabelText(f, s, x, y, w, h, align, true, c, scale, alpha);
}

struct CmdItem {
    enum class K { Word, Plate, Arrow, Button } k = K::Word;
    std::string s;       // Word/Plate: the text; Arrow: f b u d uf ub df db; Button: lp mp hp lk mk hk
    bool gap = false;    // whitespace before it in the source
    bool brk = false;    // a line break before it
    float w = 0;
};

std::vector<CmdItem> parseCommand(const std::string& cmd) {
    std::vector<CmdItem> out;
    bool gap = false, brk = false;
    auto push = [&](CmdItem it) {
        it.gap = gap;
        it.brk = brk;
        gap = brk = false;
        out.push_back(std::move(it));
    };
    std::string word;
    auto flushWord = [&]() {
        if (word.empty()) return;
        CmdItem it;
        it.s = word;
        push(it);
        word.clear();
    };
    for (size_t i = 0; i < cmd.size(); ++i) {
        const char c = cmd[i];
        if (c == '{') {
            const size_t e = cmd.find('}', i);
            if (e != std::string::npos) {
                flushWord();
                const std::vector<CommandGlyph> gl = commandTokenGlyphs(cmd.substr(i + 1, e - i - 1));
                if (gl.empty()) {
                    CmdItem it;
                    it.s = cmd.substr(i + 1, e - i - 1);
                    push(it);
                }
                bool first = true;
                for (const CommandGlyph& g : gl) {
                    CmdItem it;
                    it.s = g.s;
                    it.k = g.kind == CommandGlyph::Kind::Arrow ? CmdItem::K::Arrow
                         : g.kind == CommandGlyph::Kind::Button ? CmdItem::K::Button
                         : (g.s == "+" || g.s == ">") ? CmdItem::K::Word : CmdItem::K::Plate;
                    if (!first) gap = false;   // the parts of one token stay together
                    push(it);
                    first = false;
                }
                i = e;
                continue;
            }
        }
        if (c == '\n') {
            flushWord();
            brk = true;
            gap = false;
        } else if (c == ' ' || c == '\t' || c == '\r') {
            flushWord();
            gap = true;
        } else {
            word += c;
        }
    }
    flushWord();
    return out;
}

float cmdAngle(const std::string& d) {
    if (d == "f") return 0;
    if (d == "df") return 45;
    if (d == "d") return 90;
    if (d == "db") return 135;
    if (d == "b") return 180;
    if (d == "ub") return -135;
    if (d == "u") return -90;
    return -45;   // uf
}

// Lays the command out inside `maxW`, at most `maxLines` lines (the scale shrinks until it fits), and draws it.
// Returns the number of lines used.
}  // namespace

int drawCommandBlock(GameUi& g, const GbsFont& f, const std::string& cmd, float x, float y, float maxW, int maxLines, Col c) {
    std::vector<CmdItem> items = parseCommand(cmd);
    if (items.empty()) return 0;
    SDL_Texture* chevron = g.texture("Textures/MainMenu/arrow02.png");
    int lines = 1;
    float scale = 1.0f;
    std::vector<std::vector<size_t>> rows;
    for (;; scale -= 0.05f) {
        const float iconH = 30.0f * scale;
        const float space = 9.0f * scale;
        for (CmdItem& it : items) {
            if (it.k == CmdItem::K::Word) it.w = g.textWidth(f, it.s, (it.s == "+" || it.s == ">") ? scale * 1.4f : scale);
            else if (it.k == CmdItem::K::Plate) it.w = g.textWidth(f, it.s, scale) + 16.0f * scale;
            else if (it.k == CmdItem::K::Arrow) it.w = iconH * 0.95f;
            else it.w = iconH;
        }
        rows.assign(1, {});
        float x0 = 0;
        for (size_t i = 0; i < items.size(); ++i) {
            const CmdItem& it = items[i];
            const float gapW = rows.back().empty() ? 0.0f : (it.gap ? space : 2.0f * scale);
            if (!rows.back().empty() && (it.brk || x0 + gapW + it.w > maxW)) {
                rows.emplace_back();
                x0 = 0;
            }
            x0 += (rows.back().empty() ? 0.0f : gapW) + it.w;
            rows.back().push_back(i);
        }
        lines = static_cast<int>(rows.size());
        if (lines <= maxLines || scale <= 0.56f) break;
    }
    const float iconH = 30.0f * scale;
    const float space = 9.0f * scale;
    const float lineH = 32.0f * scale;
    for (size_t r = 0; r < rows.size() && static_cast<int>(r) < std::max(maxLines, 1); ++r) {   // (what does not fit at the smallest size is cut)
        float cx = x;
        const float ly = y + static_cast<float>(r) * lineH;
        for (size_t k = 0; k < rows[r].size(); ++k) {
            const CmdItem& it = items[rows[r][k]];
            if (k > 0) cx += it.gap ? space : 2.0f * scale;
            if (it.k == CmdItem::K::Word) {
                shadowLabel(g, f, it.s, cx, ly, it.w + 12.0f, lineH, 0, c, (it.s == "+" || it.s == ">") ? scale * 1.4f : scale, 1.0f);
            } else if (it.k == CmdItem::K::Plate) {   // "P", "K", "360": no icon in the game's files, so a plate with the letters
                g.fillRect(cx, ly + lineH * 0.12f, it.w, lineH * 0.76f, Col{0, 0, 0, 200});
                g.fillRect(cx + 1.5f, ly + lineH * 0.12f + 1.5f, it.w - 3.0f, lineH * 0.76f - 3.0f, Col{232, 220, 170, 255});
                g.drawLabelText(f, it.s, cx, ly, it.w, lineH, 1, true, Col{20, 14, 10, 255}, scale * 0.9f, 1.0f);
            } else if (it.k == CmdItem::K::Arrow) {
                if (chevron) {
                    const float ah = iconH * 1.1f, aw = ah * 0.68f;
                    g.drawTexture(chevron, cx + (it.w - aw) * 0.5f, ly + (lineH - ah) * 0.5f, aw, ah, cmdAngle(it.s), aw * 0.5f, ah * 0.5f);
                }
            } else {
                std::string up = it.s;
                for (char& ch : up) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                std::string tex = "Textures/Buttons/Arcade_Buttons_" + up + "_01.png";
                if (SDL_Texture* t = g.texture(tex)) g.drawTexture(t, cx, ly + (lineH - iconH) * 0.5f, iconH, iconH);
                else g.drawLabelText(f, up, cx, ly, it.w, lineH, 1, true, c, scale, 1.0f);
            }
            cx += it.w;
        }
    }
    return lines;
}


namespace {
// The frame of a dialog view of Options.gbs: backdrop fade, the dialog window, the winged title.  What the game's code fills in (the
// slots, the arrows, the Help1 description, the slot template DisplayGRP) is hidden: the caller draws those.  Null when the view is
// not in the data.
const GbsSceneView* drawDialogFrame(GameUi& g, GbsAnimPlayer& anim, std::string& animKey, const std::string& key, const char* viewName,
                                    const std::string& title) {
    const GbsSceneView* v = nullptr;
    if (g.loadScene("Options.gbs") && g.scene()) v = g.scene()->view(viewName);
    if (!v) return nullptr;
    if (animKey != key) {
        animKey = key;
        anim.bind(v);
        for (const char* a : {"OnActivate", "ListSetup", "P1MenuIntro"}) anim.play(a);
    }
    std::string head = title;
    for (char& c : head)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    g.setAnimPlayer(&anim);
    g.drawView(*v, [&](const GbsNode& nd, NodeState& st) {
        const std::string& nm = nd.name;
        if (nm == "BGFade") {
            st.fallback = Col{0, 0, 0, 170};
        } else if (nm == "TitleTxt") {
            st.hasText = true;
            st.text = head;
        } else if (nm == "Help1" || nm == "ArrowGroup" || nm == "DisplayGRP" || nm == "HeadersGRP" || nm.rfind("DisplayTxt", 0) == 0) {
            st.hide = true;   // (DisplayGRP is the template of a slot: the game's code moves it, we draw the bar)
        }
    });
    g.setAnimPlayer(nullptr);
    return v;
}

// Selector_Left / _Center / _Right of a slot at (x, y): the two fades are 128 px, the middle fills the rest, the right fade mirrored.
void drawSelectorBar(GameUi& g, const char* fadeTex, const char* midTex, float x, float y, float w, float h, float alpha) {
    SDL_Texture* fade = g.texture(fadeTex);
    SDL_Texture* mid = g.texture(midTex);
    if (fade && mid) {
        const Col full{255, 255, 255, 255};
        g.drawTexture(fade, x, y, 128.0f, h, 0, 0, 0, false, false, full, alpha);
        g.drawTexture(mid, x + 128.0f, y, w - 256.0f, h, 0, 0, 0, false, false, full, alpha);
        g.drawTexture(fade, x + w - 128.0f, y, 128.0f, h, 0, 0, 0, true, false, full, alpha);
    } else {
        g.fillRect(x, y, w, h, Col{120, 20, 20, 90});
    }
}

// Up / down chevrons beside the list when it scrolls.
void drawScrollArrows(GameUi& g, int n, int visible, float scroll, float top, float bottom) {
    if (n <= visible) return;
    SDL_Texture* aOff = g.texture("Textures/MainMenu/arrow01.png");
    SDL_Texture* aOn = g.texture("Textures/MainMenu/arrow02.png");
    const float cx = kMovelistSlotX + kMovelistSlotW + 26.0f;
    const Col full{255, 255, 255, 255};
    if (scroll > 0.01f && aOn) g.drawTexture(aOn, cx - 13.0f, top, 26.0f, 38.0f, -90, 13, 19);
    else if (aOff) g.drawTexture(aOff, cx - 13.0f, top, 26.0f, 38.0f, -90, 13, 19, false, false, full, 0.5f);
    if (scroll < static_cast<float>(n - visible) - 0.01f && aOn) g.drawTexture(aOn, cx - 13.0f, bottom, 26.0f, 38.0f, 90, 13, 19);
    else if (aOff) g.drawTexture(aOff, cx - 13.0f, bottom, 26.0f, 38.0f, 90, 13, 19, false, false, full, 0.5f);
}
}  // namespace

bool drawMovelistDialog(GameUi& g, GbsAnimPlayer& anim, std::string& animKey, const std::string& key, const std::vector<MovelistRow>& rows,
                        int sel, float scroll, const std::string& title, int ageMs) {
    const GbsSceneView* v = drawDialogFrame(g, anim, animKey, key, "Movelist", title);
    if (!v) return false;
    const bool prevPivot = g.otterPivot();
    g.setOtterPivot(true);
    const int n = static_cast<int>(rows.size());
    sel = n > 0 ? std::clamp(sel, 0, n - 1) : 0;
    const float selY = kMovelistSlotY + (static_cast<float>(sel) - scroll) * kMovelistSlotStep;
    const bool selShown = n > 0 && selY >= kMovelistSlotY - 1.0f && selY <= kMovelistSlotY + kMovelistSlotStep * (kMovelistSlots - 1) + 1.0f;

    const float intro = std::clamp(static_cast<float>(ageMs) / 260.0f, 0.0f, 1.0f);
    if (selShown)
        drawSelectorBar(g, "Textures/Options/selector72-fade.png", "Textures/Options/selector72-middle.png", kMovelistSlotX, selY,
                        kMovelistSlotW, 72.0f, std::clamp(static_cast<float>(ageMs) / 200.0f, 0.0f, 1.0f));
    if (const GbsFont* fT = g.fontNamed("Dialogue Text")) {
        g.clipCanvas(0, kMovelistSlotY, 1280.0f, kMovelistSlotStep * kMovelistSlots);   // the list is cut to its six slots
        for (int k = 0; k < n; ++k) {
            const float y = kMovelistSlotY + (static_cast<float>(k) - scroll) * kMovelistSlotStep;
            if (y + kMovelistSlotStep <= kMovelistSlotY || y >= kMovelistSlotY + kMovelistSlotStep * kMovelistSlots) continue;
            const MovelistRow& o = rows[static_cast<size_t>(k)];
            const bool on = k == sel;
            const bool super = o.kind.rfind("Super", 0) == 0;
            const Col nameCol = super ? Col{255, 214, 120, 255} : Col{255, 255, 255, 255};
            const float a = (on ? 1.0f : 0.78f) * intro;
            const float x = kMovelistSlotX + 60.0f + static_cast<float>(o.indent);
            const float w = kMovelistSlotW - 120.0f - static_cast<float>(o.indent);
            if (o.command.empty()) {   // a plain entry (a character of the list): one line, centred in the slot
                g.drawLabelText(*fT, o.name, x, y, w, kMovelistSlotStep, 0, true, nameCol, 1.0f, a);
                continue;
            }
            g.drawLabelText(*fT, o.name, x, y - 3.0f, w, 36.0f, 0, true, nameCol, 1.0f, a);
            std::string cmd = o.command;
            for (char& c : cmd)
                if (c == '\n') c = ' ';
            drawCommandBlock(g, *fT, cmd, x, y + 33.0f, w, 1, on ? Col{255, 255, 255, 255} : Col{225, 225, 225, 255});
        }
        g.clearClip();
    }
    drawScrollArrows(g, n, kMovelistSlots, scroll, kMovelistSlotY + 30.0f, kMovelistSlotY + kMovelistSlotStep * kMovelistSlots - 68.0f);
    g.setOtterPivot(prevPivot);
    return true;
}

bool drawOptionsDialog(GameUi& g, GbsAnimPlayer& anim, std::string& animKey, const std::string& key, const std::vector<OptionRow>& rows,
                       int sel, float scroll, const std::string& title, const std::string& desc, const std::string& note, bool noteWarn, int ageMs) {
    const GbsSceneView* v = drawDialogFrame(g, anim, animKey, key, "FullScreenOptions", title);
    if (!v) return false;
    const bool prevPivot = g.otterPivot();
    g.setOtterPivot(true);
    const int n = static_cast<int>(rows.size());
    sel = n > 0 ? std::clamp(sel, 0, n - 1) : 0;
    const float step = kOptionSlotStep;
    const float selY = kMovelistSlotY + (static_cast<float>(sel) - scroll) * step;
    const bool selShown = n > 0 && selY >= kMovelistSlotY - 1.0f && selY <= kMovelistSlotY + step * (kOptionSlots - 1) + 1.0f;
    const float intro = std::clamp(static_cast<float>(ageMs) / 260.0f, 0.0f, 1.0f);
    if (selShown)
        drawSelectorBar(g, "Textures/Options/selector36-fade.png", "Textures/Options/selector36-middle.png", kMovelistSlotX, selY,
                        kMovelistSlotW, step, std::clamp(static_cast<float>(ageMs) / 200.0f, 0.0f, 1.0f));
    if (const GbsFont* fT = g.fontNamed("Dialogue Text")) {
        SDL_Texture* mark = g.texture("Textures/Posthud/arrow01.png");
        SDL_Texture* arOff = g.texture("Textures/MainMenu/arrow01.png");
        SDL_Texture* arOn = g.texture("Textures/MainMenu/arrow02.png");
        g.clipCanvas(0, kMovelistSlotY, 1280.0f, step * kOptionSlots);
        for (int k = 0; k < n; ++k) {
            const float y = kMovelistSlotY + (static_cast<float>(k) - scroll) * step;
            if (y + step <= kMovelistSlotY || y >= kMovelistSlotY + step * kOptionSlots) continue;
            const OptionRow& o = rows[static_cast<size_t>(k)];
            const bool on = k == sel;
            const Col c = !o.enabled ? Col{130, 126, 120, 255} : on ? Col{255, 255, 255, 255} : Col{226, 222, 212, 255};
            const float a = intro;
            // (the template ChoiceGRP: marker 79, label 103 (251 wide), < 345, value 375 (197 wide), > 565 -- slot x 90)
            if (on && mark) g.drawTexture(mark, kMovelistSlotX - 11.0f, y + 7.0f, 9.0f, 21.0f, 0, 0, 0, false, false, Col{255, 255, 255, 255}, a);
            const float lw = o.choice ? 251.0f : 1000.0f;
            const float ls = std::min(1.0f, lw / std::max(1.0f, g.textWidth(*fT, o.label)));
            g.drawLabelText(*fT, o.label, kMovelistSlotX + 13.0f, y, lw + 40.0f, step, 0, true, c, ls, a);
            if (!o.choice) continue;
            const float vw = 197.0f;
            const float vs = std::min(1.0f, vw / std::max(1.0f, g.textWidth(*fT, o.value)));
            g.drawLabelText(*fT, o.value, kMovelistSlotX + 285.0f, y, vw, step, 1, true, c, vs, a);
            SDL_Texture* ar = on && o.enabled ? arOn : arOff;
            if (ar) {
                g.drawTexture(ar, kMovelistSlotX + 255.0f, y + 3.0f, 21.0f, 29.0f, 0, 0, 0, true, false, Col{255, 255, 255, 255}, a);
                g.drawTexture(ar, kMovelistSlotX + 475.0f, y + 3.0f, 21.0f, 29.0f, 0, 0, 0, false, false, Col{255, 255, 255, 255}, a);
            }
        }
        g.clearClip();
        // Help1: the description of the highlighted row, centred under the list
        if (!desc.empty() || !note.empty()) {
            const std::string both = note.empty() ? desc : (desc.empty() ? note : desc + "\n" + note);
            const float bw = kMovelistSlotW - 40.0f;
            float sc = 1.0f;
            auto lines = g.wrap(*fT, both, bw, sc);
            while (lines.size() > 2 && sc > 0.6f) {
                sc -= 0.05f;
                lines = g.wrap(*fT, both, bw, sc);
            }
            const float lh = 26.0f * sc;
            float yy = 632.0f + (2.0f - static_cast<float>(std::min<size_t>(lines.size(), 2))) * lh * 0.5f;
            for (size_t i = 0; i < lines.size() && i < 2; ++i, yy += lh) {
                const bool isNote = !note.empty() && (lines.size() == 1 ? desc.empty() : i == lines.size() - 1);
                g.drawLabelText(*fT, lines[i], kMovelistSlotX + 20.0f, yy, bw, lh, 1, true,
                                isNote ? (noteWarn ? Col{255, 205, 110, 255} : Col{200, 192, 182, 255}) : Col{255, 255, 255, 255}, sc, 1.0f);
            }
        }
    }
    drawScrollArrows(g, n, kOptionSlots, scroll, kMovelistSlotY + 6.0f, kMovelistSlotY + step * kOptionSlots - 44.0f);
    g.setOtterPivot(prevPivot);
    return true;
}

}  // namespace zengine
