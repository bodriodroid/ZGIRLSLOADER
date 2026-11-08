// attract.h - the game's opening: AttractMode.gbs, the story told in a film-reel montage (Marie and the Skull Heart, one
// shot per fighter, a flash-back montage, the logo), played with the LAGG animations of the user's own scene file.
//
// The scene has one view per shot ("shot1" ... "shot17"; "shot18" and "black" are one-tick helpers) and every view has an
// animation of the same name that runs the whole shot (fades, wipes, film reels sliding, texts coming in), so this unit only has to
// start the shots one after another, feed the label texts and play the sound cues.  Text comes from the localisation file
// (the keys are the English sentences), like everything else the original writes on screen.
//
// Play order (the file lists the views in editing order): 1 .. 17.  Nothing is bundled: every texture, font and sound is read
// from the folders the user mounted; the scene is optional (without it the game goes straight to the title).
#pragma once
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "game_ui.h"
#include "gbs_anim.h"

namespace zengine {

class AttractScreen {
public:
    using TextFn = std::function<std::string(const std::string& key, const std::string& fallback)>;

    // Reads AttractMode.gbs through `ui` (already attached to the Vfs).  false = no such scene.
    bool load(GameUi& ui);
    bool ready() const { return !order_.empty(); }

    // Starts again from the first shot.
    void start();
    // Advances the clock (any frame time; the animations run at 60 ticks per second).  False once the montage is over.
    bool update(int dtMs);
    bool finished() const { return finished_; }
    // Jumps to the end (the player pressed something).
    void skip() { finished_ = true; }

    void setText(TextFn fn) { text_ = std::move(fn); }
    // Draws the current shot (canvas 1280x720 through the viewport that is set on `ui`).
    void draw(GameUi& ui);

    // Sounds crossed since the last call: {"UISound/attract.skullheart.pop.wav", volume 0..1}.
    std::vector<std::pair<std::string, float>> takeSounds();

    int shotCount() const { return static_cast<int>(order_.size()); }
    int shotIndex() const { return cur_; }
    // For tests / captures: jumps to shot `i` (0-based) at tick `tick`.
    void jump(int i, int tick);

private:
    void enter(int i, int startTick);
    void tick();
    void warm(GameUi& ui);

    std::string file_ = "AttractMode.gbs";
    std::vector<const GbsSceneView*> order_;
    std::vector<std::string> names_;
    const GbsScene* scene_ = nullptr;
    GbsAnimPlayer player_;
    int cur_ = -1;
    int next_ = -1;         // the shot whose textures are being decoded ahead
    int prev_ = -1;         // the shot that just ended: its textures are freed once the new one is on screen
    double acc_ = 0;
    bool finished_ = true;
    TextFn text_;
    std::vector<std::pair<std::string, float>> sounds_;
};

}  // namespace zengine
