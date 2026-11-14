// test_sgmesh.cpp - the stage mesh reader (src/core/sgmesh.*): a mesh built byte by byte (static and skinned), the ways a file can be wrong,
// and - when ZDROID_TEST_STAGES names a folder - every *.sgm.msb of the real game.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "sgmesh.h"

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

namespace {
struct W {
    std::vector<uint8_t> b;
    void u64(uint64_t v) { for (int i = 7; i >= 0; --i) b.push_back(static_cast<uint8_t>(v >> (8 * i))); }
    void str(const std::string& s) { u64(s.size()); b.insert(b.end(), s.begin(), s.end()); }
    void f32(float f) { uint32_t v; std::memcpy(&v, &f, 4); for (int i = 3; i >= 0; --i) b.push_back(static_cast<uint8_t>(v >> (8 * i))); }
    void u16(uint16_t v) { b.push_back(static_cast<uint8_t>(v >> 8)); b.push_back(static_cast<uint8_t>(v)); }
    void byte(uint8_t v) { b.push_back(v); }
};

// A quad (4 vertices, 2 triangles); skinned: weights on joint 1 / 0.
std::vector<uint8_t> makeMesh(bool skinned, int jointCount) {
    W w;
    w.str("2.0");
    w.str("stage_ground");
    const float xf[13] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 90};
    for (float f : xf) w.f32(f);
    w.str(skinned ? "float p[3],n[3],uv[2]; uchar4 c; uchar4 j[4],jw[4];" : "float p[3],n[3],uv[2]; uchar4 c;");
    w.u64(skinned ? 44 : 36);
    w.u64(4);
    w.u64(2);
    w.u64(skinned ? static_cast<uint64_t>(jointCount) : 0);
    const float px[4][2] = {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
    for (int v = 0; v < 4; ++v) {
        w.f32(px[v][0]); w.f32(px[v][1]); w.f32(0.5f);
        w.f32(0); w.f32(0); w.f32(1);
        w.f32(px[v][0] > 0 ? 1.0f : 0.0f); w.f32(px[v][1] > 0 ? 1.0f : 0.0f);
        w.byte(255); w.byte(128); w.byte(64); w.byte(255);
        if (skinned) {
            w.byte(1); w.byte(0); w.byte(0); w.byte(0);
            w.byte(192); w.byte(63); w.byte(0); w.byte(0);
        }
    }
    for (uint16_t i : {0, 1, 2, 2, 1, 3}) w.u16(i);
    for (float f : {-1.0f, -1.0f, 0.5f, 1.0f, 1.0f, 0.5f}) w.f32(f);
    if (skinned) {
        for (int j = 0; j < jointCount; ++j) w.str("root|joint" + std::to_string(j));
        for (int j = 0; j < jointCount; ++j)
            for (int k = 0; k < 16; ++k) w.f32(k % 5 == 0 ? 1.0f : (k == 14 ? static_cast<float>(j) : 0.0f));
    }
    return w.b;
}
}  // namespace

int main() {
    {   // static
        sgmesh::Mesh m;
        std::string err;
        CHECK(sgmesh::parse(makeMesh(false, 0), m, &err));
        if (m.verts.size() != 4 || m.idx.size() != 6) { std::printf("static mesh did not parse: %s\n", err.c_str()); return 1; }
        CHECK(m.version == "2.0" && m.material == "stage_ground" && !m.skinned);
        CHECK(m.verts.size() == 4 && m.triangles() == 2 && m.idx.size() == 6 && m.joints.empty());
        CHECK(m.xf[3] == 1.0f && m.xf[12] == 90.0f);
        CHECK(m.verts[1].p[0] == 1.0f && m.verts[1].p[1] == -1.0f && m.verts[1].p[2] == 0.5f);
        CHECK(m.verts[3].uv[0] == 1.0f && m.verts[3].uv[1] == 1.0f && m.verts[0].n[2] == 1.0f);
        CHECK(m.verts[2].c[0] == 255 && m.verts[2].c[1] == 128 && m.verts[2].c[2] == 64 && m.verts[2].c[3] == 255);
        CHECK(m.idx[5] == 3 && m.idx[3] == 2);
        CHECK(m.bbMin[0] == -1.0f && m.bbMax[1] == 1.0f && m.bbMax[2] == 0.5f);
    }
    {   // skinned
        sgmesh::Mesh m;
        std::string err;
        CHECK(sgmesh::parse(makeMesh(true, 3), m, &err));
        if (m.verts.size() != 4 || m.joints.size() != 3) { std::printf("skinned mesh did not parse: %s\n", err.c_str()); return 1; }
        CHECK(m.skinned && m.joints.size() == 3);
        CHECK(m.joints[2].path == "root|joint2" && m.joints[2].invBind[14] == 2.0f && m.joints[2].invBind[0] == 1.0f);
        CHECK(m.verts[0].j[0] == 1 && m.verts[0].jw[0] == 192 && m.verts[0].jw[1] == 63);
        CHECK(m.verts[0].jw[0] + m.verts[0].jw[1] + m.verts[0].jw[2] + m.verts[0].jw[3] == 255);
    }
    {   // wrong files
        sgmesh::Mesh m;
        std::string err;
        std::vector<uint8_t> ok = makeMesh(false, 0);
        CHECK(!sgmesh::parse({}, m, &err));
        std::vector<uint8_t> cut(ok.begin(), ok.end() - 5);
        CHECK(!sgmesh::parse(cut, m, &err));
        std::vector<uint8_t> extra = ok;
        extra.push_back(0);
        CHECK(!sgmesh::parse(extra, m, &err) && err.find("left") != std::string::npos);
        std::vector<uint8_t> badIdx = ok;
        // the last index (3) is 2 bytes before the 24-byte bounding box
        badIdx[badIdx.size() - 24 - 1] = 9;
        CHECK(!sgmesh::parse(badIdx, m, &err) && err.find("index") != std::string::npos);
        std::vector<uint8_t> badVer = ok;
        badVer[8 + 0] = '3';   // "3.0"
        CHECK(!sgmesh::parse(badVer, m, &err) && err.find("version") != std::string::npos);
        // a skinned vertex weighting a joint that is not there
        CHECK(!sgmesh::parse(makeMesh(true, 1), m, &err));
        // a count that cannot fit
        std::vector<uint8_t> huge = ok;
        const size_t countAt = 8 + 3 + 8 + 12 + 13 * 4 + 8 + 35 + 8;   // the vertex count
        huge[countAt + 4] = 0x7f;
        CHECK(!sgmesh::parse(huge, m, &err));
    }
    // the real thing
    if (const char* dir = std::getenv("ZDROID_TEST_STAGES"); dir && *dir) {
        namespace fs = std::filesystem;
        int files = 0, bad = 0, skinned = 0;
        size_t verts = 0, tris = 0;
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            const std::string name = it->path().filename().string();
            if (name.size() < 9 || name.compare(name.size() - 8, 8, ".sgm.msb") != 0) continue;
            std::ifstream f(it->path(), std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            sgmesh::Mesh m;
            std::string err;
            ++files;
            if (!sgmesh::parse(bytes, m, &err)) {
                ++bad;
                if (bad <= 5) std::printf("  %s: %s\n", name.c_str(), err.c_str());
                continue;
            }
            verts += m.verts.size();
            tris += m.triangles();
            if (m.skinned) {
                ++skinned;
                for (const auto& v : m.verts) {   // weights add up to 255 on every vertex
                    const int s = v.jw[0] + v.jw[1] + v.jw[2] + v.jw[3];
                    if (s != 255) { ++bad; break; }
                }
            }
            for (const auto& v : m.verts)   // inside the stored bounding box (a hair of slack)
                for (int k = 0; k < 3; ++k)
                    if (v.p[k] < m.bbMin[k] - 0.01f || v.p[k] > m.bbMax[k] + 0.01f) { ++bad; goto next; }
        next:;
        }
        std::printf("real meshes: %d files (%d skinned), %zu vertices, %zu triangles, %d problems\n", files, skinned, verts, tris, bad);
        CHECK(bad == 0);
    }
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
