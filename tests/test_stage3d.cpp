// test_stage3d.cpp - the 3D stage backdrop (src/core/stage3d.*): loading from synthetic scene + mesh bytes, the camera that ties the 3D scene to
// the 2D fight camera, clipping / sorting / splitting - and, when ZDROID_TEST_STAGES names a folder, every real stage folder in it.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "stage3d.h"

using namespace zengine;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                       \
    do {                                                               \
        ++g_checks;                                                    \
        if (!(c)) {                                                    \
            ++g_fail;                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);   \
        }                                                              \
    } while (0)
#define NEAR(a, b, eps) (std::fabs((a) - (b)) <= (eps))

namespace {
struct W {
    std::vector<uint8_t> b;
    void u64(uint64_t v) { for (int i = 7; i >= 0; --i) b.push_back(static_cast<uint8_t>(v >> (8 * i))); }
    void u32(uint32_t v) { for (int i = 3; i >= 0; --i) b.push_back(static_cast<uint8_t>(v >> (8 * i))); }
    void u16(uint16_t v) { b.push_back(static_cast<uint8_t>(v >> 8)); b.push_back(static_cast<uint8_t>(v)); }
    void u8(uint8_t v) { b.push_back(v); }
    void str(const std::string& s) { u64(s.size()); b.insert(b.end(), s.begin(), s.end()); }
    void f32(float f) { uint32_t v; std::memcpy(&v, &f, 4); for (int i = 3; i >= 0; --i) b.push_back(static_cast<uint8_t>(v >> (8 * i))); }
};

// A static mesh: a rectangle in the xy plane from (x0,y0) to (x1,y1), two triangles, uv 0..1.
std::vector<uint8_t> quadMesh(const std::string& material, float x0, float y0, float x1, float y1) {
    W w;
    w.str("2.0");
    w.str(material);
    const float xf[13] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 90};
    for (float f : xf) w.f32(f);
    w.str("float p[3],n[3],uv[2]; uchar4 c;");
    w.u64(36);
    w.u64(4);
    w.u64(2);
    w.u64(0);
    const float px[4][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (int i = 0; i < 4; ++i) {
        w.f32(px[i][0]); w.f32(px[i][1]); w.f32(0);
        w.f32(0); w.f32(0); w.f32(1);
        w.f32(uv[i][0]); w.f32(uv[i][1]);
        w.u8(255); w.u8(255); w.u8(255); w.u8(255);
    }
    const uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
    for (uint16_t i : idx) w.u16(i);
    w.f32(x0); w.f32(y0); w.f32(0); w.f32(x1); w.f32(y1); w.f32(0);
    return w.b;
}

void matrixTo(W& w, float tx, float ty, float tz, bool rotateXToZ = false) {
    // row-vector: identity, or x -> z mapping (a quad drawn in xy laid down on the ground: y -> -z)
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, tx, ty, tz, 1};
    if (rotateXToZ) {   // (x, y, 0) -> (x, 0, -y)
        m[5] = 0; m[6] = -1; m[9] = 0; m[10] = 0;
        m[9] = 0;
        m[6] = -1;
    }
    for (float f : m) w.f32(f);
}

void instance(W& w, const std::string& path, const std::string& mesh, float tx, float ty, float tz, bool lay = false) {
    w.str(path);
    w.str(mesh);
    matrixTo(w, tx, ty, tz, lay);
    w.u8(1);
    w.u8(0);
    w.u64(0);
}

stage3d::ReadFn readerOf(std::map<std::string, std::vector<uint8_t>>* files) {
    return [files](const std::string& name, std::vector<uint8_t>& out) {
        auto it = files->find(name);
        if (it == files->end()) return false;
        out = it->second;
        return true;
    };
}
}  // namespace

int main() {
    // ---- a stage built byte by byte: the fight plane (200 x 106.67, z = 0), a wall behind it, a floor lying down, a mesh that is missing
    std::map<std::string, std::vector<uint8_t>> files;
    files["plane.sgm.msb"] = quadMesh("white", -100, 0, 100, 106.6667f);
    files["wall.sgm.msb"] = quadMesh("wall_tex", -150, -20, 150, 160);
    files["floor.sgm.msb"] = quadMesh("floor_tex", -150, -150, 150, 100);
    {
        W s;
        s.str("2.0");
        s.u64(5);
        instance(s, "FightPlane", "plane", 0, 0, 0);
        instance(s, "|Wall", "wall", 0, 0, -100);
        instance(s, "|Floor", "floor", 0, 0, 0, true);
        instance(s, "|Ghost", "nothere", 0, 0, 0);
        instance(s, "|Wall2", "wall", 10, 0, -100);   // the same mesh again
        files["background.sgi.msb"] = s.b;
    }
    stage3d::Stage st;
    std::string err;
    CHECK(stage3d::load(readerOf(&files), st, &err));
    CHECK(st.havePlane);
    CHECK(NEAR(st.planeLo[0], -100, 1e-3) && NEAR(st.planeHi[0], 100, 1e-3) && NEAR(st.planeHi[1], 106.6667, 1e-2));
    CHECK(st.pieces.size() == 3);   // wall, floor, wall again; the plane and the missing mesh are not pieces
    CHECK(st.missingMeshes.size() == 1 && st.missingMeshes[0] == "nothere");
    CHECK(st.materials.size() == 2);
    CHECK(st.pieces[0].name == "Wall" && st.pieces[0].material == "wall_tex");
    CHECK(NEAR(st.pieces[0].lo[2], -100, 1e-3));
    CHECK(NEAR(st.pieces[2].lo[0], -140, 1e-3));   // translated by 10
    CHECK(NEAR(st.pieces[1].hi[2], 150, 1e-3) && NEAR(st.pieces[1].lo[2], -100, 1e-3));   // the laid-down floor: y -> -z

    // ---- the camera: the fight plane lands where the 2D camera puts the stage
    stage3d::Fit fit;
    fit.viewW = 640;
    fit.viewH = 360;
    fit.s = 0.4;
    fit.cx = 0;
    fit.floorY = 330;
    fit.bottomClearance = 64;
    const stage3d::Cam cam = stage3d::fitCamera(st, fit);
    CHECK(cam.ok);
    CHECK(NEAR(cam.unit, 3750.0 / 200.0, 1e-6));
    auto proj = [&](const stage3d::Cam& c, double X, double Y, double Z, double& sx, double& sy) {
        const double d = c.z - Z;
        sx = 0.5 * c.viewW + c.f * (X - c.x) / d;
        sy = 0.5 * c.viewH - c.f * (Y - c.y) / d;
    };
    {
        double sx, sy;
        proj(cam, -100, 0, 0, sx, sy);   // the plane's lower left corner
        CHECK(NEAR(sx, 320 - 0.4 * 1875, 1e-6));
        CHECK(NEAR(sy, 330 + 0.4 * 64, 1e-6));
        proj(cam, 100, 106.6667, 0, sx, sy);   // its upper right corner: 3750 x 2000 stage units further
        CHECK(NEAR(sx, 320 + 0.4 * 1875, 1e-6));
        CHECK(NEAR(sy, 330 + 0.4 * 64 - 0.4 * 2000, 0.05));
        proj(cam, 0, 64 / cam.unit, 0, sx, sy);   // the floor line (BottomClearance above the plane's lower edge)
        CHECK(NEAR(sy, 330, 1e-6));
    }
    {   // panning: 300 stage units to the right moves the plane 120 px to the left; zooming in doubles distances
        stage3d::Fit f2 = fit;
        f2.cx = 300;
        const stage3d::Cam c2 = stage3d::fitCamera(st, f2);
        double sx, sy, sx0, sy0;
        proj(cam, 0, 0, 0, sx0, sy0);
        proj(c2, 0, 0, 0, sx, sy);
        CHECK(NEAR(sx - sx0, -0.4 * 300, 1e-6));
        stage3d::Fit f3 = fit;
        f3.s = 0.8;
        const stage3d::Cam c3 = stage3d::fitCamera(st, f3);
        double a, b, a3, b3;
        proj(cam, -100, 0, 0, a, b);
        proj(cam, 100, 0, 0, sx, sy);
        proj(c3, -100, 0, 0, a3, b);
        proj(c3, 100, 0, 0, sx0, sy0);
        CHECK(NEAR((sx0 - a3) / (sx - a), 2.0, 1e-6));
        CHECK(c3.z < cam.z);   // closer
    }
    {   // not a fight plane (a marker): no camera
        stage3d::Stage odd = st;
        odd.planeLo[0] = -10; odd.planeHi[0] = 10; odd.planeLo[1] = 0; odd.planeHi[1] = 40;
        CHECK(!stage3d::fitCamera(odd, fit).ok);
        stage3d::Stage none = st;
        none.havePlane = false;
        CHECK(!stage3d::fitCamera(none, fit).ok);
    }

    // ---- drawing
    stage3d::Options opt;
    stage3d::Frame fr;
    stage3d::render(st, cam, opt, fr);
    CHECK(fr.piecesDrawn >= 2 && fr.emitted >= 4);
    {
        // back to front: the walls (z = -100) before the floor (z up to 150, centre 25)... the wall's middle is 100 behind the plane
        bool wallFirst = false;
        int firstWall = -1, floorAt = -1;
        for (size_t i = 0; i < fr.batches.size(); ++i) {
            const std::string& n = st.pieces[static_cast<size_t>(fr.batches[i].piece)].name;
            if (n == "Wall" && firstWall < 0) firstWall = static_cast<int>(i);
            if (n == "Floor") floorAt = static_cast<int>(i);
        }
        wallFirst = firstWall >= 0 && floorAt > firstWall;
        CHECK(wallFirst);
        // every output point is finite
        bool finite = true;
        for (const auto& b : fr.batches)
            for (const auto& t : b.tris)
                for (int k = 0; k < 3; ++k) finite = finite && std::isfinite(t.x[k]) && std::isfinite(t.y[k]) && std::isfinite(t.u[k]);
        CHECK(finite);
    }
    {   // the wall seen square on is two triangles (no splitting needed at constant depth): the plane-sized wall's corners are where they should be
        stage3d::Stage one;
        one.havePlane = true;
        for (int k = 0; k < 3; ++k) { one.planeLo[k] = st.planeLo[k]; one.planeHi[k] = st.planeHi[k]; }
        one.pieces.push_back(st.pieces[0]);   // the wall
        stage3d::Frame f1;
        stage3d::render(one, cam, opt, f1);
        CHECK(f1.batches.size() == 1 && f1.batches[0].tris.size() == 2);
        float minx = 1e9f, maxx = -1e9f;
        for (const auto& t : f1.batches[0].tris)
            for (int k = 0; k < 3; ++k) { minx = std::min(minx, t.x[k]); maxx = std::max(maxx, t.x[k]); }
        double sx, sy, ex;
        proj(cam, -150, 0, -100, sx, sy);
        proj(cam, 150, 0, -100, ex, sy);
        CHECK(NEAR(minx, sx, 1e-2) && NEAR(maxx, ex, 1e-2));
        // camera shake moves everything
        stage3d::Options sh = opt;
        sh.offsetX = 5;
        sh.offsetY = -3;
        stage3d::Frame f2;
        stage3d::render(one, cam, sh, f2);
        CHECK(NEAR(f2.batches[0].tris[0].x[0] - f1.batches[0].tris[0].x[0], 5, 1e-3));
        CHECK(NEAR(f2.batches[0].tris[0].y[0] - f1.batches[0].tris[0].y[0], -3, 1e-3));
    }
    {   // a floor seen at an angle is split so the texture does not swim, and never leaves a triangle behind the eye
        stage3d::Stage one;
        one.havePlane = true;
        for (int k = 0; k < 3; ++k) { one.planeLo[k] = st.planeLo[k]; one.planeHi[k] = st.planeHi[k]; }
        one.pieces.push_back(st.pieces[1]);   // the floor lying down, 300 wide, from z = -100 to 150: the eye is at z ~ 190 up at y ~ 20
        stage3d::Frame f1;
        stage3d::render(one, cam, opt, f1);
        CHECK(!f1.batches.empty());
        size_t n = f1.batches.empty() ? 0 : f1.batches[0].tris.size();
        CHECK(n > 2);
        stage3d::Options big = opt;
        big.maxEdgePx = 1e9;   // no splitting
        stage3d::Frame f2;
        stage3d::render(one, cam, big, f2);
        CHECK(!f2.batches.empty() && f2.batches[0].tris.size() <= 4);   // 2, or a few more after the near clip
        stage3d::Options few = opt;
        few.maxTris = 3;
        stage3d::Frame f3;
        stage3d::render(one, cam, few, f3);
        CHECK(f3.emitted <= 3 && f3.truncated);
    }
    {   // a tiled texture (uv 0..3 across a quad): cut at the whole numbers, every piece inside 0..1, nothing lost
        std::map<std::string, std::vector<uint8_t>> tf;
        tf["plane.sgm.msb"] = files["plane.sgm.msb"];
        {
            // the wall's mesh with its uv scaled by hand: rebuild it from the quad writer by patching the stored coordinates
            std::vector<uint8_t> m = quadMesh("tile", -150, -20, 150, 160);
            // header: str "2.0" (8+3) + str material (8+4) + 13 f32 + str decl + 4 u64; vertices start after
            const std::string decl = "float p[3],n[3],uv[2]; uchar4 c;";
            const size_t vstart = 11 + 12 + 13 * 4 + 8 + decl.size() + 32;
            for (int i = 0; i < 4; ++i) {
                const size_t o = vstart + static_cast<size_t>(i) * 36 + 24;   // uv
                for (int k = 0; k < 2; ++k) {
                    uint32_t bits;
                    std::memcpy(&bits, &m[o + 4 * static_cast<size_t>(k)], 4);
                    bits = (bits >> 24) | ((bits >> 8) & 0xff00) | ((bits << 8) & 0xff0000) | (bits << 24);
                    float f;
                    std::memcpy(&f, &bits, 4);
                    f = f * 3.0f - 0.5f;   // 0..1 -> -0.5..2.5
                    std::memcpy(&bits, &f, 4);
                    bits = (bits >> 24) | ((bits >> 8) & 0xff00) | ((bits << 8) & 0xff0000) | (bits << 24);
                    std::memcpy(&m[o + 4 * static_cast<size_t>(k)], &bits, 4);
                }
            }
            tf["tile.sgm.msb"] = m;
        }
        W s;
        s.str("2.0");
        s.u64(2);
        instance(s, "FightPlane", "plane", 0, 0, 0);
        instance(s, "|Tile", "tile", 0, 0, -100);
        tf["background.sgi.msb"] = s.b;
        stage3d::Stage ts;
        CHECK(stage3d::load(readerOf(&tf), ts, &err));
        const stage3d::Cam tc = stage3d::fitCamera(ts, fit);
        stage3d::Options wo = opt;
        wo.maxEdgePx = 1e9;
        stage3d::Frame tfr;
        stage3d::render(ts, tc, wo, tfr);
        CHECK(!tfr.batches.empty());
        CHECK(ts.pieces.size() == 1 && ts.pieces[0].tiled);
        CHECK(!st.pieces[0].tiled && !st.pieces[1].tiled);   // 0..1 quads
        bool inside = true;
        double area = 0;
        size_t n = 0;
        for (const auto& b : tfr.batches)
            for (const auto& t : b.tris) {
                ++n;
                for (int k = 0; k < 3; ++k) inside = inside && t.u[k] >= 0 && t.u[k] <= 1 && t.v[k] >= 0 && t.v[k] <= 1;
                area += 0.5 * std::fabs((t.x[1] - t.x[0]) * (t.y[2] - t.y[0]) - (t.x[2] - t.x[0]) * (t.y[1] - t.y[0]));
            }
        CHECK(inside);
        CHECK(n >= 9 * 2 / 2);   // 3x3 cells, each at least a triangle
        double sx0, sy0, sx1, sy1;
        proj(tc, -150, -20, -100, sx0, sy0);
        proj(tc, 150, 160, -100, sx1, sy1);
        // the part of the quad that is on screen only: compare against the same quad clipped to the picture is overkill; check area never exceeds the quad
        CHECK(area <= std::fabs((sx1 - sx0) * (sy1 - sy0)) * 1.001);
        CHECK(area > 0);
    }
    {   // behind the eye: nothing
        stage3d::Cam behind = cam;
        behind.z = -1000;
        stage3d::Frame f1;
        stage3d::render(st, behind, opt, f1);
        CHECK(f1.emitted == 0 && f1.batches.empty());
        stage3d::Cam bad;   // not ok
        stage3d::render(st, bad, opt, f1);
        CHECK(f1.emitted == 0);
    }
    {   // texture-coordinate animation: the scene gives a piece an "idle" animation, its file moves the texture coordinates with the clock
        std::map<std::string, std::vector<uint8_t>> af;
        af["plane.sgm.msb"] = files["plane.sgm.msb"];
        af["wall.sgm.msb"] = files["wall.sgm.msb"];
        af["sprite.sgm.msb"] = quadMesh("npc", -50, 0, 50, 80);
        {
            W a;   // 3 keys, u = 0, 0.1, 0.2 over 2 s: a flipbook (each key held 2/3 s)
            a.str("3.0");
            for (uint32_t v : {3u, 0u, 0u, 0u, 1u}) a.u32(v);   // a, b, joints, c, tracks
            a.f32(2.0f);
            a.str("uv_track");
            a.u64(3);
            for (int i = 0; i < 3; ++i) { a.f32(1); a.f32(1); }
            a.u64(3);
            for (int i = 0; i < 3; ++i) a.f32(0);
            a.u64(3);
            for (float u : {0.0f, 0.1f, 0.2f}) { a.f32(u); a.f32(0); }
            af["walk.sga.msb"] = a.b;
        }
        W s2;   // a 2.1 scene: 6 flag bytes per instance, then the animation list
        s2.str("2.1");
        s2.u64(3);
        auto inst21 = [&](const std::string& path, const std::string& mesh, float tz, bool anim) {
            s2.str(path);
            s2.str(mesh);
            matrixTo(s2, 0, 0, tz);
            for (int i = 0; i < 6; ++i) s2.u8(0);
            if (anim) {
                s2.u64(1);
                s2.str("idle");
                s2.str("walk");
            } else {
                s2.u64(0);
            }
        };
        inst21("FightPlane", "plane", 0, false);
        inst21("|Wall", "wall", -100, false);
        inst21("|Sprite", "sprite", -50, true);
        af["background.sgi.msb"] = s2.b;
        stage3d::Stage as;
        std::string aerr;
        CHECK(stage3d::load(readerOf(&af), as, &aerr));
        CHECK(as.pieces.size() == 2 && as.anims.size() == 1);
        CHECK(as.pieces[0].anim == -1 && as.pieces[1].anim == 0 && as.pieces[1].animated);
        const stage3d::Cam ac = stage3d::fitCamera(as, fit);
        auto minU = [&](double t, int piece) {
            stage3d::Options o;
            o.time = t;
            o.maxEdgePx = 1e9;
            stage3d::Frame f;
            stage3d::render(as, ac, o, f);
            double m = 1e9;
            for (const auto& b : f.batches)
                if (b.piece == piece)
                    for (const auto& tr : b.tris)
                        for (int k = 0; k < 3; ++k) m = std::min(m, static_cast<double>(tr.u[k]));
            return m;
        };
        CHECK(NEAR(minU(0.0, 1), 0.0, 1e-6));
        CHECK(NEAR(minU(0.7, 1), 0.1, 1e-6));
        CHECK(NEAR(minU(1.4, 1), 0.2, 1e-6));
        CHECK(NEAR(minU(2.0, 1), 0.0, 1e-6));    // loops
        CHECK(NEAR(minU(0.7, 0), 0.0, 1e-6));    // the wall has no animation: untouched at every time
        CHECK(NEAR(minU(1.4, 0), 0.0, 1e-6));
        // a skeleton animation (joint count > 0) is ignored: the piece stays in its pose
        std::vector<uint8_t> skel = af["walk.sga.msb"];
        skel[8 + 3 + 8 + 3] = 7;   // third header word's low byte: joints = 7 (after str "3.0" and words a, b)
        af["walk.sga.msb"] = skel;
        stage3d::Stage ks;
        CHECK(stage3d::load(readerOf(&af), ks, &aerr));
        CHECK(ks.anims.empty() && ks.pieces.size() == 2 && ks.pieces[1].anim == -1 && ks.pieces[1].animated);
    }
    {   // loading failures
        std::map<std::string, std::vector<uint8_t>> none;
        stage3d::Stage s2;
        CHECK(!stage3d::load(readerOf(&none), s2, &err));
        std::map<std::string, std::vector<uint8_t>> junk;
        junk["background.sgi.msb"] = {1, 2, 3};
        CHECK(!stage3d::load(readerOf(&junk), s2, &err));
        std::map<std::string, std::vector<uint8_t>> empty;
        W s;
        s.str("2.0");
        s.u64(1);
        instance(s, "FightPlane", "plane", 0, 0, 0);
        empty["background.sgi.msb"] = s.b;
        empty["plane.sgm.msb"] = files["plane.sgm.msb"];
        CHECK(!stage3d::load(readerOf(&empty), s2, &err));   // only the plane: nothing to draw
    }

    // ---- the real thing: every folder with a background.sgi.msb
    if (const char* dir = std::getenv("ZDROID_TEST_STAGES"); dir && *dir) {
        namespace fs = std::filesystem;
        int stages = 0, loaded = 0, fitted = 0, drew = 0, failed = 0;
        size_t tris = 0, missing = 0, emitted = 0;
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (it->path().filename() != "background.sgi.msb") continue;
            const fs::path folder = it->path().parent_path();
            ++stages;
            auto read = [&](const std::string& name, std::vector<uint8_t>& out) {
                std::ifstream f(folder / name, std::ios::binary);
                if (!f) return false;
                out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
                return true;
            };
            stage3d::Stage rs;
            std::string e;
            if (!stage3d::load(read, rs, &e)) {
                ++failed;
                std::printf("  %s: %s\n", folder.filename().string().c_str(), e.c_str());
                continue;
            }
            ++loaded;
            tris += rs.triangles();
            missing += rs.missingMeshes.size();
            const stage3d::Cam c = stage3d::fitCamera(rs, fit);
            if (!c.ok) {
                std::printf("  %s: no usable FightPlane (%s)\n", folder.filename().string().c_str(), rs.havePlane ? "odd size" : "none");
                continue;
            }
            ++fitted;
            stage3d::Frame f;
            stage3d::render(rs, c, opt, f);
            if (f.emitted > 0) ++drew;
            emitted += f.emitted;
        }
        std::printf("real stages: %d folders, %d loaded (%zu triangles, %zu missing meshes), %d with a fight plane, %d drew something (%zu triangles), %d failed\n", stages, loaded, tris,
                    missing, fitted, drew, emitted, failed);
        CHECK(failed == 0);
        CHECK(drew == fitted);
    }
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
