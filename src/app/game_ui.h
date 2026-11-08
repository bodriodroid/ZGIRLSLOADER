// game_ui.h - draws the game's own UI scenes (the .gbs node trees from the user's data) with SDL:
// textures from ui/Win/Textures/**.dds, text with the fonts the scene embeds (metrics) and Fonts/*.dds (atlases).
//
// Nothing here ships any game content: every texture and font is read from the folders the user mounted.
// The canvas of a scene is 1280x720; setViewport() maps it onto the render target (640x360 logical = 0.5).
#pragma once
#include <SDL.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "dds.h"
#include "gbs_anim.h"
#include "gbs_scene.h"
#include "gbs_text.h"
#include "ui.h"
#include "vfs.h"

namespace zengine {

// What the caller may change about a node just before it is drawn (the scene file only holds the resting
// pose; the original animates visibility, offsets and colours from code and from the LAGG animations).
struct NodeState {
    bool hide = false;
    float dx = 0, dy = 0;       // canvas-space offset added to the node's position (children follow)
    float alpha = 1;
    Col tint{255, 255, 255, 255};
    bool additive = false;
    std::string texName;        // sprite: draw this texture (scene-style name, e.g. "Textures/MainMenu/icon01.png") instead
    bool hasText = false;       // label: text to draw (UTF-8)
    std::string text;
    Col textColor{255, 255, 255, 255};
    float textScale = 1;
    float textSqueezeX = 1;     // label: extra horizontal-only squeeze (a longer translation keeps the glyph height of the original)
    int alignOverride = -1;     // label: 0 left 1 centre 2 right
    bool skipSelf = false;      // draw children only
    float cropL = 0, cropR = 1; // sprite: only this horizontal slice (0..1, left to right on screen) is drawn - bars
    Col fallback{0, 0, 0, 0};   // sprite: flat colour drawn instead when the texture is not on disk (alpha 0 = nothing)
    SDL_Texture* rawTex = nullptr;   // sprite: draw this caller-owned texture inside the node's box instead of the scene's one
    int rawW = 0, rawH = 0;          //   (its size, for the aspect ratio)
    bool rawCover = true;            //   true = fill the box and crop the overflow, false = fit inside it (letterbox)
    bool forceBox = false;           // place the node at forceX/forceY with size forceW x forceH (parent space) whatever its resting
    float forceX = 0, forceY = 0;    //   pose or the running animation says - how the game's code drops a plate into its slot
    float forceW = 0, forceH = 0;
    int fitOverride = -1;            // label: lay the text out with this fit mode (GbsTextFit) instead of the scene's own
    float textBoxH = 0;              // label: lay the text out in a box this tall (kept centred / aligned on the node's own box)
    bool pivotAnchored = false;      // animated pose: the key's position is where the pivot sits (top-left = position - pivot), so a
                                     //   sprite whose size and pivot grow together expands about its centre (banner bars)
};

class GameUi {
public:
    GameUi() = default;
    // No SDL calls in the destructor: at shutdown the renderer (and with it every texture) is already gone.
    ~GameUi() = default;
    GameUi(const GameUi&) = delete;
    GameUi& operator=(const GameUi&) = delete;

    // Binds the renderer and the data. Drops every cached texture and the loaded scene.
    void attach(SDL_Renderer* r, const Vfs* vfs);
    void reset();

    // Reads and parses a scene file through the Vfs ("Main.gbs") and makes it the current one (fonts / texture names
    // are looked up in the current scene).  A scene that was loaded before is kept: asking for it again only switches
    // back, so views of the other scenes stay valid.
    bool loadScene(const std::string& file);
    const GbsScene* scene() const { return scene_; }
    const Vfs* vfs() const { return vfs_; }
    SDL_Renderer* renderer() const { return r_; }

    // Japanese uses the "<font>_JP" twins of the scene's fonts when they exist.
    void setJapanese(bool on) { jp_ = on; }

    // Animations: while a player is set, every node it has a pose for is drawn with that pose (position, size, rotation, pivot,
    // colour / alpha, label scale) instead of its resting one; the hook still runs first and may hide a node or add an offset.
    // The pointer is not owned; pass nullptr to go back to resting poses.
    void setAnimPlayer(const GbsAnimPlayer* p) { anim_ = p; }
    const GbsAnimPlayer* animPlayer() const { return anim_; }

    // Decodes the textures of a view ahead of time (so the first frame it appears in does not stall).  Returns how many
    // textures it had to decode; `budget` limits that per call (0 = all) so a caller can spread the work over frames.
    int preload(const GbsSceneView& v, int budget = 0);
    // Frees the textures of a view (the Attract Mode shots are 1280x720 art: keeping all of them would cost hundreds of MB).
    // They are decoded again when something asks for them.  Returns how many were freed.
    int unload(const GbsSceneView& v);

    // canvas -> target: screen = origin + canvas * scale
    void setViewport(float ox, float oy, float scale) { ox_ = ox; oy_ = oy; sc_ = scale; }
    float scale() const { return sc_; }
    float screenX(float cx) const { return ox_ + cx * sc_; }
    float screenY(float cy) const { return oy_ + cy * sc_; }

    // Texture by scene-style name ("Textures/MainMenu/icon01.png"); .png maps to the .dds on disk.  nullptr if missing.
    // A name the current scene's table places inside a texture atlas (InGame.gbs: the whole HUD) is cut out of that atlas when it
    // has no file of its own.
    SDL_Texture* texture(const std::string& name, int* w = nullptr, int* h = nullptr);
    bool hasTexture(const std::string& name);

    // ---- text (metrics are in canvas pixels at scale 1)
    const GbsFont* font(uint32_t id) const;                 // honours setJapanese()
    const GbsFont* fontNamed(const std::string& name) const;
    float textWidth(const GbsFont& f, const std::string& utf8, float scale = 1) const;
    std::vector<std::string> wrap(const GbsFont& f, const std::string& utf8, float maxWidth, float scale = 1) const;
    // Draws one line with its top-left at canvas (x, y).  Returns the width drawn.
    float drawLine(const GbsFont& f, const std::string& utf8, float x, float y, Col c, float scale = 1, float alpha = 1, float squeezeX = 1);
    // Draws a label node's text inside its box (wrapping, alignment, vertical centring).
    void drawLabelText(const GbsFont& f, const std::string& utf8, float x, float y, float w, float h, int hAlign,
                       bool vCentre, Col c, float scale = 1, float alpha = 1, float squeezeX = 1);

    // How a node's position is read.  Off (default, the older screens): the position is the node's top-left corner and only
    // the rotation uses the pivot.  On (what the scene runtime does, checked against it node by node): the position is where
    // the PIVOT sits, so the corner is position - pivot; hooks that nudged nodes by their pivot must not do it again.
    void setOtterPivot(bool on) { otterPivot_ = on; }
    bool otterPivot() const { return otterPivot_; }

    // A label laid out the way the scene runtime does it (gbs_text.h: leading, tracking, fit mode, vertical alignment), with the
    // optional shear (degrees) and drop shadow (px) the animations key.  (x, y, w, h) is the label's box in canvas pixels.
    void drawLabelBlock(const GbsFont& f, const std::string& utf8, float x, float y, float w, float h, int hAlign, int vAlign,
                        const GbsTextStyle& style, Col c, float scaleX, float scaleY, float alpha, float skewDeg = 0, int dropShadow = 0);

    // ---- sprites / scenes
    // Draws a texture stretched into the canvas rectangle.
    void drawTexture(SDL_Texture* t, float x, float y, float w, float h, float rotation = 0, float pivotX = 0,
                     float pivotY = 0, bool flipX = false, bool flipY = false, Col tint = {255, 255, 255, 255},
                     float alpha = 1, bool additive = false);
    using Hook = std::function<void(const GbsNode&, NodeState&)>;
    // `ox`,`oy` = where the parent's top-left is on the canvas.  A group's rotation (about its pivot) carries its children.
    void drawNodes(const std::vector<GbsNode>& nodes, float ox, float oy, const Hook& hook, int depth = 0);
    void drawView(const GbsSceneView& v, const Hook& hook) { drawNodes(v.nodes, 0, 0, hook); }
    // Fills a canvas rectangle with a flat colour.
    void fillRect(float x, float y, float w, float h, Col c);
    // Ring segment (or a filled disc with rInner = 0): angles in degrees clockwise from 12 o'clock, a0 < a1.
    void drawArc(float cx, float cy, float rOuter, float rInner, float a0, float a1, Col c);

private:
    struct Tex {
        SDL_Texture* t = nullptr;
        int w = 0, h = 0;
    };
    struct FontGfx {
        std::vector<Tex> pages;     // atlas pages, loaded the first time a glyph on them is drawn
        std::vector<char> tried;
    };
    bool otterPivot_ = false;
    SDL_Renderer* r_ = nullptr;
    const Vfs* vfs_ = nullptr;
    std::unordered_map<std::string, std::unique_ptr<GbsScene>> scenes_;  // every scene loaded so far, by file name
    const GbsScene* scene_ = nullptr;                                  // the current one (points into scenes_)
    std::unordered_map<std::string, Tex> tex_;
    // decoded texture atlases ("Textures/TA_1001_1.png" ...) by real path: the HUD's pictures are rectangles of them (see GbsTexture)
    std::unordered_map<std::string, std::shared_ptr<DdsImage>> atlasImg_;
    Tex loadAtlasRegion(const GbsTexture& gt);
    std::unordered_map<std::string, FontGfx> fontGfx_;
    float ox_ = 0, oy_ = 0, sc_ = 0.5f;
    bool jp_ = false;
    const GbsAnimPlayer* anim_ = nullptr;

    // Rigid transform (rotation + translation) from a node's local space to the canvas: p' = (tx + c x - s y, ty + s x + c y).
    struct Xf {
        float c = 1, s = 0, tx = 0, ty = 0;
        void apply(float x, float y, float& ox, float& oy) const { ox = tx + c * x - s * y; oy = ty + s * x + c * y; }
    };
    void drawNodesXf(const std::vector<GbsNode>& nodes, const Xf& parent, const Hook& hook, int depth);
    void fillQuad(const Xf& at, float w, float h, Col c);   // flat colour quad whose top-left / rotation come from `at`

    Tex loadDds(const std::string& realPath, int maxSize);
    bool resolveTexture(const std::string& name, std::string& real) const;
    FontGfx& gfxFor(const GbsFont& f);
    float drawGlyphs(const GbsFont& f, const std::string& s, float x, float baselineY, Col c, float scale, float alpha, bool measureOnly,
                     float squeezeX = 1);
};

}  // namespace zengine
