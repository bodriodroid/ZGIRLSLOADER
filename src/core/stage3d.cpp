#include "stage3d.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

#include "sgmesh.h"
#include "sgscene.h"

namespace zengine {
namespace stage3d {

namespace {

std::string nodeName(const std::string& path) {
    const size_t bar = path.rfind('|');
    return bar == std::string::npos ? path : path.substr(bar + 1);
}

}  // namespace

bool load(const ReadFn& read, Stage& out, std::string* err) {
    out = Stage();
    auto fail = [&](const std::string& why) {
        if (err) *err = why;
        return false;
    };
    std::vector<uint8_t> bytes;
    if (!read("background.sgi.msb", bytes)) return fail("no background.sgi.msb");
    sgscene::Scene scene;
    std::string e;
    if (!sgscene::parse(bytes, scene, &e)) return fail("background.sgi.msb: " + e);

    std::map<std::string, std::shared_ptr<const sgmesh::Mesh>> meshes;   // instances share meshes
    for (const sgscene::Instance& in : scene.instances) {
        if (!in.hasGeometry()) continue;
        std::shared_ptr<const sgmesh::Mesh> mesh;
        auto it = meshes.find(in.mesh);
        if (it != meshes.end()) {
            mesh = it->second;
        } else {
            std::vector<uint8_t> mb;
            auto m = std::make_shared<sgmesh::Mesh>();
            if (read(in.mesh + ".sgm.msb", mb) && sgmesh::parse(mb, *m, nullptr)) mesh = m;
            meshes[in.mesh] = mesh;
        }
        if (!mesh) {
            if (std::find(out.missingMeshes.begin(), out.missingMeshes.end(), in.mesh) == out.missingMeshes.end()) out.missingMeshes.push_back(in.mesh);
            continue;
        }
        if (mesh->verts.empty() || mesh->idx.empty()) continue;

        Piece pc;
        pc.name = nodeName(in.path);
        pc.material = mesh->material;
        pc.skinned = mesh->skinned;
        pc.animated = !in.anims.empty();
        pc.v.resize(mesh->verts.size());
        for (int k = 0; k < 3; ++k) {
            pc.lo[k] = 1e30f;
            pc.hi[k] = -1e30f;
        }
        const float* m = in.m;   // row-vector convention: p' = [x y z 1] * M
        for (size_t i = 0; i < mesh->verts.size(); ++i) {
            const sgmesh::Vertex& s = mesh->verts[i];
            Vtx& d = pc.v[i];
            for (int k = 0; k < 3; ++k) {
                d.p[k] = s.p[0] * m[k] + s.p[1] * m[4 + k] + s.p[2] * m[8 + k] + m[12 + k];
                pc.lo[k] = std::min(pc.lo[k], d.p[k]);
                pc.hi[k] = std::max(pc.hi[k], d.p[k]);
            }
            d.uv[0] = s.uv[0];
            d.uv[1] = s.uv[1];
            for (int k = 0; k < 4; ++k) d.c[k] = s.c[k];
        }
        if (pc.name == "FightPlane") {   // the quad that is the stage's front view: not drawn, it places the camera
            out.havePlane = true;
            for (int k = 0; k < 3; ++k) {
                out.planeLo[k] = pc.lo[k];
                out.planeHi[k] = pc.hi[k];
            }
            continue;
        }
        float uvLo[2] = {1e30f, 1e30f}, uvHi[2] = {-1e30f, -1e30f};
        for (const Vtx& v : pc.v)
            for (int k = 0; k < 2; ++k) {
                uvLo[k] = std::min(uvLo[k], v.uv[k]);
                uvHi[k] = std::max(uvHi[k], v.uv[k]);
            }
        pc.tiled = uvLo[0] < -0.35f || uvLo[1] < -0.35f || uvHi[0] > 1.35f || uvHi[1] > 1.35f;
        pc.idx = mesh->idx;
        out.pieces.push_back(std::move(pc));
    }
    for (const Piece& p : out.pieces)
        if (std::find(out.materials.begin(), out.materials.end(), p.material) == out.materials.end()) out.materials.push_back(p.material);
    if (out.pieces.empty()) return fail("the scene draws nothing");
    return true;
}

Cam fitCamera(const Stage& st, const Fit& fit) {
    Cam c;
    c.viewW = fit.viewW;
    c.viewH = fit.viewH;
    if (!st.havePlane || fit.s <= 0 || fit.stageW <= 0) return c;
    const double pw = static_cast<double>(st.planeHi[0]) - st.planeLo[0], ph = static_cast<double>(st.planeHi[1]) - st.planeLo[1];
    // A real fight plane is the stage's 15:8 front view; anything else is a marker, not a backdrop to fit a camera to.
    if (pw <= 1e-3 || ph <= 1e-3 || pw / ph < 1.5 || pw / ph > 2.3) return c;
    c.unit = static_cast<double>(fit.stageW) / pw;
    const double scale = fit.s * c.unit;                       // pixels per mesh unit on the fight plane
    c.f = 0.5 * fit.viewH / std::tan(0.5 * fit.fovDeg * 3.14159265358979323846 / 180.0);
    c.z = static_cast<double>(st.planeLo[2]) + c.f / scale;
    c.x = 0.5 * (static_cast<double>(st.planeLo[0]) + st.planeHi[0]) + fit.cx / c.unit;
    const double floorMesh = static_cast<double>(st.planeLo[1]) + fit.bottomClearance / c.unit;
    c.y = floorMesh + (fit.floorY - 0.5 * fit.viewH) / scale;
    c.ok = true;
    return c;
}

namespace {

struct VV {   // a vertex relative to the eye
    double x, y, d;   // right, up, depth in front of the eye
    float u, v;
    uint8_t c[4];
};

VV lerp(const VV& a, const VV& b, double t) {
    VV r;
    r.x = a.x + (b.x - a.x) * t;
    r.y = a.y + (b.y - a.y) * t;
    r.d = a.d + (b.d - a.d) * t;
    r.u = static_cast<float>(a.u + (b.u - a.u) * t);
    r.v = static_cast<float>(a.v + (b.v - a.v) * t);
    for (int k = 0; k < 4; ++k) r.c[k] = static_cast<uint8_t>(a.c[k] + (b.c[k] - a.c[k]) * t + 0.5);
    return r;
}

struct Ctx {
    const Cam* cam;
    const Options* opt;
    Frame* frame;
    Batch* batch;
    double margin;
    bool tiled;
};

void project(const Ctx& c, const VV& a, double& sx, double& sy) {
    const double k = c.cam->f / a.d;
    sx = 0.5 * c.cam->viewW + a.x * k + c.opt->offsetX;
    sy = 0.5 * c.cam->viewH - a.y * k + c.opt->offsetY;
}

struct P {   // a polygon corner on the picture
    float x, y, u, v;
    uint8_t c[4];
};

P lerpP(const P& a, const P& b, float t) {
    P r;
    r.x = a.x + (b.x - a.x) * t;
    r.y = a.y + (b.y - a.y) * t;
    r.u = a.u + (b.u - a.u) * t;
    r.v = a.v + (b.v - a.v) * t;
    for (int k = 0; k < 4; ++k) r.c[k] = static_cast<uint8_t>(a.c[k] + (b.c[k] - a.c[k]) * t + 0.5f);
    return r;
}

// Keeps the part of a polygon whose u (axis 0) or v (axis 1) is >= bound (above) or <= bound (below).
int clipUv(const P* in, int n, P* out, int axis, float bound, bool above) {
    int m = 0;
    for (int i = 0; i < n; ++i) {
        const P& a = in[i];
        const P& b = in[(i + 1) % n];
        const float av = (axis ? a.v : a.u) - bound, bv = (axis ? b.v : b.u) - bound;
        const bool ai = above ? av >= 0 : av <= 0, bi = above ? bv >= 0 : bv <= 0;
        if (ai) out[m++] = a;
        if (ai != bi) out[m++] = lerpP(a, b, av / (av - bv));
    }
    return m;
}

// The textures are repeating: a triangle whose coordinates run past 0..1 (tiled floors, water, fog) is cut along the whole numbers and each
// piece moved into 0..1, because the renderer only samples inside one copy of the picture.  A coordinate that only overshoots a little is left alone
// (the texture's edge pixel is repeated).
void put(Ctx& c, const Tri& t) {
    constexpr float kSlack = 0.03f;
    if (!c.tiled) {
        c.batch->tris.push_back(t);
        ++c.frame->emitted;
        return;
    }
    const float minU = std::min({t.u[0], t.u[1], t.u[2]}), maxU = std::max({t.u[0], t.u[1], t.u[2]});
    const float minV = std::min({t.v[0], t.v[1], t.v[2]}), maxV = std::max({t.v[0], t.v[1], t.v[2]});
    const bool wrapU = minU < -kSlack || maxU > 1.0f + kSlack, wrapV = minV < -kSlack || maxV > 1.0f + kSlack;
    if (!wrapU && !wrapV) {
        c.batch->tris.push_back(t);
        ++c.frame->emitted;
        return;
    }
    const int u0 = static_cast<int>(std::floor(minU + kSlack * 0.5f)), u1 = static_cast<int>(std::ceil(maxU - kSlack * 0.5f));
    const int v0 = static_cast<int>(std::floor(minV + kSlack * 0.5f)), v1 = static_cast<int>(std::ceil(maxV - kSlack * 0.5f));
    if ((u1 - u0) * (v1 - v0) > 64 || u1 <= u0 || v1 <= v0) {   // a texture repeated absurdly often (a rounding glitch): draw it once
        c.batch->tris.push_back(t);
        ++c.frame->emitted;
        return;
    }
    P poly[3];
    for (int i = 0; i < 3; ++i) {
        poly[i] = P{t.x[i], t.y[i], t.u[i], t.v[i], {t.c[i][0], t.c[i][1], t.c[i][2], t.c[i][3]}};
    }
    for (int iv = v0; iv < v1; ++iv) {
        for (int iu = u0; iu < u1; ++iu) {
            P a[10], b[10];
            int n = 3;
            std::copy(poly, poly + 3, a);
            n = clipUv(a, n, b, 0, static_cast<float>(iu), true);
            if (n >= 3) n = clipUv(b, n, a, 0, static_cast<float>(iu + 1), false);
            else continue;
            if (n >= 3) n = clipUv(a, n, b, 1, static_cast<float>(iv), true);
            else continue;
            if (n >= 3) n = clipUv(b, n, a, 1, static_cast<float>(iv + 1), false);
            else continue;
            if (n < 3) continue;
            for (int i = 1; i + 1 < n; ++i) {
                const P* q[3] = {&a[0], &a[i], &a[i + 1]};
                Tri o;
                for (int k = 0; k < 3; ++k) {
                    o.x[k] = q[k]->x;
                    o.y[k] = q[k]->y;
                    o.u[k] = std::clamp(q[k]->u - static_cast<float>(iu), 0.0f, 1.0f);
                    o.v[k] = std::clamp(q[k]->v - static_cast<float>(iv), 0.0f, 1.0f);
                    for (int j = 0; j < 4; ++j) o.c[k][j] = q[k]->c[j];
                }
                c.batch->tris.push_back(o);
                ++c.frame->emitted;
            }
        }
    }
}

void emit(Ctx& c, const VV& a, const VV& b, const VV& d, int level) {
    if (c.frame->emitted >= c.opt->maxTris) {
        c.frame->truncated = true;
        return;
    }
    const VV* v[3] = {&a, &b, &d};
    double sx[3], sy[3];
    for (int i = 0; i < 3; ++i) project(c, *v[i], sx[i], sy[i]);
    const double minX = std::min({sx[0], sx[1], sx[2]}), maxX = std::max({sx[0], sx[1], sx[2]});
    const double minY = std::min({sy[0], sy[1], sy[2]}), maxY = std::max({sy[0], sy[1], sy[2]});
    if (maxX < -c.margin || minX > c.cam->viewW + c.margin || maxY < -c.margin || minY > c.cam->viewH + c.margin) return;
    if (maxX - minX < 0.02 && maxY - minY < 0.02) return;   // a speck
    if (level < 5) {
        const double dMin = std::min({a.d, b.d, d.d}), dMax = std::max({a.d, b.d, d.d});
        if (dMax > dMin * 1.04) {   // the texture would swim under the renderer's affine interpolation
            double edge = 0;
            for (int i = 0; i < 3; ++i) {
                const int j = (i + 1) % 3;
                edge = std::max(edge, std::hypot(sx[i] - sx[j], sy[i] - sy[j]));
            }
            if (edge > c.opt->maxEdgePx) {
                const VV ab = lerp(a, b, 0.5), bc = lerp(b, d, 0.5), ca = lerp(d, a, 0.5);
                emit(c, a, ab, ca, level + 1);
                emit(c, ab, b, bc, level + 1);
                emit(c, ca, bc, d, level + 1);
                emit(c, ab, bc, ca, level + 1);
                return;
            }
        }
    }
    Tri t;
    for (int i = 0; i < 3; ++i) {
        t.x[i] = static_cast<float>(sx[i]);
        t.y[i] = static_cast<float>(sy[i]);
        t.u[i] = v[i]->u;
        t.v[i] = v[i]->v;
        for (int k = 0; k < 4; ++k) t.c[i][k] = c.opt->vertexColour ? v[i]->c[k] : 255;
    }
    put(c, t);
}

}  // namespace

void render(const Stage& st, const Cam& cam, const Options& opt, Frame& out) {
    out.batches.clear();
    out.emitted = 0;
    out.piecesDrawn = 0;
    out.truncated = false;
    if (!cam.ok) return;

    // pieces far to near (by the depth of the middle of their box; the scene's own order breaks ties)
    struct Order {
        double depth;
        int piece;
    };
    std::vector<Order> order;
    order.reserve(st.pieces.size());
    for (size_t i = 0; i < st.pieces.size(); ++i) {
        const Piece& p = st.pieces[i];
        // quick reject: the whole box behind the eye, or all of it off to one side of the picture
        const double dLo = cam.z - p.hi[2], dHi = cam.z - p.lo[2];
        if (dHi < opt.nearDepth) continue;
        if (dLo > opt.nearDepth) {
            bool left = true, right = true, above = true, below = true;
            for (int k = 0; k < 8; ++k) {
                const double X = (k & 1 ? p.hi[0] : p.lo[0]) - cam.x, Y = (k & 2 ? p.hi[1] : p.lo[1]) - cam.y;
                const double D = cam.z - (k & 4 ? p.hi[2] : p.lo[2]);
                const double sx = 0.5 * cam.viewW + X * cam.f / D + opt.offsetX, sy = 0.5 * cam.viewH - Y * cam.f / D + opt.offsetY;
                left = left && sx < 0;
                right = right && sx > cam.viewW;
                above = above && sy < 0;
                below = below && sy > cam.viewH;
            }
            if (left || right || above || below) continue;
        }
        order.push_back({cam.z - 0.5 * (static_cast<double>(p.lo[2]) + p.hi[2]), static_cast<int>(i)});
    }
    std::stable_sort(order.begin(), order.end(), [](const Order& a, const Order& b) { return a.depth > b.depth; });

    Ctx ctx{&cam, &opt, &out, nullptr, 8.0, false};
    std::vector<VV> vv;
    std::vector<std::pair<float, uint32_t>> keys;
    for (const Order& o : order) {
        const Piece& p = st.pieces[static_cast<size_t>(o.piece)];
        vv.resize(p.v.size());
        for (size_t i = 0; i < p.v.size(); ++i) {
            const Vtx& s = p.v[i];
            VV& d = vv[i];
            d.x = static_cast<double>(s.p[0]) - cam.x;
            d.y = static_cast<double>(s.p[1]) - cam.y;
            d.d = cam.z - static_cast<double>(s.p[2]);
            d.u = s.uv[0];
            d.v = s.uv[1];
            for (int k = 0; k < 4; ++k) d.c[k] = s.c[k];
        }
        keys.clear();
        const size_t tris = p.idx.size() / 3;
        keys.reserve(tris);
        for (size_t t = 0; t < tris; ++t) {
            const double depth = vv[p.idx[3 * t]].d + vv[p.idx[3 * t + 1]].d + vv[p.idx[3 * t + 2]].d;
            keys.emplace_back(static_cast<float>(depth), static_cast<uint32_t>(t));
        }
        if (tris > 1) std::stable_sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

        out.batches.emplace_back();
        Batch& batch = out.batches.back();
        batch.piece = o.piece;
        ctx.batch = &batch;
        ctx.tiled = p.tiled;
        for (const auto& k : keys) {
            const size_t t = k.second;
            VV tri[3] = {vv[p.idx[3 * t]], vv[p.idx[3 * t + 1]], vv[p.idx[3 * t + 2]]};
            const bool in0 = tri[0].d >= opt.nearDepth, in1 = tri[1].d >= opt.nearDepth, in2 = tri[2].d >= opt.nearDepth;
            if (in0 && in1 && in2) {
                emit(ctx, tri[0], tri[1], tri[2], 0);
                continue;
            }
            if (!in0 && !in1 && !in2) continue;
            // clip against the near plane: a polygon of 3 or 4 corners, fanned
            VV poly[4];
            int n = 0;
            for (int i = 0; i < 3; ++i) {
                const VV& a = tri[i];
                const VV& b = tri[(i + 1) % 3];
                const bool ai = a.d >= opt.nearDepth, bi = b.d >= opt.nearDepth;
                if (ai) poly[n++] = a;
                if (ai != bi) poly[n++] = lerp(a, b, (opt.nearDepth - a.d) / (b.d - a.d));
            }
            for (int i = 1; i + 1 < n; ++i) emit(ctx, poly[0], poly[i], poly[i + 1], 0);
        }
        if (batch.tris.empty()) {
            out.batches.pop_back();
        } else {
            ++out.piecesDrawn;
        }
    }
}

}  // namespace stage3d
}  // namespace zengine
