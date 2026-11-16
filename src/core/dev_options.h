// dev_options.h - the "Developer Options" of ZGirls Loader (C++17, no platform dependencies).
//
// One table drives everything: the page of the main menu (Settings > Developer Options), the same page inside a match
// (Pause > Help & Options > Developer Options) and the renderer.  The choices live in the [options] section of the save under
// the keys below, stored as the choice text ("Native", "High", ...), like every other setting; a missing / unknown text means the
// default.  resolve() turns them into the numbers the Play screen uses.
//
//   Render resolution  how many pixels the whole picture is drawn with before it is stretched to the window (33 / 50 / 75 / 100 %)
//   Sprite quality     how much of the fighters' art is kept in memory (Low = quarter size ... Maximum = every pixel, always)
//   Smoothing          Smooth (linear filter, sprites placed on sub-pixel positions) / Pixelated (nearest)
//   Shadows            Off / Low / Medium / High / Game (the game's own flattened-silhouette floor shadow)
//   Stage backdrop     Auto (the stage's own 2D texture, else a generated stage) / Portrait (the Character Select picture) / Generated
//   Backdrop upscale   Off / Smooth / Enhanced (bicubic 2x + light sharpening) for stage pictures smaller than the screen
//   Preloading         how many animation frames are turned into textures behind the loading screen
//   FPS, boxes, round intro, entrances, stage info, RAM display: switches   (RAM display = the seven-segment memory readout, err_overlay.h)
//   3D stages          Off (the 2D picture) / Auto (the stage's 3D scene, back to the picture when the device cannot keep up) / On (always 3D)
//   GPU effects        On: fighters and floor shadows are coloured by the PAFX effect programs on the GPU (fx_gl.h, docs/PAFX.md); Off: the CPU route
//   Stage lighting    On: the stage's lights (.lvl `Light:` lines) colour the fighters: ambient, diffuse and the rim light on their edges (fighter_light.h)
#pragma once
#include <functional>
#include <string>

namespace zengine {
namespace devopt {

enum Id : int {
    kRenderRes = 0,
    kSpriteQuality,
    kSmoothing,
    kShadows,
    kStage,
    kUpscale,
    kPreload,
    kFps,
    kBoxes,
    kRoundIntro,
    kEntrances,
    kStageInfo,
    kRam,
    kGpuFx,
    kNoLimits,
    kStage3d,
    kLighting,
    kCount
};

constexpr int kMaxChoices = 5;

struct Def {
    const char* key;                       // key in [options] of the save: "ZD_DEV_RES"
    const char* name;                      // short id used for the text keys: "res"
    int nChoices;
    const char* choices[kMaxChoices];      // stored texts, lowest to highest
    int def;                               // index of the default
    const char* label;                     // English label (the Spanish one lives in text())
};

const Def& def(Id id);
// Index of a stored text (case-insensitive); an empty / unknown text gives the default.
int indexOf(Id id, const std::string& stored);

enum class StageSource { Auto, Portrait, Generated };
enum class Upscale { Off, Smooth, Enhanced };
enum class ShadowMode { Off, Low, Medium, High, Game };
enum class Preload { Minimal, Standard, Full };
enum class Stage3d { Off, Auto, On };

// Everything the Play screen reads, resolved from the save.
struct Settings {
    double renderScale = 1.0;        // fraction of the window's pixels the picture is drawn with (1 = native)
    int spriteMinLevel = 0;          // 0 / 1 / 2: the smallest texture level a sprite may use (quality floor)
    bool spriteFullSize = false;     // "Maximum": never shrink a sprite
    size_t spriteCacheBytes = 0;     // texture memory per fighter before the oldest frames are dropped
    bool smooth = true;
    ShadowMode shadow = ShadowMode::Game;
    StageSource stage = StageSource::Auto;
    Upscale upscale = Upscale::Enhanced;
    Preload preload = Preload::Standard;
    bool fps = false, boxes = false;
    bool roundIntro = true, entrances = true;
    bool stageInfo = false;
    bool ramDebug = false;           // the seven-segment RAM / state-hash readout in the corner
    bool noLimits = false;           // No Limits Screen: a fight uses the phone's whole screen shape instead of 16:9 (screen_layout.h)
    Stage3d stage3d = Stage3d::Auto; // the stage's 3D scene (stage3d.h) behind the fighters: Off = the 2D picture, Auto = 3D unless the device is too slow for it, On = always
    bool gpuFx = false;              // sprites and shadows through the PAFX effect programs (needs an OpenGL ES 2 renderer)
    bool lighting = true;            // the stage's lights on the fighters (fighter_light.h); the GPU route does not have them yet

    // level (0 full, 1 half, 2 quarter, 3 eighth) for a sprite shown at `pxPerArtPx` screen pixels per art pixel
    int spriteLevel(double pxPerArtPx) const;
};

// `get(key)` returns the stored text of a [options] key ("" = not set).  The app wraps the save and the ZDROID_DEV_* environment.
Settings resolve(const std::function<std::string(const char* key)>& get);

// Text of the page: a menu .ini (see menu_data.h) whose labels are "@own:" keys, so the language follows the app's.
std::string menuIni();
// "@own:" strings of this module ("dev", "dev.res", "dev.res.d", "c.Native", ...).  nullptr when the key is not one of ours.
const char* text(const std::string& key, bool spanish);

}  // namespace devopt
}  // namespace zengine
