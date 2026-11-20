// test_spark_fx.cpp - the state -> picture table, where a spark frame lands on the canvas and (when ZDROID_TEST_DATA holds the
// user's own hitspark.foit) that the effect objects run their frames and end by themselves.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "spark_fx.h"
#include "vfs.h"

using namespace zengine;
using namespace zengine::fx;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                       \
    do {                                                               \
        ++g_checks;                                                    \
        if (!(c)) {                                                    \
            ++g_fail;                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);   \
        }                                                              \
    } while (0)

static bool near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) <= eps; }

static void testTable() {
    CHECK(std::string(sparkSprite("Light")) == "hitspark_omni_l");
    CHECK(std::string(sparkSprite("Hard")) == "hitspark_omni_h");
    CHECK(std::string(sparkSprite("Block")) == "block");
    CHECK(std::string(sparkSprite("BlockAG")) == "block_ag");
    CHECK(std::string(sparkSprite("Whiff")) == "miss_spark");
    CHECK(sparkSprite("None") == nullptr);          // the dispatcher itself shows nothing
    CHECK(sparkSprite("PBGC_Hearts") == nullptr);   // text / art-point effects are not drawn
    CHECK(sparkSprite("") == nullptr);
    CHECK(sparkSprite("light") == nullptr);         // names are exact
}

static void testPlace() {
    SparkCam cam;
    cam.x0 = 320;
    cam.y0 = 300;
    cam.kx = 0.5;
    cam.ky = 0.5;
    cam.scale = 0.5;
    // facing right: the hot spot (10, 20) of a 40x30 picture sits on the point (100, 60) -> (370, 270)
    SparkQuad q = placeSpark(cam, 100, 60, 40, 30, 10, 20, true, 1.0, 1.0);
    CHECK(!q.flip);
    CHECK(near(q.w, 20) && near(q.h, 15));
    CHECK(near(q.x, 370 - 10 * 0.5f) && near(q.y, 270 - 20 * 0.5f));
    // facing left: mirrored, the hot spot is measured from the other edge
    q = placeSpark(cam, 100, 60, 40, 30, 10, 20, false, 1.0, 1.0);
    CHECK(q.flip);
    CHECK(near(q.x, 370 - (40 - 10) * 0.5f));
    // the effect's own scale slots grow it around the hot spot
    q = placeSpark(cam, 100, 60, 40, 30, 10, 20, true, 2.0, 2.0);
    CHECK(near(q.w, 40) && near(q.h, 30));
    CHECK(near(q.x, 370 - 10 * 1.0f) && near(q.y, 270 - 20 * 1.0f));
    // a negative scale never turns the picture inside out
    q = placeSpark(cam, 100, 60, 40, 30, 10, 20, true, -1.0, 1.0);
    CHECK(q.w > 0);
}

static void testRealData() {
    const char* dir = std::getenv("ZDROID_TEST_DATA");
    if (!dir || !*dir) {
        std::printf("spark_fx: ZDROID_TEST_DATA not set, skipping the data driven part\n");
        return;
    }
    Vfs vfs;
    vfs.mount(dir);
    SparkFx fx;
    if (!fx.load(vfs)) {
        std::printf("spark_fx: no hitspark.foit under %s, skipping\n", dir);
        return;
    }
    CHECK(fx.ready());
    foit::rt::Event e;
    e.kind = foit::rt::Event::HitSpark;
    e.spark = 1;
    e.hitX = 100;
    e.hitY = 200;
    e.faceRight = true;
    fx.spawn(e);
    CHECK(fx.liveCount() == 1);
    int frame = -1;
    const std::string first = fx.liveState(0, &frame);
    CHECK(!first.empty());
    int ticks = 0;
    int lastFrame = -1;
    while (fx.liveCount() > 0 && ticks < 200) {
        int f = -1;
        const std::string st = fx.liveState(0, &f);
        if (st == "Light") lastFrame = f;
        fx.tick();
        ++ticks;
    }
    CHECK(fx.liveCount() == 0);   // the effect ends itself
    CHECK(ticks < 200);
    CHECK(lastFrame > 0);         // and stepped through its picture on the way
    // no spark class -> nothing; a block raises the block mark
    e.spark = 0;
    fx.spawn(e);
    CHECK(fx.liveCount() == 0);
    e.kind = foit::rt::Event::BlockSpark;
    e.spark = -1;
    fx.spawn(e);
    CHECK(fx.liveCount() == 1);
    fx.reset();
    CHECK(fx.liveCount() == 0);
    // the cap: the oldest goes
    e.kind = foit::rt::Event::HitSpark;
    e.spark = 2;
    for (int i = 0; i < SparkFx::kMaxLive + 10; ++i) fx.spawn(e);
    CHECK(static_cast<int>(fx.liveCount()) <= SparkFx::kMaxLive);
}

int main() {
    testTable();
    testPlace();
    testRealData();
    std::printf("spark_fx tests: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
