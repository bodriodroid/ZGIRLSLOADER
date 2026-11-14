// test_sgscene.cpp - the stage scene reader (src/core/sgscene.*): a scene built byte by byte, the ways a file can be wrong, and - when
// ZDROID_TEST_STAGES names a folder - every *.sgi.msb of the real game.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "sgscene.h"

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
    void u8(uint8_t v) { b.push_back(v); }
    void inst(const std::string& path, const std::string& mesh, float tx, float ty, float tz, int flagBytes, uint8_t f0, uint64_t anims) {
        str(path); str(mesh); matrix(tx, ty, tz);
        u8(f0);
        for (int i = 1; i < flagBytes; ++i) u8(0);
        u64(anims);
    }
    void str(const std::string& s) { u64(s.size()); b.insert(b.end(), s.begin(), s.end()); }
    void f32(float f) { uint32_t v; std::memcpy(&v, &f, 4); for (int i = 3; i >= 0; --i) b.push_back(static_cast<uint8_t>(v >> (8 * i))); }
    void matrix(float tx, float ty, float tz) {
        const float id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, tx, ty, tz, 1};
        for (float f : id) f32(f);
    }
};

std::vector<uint8_t> sample(const char* version = "2.0", int flagBytes = 2) {
    W w;
    w.str(version);
    w.u64(3);
    w.inst("|Back", "back_mesh", 0, 0, -300, flagBytes, 1, 0);
    w.inst("|Npc", "npc_mesh", 5, 6, 7, flagBytes, 1, 2);
    w.str("idle"); w.str("npc_idle"); w.str("CamShakeLight"); w.str("shock");
    w.inst("FightPlane", "", 0, 0, 0, flagBytes, 0, 0);
    return w.b;
}
}  // namespace

int main() {
    {
        sgscene::Scene s;
        std::string err;
        CHECK(sgscene::parse(sample(), s, &err));
        CHECK(s.version == "2.0");
        CHECK(s.instances.size() == 3);
        CHECK(s.instances[0].path == "|Back" && s.instances[0].mesh == "back_mesh");
        CHECK(s.instances[0].m[14] == -300.0f && s.instances[0].m[15] == 1.0f);
        CHECK(s.instances[0].anims.empty());
        CHECK(s.instances[1].m[12] == 5.0f && s.instances[1].m[13] == 6.0f);
        CHECK(s.instances[1].anims.size() == 2);
        CHECK(s.instances[1].anim("idle") == "npc_idle");
        CHECK(s.instances[1].anim("CamShakeLight") == "shock");
        CHECK(s.instances[1].anim("walk").empty());
        CHECK(s.instances[1].hasGeometry());
        CHECK(!s.instances[2].hasGeometry() && s.instances[2].flags[0] == 0);
        CHECK(s.instances[0].flags[0] == 1);
    }
    {   // version 2.1: six flag bytes
        sgscene::Scene s;
        CHECK(sgscene::parse(sample("2.1", 6), s));
        CHECK(s.version == "2.1" && s.instances.size() == 3);
        CHECK(s.instances[1].anim("idle") == "npc_idle" && s.instances[1].m[14] == 7.0f);
        CHECK(!sgscene::parse(sample("2.1", 2), s));   // 2.0 bytes under a 2.1 header
    }
    {   // the ways a file can be wrong
        sgscene::Scene s;
        std::string err;
        std::vector<uint8_t> good = sample();
        CHECK(!sgscene::parse({}, s, &err));
        std::vector<uint8_t> cut(good.begin(), good.end() - 5);
        CHECK(!sgscene::parse(cut, s, &err));
        std::vector<uint8_t> extra = good;
        extra.push_back(0);
        CHECK(!sgscene::parse(extra, s, &err));
        W v;
        v.str("9.9");
        v.u64(0);
        CHECK(!sgscene::parse(v.b, s, &err));
        W huge;
        huge.str("2.0");
        huge.u64(1ull << 40);
        CHECK(!sgscene::parse(huge.b, s, &err));
        W bigStr;   // a string that claims to be longer than the file
        bigStr.str("2.0");
        bigStr.u64(1);
        bigStr.u64(1ull << 40);
        CHECK(!sgscene::parse(bigStr.b, s, &err));
    }
    // the real thing
    if (const char* dir = std::getenv("ZDROID_TEST_STAGES"); dir && *dir) {
        namespace fs = std::filesystem;
        int files = 0, bad = 0;
        size_t inst = 0, anims = 0, noGeom = 0, oddMat = 0;
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            const std::string name = it->path().filename().string();
            if (name.size() < 9 || name.compare(name.size() - 8, 8, ".sgi.msb") != 0) continue;
            std::ifstream f(it->path(), std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            sgscene::Scene s;
            std::string err;
            ++files;
            if (!sgscene::parse(bytes, s, &err)) {
                ++bad;
                if (bad <= 5) std::printf("  %s: %s\n", name.c_str(), err.c_str());
                continue;
            }
            inst += s.instances.size();
            for (const auto& in : s.instances) {
                anims += in.anims.size();
                if (!in.hasGeometry()) ++noGeom;
                // an affine row-vector matrix: last column 0 0 0 1, everything finite
                if (in.m[3] != 0 || in.m[7] != 0 || in.m[11] != 0 || in.m[15] != 1) ++oddMat;
                for (float v : in.m)
                    if (!std::isfinite(v)) ++oddMat;
            }
        }
        std::printf("real scenes: %d files, %zu instances (%zu locators), %zu animation slots, %zu odd matrices, %d problems\n", files, inst,
                    noGeom, anims, oddMat, bad);
        CHECK(bad == 0);
        CHECK(oddMat == 0);
    }
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
