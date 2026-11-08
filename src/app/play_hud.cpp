// play_hud.cpp - the in-match HUD of the Play screen.
//
// It is the game's own HUD: the "HUD" and "HUD_Combo" views of InGame.gbs (read from the user's UI pack by
// gbs_scene.cpp) give every position, size and texture - the health-bar frames, the character coins, the name
// plates, the round timer with its red digits, the super-stock badge, the assist lights, the round pips and the
// combo panel.  The scene file only stores the resting pose, so what changes while playing is decided here:
//
//   health bar   energy bar cropped to the health, red "recent damage" trail behind it that waits and then
//                drains, black cover over the empty part; bars empty towards the timer (outer end stays put)
//   super ring   the arc nodes (CCRA) of the scene, drawn by GameUi like any other node: a dark groove, then one ring per stock (green,
//                turquoise, violet, coral, orange).  The game shows the ring of the stock the player has and the one that is filling, and
//                it fills by putting that ring's P?_SuperBar<N>_Anim animation on the tick that matches the value (the sweep runs from
//                155 degrees at tick 1 to 0 at tick 1000), so nothing about the ring is drawn by hand here
//   timer        Timer_Number_Overlay_<d> + Timer_Number_Glow_<d> for the tens and the ones
//   names        Textures/HUD/name_<char>.png, coin symbol Textures/HUD/character_symbol_<char>01.png, big
//                portrait Textures/HUD/character_<char>01.png (names the exe builds with "%s")
//
// Any texture that is not on disk falls back to a flat colour / the 8x8-free game font at the same place, so the
// layout is right even with a UI pack that has no Textures/HUD folder; the portrait then is the fighter's own
// idle sprite.  Nothing from the game is bundled.
#include "play.h"
#include "hud_art.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace zengine {
namespace play {

namespace {

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
bool has(const std::string& s, const char* p) { return s.find(p) != std::string::npos; }

// "P1_Bar" / "P2 Win Pip Frame 1" -> 0 / 1, anything else -> -1
int playerOf(const std::string& s) {
    if (s.size() > 3 && s[0] == 'P' && (s[1] == '1' || s[1] == '2') && (s[2] == '_' || s[2] == ' ')) return s[1] - '1';
    return -1;
}

// Crops a bar sprite to the fraction range [a, b] measured from the outer end (the end next to the coin).
void cropBar(NodeState& st, int player, float a, float b) {
    a = std::clamp(a, 0.0f, 1.0f);
    b = std::clamp(b, 0.0f, 1.0f);
    if (player == 0) {
        st.cropL = a;
        st.cropR = b;
    } else {
        st.cropL = 1.0f - b;
        st.cropR = 1.0f - a;
    }
}

}  // namespace

std::string PlayScreen::hudKey(int i) const {
    std::string n;
    if (i >= 0 && i < 2 && pick_[i] >= 0 && pick_[i] < static_cast<int>(roster_.size())) n = roster_[pick_[i]].name;
    if (n.empty() && i >= 0 && i < 2) n = skin_[i].name;
    for (char& c : n) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return n;
}

void PlayScreen::hudInit(SDL_Renderer* r) {
    if (hudTried_) return;
    hudTried_ = true;
    hudOk_ = false;
    if (!gd_ || !r) return;
    hud_.attach(r, &gd_->vfs());
    if (!hud_.loadScene("InGame.gbs") || !hud_.scene() || !hud_.scene()->view("HUD")) {
        std::printf("[play] HUD: InGame.gbs not found in the mounted data - using the plain HUD\n");
        return;
    }
    hudOk_ = true;
    int found = 0, total = 0;
    for (const GbsTexture& t : hud_.scene()->textures) {
        if (t.name.find("Textures/HUD/") != 0) continue;
        ++total;
        if (hud_.hasTexture(t.name)) ++found;
    }
    if (found == 0)
        std::printf("[play] HUD: InGame.gbs layout loaded, but none of its %d Textures/HUD files are mounted (mount your ui/Win folder) - flat colours\n", total);
    else
        std::printf("[play] HUD: InGame.gbs, %d of %d HUD textures found\n", found, total);
}

void PlayScreen::hudReset() {
    for (int i = 0; i < 2; ++i) {
        trail_[i] = static_cast<float>(std::clamp(st_.p[i].health, 0, 1000));
        trailHold_[i] = 0;
        hudLastHp_[i] = st_.p[i].health;
        hudCombo_[i] = false;
        hudComboHits_[i] = 0;
        hudStocks_[i] = std::clamp(st_.p[i].meter, 0, 5000) / 1000;
        hudFlash_[i] = hudSuperOn_[i] = false;
    }
    hudAnimView_ = nullptr;   // the animations restart from OnActivate the next time the HUD is ticked / drawn
}

// OnActivate gives every node of the view its opening pose (the combo panels and the character filmstrips parked off screen, the round pips and
// the glows at alpha 0 ...); the one-fighter animation drops the single health plate into its slot.  Both run for two ticks in the game.
void PlayScreen::hudAnimStart() {
    const GbsSceneView* v = hudOk_ && hud_.scene() ? hud_.scene()->view("HUD") : nullptr;
    hudAnimView_ = v;
    hudAnim_.bind(v);
    if (!v) return;
    hudAnim_.play("OnActivate");
    hudAnim_.play("P1_OneFighterAnim");
    hudAnim_.play("P2_OneFighterAnim");
    hudAnim_.tick();
    hudAnim_.tick();
    if (!hudAnim_.finished("P2_OneFighterAnim")) hudAnim_.tick();
    hudSeekRings();
    for (int p = 0; p < 2; ++p)
        if (hudStocks_[p] >= 5) hudAnim_.play(p == 0 ? "P1_SuperMeter_FullGlow" : "P2_SuperMeter_FullGlow");
}

// stocks (0..5) and how far the next one is (0..1); ZDROID_PLAY_HUDDEMO fakes a mid-fight meter for screenshots
void PlayScreen::hudMeter(int i, int& stocks, float& frac) const {
    const int meter = std::clamp(st_.p[i].meter, 0, 5000);
    stocks = meter / 1000;
    frac = stocks >= 5 ? 0.0f : static_cast<float>(meter % 1000) / 1000.0f;
    if (hudDemo_) {
        stocks = i == 0 ? 2 : 5;
        frac = i == 0 ? 0.35f : 0.0f;
    }
}

// The game keeps one ring animation per stock ("P1_SuperBar3_Anim" ...), 1000 ticks long, whose sweep goes from the full arc (tick 1) down to
// nothing (tick 1000).  A stock the player already has sits on tick 1 (full) - only the highest of them is visible - and the one that is
// filling is put on tick 1000 - floor(fraction * 1000), clamped to 1..1000; stocks beyond that rest on tick 1000 (nothing to draw).
void PlayScreen::hudSeekRings() {
    if (!hudAnimView_) return;
    for (int i = 0; i < 2; ++i) {
        int stocks = 0;
        float frac = 0;
        hudMeter(i, stocks, frac);
        for (int k = 1; k <= 5; ++k) {
            int tick = 1000;
            if (k <= stocks) tick = 1;
            else if (k == stocks + 1) tick = std::clamp(1000 - static_cast<int>(frac * 1000.0f), 1, 1000);
            hudAnim_.seek("P" + std::to_string(i + 1) + "_SuperBar" + std::to_string(k) + "_Anim", tick);
        }
    }
}

void PlayScreen::hudTick() {
    for (int i = 0; i < 2; ++i) {
        const int hp = std::clamp(st_.p[i].health, 0, 1000);
        const float h = static_cast<float>(hp);
        if (hp > hudLastHp_[i]) {
            trail_[i] = h;  // healed / refilled: no trail
        } else if (hp < hudLastHp_[i]) {
            trailHold_[i] = 34;  // hit: the old value stays as the red trail for a moment
        }
        hudLastHp_[i] = hp;
        if (trail_[i] < h) trail_[i] = h;
        if (trailHold_[i] > 0) {
            --trailHold_[i];
        } else if (trail_[i] > h) {
            trail_[i] = std::max(h, trail_[i] - std::max(5.0f, (trail_[i] - h) * 0.07f));
        }
    }
    if (!hudOk_) return;
    if (hudAnimView_ == nullptr || hudAnim_.view() != hudAnimView_) hudAnimStart();
    if (!hudAnimView_) return;
    ++hudDemoT_;
    for (int i = 0; i < 2; ++i) {
        const char* side = i == 0 ? "P1_" : "P2_";
        // combo panel: in while the victim is being comboed, out a second after the combo ended
        const bool on = (st_.p[i].comboCount > 1 && !st_.roundOver) || hudDemo_;
        if (on) {
            const int hits = hudDemo_ ? (i == 0 ? 7 : 124) : st_.p[i].comboCount;
            if (hits != hudComboHits_[i] || hudComboWord_[i].empty()) {
                hudComboWord_[i].clear();
                for (int n = std::min(hits, 999); n >= 2 && hudComboWord_[i].empty(); --n) hudComboWord_[i] = tr(("Combo_" + std::to_string(n)).c_str(), "");
            }
            hudComboHits_[i] = hits;
            if (!hudCombo_[i]) {
                hudAnim_.play(std::string(side) + "Combo_ShowAnim");
                hudCombo_[i] = true;
            }
        } else if (hudCombo_[i]) {
            hudAnim_.play(std::string(side) + "Combo_HideAnim");
            hudCombo_[i] = false;
        }
        // super flash: the character's filmstrip slides in, its face flickers while the flash runs, then it slides out
        bool flash = foitMode_ && foit_ && foit_->superFlash(i);
        if (hudDemo_) flash = i == 0 && hudDemoT_ >= 200 && hudDemoT_ < 320;   // screenshots: P1 flash from tick 200 to 320
        if (flash != hudFlash_[i]) {
            hudFlash_[i] = flash;
            if (flash) {
                hudAnim_.stop(std::string(side) + "Super_Anim_Out");
                hudAnim_.play(std::string(side) + "Super_Anim_In");
                hudSuperOn_[i] = true;
            } else if (hudSuperOn_[i]) {
                hudAnim_.stop(std::string(side) + "Super_Anim_In");
                hudAnim_.play(std::string(side) + "Super_Anim_Out");
            }
        }
        if (hudSuperOn_[i] && !hudFlash_[i] && hudAnim_.finished(std::string(side) + "Super_Anim_Out")) hudSuperOn_[i] = false;
        // super meter: a stock that fills flashes the coin, a full meter keeps glowing until some of it is spent
        const int stocks = std::clamp(st_.p[i].meter, 0, 5000) / 1000;
        if (stocks != hudStocks_[i]) {
            if (stocks >= 5) {
                hudAnim_.play(std::string(side) + "SuperMeter_FullGlow");
            } else {
                if (hudStocks_[i] >= 5) hudAnim_.play(std::string(side) + "SuperMeter_FullGlow_Off");
                if (stocks > hudStocks_[i]) hudAnim_.play(std::string(side) + "SuperBar_Complete");
            }
            hudStocks_[i] = stocks;
        }
    }
    hudSeekRings();
    hudAnim_.tick();
}

void PlayScreen::hudPortraitsReset() {
    for (HudPortrait& hp : hudPortrait_) {
        if (hp.tex) SDL_DestroyTexture(hp.tex);
        hp = HudPortrait{};
    }
}

// The game keeps the big portraits as three 8-bit layers (character_<name>_colors|lines|shade.pcx) and paints them with the palette
// of the colour the fighter was picked in; this does the same with the user's own files.  nullptr = the layers are not mounted.
const PlayScreen::HudPortrait* PlayScreen::hudPortrait(SDL_Renderer* r, int p) {
    if (!gd_ || !r || p < 0 || p > 1) return nullptr;
    const Skin& sk = skin_[p];
    const Palette& pal = sk.palette();
    const std::string key = hudKey(p);
    if (key.empty() || pal.tones <= 0) return nullptr;
    uint32_t h = 2166136261u;
    for (uint8_t b : pal.rgb) h = (h ^ b) * 16777619u;
    const std::string stamp = key + "#" + std::to_string(h);
    HudPortrait& hp = hudPortrait_[p];
    if (hp.stamp == stamp) return hp.tex ? &hp : nullptr;
    if (hp.tex) SDL_DestroyTexture(hp.tex);
    hp = HudPortrait{};
    hp.stamp = stamp;   // a failed build is remembered too, so the files are not re-read every frame
    hudart::Pcx layer[3];
    static const char* const kLayer[3] = {"colors", "lines", "shade"};
    for (int i = 0; i < 3; ++i) {
        std::vector<uint8_t> bytes;
        std::string err;
        if (!gd_->vfs().read("character_" + key + "_" + kLayer[i] + ".pcx", bytes) || !hudart::decodePcx(bytes.data(), bytes.size(), layer[i], &err)) return nullptr;
    }
    hudart::Image img;
    if (!hudart::composePortrait(layer[0], layer[1], layer[2], pal, img)) return nullptr;
    SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, img.w, img.h);
    if (!t) return nullptr;
    SDL_UpdateTexture(t, nullptr, img.rgba.data(), img.w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
    hp.tex = t;
    hp.w = img.w;
    hp.h = img.h;
    return &hp;
}

// The fighter's own idle sprite, chest-up, where the big portrait goes (used when Textures/HUD is not mounted).
void PlayScreen::hudPortraitFallback(SDL_Renderer* r, int p, float cx, float cy) {
    Skin& sk = skin_[p];
    if (!sk.hasMoves()) return;
    const fight::MoveSet& m = sk.moves();
    if (m.id.stand < 0) return;
    const uint16_t fi = m.frameAt(m.id.stand, 0);
    const foit::Frame* fr = m.frame(fi);
    if (fi == fight::kNoFrame || !fr) return;
    int top = 0;
    for (const foit::Box& b : fr->hurt) top = std::min(top, static_cast<int>(b.top));
    const float H = static_cast<float>(top < -60 ? -top : 330);
    const float k = 250.0f / H;               // canvas px per sprite px: the top 38% of the body fills the coin
    const float feetCanvasY = cy - 70.0f + H * k;
    const SDL_Rect clip{static_cast<int>(std::lround(hud_.screenX(cx - 66))), static_cast<int>(std::lround(hud_.screenY(cy - 84))),
                        static_cast<int>(std::lround(132 * hud_.scale())), static_cast<int>(std::lround(112 * hud_.scale()))};
    SDL_RenderSetClipRect(r, &clip);
    sk.drawFrame(r, fi, static_cast<int>(std::lround(hud_.screenX(cx))), static_cast<int>(std::lround(hud_.screenY(feetCanvasY))),
                 static_cast<double>(k * hud_.scale()), p == 0);
    SDL_RenderSetClipRect(r, nullptr);
}

bool PlayScreen::drawHudScene(SDL_Renderer* r) {
    hudInit(r);
    if (!hudOk_) return false;
    const GbsScene* sc = hud_.scene();
    const GbsSceneView* v = sc ? sc->view("HUD") : nullptr;
    if (!v) return false;
    if (hudAnimView_ != v || hudAnim_.view() != v) hudAnimStart();
    hud_.setViewport(0, 0, 0.5f);

    // ---- what the bars show
    const int secs = std::clamp((static_cast<int>(st_.roundTimer) + 59) / 60, 0, 99);
    const bool clock = !training_ && timeLimitSec_ > 0;
    float hp[2], tr[2];
    int stocks[2];
    float stockFrac[2];
    std::string key[2], nameKey[2];
    for (int i = 0; i < 2; ++i) {
        hp[i] = static_cast<float>(std::clamp(st_.p[i].health, 0, 1000)) / 1000.0f;
        tr[i] = std::max(hp[i], trail_[i] / 1000.0f);
        hudMeter(i, stocks[i], stockFrac[i]);
        key[i] = hudKey(i);
        nameKey[i] = key[i];
        if (!hud_.texture("Textures/HUD/name_" + key[i] + ".png")) nameKey[i] = "unknown";
    }
    if (hudDemo_) {  // ZDROID_PLAY_HUDDEMO: show a mid-fight HUD (health, trail, meter, combo, round pips) for screenshots
        hp[0] = 0.72f; tr[0] = 0.72f; hp[1] = 0.38f; tr[1] = 0.62f;
    }
    float bx[2] = {-5, 723}, by[2] = {0, 0};
    if (const GbsNode* b = v->find("P1_Bar")) { bx[0] = b->x; by[0] = b->y; }
    if (const GbsNode* b = v->find("P2_Bar")) { bx[1] = b->x; by[1] = b->y; }
    float tx = 538, ty = 0;
    if (const GbsNode* t = v->find("Timer")) { tx = t->x; ty = t->y; }
    const GbsFont* hudFont = hud_.fontNamed("RityRemix Ingame Hud");
    const GbsFont* nameFont = hud_.fontNamed("Guilded Header");

    // the last key a named animation of this view gives a node: where the game's code leaves it
    auto restKey = [&](const char* animName, uint32_t id) -> const GbsKey* {
        for (const GbsAnim& a : v->anims) {
            if (a.name != animName) continue;
            for (const GbsChannel& ch : a.channels)
                if (ch.nodeId == id && !ch.keys.empty()) return &ch.keys.back();
        }
        return nullptr;
    };

    auto hook = [&](const GbsNode& n, NodeState& st) {
        const std::string& nm = n.name;
        if (nm == "Vingette") {   // the dark vignette of a super flash (both sides share it): in while either filmstrip is out
            if (!hudSuperOn_[0] && !hudSuperOn_[1]) st.hide = true;
            return;
        }
        if (nm == "P1_Filmstrip" || nm == "P2_Filmstrip") {
            if (!hudSuperOn_[nm[1] - '1']) st.hide = true;
            return;
        }
        if (startsWith(nm, "P1_Face0") || startsWith(nm, "P2_Face0")) {   // the character's face on the filmstrip (382x796 art of the UI pack)
            const int fp = nm[1] - '1';
            if (nm.back() == '1') {
                st.texName = "super_" + key[fp] + "01.png";   // lives in the pack's true_color_hud folder, found by its file name
                if (!hud_.texture(st.texName)) st.hide = true;
            } else {
                st.hide = true;   // Face02OFF / Face03: the texture the game puts there is not known, a bare quad would show
            }
            return;
        }
        if (nm == "rotary1" || nm == "rotary2" || nm == "PauseGRP" ||
            nm == "P1_Super" || nm == "P2_Super" || nm == "P1_Match_Win_GRP" ||
            nm == "P2_Match_Win_GRP" || has(nm, "Hungern") || has(nm, "Dahlia") || has(nm, "Win Pip") ||
            has(nm, "Round Win") || has(nm, "Super Meter") || has(nm, "Gamertag")) {
            st.hide = true;
            return;
        }
        // ---- round timer
        if (startsWith(nm, "timer_")) {
            if (!clock) { st.hide = true; return; }
            const bool tens = has(nm, "tens"), overlay = has(nm, "overlay");
            const int d = tens ? secs / 10 : secs % 10;
            st.texName = std::string("Textures/HUD/Timer_Number_") + (overlay ? "Overlay_" : "Glow_") + std::to_string(d) + ".png";
            if (overlay && tens && !hud_.texture(st.texName) && hudFont) {  // no digit art: both digits as text
                const std::string txt = std::to_string(secs / 10) + std::to_string(secs % 10);
                const float w = hud_.textWidth(*hudFont, txt, 2.4f);
                hud_.drawLine(*hudFont, txt, tx + 59 + (84 - w) / 2, ty + 36, Col{255, 80, 70, 255}, 2.4f);
            }
            return;
        }
        const int p = playerOf(nm);
        if (p < 0) return;
        const std::string suf = nm.substr(3);
        const std::string pk = key[p];

        if (suf == "Hammer" || startsWith(suf, "F2_") || startsWith(suf, "F3_") || suf == "F1_bloodbar2" || suf == "F1_HealthDivide" ||
            suf == "Two_Fighters" || suf == "Gauge" || suf == "Stun_Glow" || suf == "Black_Cover") {
            st.hide = true;
            return;
        }
        if (startsWith(suf, "SuperBar_") && suf.size() == 10) {  // the ring of one stock (SuperBar_0 is the groove, always there)
            const int k = suf[9] - '0';
            if (k > 0 && k != stocks[p] && k != stocks[p] + 1) st.hide = true;   // the game shows the stock the player has and the one that fills
            return;
        }
        if (suf == "SuperBallFull") {  // the glow over the coin: its alpha comes from SuperBar_Complete / SuperMeter_FullGlow
            st.additive = true;
            return;
        }
        if (suf == "Black_Background") { st.fallback = Col{5, 5, 8, 255}; return; }
        if (suf == "F1_energybar") { cropBar(st, p, 0.0f, hp[p]); st.fallback = Col{64, 184, 72, 255}; return; }
        if (suf == "F1_bloodbar") { cropBar(st, p, hp[p], tr[p]); st.fallback = Col{210, 60, 40, 255}; return; }
        if (suf == "F1_blackbar") { cropBar(st, p, tr[p], 1.0f); st.fallback = Col{6, 6, 9, 255}; return; }
        if (suf == "PortraitBg") {
            if (!hud_.texture("Textures/HUD/character_coin01.png")) {
                hud_.drawArc(bx[p] + n.x + n.w / 2, by[p] + n.y + n.h / 2, 74, 0, 0, 360, Col{22, 22, 30, 255});
                hud_.drawArc(bx[p] + n.x + n.w / 2, by[p] + n.y + n.h / 2, 76, 70, 0, 360, Col{110, 108, 100, 255});
            }
            return;
        }
        if (suf == "coin") {
            st.texName = "Textures/HUD/character_symbol_" + nameKey[p] + "01.png";
            return;
        }
        if (suf == "name") {
            st.texName = "Textures/HUD/name_" + nameKey[p] + ".png";
            if (!hud_.texture(st.texName) && nameFont) {
                const std::string txt = playerName(p);
                const float sc2 = 0.62f;
                const float w = hud_.textWidth(*nameFont, txt, sc2);
                hud_.fillRect(bx[p] + n.x + 2, by[p] + n.y + 8, n.w - 4, 22, Col{14, 14, 18, 220});
                hud_.drawLine(*nameFont, txt, bx[p] + n.x + (n.w - w) / 2, by[p] + n.y + 9, Col{222, 218, 200, 255}, sc2);
            }
            return;
        }
        if (suf == "SuperNum") {
            st.texName = "Textures/HUD/super_stock_numbers_0" + std::to_string(stocks[p]) + ".png";
            if (!hud_.texture(st.texName) && hudFont) {
                const float cx = bx[p] + n.x + n.w / 2, cy = by[p] + n.y + n.h / 2;
                hud_.drawArc(cx, cy, 22, 0, 0, 360, Col{18, 70, 28, 255});
                hud_.drawArc(cx, cy, 22, 19, 0, 360, Col{120, 118, 110, 255});
                const std::string txt = std::to_string(stocks[p]);
                const float w = hud_.textWidth(*hudFont, txt, 1.3f);
                hud_.drawLine(*hudFont, txt, cx - w / 2, cy - 18, Col{110, 255, 130, 255}, 1.3f);
            }
            return;
        }
        if (suf == "One_Fighter") {  // single fighter per side: the game's own P?_OneFighterAnim drops this plate over the two sub-bar slots
            if (const GbsKey* k = restKey(p == 0 ? "P1_OneFighterAnim" : "P2_OneFighterAnim", n.id)) {
                st.forceBox = true;
                st.forceX = k->x; st.forceY = k->y; st.forceW = k->w; st.forceH = k->h;
            }
            return;
        }
        if (suf == "bigportrait") {
            const std::string tn = "Textures/HUD/character_" + pk + "01.png";
            if (hud_.texture(tn)) {
                st.texName = tn;
            } else if (const HudPortrait* hp = hudPortrait(r, p)) {   // the palettized layers, painted in the picked colour
                st.rawTex = hp->tex;
                st.rawW = hp->w;
                st.rawH = hp->h;
                st.rawCover = false;
            } else {
                st.hide = true;
                hudPortraitFallback(r, p, bx[p] + (p == 0 ? 96.0f : 466.0f), by[p] + 84.0f);
            }
            return;
        }
    };
    hud_.setAnimPlayer(&hudAnim_);
    hud_.drawView(*v, hook);
    hud_.setAnimPlayer(nullptr);   // node ids are per view: the poses above must not leak onto the views drawn below

    // ---- round pips: an empty frame per round needed, the winner's symbol in the ones already won
    if (wins_[0] + wins_[1] > 0 || hudDemo_) {
        for (int p = 0; p < 2; ++p) {
            for (int k = 0; k < roundsToWin_; ++k) {
                const float x = p == 0 ? 500.0f - 38.0f * k : 742.0f + 38.0f * k, y = 125.0f;
                if (SDL_Texture* t = hud_.texture("Textures/HUD/win_circle01.png")) hud_.drawTexture(t, x, y, 37, 37);
                else hud_.drawArc(x + 18.5f, y + 18.5f, 18, 15, 0, 360, Col{150, 148, 140, 255});
                if (k < wins_[p] || (hudDemo_ && p == 0 && k == 0)) {
                    if (SDL_Texture* t = hud_.texture("Textures/HUD/character_symbol_" + nameKey[p] + "01.png")) hud_.drawTexture(t, x + 3, y + 3, 32, 32);
                    else hud_.drawArc(x + 18.5f, y + 18.5f, 12, 0, 0, 360, Col{236, 200, 80, 255});
                }
            }
        }
    }

    // ---- combo panel digits (the frame/filmstrip came with the HUD view above)
    if (const GbsSceneView* cv = sc->view("HUD_Combo")) {
        for (int p = 0; p < 2; ++p) {
            const std::string hideName = p == 0 ? "P1_Combo_HideAnim" : "P2_Combo_HideAnim";
            if (!hudCombo_[p] && !hudAnim_.running(hideName)) continue;
            const GbsNode* g = cv->find(p == 0 ? "P1_Combo" : "P2_Combo");
            const GbsNode* hg = v->find(p == 0 ? "P1_Combo" : "P2_Combo");   // the panel's group in the HUD view: the animations move it
            if (!g || !hg) continue;
            const GbsPose* ps = hudAnim_.pose(hg->id);
            const float gx = ps ? ps->x : g->x, gy = ps ? ps->y : g->y;
            const int hits = std::clamp(hudComboHits_[p], 0, 999);
            auto digitHook = [&](const GbsNode& n, NodeState& st) {
                const std::string& nm = n.name;
                if (has(nm, "Text Holder")) { st.hide = true; return; }
                if (has(nm, "Label")) {   // the combo's name under the count; the "Backer" is its dark shadow
                    if (hudComboWord_[p].empty()) { st.hide = true; return; }
                    st.hasText = true;
                    st.text = hudComboWord_[p];
                    const bool backer = has(nm, "Backer");
                    st.textColor = backer ? Col{0, 0, 0, 200} : Col{255, 236, 170, 255};
                    if (backer) { st.dx = 1.5f; st.dy = 1.5f; }
                    return;
                }
                const bool back = has(nm, "_Back");
                int d = -1;
                if (has(nm, "Hundreds")) d = hits >= 100 ? hits / 100 : -1;
                else if (has(nm, "Tens")) d = hits >= 10 ? (hits / 10) % 10 : -1;
                else if (has(nm, "Ones")) d = hits % 10;
                else return;
                if (d < 0) { st.hide = true; return; }
                st.texName = "Textures/HUD/combo_number_0" + std::to_string(d) + (back ? "_black.png" : "_color.png");
                if (has(nm, "Ones") && !back && !hud_.texture(st.texName)) {  // no digit art: the count as text
                    if (const GbsFont* cf = hud_.fontNamed("ComboText")) {
                        const std::string txt = std::to_string(hits);
                        const float w = hud_.textWidth(*cf, txt, 1.6f);
                        hud_.drawLine(*cf, txt, gx + 278 - w, gy + 52, Col{255, 214, 90, 255}, 1.6f);
                    }
                }
            };
            hud_.drawNodes(g->kids, gx, gy, digitHook);
        }
    }
    return true;
}

void PlayScreen::drawFps(Ui& ui) {
    if (!showFps_) return;
    const int fps = static_cast<int>(std::lround(1000.0 / std::max(1.0, frameMs_)));
    char buf[64];
    std::snprintf(buf, sizeof buf, "FPS %d  %.1fms  sim 60Hz", fps, frameMs_);
    ui.text(5, 2, buf, Col{232, 96, 96, 230});
}

void PlayScreen::drawHud(Ui& ui, SDL_Renderer* r) {
    if (!drawHudScene(r)) drawHudFlat(ui);
    drawFps(ui);
}

}  // namespace play
}  // namespace zengine
