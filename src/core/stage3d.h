// stage3d.h - the 3D backdrop of a stage, as geometry the 2D fight camera can look at (C++17, no platform dependencies).
//
// A 3D stage folder holds `background.sgi.msb` (sgscene.h: which meshes, where) and one `<mesh>.sgm.msb` per instance (sgmesh.h).  load() turns
// that into world-space triangle lists ("pieces", still pose: skeleton animations are not played yet).  fitCamera() then places a pinhole camera
// so that the *fight plane* - the quad every scene carries under the name "FightPlane", which is the stage's whole 3750x2000 front view - lands
// exactly where the 2D fight camera (zoom `s`, pan `cx`, floor line `floorY`) puts the stage, and render() projects, clips, sorts back to front and
// splits the triangles that perspective would bend into flat batches a 2D renderer can draw.
//
// Units: the meshes are in "mesh units"; the stage is 16 stage units per mesh unit on the newer scenes and ~14.3 on the older ones (it is simply
// StageSize.x / width of the FightPlane quad, so nothing is hard coded).  The scene's camera looks down -z; +y is up; the fight plane is
// usually z = 0 (z = 90 on the oldest scene).
//
// Nothing here draws and no game data is included.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "sganim.h"

namespace zengine {
namespace stage3d {

struct Vtx {
    float p[3];
    float uv[2];
    uint8_t c[4];
};

// One mesh instance in its place in the world.
struct Piece {
    std::string name, material;      // node name ("BGMesh2"), texture name ("asg_labs")
    std::vector<Vtx> v;              // world space
    std::vector<uint16_t> idx;       // 3 per triangle
    float lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};   // world bounding box
    bool tiled = false;              // its texture coordinates run well past 0..1: the texture repeats (floors, water, fog); a mere overshoot is a baked picture's edge
    bool skinned = false;            // drawn in its bind pose
    bool animated = false;           // the scene gives it an "idle" animation
    int anim = -1;                   // index into Stage::anims when that animation moves its texture coordinates (flipbook / scroll); skeleton animations are not played yet
};

struct Stage {
    std::vector<Piece> pieces;                 // in the scene's order
    bool havePlane = false;                    // the scene has a FightPlane
    float planeLo[3] = {0, 0, 0}, planeHi[3] = {0, 0, 0};
    std::vector<sganim::Anim> anims;           // the texture-coordinate animations the pieces use
    std::vector<std::string> materials;        // distinct texture names of the pieces
    std::vector<std::string> missingMeshes;    // meshes the scene names and the folder does not have
    size_t triangles() const {
        size_t n = 0;
        for (const Piece& p : pieces) n += p.idx.size() / 3;
        return n;
    }
};

// `read(file, out)` returns the bytes of a file of the stage folder ("background.sgi.msb", "BGMesh2.sgm.msb").
using ReadFn = std::function<bool(const std::string& file, std::vector<uint8_t>& out)>;

// false (+ `err`) when the scene is missing / not a scene, or when it draws nothing.  A missing or damaged mesh only drops that instance
// (listed in `missingMeshes`).
bool load(const ReadFn& read, Stage& out, std::string* err = nullptr);

// ---- camera ----
struct Fit {
    double viewW = 640, viewH = 360;   // the picture the fight is drawn in (logical pixels)
    double fovDeg = 66;                // the .lvl's CAMERA line (vertical)
    int stageW = 3750;                 // StageSize x
    int bottomClearance = 64;          // BottomClearance: the fighters' floor line is this many stage units above the plane's lower edge
    double s = 0.3;                    // pixels per stage unit on the fight plane (the 2D camera's zoom)
    double cx = 0;                     // the stage unit (0 = the stage's middle) in the middle of the picture
    double floorY = 330;               // picture row of the floor line
};

struct Cam {
    bool ok = false;
    double viewW = 640, viewH = 360;
    double f = 1;                      // focal length in pixels
    double x = 0, y = 0, z = 0;        // eye, mesh units
    double unit = 16;                  // stage units per mesh unit
};

Cam fitCamera(const Stage& st, const Fit& fit);

// ---- drawing list ----
struct Tri {
    float x[3], y[3];                  // picture pixels
    float u[3], v[3];                  // texture coordinates as stored (flip v for DDS textures: v' = 1 - v)
    uint8_t c[3][4];                   // vertex colour (white unless Options::vertexColour)
};

struct Batch {
    int piece = 0;                     // index into Stage::pieces (texture = its material)
    std::vector<Tri> tris;             // back to front
};

struct Options {
    bool vertexColour = false;         // modulate by the stored vertex colours (off: the textures are drawn as they are)
    double maxEdgePx = 40;             // a triangle seen with perspective is split until its edges are shorter than this
    double nearDepth = 0.5;            // mesh units in front of the eye
    double offsetX = 0, offsetY = 0;   // added to every output point (camera shake)
    double time = 0;                   // seconds, for the texture-coordinate animations
    size_t maxTris = 80000;            // a frame never emits more than this many triangles
};

struct Frame {
    std::vector<Batch> batches;        // far pieces first
    size_t emitted = 0;                // triangles in the batches
    size_t piecesDrawn = 0;
    bool truncated = false;            // maxTris reached
};

void render(const Stage& st, const Cam& cam, const Options& opt, Frame& out);

}  // namespace stage3d
}  // namespace zengine
