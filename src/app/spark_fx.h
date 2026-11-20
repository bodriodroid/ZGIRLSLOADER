// spark_fx.h - the sparks of a fight: hit sparks, block marks and the dust / shockwave puffs the game's `hitspark` pseudo-character
// asks for.  Presentation only: nothing here touches the match.
//
// How the game's own data drives it (all read from the user's files at runtime, nothing of them is shipped):
//   * `hitspark.foit` is a character whose states are the effects.  Its `None` state looks at slot 168 (the spark class of the hit:
//     1 light, 2 medium, 3 hard, 4 special, 5 grab, 201.. the character families, -1 / -5 a block on the ground / in the air) and
//     jumps to the state that plays that effect.  Every state steps FX32FRAME (slot 81) through the frames of its picture and the
//     object ends itself (the commit record the other helpers use).
//   * the pictures are tile sprites in data/sprites: <name>.spr.msb (the frame grids) + <name>.dds (plain RGBA atlas, 16x16 tiles).
//   * fight::FoitDuel raises one rt::Event per hit / block with that class, the point where the boxes touched and which way the
//     attacker faced.  SparkFx runs the effect objects in a world of its own (so nothing can leak into the fight's state, and a
//     rollback resimulation cannot double them) and draws them in front of the fighters.
//
// What is NOT confirmed against the shipped engine: which picture belongs to which state.  The game stores the link in code; the
// table here (kSparks in spark_fx.cpp) is read off the names and the frame counts (state `Light` has 8 frames, `hitspark_omni_l` 8).
// The pictures are stored at twice the canvas resolution (like the character select sprites), drawn at kTexScale.
#pragma once
#include <SDL.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "foit.h"
#include "foit_rt.h"
#include "vfs.h"

namespace zengine {
namespace fx {

namespace rt = foit::rt;

// stage units -> canvas pixels: x = x0 + ux * kx, y = y0 - uy * ky; `scale` sizes the picture (a spark is never squashed).
struct SparkCam {
    double x0 = 0, y0 = 0, kx = 1, ky = 1, scale = 1;
};

// Where a frame lands on the canvas (hot spot on the point, flipped when the spark faces left).  Pure: tests use it.
struct SparkQuad {
    float x = 0, y = 0, w = 0, h = 0;
    bool flip = false;
};
SparkQuad placeSpark(const SparkCam& cam, double ux, double uy, int texW, int texH, float hotX, float hotY, bool faceRight, double xscale, double yscale);

// The picture that belongs to the effect state `state` of hitspark.foit ("Light" -> "hitspark_omni_l"); nullptr = no picture (the
// state is a text / mark effect the port does not draw, or an unknown name).
const char* sparkSprite(const std::string& state);

class SparkFx {
public:
    static constexpr int kMaxLive = 24;            // the oldest effect goes when more are alive
    static constexpr float kTexScale = 0.5f;       // sprite pixels -> stage units
    static constexpr size_t kTexBudget = 64u << 20;  // bytes of composed frames kept before the least recently used picture goes

    SparkFx();
    ~SparkFx();   // no SDL calls: the owner calls release() while the renderer is alive
    SparkFx(const SparkFx&) = delete;
    SparkFx& operator=(const SparkFx&) = delete;

    // Reads hitspark.foit through the Vfs.  false = the user's data has none (no sparks, the fight is unaffected).
    bool load(const Vfs& vfs);
    bool ready() const { return loaded_; }

    void reset();                      // drops every live effect (a new round / match); pictures stay cached
    void release(bool sdlAlive = true);  // pictures too

    // One HitSpark / BlockSpark event of the fight (others are ignored).
    void spawn(const rt::Event& e);
    // One 60 Hz step of every live effect.
    void tick();
    // Draws every live effect.  Also loads one queued picture per call (the commonest ones are queued by load()), so the first hit
    // does not stall the frame that shows it.
    void draw(SDL_Renderer* r, const Vfs& vfs, const SparkCam& cam);

    size_t liveCount() const { return live_.size(); }
    size_t textureBytes() const { return texBytes_; }
    // The state name and frame the i-th live effect shows right now (tests / logs); "" when out of range.
    std::string liveState(size_t i, int* frame = nullptr) const;
    int spawned() const { return spawned_; }

private:
    struct Fx {
        rt::Object o;
    };
    struct FrameTex {
        SDL_Texture* tex = nullptr;
        int w = 0, h = 0;
        float hotX = 0, hotY = 0;   // hot spot inside the (cropped) texture, in texels
    };
    struct Sprite {
        std::vector<FrameTex> frames;
        bool ok = false;
        size_t bytes = 0;
        unsigned long long used = 0;
    };
    Sprite& sprite(SDL_Renderer* r, const Vfs& vfs, const std::string& name);
    void evict(const std::string& keep);
    void dropSprite(Sprite& s, bool sdlAlive);

    foit::Character ch_;
    bool loaded_ = false;
    rt::World world_;
    std::unique_ptr<rt::Object> dummy_;   // the "leader" the effects point at (an effect that is its own leader never leaves the world)
    std::vector<std::unique_ptr<Fx>> live_;
    int spawned_ = 0;

    std::unordered_map<std::string, Sprite> sprites_;
    std::vector<std::string> warm_;       // pictures still to load (one per draw)
    SDL_Renderer* owner_ = nullptr;       // the renderer the cached textures belong to
    size_t texBytes_ = 0;
    unsigned long long clock_ = 0;
};

}  // namespace fx
}  // namespace zengine
