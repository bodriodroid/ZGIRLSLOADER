// stage3d_gfx.h - draws a 3D stage's backdrop (core/stage3d.h) with SDL_RenderGeometry: the textures of its meshes, one draw call per mesh.
//
// loadCpu() runs on a worker thread (reads the scene, the meshes and decodes the textures); upload() turns the result into SDL textures on the
// render thread; draw() is called every frame from the fight with the 2D camera's numbers.  When the stage's textures are not in the mounted data
// (the data player has is what it has) usable() says so and the fight keeps its 2D picture.
#pragma once
#include <SDL.h>

#include <string>
#include <vector>

#include "dds.h"
#include "gamedata.h"
#include "stage3d.h"

namespace zengine {

class Stage3DGfx {
public:
    struct Cpu {
        stage3d::Stage stage;
        struct Tex {
            std::string name;   // material, as the meshes name it
            DdsImage img;
        };
        std::vector<Tex> tex;
        std::vector<std::string> noTexture;   // materials with no texture file in the data
        std::string note;                     // a line for the log
    };

    // Worker thread.  `maxTex` caps a texture's largest side (a smaller mip is decoded when the file has one).  false + `err` when the stage has no 3D scene
    // or too little of it can be drawn with the textures at hand.
    static bool loadCpu(const GameData& gd, const std::string& levelName, int maxTex, Cpu& out, std::string* err);

    bool upload(SDL_Renderer* r, Cpu& cpu, bool smooth);
    void reset(SDL_Renderer* r);

    bool usable() const { return usable_; }
    const stage3d::Stage& stage() const { return stage_; }

    // One frame.  Returns the number of triangles handed to the renderer.
    int draw(SDL_Renderer* r, const stage3d::Fit& fit, double shakeX, double shakeY);

    // How long the last draw() took (smoothed), for the governor that drops back to the 2D picture on a slow device.
    double drawMs() const { return drawMs_; }
    size_t lastTris() const { return lastTris_; }
    const std::string& note() const { return note_; }

private:
    stage3d::Stage stage_;
    std::vector<SDL_Texture*> tex_;     // per entry of stage_.materials
    std::vector<int> pieceTex_;         // per piece: index into tex_ (-1: nothing to draw it with)
    bool usable_ = false;
    stage3d::Frame frame_;
    std::vector<SDL_Vertex> verts_;
    double drawMs_ = 0;
    size_t lastTris_ = 0;
    bool geometryWarned_ = false;
    std::string note_;
};

}  // namespace zengine
