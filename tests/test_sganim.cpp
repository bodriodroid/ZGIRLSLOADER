// test_sganim.cpp - the stage texture-coordinate animations (src/core/sganim.*): built byte by byte, the ways a file can be wrong, sampling of flipbooks
// and scrolls, and - when ZDROID_TEST_STAGES names a folder - every *.sga.msb of the real game (each one parses, or is a skeleton animation).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "sganim.h"

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
    void str(const std::string& s) { u64(s.size()); b.insert(b.end(), s.begin(), s.end()); }
    void f32(float f) { uint32_t v; std::memcpy(&v, &f, 4); u32(v); }
};

// translation keys (u, v); scale 1,1 and rotation 0 on every key
std::vector<uint8_t> anim(float duration, const std::vector<std::pair<float, float>>& trans, uint32_t joints = 0, const char* version = "3.0") {
    W w;
    w.str(version);
    w.u32(static_cast<uint32_t>(trans.size()));
    w.u32(0);
    w.u32(joints);
    w.u32(0);
    w.u32(1);
    w.f32(duration);
    w.str("uv_track");
    w.u64(trans.size());
    for (size_t i = 0; i < trans.size(); ++i) { w.f32(1); w.f32(1); }
    w.u64(trans.size());
    for (size_t i = 0; i < trans.size(); ++i) w.f32(0);
    w.u64(trans.size());
    for (const auto& t : trans) { w.f32(t.first); w.f32(t.second); }
    return w.b;
}
}  // namespace

int main() {
    {   // a flipbook: 4 frames of a sheet 4 cells wide (cell = 0.25), 2 seconds
        sganim::Anim a;
        std::string err;
        CHECK(sganim::parse(anim(2.0f, {{0, 0}, {0.25f, 0}, {0.5f, 0}, {0.75f, 0}}), a, &err));
        CHECK(a.stepped && NEAR(a.duration, 2.0, 1e-6) && a.tracks.size() == 1 && a.tracks[0].keys() == 4 && a.tracks[0].name == "uv_track");
        // each key is held for duration / keys = 0.5 s, and the animation loops
        CHECK(NEAR(sganim::sample(a, 0.0).tu, 0.0, 1e-6));
        CHECK(NEAR(sganim::sample(a, 0.49).tu, 0.0, 1e-6));
        CHECK(NEAR(sganim::sample(a, 0.5).tu, 0.25, 1e-6));
        CHECK(NEAR(sganim::sample(a, 1.6).tu, 0.75, 1e-6));
        CHECK(NEAR(sganim::sample(a, 2.1).tu, 0.0, 1e-6));    // wrapped
        CHECK(NEAR(sganim::sample(a, -0.1).tu, 0.75, 1e-6));  // before the start wraps backwards
        float u = 0.1f, v = 0.2f;
        sganim::sample(a, 1.0).apply(u, v);
        CHECK(NEAR(u, 0.6, 1e-6) && NEAR(v, 0.2, 1e-6));
    }
    {   // a scroll: v from 0 to -1 in 11 keys over 1 s: interpolated, the last key meets the first
        std::vector<std::pair<float, float>> k;
        for (int i = 0; i <= 10; ++i) k.push_back({0, -0.1f * static_cast<float>(i)});
        sganim::Anim a;
        CHECK(sganim::parse(anim(1.0f, k), a));
        CHECK(!a.stepped);
        CHECK(NEAR(sganim::sample(a, 0.0).tv, 0.0, 1e-6));
        CHECK(NEAR(sganim::sample(a, 0.25).tv, -0.25, 1e-5));   // between keys
        CHECK(NEAR(sganim::sample(a, 0.5).tv, -0.5, 1e-5));
        CHECK(NEAR(sganim::sample(a, 0.99).tv, -0.99, 1e-4));
    }
    {   // a slow drift: two keys a whole texture apart over 1000 s is a scroll, not a flipbook
        sganim::Anim a;
        CHECK(sganim::parse(anim(1000.0f, {{0, 0}, {-1, 0}}), a));
        CHECK(!a.stepped);
        CHECK(NEAR(sganim::sample(a, 500.0).tu, -0.5, 1e-5));
    }
    {   // one key: always that key
        sganim::Anim a;
        CHECK(sganim::parse(anim(1.0f, {{0.3f, 0.4f}}), a));
        CHECK(NEAR(sganim::sample(a, 12.3).tu, 0.3, 1e-6) && NEAR(sganim::sample(a, 12.3).tv, 0.4, 1e-6));
    }
    {   // the ways a file can be wrong
        sganim::Anim a;
        std::string err;
        CHECK(!sganim::parse({}, a, &err));
        CHECK(!sganim::parse(anim(1.0f, {{0, 0}, {1, 0}}, 5), a, &err) && err == "skeleton animation");
        CHECK(!sganim::parse(anim(1.0f, {{0, 0}}, 0, "2.0"), a, &err));
        std::vector<uint8_t> good = anim(1.0f, {{0, 0}, {0.5f, 0}});
        std::vector<uint8_t> cut(good.begin(), good.end() - 3);
        CHECK(!sganim::parse(cut, a, &err));
        std::vector<uint8_t> extra = good;
        extra.push_back(1);
        CHECK(!sganim::parse(extra, a, &err));
        CHECK(!sganim::parse(anim(0.0f, {{0, 0}}), a, &err));   // no duration
        W huge;
        huge.str("3.0");
        for (int i = 0; i < 4; ++i) huge.u32(0);
        huge.u32(1);
        huge.f32(1);
        huge.str("uv_track");
        huge.u64(1ull << 40);
        CHECK(!sganim::parse(huge.b, a, &err));
        // an empty Anim samples as the identity
        const sganim::Uv id = sganim::sample(sganim::Anim(), 3.0);
        CHECK(id.su == 1 && id.tu == 0);
    }
    if (const char* dir = std::getenv("ZDROID_TEST_STAGES"); dir && *dir) {
        namespace fs = std::filesystem;
        int files = 0, ok = 0, skeleton = 0, bad = 0, stepped = 0;
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            const std::string name = it->path().filename().string();
            if (name.size() < 9 || name.compare(name.size() - 8, 8, ".sga.msb") != 0) continue;
            std::ifstream f(it->path(), std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            sganim::Anim a;
            std::string err;
            ++files;
            if (sganim::parse(bytes, a, &err)) {
                ++ok;
                if (a.stepped) ++stepped;
                // sampling never produces a non-finite value
                const double dur = a.duration;
                for (double t : {0.0, 0.3, dur * 0.5, dur, dur * 3.7}) {
                    const sganim::Uv u = sganim::sample(a, t);
                    if (!std::isfinite(u.tu) || !std::isfinite(u.tv) || !std::isfinite(u.su) || !std::isfinite(u.sv)) ++bad;
                }
            } else if (err == "skeleton animation") {
                ++skeleton;
            } else {
                ++bad;
                if (bad <= 5) std::printf("  %s: %s\n", name.c_str(), err.c_str());
            }
        }
        std::printf("real animations: %d files, %d texture animations (%d flipbooks), %d skeleton animations (not read), %d problems\n", files, ok, stepped, skeleton, bad);
        CHECK(bad == 0);
    }
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
