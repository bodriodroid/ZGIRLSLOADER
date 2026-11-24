// play.h - the Play screen: a real, playable match on top of the deterministic fight runtime.
//
// The runtime in src/core/fight.h already is the game: fixed-point positions, states, hitstun,
// blocking, meter, pushboxes, one GGPO-shaped tick per frame.  What was missing was a screen
// that feeds it input and draws the result - that is this file.
//
// Everything a fighter does comes from the user's own extracted data: <char>.foit gives the real
// states (frames, durations, pivots, hit / hurt / push boxes, damage, hitstun ...), <char>.art /
// .art-pt the pixels and <char>_Np.dds the palette.  src/core/moveset.h turns the .foit into move
// timelines and src/core/duel.h runs them at a fixed 60 Hz; this file only feeds input and draws.
// The stage comes from the .lvl next to stages.ini (spawn points, bounds, "2D <texture>" backdrop).
//
// A character whose .foit cannot be parsed (or has no usable data) falls back to the generic
// runtime and a blocky dummy, so the screen always runs - even with no game data mounted.
#pragma once
#include <SDL.h>


#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "imgscale.h"
#include "arcade.h"
#include "art.h"
#include "audio.h"
#include "music_player.h"
#include "dds.h"
#include "duel.h"
#include "confirm_dialog.h"
#include "dev_options.h"
#include "game_ui.h"
#include "movelist_dialog.h"
#include "gbs_anim.h"
#include "fight.h"
#include "fighter_light.h"
#include "foit.h"
#include "foit_duel.h"
#include "fx_gl.h"
#include "gamedata.h"
#include "lesson.h"
#include "lesson_hud.h"
#include "level.h"
#include "menu_data.h"
#include "spark_fx.h"
#include "super_fx.h"
#include "moveset.h"
#include "palfx.h"
#include "splash.h"
#include "shadow_layer.h"
#include "stage3d_gfx.h"
#include "target_switch.h"
#include "touch_pad.h"
#include "chord_assist.h"
#include "ui.h"

namespace zengine {
namespace play {

// Clip slots a fighter animation is picked from.
enum Anim {
    kIdle = 0,
    kWalkF,
    kWalkB,
    kCrouch,
    kJump,
    kFall,
    kLand,
    kAtkL,
    kAtkM,
    kAtkH,
    kHurt,
    kBlock,
    kDown,
    kWin,
    kAnimCount
};

// One character's drawable form: the parsed .foit (as a MoveSet) + .art/.art-pt bytes + palette + a texture cache.
//
// Sprites are drawn at the size they have on the real screen: positions and sizes are floats (SDL_RenderCopyExF), the textures are
// filtered (Smoothing = Smooth) and a sprite is kept at the smallest power-of-two reduction that still has a texel per screen pixel
// ("level", see imgscale.h; the Sprite quality setting can force a lower one).  The cache is a budget in bytes, evicting the frame
// that was drawn longest ago.  decode() is a pure function of the loaded data, so frames can be turned into pixels on worker threads
// (the loading screen, and a few frames ahead during a fight) while insert() / drawing stay on the render thread.
class Skin {
public:
    std::string name;
    bool ok = false;  // true when the real .foit/.art data was loaded
    std::string err;

    // One image slot of one frame, decoded to RGBA at the skin's current level.
    struct Decoded {
        std::vector<uint8_t> rgba;   // CPU route: the coloured pixels
        std::vector<uint8_t> index;  // GPU route (FxRenderer::packIndex): band + grad planes, coloured by the PAFX programs
        int w = 0, h = 0;          // pixels of `rgba` / `index` (the texture)
        int fullW = 0, fullH = 0;  // size of the art itself (what the frame's geometry is measured in)
        unsigned epoch = 0;        // configure() generation the pixels belong to
        bool ok = false;
        bool mirrored = false;     // baked for a sprite drawn flipped (only when the stage's lights have a rim: it falls on the other side)
    };

    void reset(SDL_Renderer* r);
    // Parses <c>.foit, reads .art/.art-pt and the palette `palIdx`. Never fatal: on failure ok stays
    // false and the Play screen draws the built-in dummy instead.
    bool load(const GameData& gd, const CharacterAssets& c, int palIdx, SDL_Renderer* r, std::string* errOut, const std::string& palPath = {});
    // The part of load() that needs no renderer (file reads, .foit parse, palette, move sets): safe to run on a
    // worker thread while the Play screen animates its loading screen.  The skin must have been reset() before.
    bool loadData(const GameData& gd, const CharacterAssets& c, int palIdx, std::string* errOut, const std::string& palPath = {});

    // sprite level (0 full ... 3 eighth size), filter and texture budget; changing any of them drops the cache
    void configure(SDL_Renderer* r, int level, bool smooth, size_t cacheBytes);
    int level() const { return level_; }
    unsigned epoch() const { return epoch_; }

    // GPU effects (PAFX, src/app/fx_gl.h).  With a ready FxRenderer, full-size sprites (nearest or bilinear) and floor shadows go through the
    // effect programs: the GPU gets the index planes and the palette and colours them itself, which is what makes the hue shift /
    // super flash / darkening possible.  Reduced sprites (index planes cannot be shrunk) and anything the GPU
    // refuses stay on the CPU route, so nothing ever disappears.  Pass nullptr to switch back; the skin frees its GPU textures first
    // (call this while the renderer's context is alive and before FxRenderer::shutdown()).
    void setFx(FxRenderer* fx);
    bool gpuActive() const { return gpu_; }
    // what the GPU route applies on top of the plain palette colours (default: nothing)
    void setScreenFx(const fx::ScreenFx& s) { screenFx_ = s; }
    void setHue(float cosA, float sinA, float amount) { hueCos_ = cosA, hueSin_ = sinA, hueAmount_ = amount; }
    // CPU route stand-in for the darkening of ScreenFx (the CPU cannot remap per pixel): a colour multiplier on every sprite drawn
    // afterwards (1, 1, 1 = untouched).  The GPU route ignores it - it has the exact maths in setScreenFx.
    void setTint(float r, float g, float b) { tint_[0] = r, tint_[1] = g, tint_[2] = b; }
    // CPU route stand-in for a palette effect (palfx.h): the sprite is blended toward colour (r, g, b) by `a` 0..1 (a multiply, plus
    // an additive pass for bright colours).  `a` 0 = off.  The GPU route ignores it.
    void setWash(float r, float g, float b, float a) { wash_[0] = r, wash_[1] = g, wash_[2] = b, wash_[3] = a; }
    // The super outline of the GPU route: after the normal sprite, its silhouette in two flat colours blended by `mix`, drawn with
    // `strength` 0..1 (0 = off).  Needs an effect program set that has the outline pass; otherwise nothing is drawn.
    void setSuperOutline(const float a[3], const float b[3], float mix, float strength);
    int gpuTextures() const;   // cached images that live on the GPU route (tests, the dev overlay)

    bool hasMoves() const { return ok && ms_.valid(); }
    const fight::MoveSet& moves() const { return ms_; }
    // Draws frame `idx` of the .foit with its pivot (the point between the feet) at (feetX, feetY), in screen units.
    // Returns the number of images drawn (0 = nothing visible, e.g. the .art-pt is missing).
    // `xs` / `ys` stretch the art around the feet point (a negative `xs` mirrors it; 0 draws nothing), `alpha` is 0..1: the object
    // slots XSCALE / YSCALE / ALPHA of the data driven fight (foit_duel.h)
    int drawFrame(SDL_Renderer* r, uint16_t idx, double feetX, double feetY, double scale, bool faceRight, double xs = 1.0, double ys = 1.0,
                  double alpha = 1.0);
    // The floor shadow of frame `idx`: its silhouette flattened onto the floor (PC game: FloorShadow).  `floorY` is the screen y of the
    // floor under the feet, `air` how high the fighter is above it (stage units), `flat` how far (screen px per stage unit of height) the
    // silhouette is pushed towards the camera.  fade = 0: constant alpha; 1: alpha falls linearly to 0 at height `fadeDist` (per vertex).
    int drawShadow(SDL_Renderer* r, uint16_t idx, double feetX, double floorY, double air, double scale, bool faceRight, double flat,
                   double alpha, bool fade, double fadeDist);
    // Decodes the images of frame `idx` into the cache ahead of time so a frame never has to be decoded in the
    // middle of a fight.  Returns true when something had to be decoded (the caller spreads these over ticks).
    bool prefetch(SDL_Renderer* r, uint16_t idx, bool mirrored = false);
    // Worker-thread half of prefetch: pixels of one slot (ok = false when the slot has nothing to draw).  `mirrored`: the sprite is going to be drawn
    // flipped (matters only with stage lighting, whose rim light falls on the other edge of a flipped sprite).
    Decoded decode(uint16_t frame, int slot, bool mirrored = false) const;
    // Render-thread half: turns decoded pixels into the cached texture (ignored when configure() ran in between).
    void insert(SDL_Renderer* r, uint16_t frame, int slot, Decoded&& d);
    bool cached(uint16_t frame, int slot, bool mirrored = false) const;
    // The stage's lights on this fighter (CPU route; the GPU route draws palette colours only): nullptr or a light that changes nothing = off.
    // Call between reset() and loadData() / any decode; drops the cached sprites when the lights changed.
    void setLight(SDL_Renderer* r, const FighterLight* light);
    bool lightOn() const { return lightOn_; }
    // what a frame costs in texture memory at this level (planning the preload)
    size_t frameBytes(uint16_t frame) const;
    size_t cacheUsed() const { return bytes_; }
    size_t cacheLimit() const { return cacheLimit_; }
    // the palette this skin was built with (the colours picked in the Character Select) - the HUD portrait wears the same
    const Palette& palette() const { return pal_; }
    bool hasArt() const { return !art_.empty(); }
    bool hasArtPt() const { return !artPt_.empty(); }

private:
    struct Tex {
        SDL_Texture* t = nullptr;  // CPU route
        FxIndexTex gl;             // GPU route (exactly one of the two is set for a cached image)
        int w = 0, h = 0;          // texture size
        int fullW = 0, fullH = 0;  // art size
        uint32_t use = 0;          // value of useClock_ when it was last drawn
    };
    // bit 3: the copy baked for a flipped sprite (only exists when the stage's lights have a rim)
    static uint32_t key(uint16_t frame, int slot, bool mirrored = false) {
        return (static_cast<uint32_t>(frame) << 4) | (mirrored ? 8u : 0u) | static_cast<uint32_t>(slot);
    }
    bool lightsImage(const foit::Image& im) const { return lightOn_ && !gpu_ && !lit_.empty() && !im.silhouette(); }
    // does the copy of this image depend on the sprite's orientation?
    bool orientationMatters(const foit::Image& im) const { return lightsImage(im) && !light_.dirs.empty(); }
    const Tex& image(SDL_Renderer* r, uint16_t frame, int slot, bool mirrored = false);
    void evict();
    void freeTex(Tex& t, bool sdlAlive);   // an SDL texture is destroyed only when the renderer is still there
    void freeAll(bool sdlAlive);           // every cached image and the GPU palette
    bool ensureFxPalette();
    void dropGpu();         // the GPU refused something: back to the CPU route for good (until setFx() again)
    bool wantGpu() const { return fx_ && fx_->ready() && !gpuFailed_ && level_ == 0 && (!smooth_ || fx_->hasSmooth()); }
    int drawFrameGpu(SDL_Renderer* r, uint16_t idx, double feetX, double feetY, bool faceRight, double sxk, double syk, double alpha);
    int drawShadowGpu(SDL_Renderer* r, uint16_t idx, double feetX, double floorY, double air, double scale, bool faceRight, double flat,
                      double alpha, bool fade, double fadeDist);

    fight::MoveSet ms_;
    std::vector<uint8_t> art_, artPt_, tables_;
    Palette pal_;
    FighterLight light_;         // the stage's lights (setLight)
    bool lightOn_ = false;       // light_ changes the colours and the route allows it
    std::vector<uint8_t> lit_;   // buildLitTable(pal_): which (band, grad) pixels take light
    std::unordered_map<uint32_t, Tex> tex_;
    size_t bytes_ = 0, cacheLimit_ = 160u * 1024u * 1024u;
    uint32_t useClock_ = 0;
    int level_ = 0;
    bool smooth_ = true;
    unsigned epoch_ = 1;
    FxRenderer* fx_ = nullptr;   // not owned
    bool gpu_ = false;           // the GPU route is in use (fx_ ready, level 0)
    bool gpuFailed_ = false;     // dropGpu() ran: stay on the CPU route
    FxPaletteTex palGl_;         // this skin's palette on the GPU
    ShadowLayer shadowLayer_;    // the floor shadow's off-screen layer (CPU route)
    fx::ScreenFx screenFx_;
    float hueCos_ = 1, hueSin_ = 0, hueAmount_ = 0;
    float tint_[3] = {1, 1, 1};
    float wash_[4] = {1, 1, 1, 0};   // CPU route palette effect: colour + strength (0 = off)
    float outA_[3] = {1, 1, 1}, outB_[3] = {1, 1, 1}, outMix_ = 0, outStrength_ = 0;
};

// A decoded stage backdrop, kept as a single SDL texture.
class Backdrop {
public:
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0;
    void reset(SDL_Renderer* r) {
        if (r && tex) SDL_DestroyTexture(tex);
        tex = nullptr;
        w = h = 0;
    }
    bool set(SDL_Renderer* r, const DdsImage& img, bool smooth = true) {
        reset(r);
        int tw = img.width, th = img.height;
        tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, tw, th);
        if (tex) {
            SDL_UpdateTexture(tex, nullptr, img.rgba.data(), tw * 4);
        } else {   // the GPU's largest texture is smaller than the picture (3750x2000 backdrops on a 2048 limit): half, quarter, eighth size
            for (int f = 2; f <= 8 && !tex; f *= 2) {
                int sw = 0, sh = 0;
                const std::vector<uint8_t> px = shrinkRgba(img.rgba.data(), img.width, img.height, f, sw, sh);
                if (px.empty()) break;
                tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, sw, sh);
                if (tex) {
                    SDL_UpdateTexture(tex, nullptr, px.data(), sw * 4);
                    tw = sw;
                    th = sh;
                }
            }
        }
        if (!tex) return false;
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex, smooth ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);   // the picture is shown bigger than stored
        w = tw;
        h = th;
        return true;
    }
};

enum class Phase { Select, Loading, Fight, RoundEnd, MatchEnd };

// Menus drawn over the match (play_menu.cpp), all from InGame.gbs / PrePostHud.gbs / Help.gbs.
enum class Overlay { None, Pause, Options, Help, End, GameOver, Dev, Movelist };

// How the main menu starts a match.
enum class Mode {
    VsCpu,     // P1 on the controls, CPU opponent (Arcade / CPU Quick Match)
    Versus,    // two humans (keyboard: WASD+JKL/UIO vs arrows+1-6); on touch P2 has no controls yet
    Training,  // no timer, health refills, CPU stands still-ish
    Arcade,    // P1 against the ladder of eight random CPU fights and the boss (core/arcade.h)
};

// Match rules the menu's settings feed in (OPT_STR_SPDIFFICULTY, OPT_INT_NUM_ROUNDS, OPT_STR_TIME_LIMIT).
struct Rules {
    int difficulty = 1;     // 0 easy, 1 normal, 2 hard
    int rounds = 3;         // "Number of Rounds": 1, 3, 5 or 7 -> first to (rounds + 1) / 2
    int timeLimitSec = 99;  // 0 = infinite
};

class PlayScreen {
public:
    // ---- wiring from the loader
    void setGameData(const GameData* gd) {
        gd_ = gd;
        hudTried_ = hudOk_ = false;  // the HUD scene is read again from the (new) data
        for (HudPortrait& hp : hudPortrait_) hp.stamp.clear();
    }
    void setRoster(const std::vector<CharacterAssets>& roster);
    void setLevels(const std::vector<LevelInfo>& levels);
    void setStageTable(const std::vector<StageEntry>& table) { stageTable_ = table; }   // stages.ini: menu stage id -> .lvl
    void setAudio(Audio* a) { audio_ = a; }   // the volumes of the in-match Options page are applied to it
    void setMusic(MusicPlayer* m) { music_ = m; }

    // ---- per-frame
    void update(Ui& ui, int dtMs);  // runs the 60 Hz simulation
    void render(Ui& ui, SDL_Renderer* r);
    Phase phase() const { return phase_; }
    // Android back / Escape while playing: leave the match. Returns true when it consumed it.
    // No Limits Screen: true while a match is on screen and the player turned the option on - the viewer then gives it the whole screen's
    // shape (screen_layout.h) and tells it the width with setViewWidth() before update() / render().  Menus and loading stay 16:9.
    bool wantsWide() const { return dev_.noLimits && phase_ != Phase::Select && phase_ != Phase::Loading; }
    void setViewWidth(int w);
    int viewWidth() const { return viewW_; }
    bool onBack();
    // Main-menu entry point: the menu's Character Select (charselect.cpp) picked the fighters; starts the match straight away.
    // p1 / p2: character ids ("filia"; "" or "random" = any), stage: stage id ("new_meridian", "random", "" = keep),
    // p1PalPath: real path of the colours picked for P1 ("" = the player-number default).
    void enterMatch(SDL_Renderer* r, Mode mode, const Rules& rules, const std::string& p1, const std::string& p2, const std::string& stage,
                    const std::string& p1PalPath, const std::string& p2PalPath = {});
    // True once after the player left a match (Back, or Characters on the result screen): the viewer returns to the menu.
    bool takeExit() {
        const bool e = exitToMenu_;
        exitToMenu_ = false;
        return e;
    }

    // ---- Story Mode hand-off (story_mode.cpp): one fight of a character's story, no character select
    enum class StoryOutcome { None, Won, Lost, Quit };
    // p1 / p2: names as the story writes them ("Filia", "BossMarie", "Random"); level: level stem ("" = keep the stage).
    // `r` is the renderer the fighters are loaded with (the screen may not have been drawn yet).
    void enterStory(SDL_Renderer* r, const std::string& p1, const std::string& p2, const std::string& level, int rounds, int difficulty);
    bool inStory() const { return storyMode_; }
    bool inArcade() const { return arcadeMode_; }
    // True once after the Arcade run was won to the end (the boss fell): the viewer shows the credits, then the menu.
    bool takeCredits() {
        const bool c = arcadeCredits_;
        arcadeCredits_ = false;
        return c;
    }
    // Set once the player leaves the result screen of a story fight (or presses Back during it).
    StoryOutcome storyOutcome() const { return storyOutcome_; }
    // Back to the normal modes (clears the outcome, shows the character select next time).
    void leaveStory();

    // ---- Lessons (play_lesson.cpp): Tutorials, Trials and Challenges.  A fight in the data driven engine whose second fighter and rules are run by
    // one of the game's Lua scripts (core/lesson.h); the lesson's HUD (objectives, explanation pages) is drawn over the match.
    struct LessonRun {
        std::string p1, p2, level;        // fighter names as the .ini writes them, level = the .ini's `World`
        std::string script;               // the `LuaFile` ("" = no script: a challenge's rules only)
        bool training = true;             // TrainingMode: the match never ends
        int timeLimitSec = 0;             // 0 = no clock
        int level_ai = 3;                 // what getDifficulty() answers in the scripts (the challenge's AILevel)
        bool hasRules = false;            // a challenge: `rules` apply (life / meter set-up and modifiers, locked move kinds)
        script::Rules rules;
    };
    enum class LessonOutcome { None, Done, Quit };
    void enterLesson(SDL_Renderer* r, const LessonRun& run);
    bool inLesson() const { return lessonMode_; }
    // Set once the lesson was completed (a moment after the script said so) or the player gave it up.
    LessonOutcome lessonOutcome() const { return lessonOutcome_; }
    // The player's life left when the lesson ended, 0..1 (a challenge carries it into the next fight).
    float lessonPlayerLife() const;
    void leaveLesson();

    // language of the localised menu texts ("en", "es" ...); the pause / end-of-match menus read <lang>_loc.json
    void setLanguage(const std::string& lang);

    // ---- state exposed to the smoke test
    const char* phaseName() const;
    int health(int i) const { return st_.p[i].health; }

private:
    // ---- configuration
    const GameData* gd_ = nullptr;
    SDL_Renderer* ren_ = nullptr;
    std::vector<CharacterAssets> roster_;
    std::vector<LevelInfo> levels_;
    FighterLight fighterLight_;   // the current stage's lights on the fighters (see fighter_light.h)
    std::vector<StageEntry> stageTable_;
    int levelIndexFor(const std::string& stage) const;   // resolveLevel() over levels_ + stageTable_; -1 = not found
    int randomLevelIndex() const;                        // a stage "Random" may pick
    int pick_[2] = {0, 0};         // index into roster_
    bool cpu_[2] = {false, true};  // who the AI drives
    int difficulty_ = 1;           // 0 easy, 1 normal, 2 hard
    bool training_ = false;
    int roundsToWin_ = 2;          // first to N round wins
    int timeLimitSec_ = 99;        // 0 = no clock
    std::string palPath_[2];       // colours picked in the Character Select ("" = default by player number)
    bool exitToMenu_ = false;

    // ---- gameplay
    fight::MatchState st_{};
    std::unique_ptr<fight::LocalSession> sess_;
    Phase phase_ = Phase::Select;
    Skin skin_[2];
    // ---- loading: the stage first, then the fighters' data, then their sprites (worker threads decode, this thread uploads a few
    // ms per frame, so the loading screen keeps animating).
    struct WarmResult {
        int player = 0;
        uint16_t frame = 0;
        int slot = 0;
        Skin::Decoded d;
    };
    using WarmBatch = std::future<std::vector<WarmResult>>;
    struct LoadJob {
        std::future<bool> bg;           // 1. the stage picture (decoded + upscaled off-thread)
        std::future<bool> bg3;          //    and the stage's 3D scene (meshes + textures), when the stage has one
        Stage3DGfx::Cpu cpu3;
        std::string err3;
        std::future<bool> skin[2];      // 2. the fighters' .foit / .art / palette, started once the stage is in
        std::future<int> music;         //    the stage theme decoded beside them (MusicPlayer::load)
        DdsImage bgImg;
        std::string key, bgLevel, bgFile;
        int bgSrcW = 0, bgSrcH = 0;     // size of the stage picture as stored (bgImg may be an enhanced copy)
        bool bgPano = false;            // the picture is the stage's whole front view: 1 pixel = 1 stage unit (the PC's 2D backdrops, 3750x2000)
        bool skinsStarted = false, collected = false;
        std::vector<std::pair<int, uint16_t>> warm;   // 3. (player, frame) turned into textures before the fight starts
        size_t warmNext = 0;                          //    next item to hand to a worker
        size_t warmDone = 0;                          //    items uploaded
        struct Inflight {
            WarmBatch f;
            size_t items = 0;
        };
        std::vector<Inflight> inflight;
        std::vector<WarmResult> ready;                //    decoded, waiting for their turn to be uploaded
        size_t readyPos = 0;
        int ms = 0;
        ~LoadJob() {
            for (auto& f : skin) if (f.valid()) f.wait();
            if (bg.valid()) bg.wait();
            if (bg3.valid()) bg3.wait();
            if (music.valid()) music.wait();
            for (auto& b : inflight) if (b.f.valid()) b.f.wait();
        }
    };
    std::unique_ptr<LoadJob> job_;   // declared after skin_: destroyed (= waited for) first
    // frames being decoded a few ticks ahead during a fight (after skin_: destroyed first)
    struct Prefetch {
        int player = 0;
        uint16_t frame = 0;
        WarmBatch f;
    };
    std::vector<Prefetch> pf_;
    std::string loadedKey_;          // fighters + colours + stage of the data currently loaded
    LoadingSplash splash_;
    bool splashTried_ = false;
    std::string loadLabel_;
    // Why the match is not on the stage the menu asked for (shown on the loading screen and for the first seconds of the fight, whatever
    // the Dev "stage info" switch says: a silent fall back to the generated test stage is the worst thing to debug on a phone).
    std::string stageAsked_;          // the stage id the menu / story asked for ("casino", "stage_atrium" ...)
    bool stageUnresolved_ = false;    // ... and no .lvl matched it
    uint32_t stageNoteUntilMs_ = 0;   // SDL_GetTicks() until which the problem line stays on screen
    std::string stageProblem() const; // "" when the stage is what was asked for and has its backdrop
    Backdrop bg_;
    double bgCenterU_ = 0.5;   // where the stage's centre line is across the backdrop picture (0..1)
    Backdrop shadow_;     // the fighters' floor shadow: one soft black ellipse, scaled and faded per fighter
    std::string bgName_;
    int wins_[2] = {0, 0};
    int round_ = 1;
    int endTimer_ = 0;
    int endReason_ = 0;  // 1 = K.O., 2 = timeout
    std::string endText_;
    double acc_ = 0;
    fight::DuelContent duel_;      // both fighters' move sets + stage bounds (duelMode_ only)
    bool duelMode_ = false;        // true when both fighters run on their real .foit moves
    // The fight run by the .foit data itself (core/foit_duel.h): the world, its round flow and its GGPO callbacks.  On by default when
    // both fighters have their data; ZDROID_PLAY_OLDDUEL=1 (or "oldduel" in the flags file) keeps the hand-made move tables of duel.cpp.
    std::unique_ptr<fight::FoitDuel> foit_;
    bool foitMode_ = false;
    int foitPhase_ = -1;           // the world's phase at the last tick: a change moves the cards / banners along
    std::vector<fight::FoitView> views_;
    fx::SparkFx sparks_;   // hit / block sparks and dust of the data driven fight (hitspark.foit + data/sprites)
    bool buildFoit(const LevelInfo* lv);
    void hitFlashTick();
    void syncFoitFlow();           // after every tick: the world's round flow -> phase_ / cards / wins / the end of the match
    void drawFoitView(Ui& ui, SDL_Renderer* r, const fight::FoitView& v, double sx, double sy, double s);
    // ---- the picture of a super move (presentation only: stepped per displayed tick, never saved, nothing in the sim reads it)
    fx::SuperFxTimeline superFx_;
    fx::SuperFxState superState_;
    // ---- No Limits Screen (screen_layout.h): the picture is `viewW_` logical units wide (640 = 16:9).  The world (stage, fighters, pad) uses
    // all of it; the HUD, the round banners and the menus are designed for 640 and are drawn into a transparent 640x360 target that is then
    // copied to the middle ("safe area"), with the pointer moved the same way while they are handled.
    int viewW_ = 640;
    touch::Layout baseLayout_ = touch::Layout::defaults();   // the pad's layout for a 640-wide picture (the player's own, if any)
    SDL_Texture* ovRt_ = nullptr;
    int ovW_ = 0, ovH_ = 0;
    SDL_Texture* ovPrev_ = nullptr;   // the render target that was active when the overlay began
    float ovPrevSx_ = 1.0f, ovPrevSy_ = 1.0f;
    Input ovInput_;                   // the pointer as the viewer gave it, while the overlay shows it in 640-space
    bool beginOverlay(SDL_Renderer* r, Ui& ui);
    void endOverlay(SDL_Renderer* r, Ui& ui);   // also copies the outermost columns over the side bands
    int safeX() const { return (viewW_ - 640) / 2; }

    // ---- palette effects (colour washes of a counter hit, a status ...): presentation only, stepped per displayed tick
    palfx::Library palfxLib_;
    bool palfxLoaded_ = false;     // the table was looked for (once per fight)
    palfx::FighterFx palfx_[2];
    void palFxTick();              // after every tick: reads the bodies' PALFX values and moves each effect one frame
    void applyPalFxToSkin(Skin& sk, const fight::FoitView& v);
    int superOwner_ = -1;          // side whose super is on screen (kept while the dark stage fades out), -1 = none
    fx::FxTuning neutralTun_ = fx::neutralTuning();
    bool superDemo_ = false;       // capture aid (ZDROID_PLAY_SUPERDEMO / 'superdemo'): player 1 casts a super half a second into the fight, player 2 stands still
    int superDemoTick_ = 0;
    void superFxTick();            // after every tick: reads the world's SUPERFLASH / SUPERSHADOWS and moves the timeline on
    const fx::FxTuning& fxTuning() const { return fxr_.ready() ? fxr_.tuning() : neutralTun_; }
    bool superGpu() const { return skin_[0].gpuActive() || skin_[1].gpuActive(); }
    void applySuperToSkin(Skin& sk, const fight::FoitView& v);   // screen effect / tint / outline of one object, before it is drawn
    void clearSuperOnSkins();
    void superBackdropPass(SDL_Renderer* r);   // right after the stage: darkens it (and tints it on the GPU route)
    void superTopPass(SDL_Renderer* r);        // CPU route: the flash as one overlay over the finished picture
    bool showBoxes_ = false;       // F1: draw hit (red) / hurt (green) / push (blue) boxes
    bool f1Prev_ = false;
    int hitFlash_[2] = {0, 0};
    int prevHealth_[2] = {1000, 1000};
    int compState_[2] = {-1, -1}, compTick_[2] = {0, 0};   // companion object (Umbrella's Hungern): state + own clock
    int shake_ = 0;
    uint32_t aiSeed_ = 0x9E3779B9u;
    bool padVisible_ = false;
    int chordWindow_ = 3;            // option "Super Chord Assist": ticks a lone attack press may wait for its partner (0 = off)
    fight::ChordAssist chord_[2];   // PP / KK / PPP / KKK for a thumb pair: holds a lone press back a few ticks while a super is affordable
    touch::TouchPad touchPad_;     // stick + buttons (src/core/touch_pad.h); layout from touch_layout.txt if present
    bool padForced_ = false;       // ZDROID_PLAY_PAD=1: show the pad without a touch screen (desktop / screenshots)
    std::vector<touch::Finger> fakeFingers_;  // ZDROID_PLAY_FAKETOUCH="x,y;x,y": fingers held for captures / tests
    std::vector<touch::Finger> fingers(const Ui& ui) const;
    bool startPrev_ = false;
    int stageIdx_ = 0;
    bool storyMode_ = false;                         // the fight was started by Story Mode
    // ---- Lessons (play_lesson.cpp)
    bool lessonMode_ = false;
    LessonRun lessonRun_;
    LessonOutcome lessonOutcome_ = LessonOutcome::None;
    std::unique_ptr<script::Lesson> lesson_;         // the script of the lesson, hooked into foit_
    LessonHud lessonHud_;
    int lessonDoneTicks_ = 0;                        // the "lesson complete" banner counts down, then the outcome is Done
    uint16_t lessonPadPrev_ = 0;
    void beginLessonScript();                        // after buildFoit(): loads the script (a failure is logged; the fight goes on without it)
    void lessonTick(uint16_t p1);                    // every simulated tick, before the step
    void drawLessonHud(Ui& ui);
    // ---- Arcade Mode: the ladder (core/arcade.h) decides who comes next; the match flow around it is the normal one
    bool arcadeMode_ = false, arcadeCredits_ = false;
    arcade::Ladder ladder_;
    int arcadeRounds_ = 3;                           // the player's "Number of Rounds" (the boss fight is always one)
    void beginArcade(const Rules& rules);
    void startArcadeFight();                         // the ladder's current fight: opponent, stage, AI level, rounds
    void arcadeNext();                               // the fight was won: the next one, or the ending after the boss
    void drawArcadeTag(Ui& ui, SDL_Renderer* r);     // "ARCADE MODE  3/9" / "FINAL BOSS" under the round clock
    StoryOutcome storyOutcome_ = StoryOutcome::None;
    int lastWinner_ = -1;                            // 0 / 1 once the match is over
    int findFighter(const std::string& name) const;  // roster index for a story name, -1 = none

    // ---- HUD (play_hud.cpp): the game's own InGame.gbs "HUD" view, driven by the match state
    GameUi hud_;
    bool hudTried_ = false, hudOk_ = false;
    // the big HUD portraits, composed from the fighter's palettized layers and the colour it was picked in
    struct HudPortrait {
        SDL_Texture* tex = nullptr;
        int w = 0, h = 0;
        std::string stamp;   // character + palette: rebuilt when it changes
    } hudPortrait_[2];
    const HudPortrait* hudPortrait(SDL_Renderer* r, int p);
    void hudPortraitsReset();
    float trail_[2] = {1000, 1000};   // the red "recent damage" part of the health bar, in health units
    int trailHold_[2] = {0, 0};       // ticks the trail waits before it drains
    int hudLastHp_[2] = {1000, 1000};
    bool hudHurt_[2] = {false, false};  // the side has lost health since the round began: the game's bar goes from green to yellow for good
    // the HUD view's own animations, played the way the game's HUD handler does: OnActivate sets every node's starting pose, the combo panel
    // comes in with P?_Combo_ShowAnim and leaves with P?_Combo_HideAnim (which first holds the last count for a second), a completed super
    // stock plays P?_SuperBar_Complete and a full meter loops P?_SuperMeter_FullGlow
    GbsAnimPlayer hudAnim_;
    const GbsSceneView* hudAnimView_ = nullptr;
    bool hudCombo_[2] = {false, false};   // the combo panel is out (shown or holding its last count)
    int hudComboHits_[2] = {0, 0};        // the count the panel shows (kept while it holds after the combo ends)
    std::string hudComboWord_[2];         // the combo's name (loc "Combo_<hits>", the last one that exists at or below the count)
    int hudStocks_[2] = {0, 0};
    int hudDemoT_ = 0;                    // ZDROID_PLAY_HUDDEMO: ticks since the HUD started (drives a fake super flash for screenshots)
    bool hudFlash_[2] = {false, false};   // the fighter's super flash is running: its filmstrip is on screen
    bool hudSuperOn_[2] = {false, false}; // Super_Anim_In has started and Super_Anim_Out has not finished yet
    void hudAnimStart();                  // (re)starts the HUD view's animations from the round's opening poses
    void hudMeter(int i, int& stocks, float& frac) const;   // stocks (0..5) and how far the next one is (0..1) of player i's super meter
    void hudSeekRings();                  // puts the P?_SuperBar<N>_Anim animations on the tick that matches the super meters
    bool hudDemo_ = false;            // ZDROID_PLAY_HUDDEMO: fixed mid-fight values on the HUD (screenshots)
    bool showFps_ = true;
    bool f2Prev_ = false;
    double frameMs_ = 16.7;           // smoothed frame time for the FPS read-out

    // ---- Developer Options (core/dev_options.h): read from the save (+ ZDROID_DEV_* in the environment) when a match is set up
    // and again when the in-match page changes a value
    devopt::Settings dev_;
    void refreshDev();
    void applyDevToSkins(SDL_Renderer* r);
    // GPU effects (fx_gl.h): one renderer, borrowed by both skins
    FxRenderer fxr_;
    std::string fxStatus_;            // "off", "on, ..." or why it stayed off
    void syncFx(SDL_Renderer* r);
    double spritePx_ = 1.0;           // screen pixels per art pixel at the closest camera zoom (picks the sprite level)
    std::string bgFile_;              // which picture the stage backdrop came from ("" = the generated stage)
    int bgSrcW_ = 0, bgSrcH_ = 0;     // its size as stored
    // the stage's 3D scene (stage3d_gfx.h): drawn instead of the 2D picture while use3d() is true
    Stage3DGfx stage3d_;
    bool stage3dSlow_ = false;        // Auto: the 3D pass was too slow on this device, the 2D picture takes over for the rest of the match
    int stage3dSlowFrames_ = 0;
    int stage3dTicks_ = 0;            // sim ticks since the match began: the clock of the scene's animations (presentation only, never saved)
    bool use3d() const { return stage3d_.usable() && dev_.stage3d != devopt::Stage3d::Off && !stage3dSlow_; }
    bool bgPano_ = false;             // a real 2D backdrop (3750x2000 = the .lvl's StageSize): laid over the stage 1 pixel = 1 stage unit
    // render resolution < 100 %: the match is drawn into this smaller texture, then stretched over the window
    SDL_Texture* rt_ = nullptr;
    TargetSwitch scaledSwitch_;   // beginScaled() .. endScaled()
    int rtW_ = 0, rtH_ = 0;
    bool beginScaled(SDL_Renderer* r);
    void endScaled(SDL_Renderer* r);
    void drawGeneratedStage(Ui& ui, const LevelInfo* lv, double cx, double s, int floorY, double bound);
    void drawStageInfo(Ui& ui);
    // the Developer Options page inside a match (Overlay::Dev): a scrolling list of the same rows as the menu's page
    int devSel_ = 0, devScroll_ = 0;
    void drawDev(Ui& ui, SDL_Renderer* r);
    // worker-thread job shared by the loading screen and the fight's look-ahead: every image slot of the listed (player, frame)
    static std::vector<WarmResult> decodeItems(const Skin* a, const Skin* b, const std::vector<std::pair<int, uint16_t>>& items, bool mirror0 = false,
                                               bool mirror1 = true);

    // ---- overlay menus (play_menu.cpp)
    Overlay overlay_ = Overlay::None;
    Audio* audio_ = nullptr;
    MusicPlayer* music_ = nullptr;
    int musicIdx_ = -1;           // the stage theme of this match (MusicLibrary index), -1 = none
    bool musicCutIntro_ = false;  // the .lvl's Music_InterruptIntro: cut the intro when the fight starts
    int menuSel_ = 0, overlayT_ = 0, toastT_ = 0;
    std::vector<MovelistRow> mlRows_;
    std::string mlTitle_, mlKey_;
    int mlSel_ = 0;
    float mlScroll_ = 0;
    GbsAnimPlayer mlAnim_;
    std::string toast_, lang_ = "en";
    Localization loc_;
    bool locTried_ = false;
    GbsAnimPlayer goAnim_, loadAnim_, flyAnim_;   // GameOver.gbs / LoadingScreen.gbs / FlyingText.gbs animations
    // round intro (PrePostHud.gbs): 1 = "Take N" coin + film strips, 2 = "Action!" (or "Showtime!" on the deciding round)
    GbsAnimPlayer introAnim_;
    int introStage_ = 0, introT_ = 0, introBound_ = -1;   // 3 = the fighters' entrance animations (before the "Take N" card), 1, 2 as above
    uint32_t introSeed_ = 0x51ED270Bu;                    // which entrance each fighter plays
    bool appearPrev_ = false, appearSkip_ = false;        // a button press cuts the entrances short
    int pickIntro(const fight::MoveSet& ms);              // an entrance animation of the move set (-1 = none)
    uint16_t introTimer_ = 0;     // the round clock does not run during the "Take" part
    void drawRoundIntro(Ui& ui, SDL_Renderer* r);
    void drawRoundEnd(Ui& ui, SDL_Renderer* r);   // K.O. / TIME UP in the game's winged font
    // round result banner (PrePostHud.gbs "Round Win"): the checkered plate with the "That's a wrap" / "Perfect" header, the winner line on
    // top and the verdict under it (UIHudHandler's round-result event: the verdict comes from how much life the winner has left)
    GbsAnimPlayer rwAnim_;
    bool rwOn_ = false, rwBound_ = false, rwPerfect_ = false;
    int rwDelay_ = 0;                  // ticks of the K.O. text before the banner comes in
    std::string rwHeader_, rwFooter_;
    void beginRoundWin();
    bool drawRoundWin(Ui& ui, SDL_Renderer* r);   // false: the scene / view is not in the data (the plain K.O. text stays)
    bool goBound_ = false, loadBound_ = false, flyBound_ = false;
    GbsAnimPlayer csAnim_;   // InGame.gbs "Continue Service" (the end-of-match banner)
    bool csBound_ = false;
    int loadTickMs_ = 0;
    std::string tr(const char* key, const char* fallback);
    void openOverlay(Overlay o);
    // The game's "WARNING / Are you sure?" window before something is thrown away (Exit Game, Return to Main Menu, Give Up).
    // It opens over the menu that asked (which keeps drawing underneath, deaf to input); "No" / back just closes it.
    enum class ConfirmAct { ExitMatch, ReturnToMenu, GiveUp };
    ConfirmDialog confirm_;
    ConfirmAct confirmAct_ = ConfirmAct::ExitMatch;
    void askConfirm(ConfirmAct act);
    void doConfirmed(ConfirmAct act);
    void drawConfirm(Ui& ui, SDL_Renderer* r);
    void drawOverlay(Ui& ui, SDL_Renderer* r);
    void showToast(const std::string& msg);          // a FlyingText.gbs banner ("Not available in this version")
    void drawToast(Ui& ui, SDL_Renderer* r);
    void drawPause(Ui& ui, SDL_Renderer* r);
    void makeShadowTexture(SDL_Renderer* r);
    void drawHelp(Ui& ui, SDL_Renderer* r);
    // Pause -> Command Lists...: Options.gbs "Movelist" for player 1's character (movelist_<Name>.ini), shared with the main menu.
    bool loadMovelist();                       // rows for the P1 character; false when the data has no list for it
    void drawMovelist(Ui& ui, SDL_Renderer* r);
    void drawOptions(Ui& ui, SDL_Renderer* r);
    void applyOptions();   // volumes -> audio, touch-pad size / opacity -> the pad (reads [options] of the save)
    void drawEndMenu(Ui& ui, SDL_Renderer* r);
    void drawGameOver(Ui& ui, SDL_Renderer* r);
    bool drawLoadingScene(SDL_Renderer* r, int ms, bool overStage = false);
    void drawMenuRow(float x, float y, float w, const std::string& label, bool sel, bool enabled);
    int menuInput(Ui& ui, int n, float x, float y, float w, const std::vector<char>& enabled);

    // ---- internals
    void startMatch();
    void beginLoad(const std::string& key);
    void updateLoading(int dtMs);
    void finishLoad();
    void drawLoading(Ui& ui, SDL_Renderer* r);
    float loadProgress() const;
    const char* loadStageName() const;   // "stage" / "fighters" / "sprites" while loading
    std::string loadKey() const;
    void startRound();
    void stepSim(uint16_t p1, uint16_t p2);
    void prefetchFrames();
    uint16_t humanButtons(int player, Ui& ui);
    uint16_t chordPass(int player, uint16_t raw);   // humanButtons -> ChordAssist, exactly once per simulated tick
    uint16_t aiButtons(int me);
    uint32_t aiRandom();
    void leaveMatch();
    void drawWorld(Ui& ui, SDL_Renderer* r);
    void drawHud(Ui& ui, SDL_Renderer* r);
    void drawHudFlat(Ui& ui);                  // the plain fallback HUD (no InGame.gbs mounted)
    bool drawHudScene(SDL_Renderer* r);        // the real HUD; false when the scene is not available
    void hudInit(SDL_Renderer* r);
    void hudReset();                           // new round: bars full
    void hudTick();                            // one sim tick: damage trail, combo panel slide
    std::string hudKey(int i) const;           // "painwheel", "msfortune", ... (texture name part)
    void hudPortraitFallback(SDL_Renderer* r, int p, float cx, float cy);
    void drawFps(Ui& ui);
    void drawPad(Ui& ui, SDL_Renderer* r);
    void drawDummy(Ui& ui, const fight::Fighter& f, Anim anim, int sx, int sy, double px, int player);
    void drawFighter(Ui& ui, SDL_Renderer* r, int player, double feetX, double feetY, double scale);
    void drawBoxes(Ui& ui, int player, double s, double cx, double lift);
    std::string playerName(int i) const;
};

Anim animFor(const fight::Fighter& f);

}  // namespace play
}  // namespace zengine
