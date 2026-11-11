// sg_loader unit tests - dependency-free (no test framework).
//
// Environment (all optional; tests that need them print SKIP):
//   ZDROID_TEST_DATA    ':'-separated folders holding the user's extracted game data (core/, Win/, ui/)
//   ZDROID_TEST_ORACLE  folder produced by tests/oracle/gen_oracle.py
//   ZDROID_TEST_CHAR    folder with the user's real annie.foit + annie.art + annie.art-pt + annie_1p.dds
#include <algorithm>
#include <sys/stat.h>
#include <unistd.h>
#include <csignal>
#include <sys/wait.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <filesystem>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "arcade.h"
#include "art.h"
#include "dds.h"
#include "errcode.h"
#include "modcheck.h"
#include "ramprobe.h"
#include "dev_options.h"
#include "imgscale.h"
#include "duel.h"
#include "foit.h"
#include "foit_vm.h"
#include "foit_rt.h"
#include "foit_duel.h"
#include "gamedata.h"
#include "gbs.h"
#include "gbs_anim.h"
#include "gbs_arc.h"
#include "menu_list_anim.h"
#include "gbs_scene.h"
#include "gbs_text.h"
#include "level.h"
#include "hud_art.h"
#include "menu_data.h"
#include "music_lib.h"
#include "modsdir.h"
#include "palgen.h"
#include "palmod.h"
#include "moveset.h"
#include "steam_dl.h"
#include "steam_proc.h"
#include "spr_tiles.h"
#include "savini.h"
#include "steam_task.h"
#include "story.h"
#include "text_formats.h"
#include "vfs.h"

namespace fs = std::filesystem;
using namespace zengine;

static int g_checks = 0, g_fail = 0, g_skipped = 0;

#define CHECK(c)                                                                  \
    do {                                                                          \
        ++g_checks;                                                               \
        if (!(c)) {                                                               \
            ++g_fail;                                                             \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);     \
        }                                                                         \
    } while (0)

template <class A, class B>
static void checkEq(const A& a, const B& b, const char* ea, const char* eb, int line) {
    ++g_checks;
    if (!(a == b)) {
        ++g_fail;
        std::ostringstream o;
        o << "FAIL line " << line << ": " << ea << " == " << eb << "  (" << a << " vs " << b << ")";
        std::fprintf(stderr, "%s\n", o.str().c_str());
    }
}
#define CHECK_EQ(a, b) checkEq((a), (b), #a, #b, __LINE__)

static void skip(const char* what) {
    ++g_skipped;
    std::printf("  SKIP %s\n", what);
}

static std::string makeTempDir(const char* tag) {
    std::string base = (fs::temp_directory_path() / (std::string("sgl_") + tag + "_XXXXXX")).string();
    std::vector<char> buf(base.begin(), base.end());
    buf.push_back(0);
    if (!mkdtemp(buf.data())) { std::perror("mkdtemp"); std::exit(2); }
    return buf.data();
}

static void writeText(const std::string& path, const std::string& text) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

static std::vector<std::string> splitColon(const char* s) {
    std::vector<std::string> r;
    std::string cur;
    for (; s && *s; ++s) {
        if (*s == ':') { if (!cur.empty()) r.push_back(cur); cur.clear(); }
        else cur.push_back(*s);
    }
    if (!cur.empty()) r.push_back(cur);
    return r;
}


// ============================================================ MUSIC CATALOGUE
// A fake data folder: music.ini + tiny files named like the game's themes (the library only looks at names and sizes).
static void writeBlob(const std::string& path, size_t bytes) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream(path, std::ios::binary) << std::string(bytes, 'x');
}

static void testMusicLibrary() {
    std::printf("[music catalogue]\n");
    CHECK_EQ(MusicLibrary::normalise("_Empty_Lab8"), std::string("emptylab8"));
    CHECK_EQ(MusicLibrary::levelWorld("river_king_casino"), std::string("riverking"));
    CHECK_EQ(MusicLibrary::levelWorld("rooftops_day_3d"), std::string("meridianrooftops"));
    CHECK_EQ(MusicLibrary::levelWorld("asg_labs_empty_3d"), std::string("emptylab8"));
    CHECK_EQ(MusicLibrary::levelWorld("new_meridian_3d"), std::string("newmeridian"));
    CHECK_EQ(MusicLibrary::levelWorld("arena_SGCS2024"), std::string("arena"));

    const std::string root = makeTempDir("music");
    writeText(root + "/core/music.ini",
              "# comment\r\n"
              "Track: Moonlit Melee\r\nStage: New_Meridian\r\nFiles: skull02_1, skull02_2\r\n\r\n"
              "Track: Clear Breeze\r\nStage: Meridian_Rooftops\r\nFiles: skull_rooftops_1, skull_rooftops_2\r\n\r\n"
              "Track: Where Money Flows Like Water \r\nStage: River_King \r\nFiles: skull_casino_1, skull_casino_2\r\n\r\n"
              "Track: Skull Heart Arrhythmia\r\nStage: Falling_Wreckage\r\nFiles: skull_boss_1, skull_boss_2\r\n\r\n"
              "Track: Moonlit Melee(cutscene) \r\nFiles: blankmusic, sg02_dnb_loop\r\n\r\n"
              "Track: In Rapid Succession\r\nFiles: sg_news_intro, sg_news_loop\r\n\r\n"
              "Track: Whiling the Hours Away\r\nFiles: sg_k1_intro, sg_k1_loop\r\n\r\n"
              "Track: A Roll of the Dice\r\nFiles: sg31_intro, sg31_loop\r\n\r\n"
              "Track: Not On Disk\r\nStage: Gehenna\r\nFiles: sg_gehenna_1, sg_gehenna_2\r\n");
    const size_t big = 200 * 1024;
    for (const char* f : {"skull02_1", "skull02_2", "skull_rooftops_1", "skull_rooftops_2", "skull_casino_1", "skull_casino_2", "skull_boss_1",
                          "skull_boss_2", "sg02_dnb_loop", "sg_news_intro", "sg_news_loop", "sg_k1_intro", "sg_k1_loop", "sg31_intro", "sg31_loop"})
        writeBlob(root + "/music/" + f + ".wav", big);
    // themes the ini does not list: menu / select, a credits theme with an intro, a lone ending loop, a number cue, a letter cue
    writeBlob(root + "/music/menu_intro.wav", big);
    writeBlob(root + "/music/menu_loop.wav", big);
    writeBlob(root + "/music/select_intro.wav", big);
    writeBlob(root + "/music/select_loop.wav", big);
    writeBlob(root + "/music/credits_intro.wav", big);
    writeBlob(root + "/music/credits_loop.wav", big);
    writeBlob(root + "/music/ending_loop.wav", big);
    writeBlob(root + "/music/sg24_loop.wav", big);
    writeBlob(root + "/music/sg_r_loop.wav", big);
    writeBlob(root + "/UISound/click.wav", big);        // effects folder: never a theme
    writeBlob(root + "/music/short_jingle.wav", 20 * 1024);   // too short to be a theme

    Vfs v;
    v.mount(root);
    MusicLibrary lib;
    lib.scan(v);
    CHECK(lib.entries().size() >= 15);
    CHECK(lib.presentCount() >= 14);

    auto titleOf = [&](int i) { return i >= 0 ? lib.entries()[static_cast<size_t>(i)].title : std::string("<none>"); };
    auto loopOf = [&](int i) { return i >= 0 ? lib.entries()[static_cast<size_t>(i)].loopStem : std::string("<none>"); };
    // fights: the stage's own theme, files resolved by name
    CHECK_EQ(titleOf(lib.forLevel("new_meridian_3d")), std::string("Moonlit Melee"));
    CHECK_EQ(titleOf(lib.forLevel("rooftops_day_3d")), std::string("Clear Breeze"));
    CHECK_EQ(titleOf(lib.forLevel("river_king_casino")), std::string("Where Money Flows Like Water"));
    CHECK_EQ(titleOf(lib.forFight("falling_wreckage")), std::string("Skull Heart Arrhythmia"));
    CHECK_EQ(lib.forLevel("gehenna_3d"), -1);               // its files are not on disk
    CHECK(lib.forLevel("boardwalk_3d") < 0);                // music.ini has no stage for it ...
    const int stand = lib.forFight("boardwalk_3d");         // ... so a stage theme stands in, always the same one
    CHECK(stand >= 0);
    CHECK_EQ(lib.forFight("boardwalk_3d"), stand);
    CHECK(lib.entries()[static_cast<size_t>(stand)].kind == MusicEntry::Kind::Stage);
    CHECK(lib.entries()[static_cast<size_t>(lib.forFight("gehenna_3d"))].present());
    // files: the intro comes first in "Files:", the loop second; "blankmusic" is no intro
    const MusicEntry& moon = lib.entries()[static_cast<size_t>(lib.forLevel("new_meridian"))];
    CHECK_EQ(moon.introFile, std::string("skull02_1.wav"));
    CHECK_EQ(moon.loopFile, std::string("skull02_2.wav"));
    // story cues
    CHECK_EQ(loopOf(lib.forCue("24")), std::string("sg24_loop"));       // discovered file, named sg<code>
    CHECK_EQ(loopOf(lib.forCue("31")), std::string("sg31_loop"));       // from music.ini
    CHECK_EQ(loopOf(lib.forCue("02")), std::string("sg02_dnb_loop"));   // the cutscene version of Moonlit Melee
    CHECK_EQ(loopOf(lib.forCue("NEWS")), std::string("sg_news_loop"));
    CHECK_EQ(loopOf(lib.forCue("K")), std::string("sg_k1_loop"));       // sg_k1: a letter then a digit
    CHECK_EQ(loopOf(lib.forCue("R")), std::string("sg_r_loop"));
    CHECK_EQ(loopOf(lib.forCue("BOSS")), std::string("skull_boss_2"));
    CHECK_EQ(lib.forCue("W"), -1);                                       // nothing named for it: the caller keeps the music
    CHECK_EQ(lib.forCue("STOP"), -1);
    CHECK_EQ(lib.forCue("21"), -1);                                      // "skull_rooftops" must not answer to "R"-like prefixes
    // screens
    CHECK_EQ(loopOf(lib.menu()), std::string("menu_loop"));
    CHECK_EQ(loopOf(lib.select()), std::string("select_loop"));
    CHECK_EQ(loopOf(lib.credits()), std::string("credits_loop"));
    CHECK_EQ(loopOf(lib.ending()), std::string("ending_loop"));
    CHECK_EQ(loopOf(lib.gallery()), std::string("menu_loop"));           // no gallery theme in the data: the menu's
    CHECK(lib.summary().find("missing") != std::string::npos);

    // nothing but the effects folder: every screen is silent, nothing crashes
    const std::string root2 = makeTempDir("music_empty");
    writeBlob(root2 + "/UISound/click.wav", big);
    Vfs v2;
    v2.mount(root2);
    MusicLibrary none;
    none.scan(v2);
    CHECK_EQ(none.entries().size(), static_cast<size_t>(0));
    CHECK_EQ(none.menu(), -1);
    CHECK_EQ(none.credits(), -1);
    CHECK_EQ(none.gallery(), -1);
    CHECK_EQ(none.forFight("arena"), -1);
    CHECK_EQ(none.forCue("24"), -1);
}

// The test level (Training Stage, test1, test2) has its own theme - the "Final Remake" files, named the way a person typed them - when
// the user's data has them; a music.ini of the user's can give any stage a theme, and wins over the loader's own.
static void testBonusMusic() {
    std::printf("[music: test level theme + extra music.ini]\n");
    const size_t big = 200 * 1024;
    // 1. the files are there (note the space before ".wav" of the intro); the game's music.ini has nothing for the training stage
    const std::string a = makeTempDir("music_bonus");
    writeText(a + "/core/music.ini", "Track: Moonlit Melee\nStage: New_Meridian\nFiles: skull02_1, skull02_2\n");
    writeBlob(a + "/music/skull02_1.wav", big);
    writeBlob(a + "/music/skull02_2.wav", big);
    writeBlob(a + "/music/Hymn For The Weekend Final Remake Intro .wav", big);
    writeBlob(a + "/music/Hymn For The Weekend Final Remake Loop.wav", big);
    Vfs va;
    va.mount(a);
    MusicLibrary la;
    la.scan(va);
    auto title = [](const MusicLibrary& l, int i) { return i >= 0 ? l.entries()[static_cast<size_t>(i)].title : std::string("<none>"); };
    CHECK_EQ(title(la, la.forLevel("training_3d")), std::string("Hymn For The Weekend (Final Remake)"));
    CHECK_EQ(title(la, la.forLevel("test1")), std::string("Hymn For The Weekend (Final Remake)"));
    CHECK_EQ(title(la, la.forFight("training_3d")), std::string("Hymn For The Weekend (Final Remake)"));
    CHECK_EQ(title(la, la.forLevel("new_meridian_3d")), std::string("Moonlit Melee"));
    // ... and it is the test level's alone: a stage with no theme of its own gets a stand-in from the game's, never this one
    CHECK_EQ(title(la, la.forFight("arena")), std::string("Moonlit Melee"));
    const int h = la.forLevel("training_3d");
    CHECK(h >= 0 && la.entries()[static_cast<size_t>(h)].introFile == "Hymn For The Weekend Final Remake Intro .wav");
    CHECK(h >= 0 && la.entries()[static_cast<size_t>(h)].loopFile == "Hymn For The Weekend Final Remake Loop.wav");
    int hymns = 0;   // not listed a second time as a stray "other" theme
    for (const MusicEntry& e : la.entries()) hymns += e.title.find("Hymn") != std::string::npos || e.title.find("hymn") != std::string::npos;
    CHECK_EQ(hymns, 1);
    // 2. without the files the stage has no theme of its own and nothing is listed as missing
    const std::string b = makeTempDir("music_nobonus");
    writeText(b + "/core/music.ini", "Track: Moonlit Melee\nStage: New_Meridian\nFiles: skull02_1, skull02_2\n");
    writeBlob(b + "/music/skull02_1.wav", big);
    writeBlob(b + "/music/skull02_2.wav", big);
    Vfs vb;
    vb.mount(b);
    MusicLibrary lb;
    lb.scan(vb);
    CHECK_EQ(lb.forLevel("training_3d"), -1);
    // only the test-level files and no game theme at all: the other stages stay silent instead of borrowing it
    const std::string only = makeTempDir("music_only_bonus");
    writeBlob(only + "/m/Hymn For The Weekend Final Remake Intro .wav", big);
    writeBlob(only + "/m/Hymn For The Weekend Final Remake Loop.wav", big);
    Vfs vo;
    vo.mount(only);
    MusicLibrary lo;
    lo.scan(vo);
    CHECK_EQ(title(lo, lo.forFight("training_3d")), std::string("Hymn For The Weekend (Final Remake)"));
    CHECK_EQ(lo.forFight("arena"), -1);
    CHECK_EQ(lo.forFight("river_king_casino"), -1);
    CHECK_EQ(lb.summary().find("Hymn"), std::string::npos);
    // 3. another music.ini (a pack) with a theme for the same stage beats the loader's own; its stage-less names are found too
    writeText(a + "/pack/music.ini", "Track: My Test Tune\nStage: Training_Stage\nFiles: my tune intro, my tune loop\n");
    writeBlob(a + "/pack/My_Tune_Intro.wav", big);
    writeBlob(a + "/pack/my-tune-loop.wav", big);
    Vfs vc;
    vc.mount(a);
    MusicLibrary lc;
    lc.scan(vc);
    CHECK_EQ(title(lc, lc.forLevel("training_3d")), std::string("My Test Tune"));
    CHECK_EQ(title(lc, lc.forLevel("test2")), std::string("Hymn For The Weekend (Final Remake)"));   // the pack did not claim test2
    CHECK_EQ(title(lc, lc.forLevel("new_meridian_3d")), std::string("Moonlit Melee"));              // the game's own ini still counts
}

// ============================================================ VFS
static void testVfs() {
    std::printf("[vfs]\n");
    CHECK_EQ(normalizeKey("ARCADE\\Story\\Filia.STM"), std::string("arcade/story/filia.stm"));
    CHECK_EQ(normalizeKey("./a//b"), std::string("a/b"));
    CHECK_EQ(normalizeKey("/x/y"), std::string("x/y"));

    const std::string root = makeTempDir("vfs");
    writeText(root + "/r1/Core/Core/Arcade/Story/Filia.stm", "one");
    writeText(root + "/r1/Core/Core/toc.ini", "first");
    writeText(root + "/r1/extract/skipme.txt", "x");
    writeText(root + "/r1/characters/filia.foit", "in characters");
    writeText(root + "/r1/other/deep/dir/filia.foit", "elsewhere");
    writeText(root + "/r2/core/core/toc.ini", "second");

    Vfs v;
    CHECK_EQ(v.mount(root + "/r1"), static_cast<size_t>(5 - 1));  // extract/ is skipped
    CHECK_EQ(v.mount(root + "/does_not_exist"), static_cast<size_t>(0));
    CHECK(v.exists("arcade/story/filia.stm"));
    CHECK(v.exists("ARCADE\\STORY\\FILIA.STM"));
    CHECK(v.exists("Filia.stm"));
    CHECK(v.exists("core/arcade/story/filia.stm"));
    CHECK(!v.exists("extract/skipme.txt"));
    CHECK(!v.exists("skipme.txt"));
    CHECK(!v.exists("arcade/filia.stm"));  // suffixes must end on a path-part boundary

    std::string t;
    CHECK(v.readText("toc.ini", t));
    CHECK_EQ(t, std::string("first"));
    v.mount(root + "/r2");  // later mount overrides earlier for identical suffix keys
    CHECK(v.readText("toc.ini", t));
    CHECK_EQ(t, std::string("second"));

    std::string real;
    CHECK(v.pick("filia.foit", &real));
    CHECK(real.find("characters") != std::string::npos);  // "characters" paths win
    CHECK_EQ(v.listByExt(".foit").size(), static_cast<size_t>(2));
    CHECK_EQ(v.findByName("FILIA.FOIT").size(), static_cast<size_t>(2));
    fs::remove_all(root);
}

// ============================================================ text formats (sample lines copied from the real files)
static void testTextFormats() {
    std::printf("[text formats]\n");
    const char* stages =
        "# comment\n\nNew_Meridian new_meridian_3d  stage_new_meridian\n"
        "Arena_Evo arena_Evo arena_Evo IsAlt>Arena ExcludeFromRandom\r\n";
    auto st = parseStages(stages);
    CHECK_EQ(st.size(), static_cast<size_t>(2));
    CHECK_EQ(st[0].level, std::string("new_meridian_3d"));
    CHECK_EQ(st[0].picture, std::string("stage_new_meridian"));
    CHECK_EQ(st[1].options.size(), static_cast<size_t>(2));
    CHECK_EQ(st[1].options[0], std::string("IsAlt>Arena"));

    auto mu = parseMusic("Track: Moonlit Melee\nStage: New_Meridian, Other\nFiles: skull02_1, skull02_2\n\nTrack: X\nFiles: a, b");
    CHECK_EQ(mu.size(), static_cast<size_t>(2));
    CHECK_EQ(mu[0].stages.size(), static_cast<size_t>(2));
    CHECK_EQ(mu[0].files[1], std::string("skull02_2"));
    CHECK(mu[1].stages.empty());

    auto toc = parseToc("File: Peacock.stm\nFile: Ms.Fortune.stm\n");
    CHECK_EQ(toc.size(), static_cast<size_t>(2));
    CHECK_EQ(toc[1], std::string("Ms.Fortune.stm"));

    auto cj = parseCompile("NY #Y\nArtAssets/filia/filia.spt #input\nexport/temp/filia.foit #output\n");
    CHECK_EQ(cj.size(), static_cast<size_t>(1));
    CHECK_EQ(cj[0].foit, std::string("export/temp/filia.foit"));

    auto stm = parseStm("CharName Filia\r\n\r\nArcadeNode\r\nNodeType Fight\r\nFileName arcade/story/A.ini\r\nTimelinePos 2\r\n\r\nArcadeNode Complete\r\n");
    CHECK_EQ(stm.charName, std::string("Filia"));
    CHECK_EQ(stm.nodes.size(), static_cast<size_t>(2));
    CHECK(stm.nodes[0].get("NodeType") && *stm.nodes[0].get("NodeType") == "Fight");
    CHECK(stm.nodes[0].get("Missing") == nullptr);
    CHECK_EQ(stm.nodes[1].arg, std::string("Complete"));
    auto cm = parseStm("ArcadeNode\nFileName arcade/a.ini\t#old.ini\nDisplayText Keep #1 intact\n");
    CHECK(cm.nodes[0].get("FileName") && *cm.nodes[0].get("FileName") == "arcade/a.ini");
    CHECK(cm.nodes[0].get("DisplayText") && *cm.nodes[0].get("DisplayText") == "Keep");  // ' #' always starts a comment
}

// ============================================================ GBS (synthetic)
static void put32(std::vector<uint8_t>& v, size_t off, uint32_t x) {
    for (int i = 0; i < 4; ++i) v[off + i] = static_cast<uint8_t>(x >> (8 * i));
}
static std::vector<uint8_t> makeGbs(int views, uint32_t count4 = 0) {
    const uint32_t chunk = 12 + 64 + 20;
    const uint32_t payload = chunk * views;
    const uint32_t tail = count4 * kGbsTailEntrySize;
    std::vector<uint8_t> f(0x38 + payload + tail + 4, 0);
    put32(f, 0, kGbsMagic);
    put32(f, 4, static_cast<uint32_t>(f.size()));
    const float ver = 0.95f;
    std::memcpy(&f[8], &ver, 4);
    put32(f, 0x0C, 1234);
    put32(f, 0x1C, views);
    put32(f, 0x20, count4);
    put32(f, 0x34, payload);
    for (int i = 0; i < views; ++i) {
        const size_t o = 0x38 + static_cast<size_t>(i) * chunk;
        put32(f, o, kGbsViewTag);
        put32(f, o + 4, chunk);
        put32(f, o + 8, 0xFFFFFFFFu);
        const std::string nm = "View" + std::to_string(i);
        std::memcpy(&f[o + 12], nm.data(), nm.size());
    }
    put32(f, f.size() - 4, kGbsFooter);
    return f;
}
static void testGbsSynthetic() {
    std::printf("[gbs synthetic]\n");
    Gbs g;
    std::string err;
    auto f = makeGbs(3, 2);
    CHECK(parseGbs(f.data(), f.size(), g, &err));
    CHECK_EQ(g.views.size(), static_cast<size_t>(3));
    CHECK_EQ(g.views[2].name, std::string("View2"));
    CHECK_EQ(g.sceneId, 1234u);
    CHECK_EQ(g.tailSize, 2u * kGbsTailEntrySize);
    auto z = makeGbs(0);
    CHECK(parseGbs(z.data(), z.size(), g, &err));
    CHECK(g.views.empty());

    auto bad = makeGbs(2);
    bad[0] = 'X';
    CHECK(!parseGbs(bad.data(), bad.size(), g, &err));
    bad = makeGbs(2);
    put32(bad, 4, 99999);
    CHECK(!parseGbs(bad.data(), bad.size(), g, &err));
    bad = makeGbs(2);
    put32(bad, bad.size() - 4, 0);
    CHECK(!parseGbs(bad.data(), bad.size(), g, &err));
    bad = makeGbs(2);
    put32(bad, 0x38, 0x41414141);  // first view tag clobbered
    CHECK(!parseGbs(bad.data(), bad.size(), g, &err));
    bad = makeGbs(2);
    put32(bad, 0x38 + 4, 100000);  // chunk size beyond payload
    CHECK(!parseGbs(bad.data(), bad.size(), g, &err));
    bad = makeGbs(2);
    put32(bad, 0x1C, 3);           // claims 3 views, only 2 exist
    CHECK(!parseGbs(bad.data(), bad.size(), g, &err));
    CHECK(!parseGbs(bad.data(), 10, g, &err));  // truncated
}

// ============================================================ RLE

// ---------------------------------------------------------------------------------------- menu data
static void testMenuData() {
    std::printf("[menu .ini + localisation]\n");
    const char* ini =
        "TopText: Single Player # trailing comment\r\n"
        "\r\n"
        "Option:\r\nType: Advance\r\nText: Arcade Mode \r\nDescription: Arcade Mode Desc\r\nKeyword: ACT_ARCADE\r\n"
        "##Option:\r\n##Type: Advance\r\n"
        "Option:\nDefaultCursorPos\nType: Setting\nText: Difficulty\nDefaultChoice: Normal\nChoice: Hard\nChoice: Nightmare\n"
        "TrialModeDefaultChoice: Easy\nKeyword: OPT_STR_SPDIFFICULTY\n"
        "Option:\nType: Setting\nText: Rounds\nChoice: 1\nDefaultChoice: 3\nChoice: 5\nKeyword: OPT_INT_NUM_ROUNDS\n"
        "Option:\nType: Advance\nText: Online\nDescription: Online: Ranked Desc_STEAM\nP1Lock: true\nKeyword: ACT_ONLINE\n"
        "BlockedPlatforms: SWITCH ANDROID\n"
        "Option:\nType: Setting\nText: Mute\nAllowedPlatforms: STEAM\nChoice: No\nKeyword: OPT_X\n"
        "Option:\nType: Submenu\nText: Voice\nDescription: null\nKeyword: voiceSettings.ini\n"
        "Option:\n";  // an empty trailing block must be dropped
    const MenuDef d = parseMenuIni(ini);
    CHECK_EQ(d.topText, std::string("Single Player"));
    CHECK_EQ(d.options.size(), size_t(6));
    CHECK_EQ(d.options[0].text, std::string("Arcade Mode"));          // trailing space trimmed
    CHECK_EQ(d.options[0].keyword, std::string("ACT_ARCADE"));
    CHECK(d.options[0].type == MenuOptType::Advance);
    CHECK(d.options[0].hasDescription());
    CHECK(d.options[1].defaultCursor);
    CHECK_EQ(d.options[1].choices.size(), size_t(3));                  // DefaultChoice is part of the list...
    CHECK_EQ(d.options[1].choices[0], std::string("Normal"));          // ...at its own position
    CHECK_EQ(d.options[1].defaultChoice, std::string("Normal"));
    CHECK_EQ(d.options[2].choices.size(), size_t(3));
    CHECK_EQ(d.options[2].choices[1], std::string("3"));
    CHECK_EQ(d.options[2].defaultChoice, std::string("3"));
    CHECK_EQ(d.options[3].description, std::string("Online: Ranked Desc_STEAM"));  // value may contain ':'
    CHECK(d.options[3].p1Lock);
    CHECK(!d.options[3].visibleOn("ANDROID"));
    CHECK(d.options[3].visibleOn("STEAM"));
    CHECK(d.options[4].visibleOn("STEAM"));
    CHECK(!d.options[4].visibleOn("ANDROID"));                         // AllowedPlatforms: STEAM only
    CHECK(d.options[5].type == MenuOptType::Submenu);
    CHECK(!d.options[5].hasDescription());                             // "null"
    CHECK(parseMenuIni("").options.empty());
    CHECK(parseMenuIni("garbage without colon\n:::\n").options.empty());

    Localization loc;
    std::string err;
    const std::string json =
        "\xEF\xBB\xBF{\n \"Hello\": \"Hola\",\n \"Esc\": \"\\u00bfEst\\u00e1s seguro?\\n\\\"s\\\"\",\n"
        " \"Nul\": null, \"Num\": 42, \"Obj\": {\"a\": [1, {\"b\": \"}\"}]},\n"
        " \"Emoji\": \"\\ud83d\\ude00\", \"Lone\": \"\\ud83d x\", \"Slash\": \"a\\/b\"\n}";
    CHECK(loc.parseJson(json, &err));
    CHECK_EQ(loc.tr("Hello"), std::string("Hola"));
    CHECK_EQ(loc.tr("Esc"), std::string("\xC2\xBF" "Est\xC3\xA1s seguro?\n\"s\""));
    CHECK_EQ(loc.tr("Nul"), std::string("Nul"));                       // null -> falls back to the key
    CHECK(!loc.has("Num"));
    CHECK_EQ(loc.tr("Emoji"), std::string("\xF0\x9F\x98\x80"));        // surrogate pair -> one code point
    CHECK_EQ(loc.tr("Lone"), std::string("\xEF\xBF\xBD x"));            // lone surrogate -> U+FFFD
    CHECK_EQ(loc.tr("Slash"), std::string("a/b"));
    CHECK_EQ(loc.tr("Missing key"), std::string("Missing key"));
    CHECK_EQ(loc.trOr("Missing key", "fb"), std::string("fb"));
    CHECK(!loc.parseJson("{\"a\": \"unterminated", &err));
    CHECK(!err.empty());
    CHECK(!loc.parseJson("[1,2]", &err));
    CHECK(loc.parseJson("{}"));
    CHECK_EQ(loc.size(), size_t(0));
}

static void testMenuDataReal(const std::vector<std::string>& dirs) {
    std::printf("[menu .ini + localisation on the real files]\n");
    GameData gd;
    for (const std::string& d : dirs) gd.mount(d);
    std::string t;
    if (!gd.vfs().readText("singlePlayer.ini", t)) { skip("singlePlayer.ini not in the mounted data"); return; }
    const MenuDef sp = parseMenuIni(t);
    CHECK_EQ(sp.topText, std::string("Single Player"));
    CHECK(sp.options.size() >= 4);
    std::string json;
    if (!gd.vfs().readText("en_loc.json", json)) { skip("en_loc.json not in the mounted data"); return; }
    Localization en, es;
    CHECK(en.parseJson(json));
    CHECK(en.size() > 9000);
    // every label and description in the menu files the ZDroid menu reads resolves in the English table
    size_t missing = 0, total = 0;
    for (const char* f : {"singlePlayer.ini", "versusSTEAM.ini", "training.ini", "extras.ini", "helpSTEAM.ini", "options.ini"}) {
        if (!gd.vfs().readText(f, t)) continue;
        for (const MenuOption& o : parseMenuIni(t).options) {
            ++total;
            if (!en.has(o.text)) { ++missing; std::fprintf(stderr, "  no en text for '%s' (%s)\n", o.text.c_str(), f); }
        }
    }
    CHECK(total > 20);
    CHECK_EQ(missing, size_t(0));
    if (gd.vfs().readText("es_loc.json", json)) {
        CHECK(es.parseJson(json));
        CHECK_EQ(es.size(), en.size());
        CHECK(es.tr("Single Player") != en.tr("Single Player"));      // really translated
        CHECK(en.has("Press Any Button to Start"));
        CHECK(en.has("Copyright"));
    } else skip("es_loc.json not in the mounted data");
}

static void testGbsSceneReal(const std::vector<std::string>& dirs) {
    std::printf("[gbs scene graph on the real files]\n");
    GameData gd;
    for (const std::string& d : dirs) gd.mount(d);
    std::vector<std::string> files = gd.vfs().listByExt(".gbs");
    if (files.empty()) { skip("no .gbs in the mounted data"); return; }
    std::vector<uint8_t> mainData;
    size_t parsed = 0, nodes = 0;
    std::vector<std::string> seen;
    for (const std::string& f : files) {
        std::string base = f.substr(f.find_last_of("/\\") + 1);
        bool dup = false;
        for (const std::string& s2 : seen) if (s2 == base) dup = true;
        if (dup) continue;
        seen.push_back(base);
        std::vector<uint8_t> data;
        if (!readFile(f, data)) continue;
        GbsScene sc;
        std::string err;
        const bool ok = parseGbsScene(data.data(), data.size(), sc, &err);
        if (!ok) std::fprintf(stderr, "  %s: %s\n", base.c_str(), err.c_str());
        CHECK(ok);
        if (!ok) continue;
        ++parsed;
        for (const GbsSceneView& v : sc.views) for (const GbsNode& n : v.nodes) nodes += countNodes(n);
        if (base == "Main.gbs") mainData = data;
    }
    CHECK(parsed >= 10);
    CHECK(nodes > 1000);
    if (mainData.empty()) { skip("Main.gbs not mounted"); return; }
    GbsScene sc;
    CHECK(parseGbsScene(mainData.data(), mainData.size(), sc));
    CHECK_EQ(sc.fonts.size(), size_t(32));
    CHECK_EQ(sc.textures.size(), size_t(213));
    CHECK_EQ(sc.views.size(), size_t(10));
    const GbsFont* sel = sc.fontByName("SelznickTitleTexOtl");
    const GbsFont* desc = sc.fontByName("Main Menu Desc");
    CHECK(sel != nullptr);
    CHECK(desc != nullptr);
    if (desc) {
        CHECK_EQ(desc->atlasW, 512);
        const GbsGlyph* a = desc->find('A');
        CHECK(a != nullptr);
        if (a) CHECK(a->w > 0 && a->h > 0 && a->advance > 0);
        CHECK(desc->find(0x7FFFFFF) == nullptr);
    }
    const GbsSceneView* mm = sc.view("MainMenu_New");
    CHECK(mm != nullptr);
    if (mm) {
        CHECK_EQ(mm->nodes.size(), size_t(30));
        CHECK_EQ(mm->width, 1280.0f);
        CHECK_EQ(mm->height, 720.0f);
        const GbsNode* under = mm->find("Underglow");
        CHECK(under && under->type == GbsNode::Type::Sprite);
        if (under) CHECK_EQ(sc.textureName(under->texId), std::string("Textures/MainMenu/underglow01.png"));
        const GbsNode* item = mm->find("MenuItem0");
        CHECK(item && item->type == GbsNode::Type::Label);
        if (item && sel) { CHECK_EQ(item->fontId, sel->id); CHECK_EQ(item->hAlign, 2); }
        const GbsNode* rw = mm->find("Right Wing");
        CHECK(rw && rw->flipX && !rw->flipY);
        CHECK(mm->animSize > 0);
    }
    const GbsSceneView* sp = sc.view("Splash_2ND");
    CHECK(sp && sp->find("Skullgirls Text") && sp->find("PressButton"));
    // no crash on damaged input: flip single bytes everywhere in the first 64 KiB and some of the views
    size_t crashes = 0;
    uint32_t rng = 12345;
    for (int i = 0; i < 400; ++i) {
        std::vector<uint8_t> bad = mainData;
        rng = rng * 1664525u + 1013904223u;
        const size_t at = (i < 100 ? 0x20 + (rng >> 8) % 0x40 : 0xCCC5C + (rng >> 8) % 0x2000);
        bad[at % bad.size()] ^= static_cast<uint8_t>(1u << (rng & 7));
        GbsScene t;
        (void)parseGbsScene(bad.data(), bad.size(), t);
        ++crashes;  // reaching this line is the check (ASan/UBSan builds turn a bad read into a failure)
    }
    CHECK_EQ(crashes, size_t(400));
}

// ---- LAGG animations: synthetic channel maths + the real files
static GbsKey mkKey(int t, float x, uint32_t argb, int a = 0, int b = 0) {
    GbsKey k;
    k.t = t;
    k.kind = 'S';
    k.x = x;
    k.w = 100;
    k.h = 50;
    k.argb = argb;
    k.modeA = a;
    k.modeB = b;
    return k;
}

static void testGbsAnimSynthetic() {
    std::printf("[gbs animations: channel sampling + player]\n");
    GbsChannel ch;
    ch.nodeId = 7;
    ch.keys = {mkKey(0, 0, 0xFFFFFFFFu), mkKey(10, 100, 0x00FFFFFFu), mkKey(20, 100, 0x00FFFFFFu)};
    GbsPose p = sampleChannel(ch, 0);
    CHECK_EQ(p.x, 0.0f);
    CHECK_EQ(p.argb, 0xFFFFFFFFu);
    p = sampleChannel(ch, 5);
    CHECK_EQ(p.x, 50.0f);
    CHECK_EQ(p.argb >> 24, 128u);  // 255 -> 0 halfway (rounded)
    CHECK_EQ(p.argb & 0xFFFFFFu, 0xFFFFFFu);
    p = sampleChannel(ch, 10);
    CHECK_EQ(p.x, 100.0f);
    CHECK_EQ(p.argb >> 24, 0u);
    p = sampleChannel(ch, 99);  // after the last key the last pose holds
    CHECK_EQ(p.x, 100.0f);
    p = sampleChannel(ch, -3);  // before the first key the first pose holds
    CHECK_EQ(p.x, 0.0f);
    // the easing belongs to the key a segment starts on: out lands softly (ahead of the line), in starts slowly (behind it)
    GbsChannel e;
    e.keys = {mkKey(0, 0, 0xFFFFFFFFu, 2, 50), mkKey(10, 100, 0xFFFFFFFFu)};
    CHECK(sampleChannel(e, 5).x > 50.0f);
    CHECK_EQ(sampleChannel(e, 0).x, 0.0f);
    CHECK_EQ(sampleChannel(e, 10).x, 100.0f);
    e.keys[0].modeA = 1;
    CHECK(sampleChannel(e, 5).x < 50.0f);
    e.keys[0].modeA = 3;
    CHECK(sampleChannel(e, 2).x < 20.0f);          // in-out: slow at both ends ...
    CHECK(sampleChannel(e, 8).x > 80.0f);
    CHECK(std::fabs(sampleChannel(e, 5).x - 50.0f) < 0.2f);  // ... and through the middle
    e.keys[0].modeA = 0;                           // no type = straight line whatever the amount says
    e.keys[0].modeB = 100;
    CHECK_EQ(sampleChannel(e, 5).x, 50.0f);
    e.keys[0].modeA = 2;                           // an easing on the key a segment ENDS on does nothing
    e.keys[0].modeB = 0;
    e.keys[1].modeA = 2;
    e.keys[1].modeB = 100;
    CHECK_EQ(sampleChannel(e, 5).x, 50.0f);
    CHECK_EQ(easeSegment(0, 0, 0.25f), 0.25f);
    CHECK_EQ(easeSegment(1, 0, 0.25f), 0.25f);
    CHECK_EQ(easeSegment(2, 100, 0.5f), 1.0f - std::pow(0.5f, 5.0f));  // amount 100 = exponent 5
    CHECK_EQ(easeSegment(1, 25, 0.5f), std::pow(0.5f, 2.0f));          // amount 25 = exponent 2
    // keys at / past the animation length are not reached: the segment before them stays unfinished
    GbsChannel late;
    late.keys = {mkKey(0, 0, 0xFFFFFFFFu), mkKey(4, 40, 0xFFFFFFFFu), mkKey(99, 500, 0xFFFFFFFFu)};
    CHECK_EQ(sampleChannel(late, 4, 6).x, 40.0f);
    CHECK_EQ(sampleChannel(late, 5, 6).x, 40.0f);  // would have been on its way to 500
    CHECK(sampleChannel(late, 5).x > 40.0f);       // (without a length every key counts)

    // a view with a looping blink (len 6, loop 2..5), a one-shot move (len 4) and a sound cue
    GbsSceneView v;
    GbsAnim blink;
    blink.name = "Blink";
    blink.length = 6;
    blink.loopStart = 2;
    blink.loopEnd = 5;
    GbsChannel c1;
    c1.nodeId = 1;
    c1.keys = {mkKey(0, 0, 0x00FFFFFFu), mkKey(2, 0, 0xFFFFFFFFu), mkKey(5, 0, 0x00FFFFFFu)};
    blink.channels.push_back(c1);
    blink.sounds.push_back(GbsSoundCue{0, 3, 1.0f});
    GbsAnim move;
    move.name = "Move";
    move.length = 4;
    GbsChannel c2;
    c2.nodeId = 1;
    c2.keys = {mkKey(0, 0, 0xFFFFFFFFu), mkKey(3, 30, 0xFFFFFFFFu)};
    move.channels.push_back(c2);
    v.anims = {blink, move};
    GbsAnimPlayer pl;
    pl.bind(&v);
    CHECK(pl.known("Blink") && !pl.known("Nope"));
    CHECK(!pl.play("Nope"));
    CHECK(pl.pose(1) == nullptr);
    CHECK(pl.play("Blink"));
    CHECK(pl.pose(1) != nullptr);
    CHECK_EQ(pl.tickOf("Blink"), 0);
    CHECK_EQ(pl.takeCues().size(), size_t(1));  // the cue at tick 0 fires when it starts
    for (int i = 0; i < 5; ++i) pl.tick();
    CHECK_EQ(pl.tickOf("Blink"), 5);
    pl.tick();                                   // 6 > loopEnd -> back to the loop start
    CHECK_EQ(pl.tickOf("Blink"), 2);
    CHECK(pl.running("Blink") && !pl.finished("Blink"));
    for (int i = 0; i < 100; ++i) pl.tick();
    CHECK(pl.running("Blink"));                  // loops until stopped
    // the later animation wins the node; when it ends its last pose holds
    pl.play("Move");
    for (int i = 0; i < 10; ++i) pl.tick();
    CHECK(pl.finished("Move"));
    CHECK_EQ(pl.pose(1)->x, 30.0f);
    pl.stop("Move");                             // the blink gets the node back
    CHECK_EQ(pl.pose(1)->x, 0.0f);
    pl.clear();
    CHECK(pl.pose(1) == nullptr);
    CHECK_EQ(pl.tickOf("Blink"), -1);
    // determinism: the same calls give the same poses
    GbsAnimPlayer a, b;
    a.bind(&v);
    b.bind(&v);
    a.play("Blink");
    b.play("Blink");
    for (int i = 0; i < 37; ++i) { a.tick(); b.tick(); }
    CHECK_EQ(a.pose(1)->argb, b.pose(1)->argb);
    CHECK_EQ(a.tickOf("Blink"), b.tickOf("Blink"));
}

// ---- label text layout (gbs_text): synthetic font, exact numbers
static GbsFont mkTextFont() {
    GbsFont f;
    f.name = "T";
    f.lineHeight = 30;  // font size
    f.baseline = 24;    // max top
    auto add = [&](uint32_t cp, int w, int h, int top, int adv, int bx) {
        GbsGlyph g;
        g.cp = cp; g.w = w; g.h = h; g.bearingY = top; g.advance = adv; g.bearingX = bx;
        f.glyphs.push_back(g);
    };
    add('.', 4, 4, 4, 6, 1);
    add('?', 10, 20, 22, 12, 1);
    add('A', 10, 20, 22, 12, 1);
    add('B', 10, 20, 22, 12, 1);
    std::sort(f.glyphs.begin(), f.glyphs.end(), [](const GbsGlyph& a, const GbsGlyph& b) { return a.cp < b.cp; });
    return f;
}

// ---- arc nodes ("CCRA"): the band's geometry, the key layout and the animation-as-value trick the HUD uses
static bool near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) <= eps; }

static void testGbsArc() {
    std::printf("[gbs arc nodes: geometry + seek]\n");
    GbsArcStyle s;
    s.segments = 20;
    s.radius = 80;
    s.thickness = 18;
    s.sweep = 90;
    s.start = 0;
    s.clockwise = true;
    // clockwise from 3 o'clock on a y-down screen: the start is east, a quarter turn later it is south
    GbsArcPoint p = gbsArcPoint(s, 0.0f, 10);
    CHECK(near(p.x, 10) && near(p.y, 0));
    p = gbsArcPoint(s, 1.0f, 10);
    CHECK(near(p.x, 0) && near(p.y, 10));
    // the other direction starts at the opposite end of the circle (west) and runs counter-clockwise on the screen: west -> south
    s.clockwise = false;
    p = gbsArcPoint(s, 0.0f, 10);
    CHECK(near(p.x, -10) && near(p.y, 0));
    p = gbsArcPoint(s, 1.0f, 10);
    CHECK(near(p.x, 0) && near(p.y, 10));
    // the two directions of the HUD's P1 pair cover the same stretch: groove (start 44, clockwise) = full ring (start 19, the other way, 155)
    GbsArcStyle g;
    g.segments = 20; g.radius = 80; g.thickness = 18; g.sweep = 155; g.start = 44; g.clockwise = true;
    GbsArcStyle b = g;
    b.start = 19; b.clockwise = false;
    const GbsArcPoint g0 = gbsArcPoint(g, 0.0f, 80), g1 = gbsArcPoint(g, 1.0f, 80);
    const GbsArcPoint b0 = gbsArcPoint(b, 0.0f, 80), b1 = gbsArcPoint(b, 1.0f, 80);
    CHECK(near(g0.x, b1.x, 0.5f) && near(g0.y, b1.y, 0.5f));   // the groove begins where the ring ends ...
    CHECK(near(g1.x, b0.x, 0.5f) && near(g1.y, b0.y, 0.5f));   // ... and ends where the ring begins
    // the band: 6 points per segment, all on the inner or the outer circle
    const std::vector<GbsArcPoint> tris = gbsArcTriangles(g);
    CHECK_EQ(tris.size(), static_cast<size_t>(20 * 6));
    for (const GbsArcPoint& q : tris) {
        const float d = std::sqrt(q.x * q.x + q.y * q.y);
        CHECK(near(d, 89, 0.05f) || near(d, 71, 0.05f));
    }
    // nothing to draw: no sweep, no thickness, fewer than 2 segments
    GbsArcStyle z = g;
    z.sweep = 0;
    CHECK(gbsArcTriangles(z).empty());
    z = g; z.thickness = 0;
    CHECK(gbsArcTriangles(z).empty());
    z = g; z.segments = 1;
    CHECK(gbsArcTriangles(z).empty());

    // seek: the animation's time is the value (a ring animation runs from the full sweep at tick 0 to none at its last tick)
    auto ringKey = [](int t, float sweep) {
        GbsKey k;
        k.t = t;
        k.kind = 'R';
        k.arcRadius = 80; k.arcThickness = 18; k.arcSweep = sweep; k.arcStart = 19;
        k.argb = 0xFF00A016u;
        return k;
    };
    GbsSceneView v;
    GbsAnim ring;
    ring.name = "Ring";
    ring.length = 1000;
    GbsChannel ch;
    ch.nodeId = 9;
    ch.keys = {ringKey(0, 155), ringKey(999, 0)};
    ring.channels.push_back(ch);
    GbsAnim other;
    other.name = "Other";
    other.length = 10;
    GbsChannel och;
    och.nodeId = 9;
    och.keys = {ringKey(0, 77)};
    other.channels.push_back(och);
    v.anims = {ring, other};
    GbsAnimPlayer pl;
    pl.bind(&v);
    CHECK(!pl.seek("Missing", 5));
    CHECK(pl.seek("Ring", 1));
    const GbsPose* ps = pl.pose(9);
    CHECK(ps != nullptr);
    if (ps) {
        CHECK_EQ(ps->kind, 'R');
        CHECK(near(ps->arcSweep, 155.0f * (1.0f - 1.0f / 999.0f), 0.01f));
        CHECK_EQ(ps->arcStart, 19.0f);
        CHECK_EQ(ps->argb, 0xFF00A016u);
    }
    for (int i = 0; i < 30; ++i) pl.tick();   // a sought animation stays where it was put
    CHECK_EQ(pl.tickOf("Ring"), 1);
    CHECK(pl.seek("Ring", 1000));              // past the end: the last tick (nothing left of the sweep)
    CHECK_EQ(pl.tickOf("Ring"), 999);
    ps = pl.pose(9);
    CHECK(ps && near(ps->arcSweep, 0.0f));
    // a node two animations share goes to the one that started later; seeking again does not change that order
    CHECK(pl.play("Other"));
    CHECK(near(pl.pose(9)->arcSweep, 77.0f));
    CHECK(pl.seek("Ring", 500));
    CHECK(near(pl.pose(9)->arcSweep, 77.0f));
}

static void testGbsArcReal(const std::vector<std::string>& dirs) {
    std::printf("[gbs arc nodes on the real InGame.gbs]\n");
    GameData gd;
    for (const std::string& d : dirs) gd.mount(d);
    std::vector<uint8_t> data;
    for (const std::string& f : gd.vfs().listByExt(".gbs"))
        if (f.substr(f.find_last_of("/\\") + 1) == "InGame.gbs" && readFile(f, data)) break;
    if (data.empty()) { skip("InGame.gbs not mounted"); return; }
    GbsScene sc;
    CHECK(parseGbsScene(data.data(), data.size(), sc));
    size_t arcs = 0;
    struct W { static void go(const GbsNode& n, size_t& c) { if (n.type == GbsNode::Type::Arc) ++c; for (const GbsNode& k : n.kids) go(k, c); } };
    for (const GbsSceneView& v : sc.views) for (const GbsNode& n : v.nodes) W::go(n, arcs);
    CHECK_EQ(arcs, static_cast<size_t>(28));   // 2 players x (groove + 5 stocks + flash), twice
    const GbsSceneView* hud = sc.view("HUD");
    CHECK(hud != nullptr);
    if (!hud) return;
    const GbsNode* g1 = hud->find("P1_SuperBar_0");
    const GbsNode* b1 = hud->find("P1_SuperBar_1");
    const GbsNode* g2 = hud->find("P2_SuperBar_0");
    const GbsNode* b2 = hud->find("P2_SuperBar_1");
    CHECK(g1 && b1 && g2 && b2);
    if (!(g1 && b1 && g2 && b2)) return;
    CHECK(g1->type == GbsNode::Type::Arc);
    CHECK_EQ(g1->arcSegments, 20);
    CHECK_EQ(g1->arcRadius, 80.0f);
    CHECK_EQ(g1->arcThickness, 18.0f);
    CHECK_EQ(g1->arcSweep, 155.0f);
    CHECK_EQ(g1->arcStart, 44.0f);
    CHECK_EQ(g1->colour, 0xFF000000u);
    CHECK(g1->arcClockwise);
    CHECK(!b1->arcClockwise);                  // the rings run against the groove ...
    CHECK(g2->arcStart == 316.0f && !g2->arcClockwise && b2->arcClockwise);   // ... and player 2 is the mirror image
    CHECK_EQ(b1->colour, 0xFF008000u);
    // the stock-1 ring's own animation: full sweep at tick 0, none at tick 999, brighter green than at rest
    const GbsAnim* an = hud->anim("P1_SuperBar1_Anim");
    CHECK(an != nullptr);
    if (an) {
        CHECK_EQ(an->length, 1000);
        const GbsChannel* ch = nullptr;
        for (const GbsChannel& c : an->channels) if (c.nodeId == b1->id) ch = &c;
        CHECK(ch != nullptr);
        if (ch) {
            const GbsPose mid = sampleChannel(*ch, 500, an->length);
            CHECK_EQ(mid.kind, 'R');
            CHECK(near(mid.arcSweep, 155.0f * (1.0f - 500.0f / 999.0f), 0.2f));
            CHECK_EQ(mid.arcStart, 19.0f);
            CHECK_EQ(mid.argb, 0xFF00A016u);
            CHECK_EQ(sampleChannel(*ch, 999, an->length).arcSweep, 0.0f);
        }
    }
}

static void testGbsTextLayout() {
    std::printf("[gbs text layout: leading, tracking, fit modes]\n");
    const GbsFont f = mkTextFont();
    GbsTextStyle st;
    GbsTextBlock b = layoutGbsText(f, "AB", 500, 200, 1, 1, st);
    CHECK_EQ(b.lines.size(), size_t(1));
    CHECK_EQ(b.lines[0].quads.size(), size_t(2));
    CHECK_EQ(b.lines[0].quads[0].left, 1);        // pen + left bearing
    CHECK_EQ(b.lines[0].quads[0].top, 1);         // baseline 24 - (top 22 + 1)
    CHECK_EQ(b.lines[0].quads[0].right, 11);
    CHECK_EQ(b.lines[0].quads[0].bottom, 21);
    CHECK_EQ(b.lines[0].quads[1].left, 13);       // + advance 12
    CHECK_EQ(b.lines[0].width(), 23);
    st.tracking = -1;                             // tracking is added after every glyph
    b = layoutGbsText(f, "AB", 500, 200, 1, 1, st);
    CHECK_EQ(b.lines[0].quads[1].left, 12);
    st.tracking = 0;
    b = layoutGbsText(f, "A B", 500, 200, 1, 1, st);   // a space = size / 3
    CHECK_EQ(b.lines[0].quads[1].left, 12 + 10 + 1);
    st.tracking = 2;
    b = layoutGbsText(f, "A B", 500, 200, 1, 1, st);   // the tracking also widens the gap
    CHECK_EQ(b.lines[0].quads[1].left, 14 + 12 + 1);
    st.tracking = 0;
    b = layoutGbsText(f, "Z", 500, 200, 1, 1, st);     // a glyph the font lacks shows as '?'
    CHECK_EQ(b.lines[0].quads[0].glyph->cp, uint32_t('?'));
    CHECK(layoutGbsText(f, "", 500, 200, 1, 1, st).empty());

    // line feeds and leading: the next line starts size + (int)(size * leading) lower
    st.leading = 0.15f;
    b = layoutGbsText(f, "A\nB", 500, 200, 1, 1, st);
    CHECK_EQ(b.lines.size(), size_t(2));
    CHECK_EQ(b.lines[1].offsetY, 34);
    CHECK_EQ(b.lines[1].quads[0].top, 34 + 1);
    CHECK_EQ(static_cast<int>(b.textH), 34 + 30);
    // a line feed that would put the next line below the box ends the text
    b = layoutGbsText(f, "A\nB", 500, 50, 1, 1, st);
    CHECK_EQ(b.lines.size(), size_t(1));
    st.leading = 0;

    // wrapping: "AB" is 24 wide, a space 10, so the second word (34 .. 58) does not fit a box of 40
    b = layoutGbsText(f, "AB AB", 40, 200, 1, 1, st);
    CHECK_EQ(b.lines.size(), size_t(2));
    CHECK_EQ(b.lines[1].offsetY, 30);
    CHECK_EQ(b.lines[1].quads[0].left, 1);
    b = layoutGbsText(f, "AB AB", 40, 40, 1, 1, st);   // a box one line high drops what would wrap below it
    CHECK_EQ(b.lines.size(), size_t(1));
    st.fit = kFitNoWrap;
    b = layoutGbsText(f, "AB AB", 40, 200, 1, 1, st);
    CHECK_EQ(b.lines.size(), size_t(1));
    CHECK_EQ(b.lines[0].quads.size(), size_t(4));

    // scale down: only when too wide, and then to the box exactly (lines start at the left edge)
    st.fit = kFitScaleDown;
    b = layoutGbsText(f, "AB", 100, 200, 1, 1, st);
    CHECK_EQ(b.lines[0].quads[1].left, 13);
    b = layoutGbsText(f, "AB", 20, 200, 1, 1, st);
    CHECK_EQ(b.lines[0].quads[0].left, 0);
    CHECK_EQ(b.lines[0].quads[1].left, 10);
    CHECK(b.lines[0].quads[1].right <= 20);
    st.fit = kFitScaleToFit;
    b = layoutGbsText(f, "AB", 46, 200, 1, 1, st);     // 23 wide in a box of 46: stretched
    CHECK(b.lines[0].quads[1].right >= 42 && b.lines[0].quads[1].right <= 46);
    // the block scale divides the box: at 0.5 the same text only needs a 12 px box
    st.fit = kFitWrap;
    b = layoutGbsText(f, "AB AB", 40, 200, 0.5f, 0.5f, st);
    CHECK_EQ(b.lines.size(), size_t(1));

    // truncate: one line that is too wide ends in three dots and fits the box
    st.fit = kFitTruncate;
    b = layoutGbsText(f, "ABABABAB", 60, 200, 1, 1, st);
    CHECK_EQ(b.lines.size(), size_t(1));
    const std::vector<GbsTextQuad>& q = b.lines[0].quads;
    CHECK(q.size() >= 4);
    CHECK(q[q.size() - 1].glyph->cp == uint32_t('.') && q[q.size() - 3].glyph->cp == uint32_t('.'));
    CHECK(q.back().right <= 60);
    b = layoutGbsText(f, "AB", 60, 200, 1, 1, st);     // fits: untouched
    CHECK_EQ(b.lines[0].quads.size(), size_t(2));
    st.fit = kFitWrap;

    // alignment of a line inside the box
    b = layoutGbsText(f, "AB", 100, 100, 1, 1, st);
    float dx = 0, dy = 0;
    gbsLineOffset(b, 0, 1, 1, dx, dy);                 // centred both ways
    CHECK_EQ(dx, (100.0f - 23.0f) / 2);
    CHECK_EQ(dy, (100.0f - 30.0f) / 2);
    gbsLineOffset(b, 0, 2, 2, dx, dy);
    CHECK_EQ(dx, 100.0f - 23.0f);
    CHECK_EQ(dy, 100.0f - 30.0f);
    gbsLineOffset(b, 0, 0, 0, dx, dy);
    CHECK_EQ(dx, 0.0f);
    CHECK_EQ(dy, 0.0f);
}

// MenuListAnim: which animation plays when (synthetic view: only the names and lengths matter here)
static void testMenuListAnimSynthetic() {
    std::printf("[main menu list animations]\n");
    CHECK_EQ(MenuListAnim::itemOf(0, 10), 6);
    CHECK_EQ(MenuListAnim::itemOf(3, 10), 9);
    CHECK_EQ(MenuListAnim::itemOf(4, 10), 10);    // the centre label carries the entry that was highlighted
    CHECK_EQ(MenuListAnim::itemOf(5, 10), 11);
    CHECK_EQ(MenuListAnim::itemOf(9, 10), 15);
    CHECK_EQ(MenuListAnim::itemOf(-1, 10), 5);    // MenuItemOffTop
    GbsSceneView v;
    auto add = [&](const char* name, int len) { GbsAnim a; a.name = name; a.length = len; v.anims.push_back(a); };
    for (const char* n : {"OnActivate", "MenuReset2", "SubMenuReset", "News_Frame_Anim_On"}) add(n, 1);
    add("MenuIntro", 4);
    add("MenuFilmUp", 3);
    add("MenuFilmDown", 3);
    add("SubMenuIntro", 3);
    add("SubMenuOutro", 2);
    MenuListAnim m;
    m.bind(&v);
    CHECK(m.mode() == MenuListAnim::Mode::Off);
    std::vector<char> usable(8, 1);
    usable[3] = 0;   // entry 3 is locked
    m.activate(4, usable);
    CHECK(m.introRunning() && !m.moving());
    // the names of a move follow the tint letters of the entries the animations touch
    {
        const std::vector<std::string> up = m.moveAnims(+1, 2);   // highlight 2 -> 3 (locked): the entry that becomes the header art is dark
        CHECK_EQ(up.size(), size_t(5));
        CHECK(up[0] == "MenuFilmUp");
        CHECK(up[1] == "Menu0UpU");                // entry -2 does not exist: counts as usable
        CHECK(up[2] == "CenterSwapLocked");
        CHECK(up[3] == "Menu4UpLU");               // first letter: entry 3 (MenuItem5), second: entry 2 (MenuItem4)
        CHECK(up[4] == "MenuUp8UU");
        const std::vector<std::string> dn = m.moveAnims(-1, 4);   // highlight 4 -> 3
        CHECK(dn[0] == "MenuFilmDown");
        CHECK(dn[1] == "MenuDown0UU");
        CHECK(dn[2] == "CenterSwapLocked");
        CHECK(dn[3] == "Menu4DUL");                // first letter: entry 4 (MenuItem4), second: entry 3 (MenuItem3)
        CHECK(dn[4] == "Menu8DU");
        const std::vector<std::string> dn2 = m.moveAnims(-1, 8);  // highlight 8 -> 7: entry 4+4=... entries past the end are usable
        CHECK(dn2[2] == "MenuCenterSwap");
    }
    // the intro ends by itself, then the list takes moves one step at a time
    for (int i = 0; i < 2; ++i) m.tick();
    CHECK(m.introRunning());
    m.tick();                                      // the last tick of a 4-tick animation
    CHECK(m.mode() == MenuListAnim::Mode::List);
    m.setTarget(5, usable);
    m.tick();
    CHECK(m.moving() && m.moveDir() == 1 && m.listSel() == 4);   // the labels still show the old window while it plays
    for (int i = 0; i < 4; ++i) m.tick();
    CHECK(!m.moving() && m.listSel() == 5);
    // a far jump does not scroll through every entry
    m.setTarget(0, usable);
    m.tick();
    CHECK(!m.moving() && m.listSel() == 0);
    // a page over the list: the band slides, and back
    m.enterSub();
    CHECK(m.mode() == MenuListAnim::Mode::Sub && m.player().running("SubMenuIntro"));
    m.leaveSub();
    CHECK(m.player().running("SubMenuOutro"));
    m.setTarget(1, usable);
    m.tick();
    CHECK(!m.moving());                                          // no move while the band is still coming back
    for (int i = 0; i < 3; ++i) m.tick();
    m.tick();
    CHECK(m.moving() || m.listSel() == 1);
}

// The animations the director picks all exist in the real MainMenu_New view (names only, from the user's own Main.gbs)
static void testMenuListAnimReal(const std::vector<std::string>& dirs) {
    std::printf("[main menu list animations on the real file]\n");
    GameData gd;
    for (const std::string& d : dirs) gd.mount(d);
    std::vector<std::string> files = gd.vfs().listByExt(".gbs");
    std::vector<uint8_t> data;
    bool found = false;
    for (const std::string& f : files)
        if (f.size() >= 8 && f.compare(f.size() - 8, 8, "Main.gbs") == 0 && readFile(f, data)) { found = true; break; }
    if (!found) { skip("no Main.gbs in the mounted data"); return; }
    GbsScene sc;
    CHECK(parseGbsScene(data.data(), data.size(), sc, nullptr));
    const GbsSceneView* mv = sc.view("MainMenu_New");
    CHECK(mv != nullptr);
    if (!mv) return;
    for (const char* n : {"OnActivate", "MenuIntro", "MenuReset2", "SubMenuIntro", "SubMenuOutro", "SubMenuReset", "News_Frame_Anim_On",
                          "MenuFilmUp", "MenuFilmDown", "MenuCenterSwap", "CenterSwapLocked"})
        CHECK(mv->anim(n) != nullptr);
    for (int slot = 0; slot <= 9; ++slot) {
        if (slot == 4) continue;
        CHECK(mv->anim("Menu" + std::to_string(slot)) != nullptr);
        CHECK(mv->anim("Menu" + std::to_string(slot) + "Locked") != nullptr);
    }
    MenuListAnim m;
    m.bind(mv);
    // every combination of locked / usable entries around the highlight names an animation the view has
    for (int mask = 0; mask < 64; ++mask) {
        std::vector<char> u(12, 1);
        for (int b = 0; b < 6; ++b) u[static_cast<size_t>(3 + b)] = (mask >> b) & 1;
        m.activate(6, u);
        for (int dir : {-1, 1})
            for (const std::string& n : m.moveAnims(dir, 6)) CHECK(mv->anim(n) != nullptr);
    }
    // it runs: intro, one step down and back up with the real data
    std::vector<char> u(9, 1);
    m.activate(2, u);
    for (int i = 0; i < 71; ++i) m.tick();
    CHECK(m.mode() == MenuListAnim::Mode::List);
    m.setTarget(3, u);
    m.tick();
    CHECK(m.moving());
    for (int i = 0; i < 8; ++i) m.tick();
    CHECK(!m.moving() && m.listSel() == 3);
    const GbsNode* grp = mv->find("MenuGRP");
    CHECK(grp != nullptr);
    if (grp) {
        const GbsPose* ps = m.player().pose(grp->id);
        CHECK(ps != nullptr && std::fabs(ps->y - grp->y) < 0.51f);   // MenuReset2 put the group back where it rests
    }
}

static void testGbsAnimReal(const std::vector<std::string>& dirs) {
    std::printf("[gbs animations on the real files]\n");
    GameData gd;
    for (const std::string& d : dirs) gd.mount(d);
    std::vector<std::string> files = gd.vfs().listByExt(".gbs");
    if (files.empty()) { skip("no .gbs in the mounted data"); return; }
    size_t anims = 0, keys = 0, cues = 0, files_ok = 0;
    std::vector<std::string> seen;
    std::vector<uint8_t> mainData;
    for (const std::string& f : files) {
        const std::string base = f.substr(f.find_last_of("/\\") + 1);
        bool dup = false;
        for (const std::string& s2 : seen) if (s2 == base) dup = true;
        if (dup) continue;
        seen.push_back(base);
        std::vector<uint8_t> data;
        if (!readFile(f, data)) continue;
        GbsScene sc;
        std::string err;
        if (!parseGbsScene(data.data(), data.size(), sc, &err)) { std::fprintf(stderr, "  %s: %s\n", base.c_str(), err.c_str()); CHECK(false); continue; }
        ++files_ok;
        if (base == "Main.gbs") mainData = data;
        for (const GbsSceneView& v : sc.views) {
            // every channel names a node of the view (or a node this view animates but nests deeper)
            std::vector<uint32_t> ids;
            struct W { static void go(const GbsNode& n, std::vector<uint32_t>& o) { o.push_back(n.id); for (const GbsNode& k : n.kids) go(k, o); } };
            for (const GbsNode& n : v.nodes) W::go(n, ids);
            for (const GbsAnim& a : v.anims) {
                ++anims;
                CHECK(a.length >= 1);
                CHECK(!a.loops() || a.loopStart <= a.loopEnd);  // (one cutscene loop runs past its length: the player clamps it)
                for (const GbsChannel& ch : a.channels) {
                    keys += ch.keys.size();
                    bool known = false;
                    for (uint32_t id : ids) known |= id == ch.nodeId;
                    CHECK(known);
                    for (size_t i = 1; i < ch.keys.size(); ++i) CHECK(ch.keys[i - 1].t <= ch.keys[i].t);
                }
                for (const GbsSoundCue& c : a.sounds) { ++cues; CHECK(!sc.soundName(c.soundId).empty()); }
            }
        }
    }
    CHECK(files_ok >= 10);
    CHECK(anims > 200);
    CHECK(keys > 10000);
    if (mainData.empty()) { skip("Main.gbs not mounted"); return; }
    GbsScene sc;
    CHECK(parseGbsScene(mainData.data(), mainData.size(), sc));
    CHECK(sc.sounds.size() >= 10);
    CHECK_EQ(sc.soundName(0xE8040001u), std::string("UISound/menu.select.wav"));
    const GbsSceneView* sp = sc.view("Splash_2ND");
    CHECK(sp != nullptr);
    if (sp) {
        const GbsAnim* on = sp->anim("OnActivate");
        CHECK(on != nullptr);
        if (on) {
            CHECK_EQ(on->length, 79);
            CHECK_EQ(on->loopStart, 39);
            CHECK_EQ(on->loopEnd, 78);
            CHECK_EQ(on->channels.size(), size_t(34));
        }
        // the shield flip-book: one of the ten frames is lit at every tick of the loop (2 ticks each, 20 ticks a turn)
        GbsAnimPlayer pl;
        pl.bind(sp);
        CHECK(pl.play("OnActivate"));
        for (int t = 0; t < 200; ++t) {
            int lit = 0;
            for (int i = 1; i <= 10; ++i) {
                char nm[32];
                std::snprintf(nm, sizeof nm, "Top_Shield_Frm%02d", i);
                const GbsNode* n = sp->find(nm);
                const GbsPose* p = n ? pl.pose(n->id) : nullptr;
                if (p && p->alpha() > 0.5f) ++lit;
            }
            if (t >= 2) CHECK_EQ(lit, 1);
            pl.tick();
        }
        CHECK(pl.running("OnActivate"));
    }
    const GbsSceneView* mm = sc.view("MainMenu_New");
    if (mm) {
        const GbsAnim* intro = mm->anim("MenuIntro");
        CHECK(intro && intro->length == 70 && intro->sounds.size() == 8);
        if (intro && intro->sounds.size() == 8) { CHECK_EQ(intro->sounds[0].t, 0); CHECK_EQ(intro->sounds[1].t, 8); }
        const GbsAnim* down = mm->anim("MenuDown");
        CHECK(down && down->length == 6);
    }
}

static void testRle() {
    std::printf("[rle]\n");
    std::vector<uint8_t> out;
    auto run = [&](std::vector<uint8_t> s, size_t limit) { return rleDecode(s.data(), s.size(), 0, s.size(), limit, out); };
    CHECK(run({0x02, 'a', 'b', 'c'}, 10));                         // literals
    CHECK((out == std::vector<uint8_t>{'a', 'b', 'c'}));
    CHECK(run({0x84, 'x'}, 10));                                   // repeat 5
    CHECK_EQ(out.size(), static_cast<size_t>(5));
    CHECK(run({0xFC, 'y'}, 200));                                  // max short repeat 125
    CHECK_EQ(out.size(), static_cast<size_t>(125));
    CHECK(run({0xFD, 200, 'z'}, 300));
    CHECK_EQ(out.size(), static_cast<size_t>(200));
    CHECK(run({0xFE, 0x01, 0x00, 'q'}, 300));                      // u16be 256
    CHECK_EQ(out.size(), static_cast<size_t>(256));
    CHECK(run({0xFF, 0x00, 0x03, 0x00, 'w'}, 1000));               // u24be 768
    CHECK_EQ(out.size(), static_cast<size_t>(768));
    CHECK(run({0x00, 7, 0x80, 8}, 3));                             // 1 literal + 1 repeat, exactly at limit
    CHECK(!run({0x00, 7, 0x80, 8}, 1));                            // over the limit
    CHECK(!run({0x03, 'a', 'b'}, 10));                             // literal runs past the end
    CHECK(!run({0xFD, 5}, 10));                                    // truncated opcode
    std::vector<uint8_t> two = {0x00, 'a', 0x00, 'b'};
    CHECK(!rleDecode(two.data(), two.size(), 0, 3, 10, out));      // stream does not end exactly at `end`
    CHECK(!rleDecode(two.data(), two.size(), 0, 99, 10, out));     // end beyond data
}

// ============================================================ art decoder vs the Python reference
static std::vector<uint8_t> slurp(const std::string& p) {
    std::vector<uint8_t> v;
    readFile(p, v);
    return v;
}

static void testOracle(const std::string& root) {
    std::printf("[art decoder vs sg_extract.py oracle]\n");
    std::ifstream cases(root + "/cases.txt");
    std::string name;
    int totalRecs = 0, totalFrames = 0;
    while (std::getline(cases, name)) {
        if (name.empty()) continue;
        const std::string d = root + "/" + name + "/";
        ArtFile af;
        const bool loaded = af.load(slurp(d + "chr.foit"), slurp(d + "chr.art"), slurp(d + "chr.art-pt"));

        std::ifstream meta(d + "meta.txt");
        std::string tok;
        int tones = 0, bands = 0;
        size_t n = 0;
        double covA = 0, covP = 0;
        struct Rec { int i; std::string src; int tag, w, h, n; unsigned A, B, foitOff, off, size; };
        std::vector<Rec> recs;
        while (meta >> tok) {
            if (tok == "palette") meta >> tones >> bands;
            else if (tok == "records") meta >> n;
            else if (tok == "coverage") meta >> covA >> covP;
            else if (tok == "rec") {
                Rec r;
                meta >> r.i >> r.src >> r.tag >> r.w >> r.h >> r.n >> r.A >> r.B >> r.foitOff >> r.off >> r.size;
                recs.push_back(r);
            }
        }
        CHECK_EQ(loaded, n > 0);
        CHECK_EQ(af.records().size(), n);
        CHECK_EQ(recs.size(), n);
        const double dA = af.coverageArt() - covA, dP = af.coveragePt() - covP;
        CHECK(dA < 1e-9 && dA > -1e-9);
        CHECK(dP < 1e-9 && dP > -1e-9);

        Palette pal;
        std::string perr;
        CHECK(readDds(slurp(d + "pal.dds").data(), slurp(d + "pal.dds").size(), pal, &perr));
        CHECK_EQ(pal.tones, tones);
        CHECK_EQ(pal.bands, bands);
        const std::vector<uint8_t> tables = buildRgbaTables(pal);

        for (size_t i = 0; i < n && i < af.records().size(); ++i) {
            const ArtRecord& r = af.records()[i];
            const Rec& e = recs[i];
            CHECK_EQ(r.src == ArtRecord::Src::Art ? std::string("art") : std::string("art-pt"), e.src);
            CHECK_EQ(static_cast<int>(r.tag), e.tag);
            CHECK_EQ(static_cast<int>(r.w), e.w);
            CHECK_EQ(static_cast<int>(r.h), e.h);
            CHECK_EQ(static_cast<int>(r.n), e.n);
            CHECK_EQ(r.A, e.A);
            CHECK_EQ(r.B, e.B);
            CHECK_EQ(r.foitOff, e.foitOff);
            CHECK_EQ(r.off, e.off);
            CHECK_EQ(r.compressedSize(), e.size);

            ArtPlanes pl;
            CHECK(af.decode(i, pl));
            const std::string fb = d + "frame_" + std::to_string(i);
            CHECK(pl.band == slurp(fb + ".band"));
            CHECK(pl.grad == slurp(fb + ".grad"));
            CHECK(toRgba(pl, tables) == slurp(fb + ".rgba"));
            ++totalFrames;
        }
        totalRecs += static_cast<int>(n);
    }
    std::printf("  compared %d records / %d frames byte-for-byte\n", totalRecs, totalFrames);
    CHECK(totalFrames > 100);

    // the preview palette used when no .dds exists
    const std::vector<uint8_t> fb = slurp(root + "/fallback.bin");
    const Palette fp = fallbackPalette();
    CHECK(!fb.empty());
    CHECK(fp.rgb == fb);

    // PNG writer: structurally valid, round-trips through the Python check run by the driver script
    // 200x150 RGBA = 120000 bytes of pixel data (> 65535, so the stored-deflate path needs several blocks)
    std::vector<uint8_t> px(200 * 150 * 4);
    for (int y = 0; y < 150; ++y)
        for (int x = 0; x < 200; ++x)
            for (int c = 0; c < 4; ++c) px[(static_cast<size_t>(y) * 200 + x) * 4 + c] = static_cast<uint8_t>(x * 3 + y * 5 + c * 11);
    auto png = encodePngRgba(px.data(), 200, 150);
    CHECK(png.size() > 120000);
    CHECK(png[1] == 'P' && png[2] == 'N' && png[3] == 'G');
    if (const char* outp = std::getenv("ZDROID_PNG_OUT")) {
        std::ofstream(outp, std::ios::binary).write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    }
}

// ============================================================ real game data
static void testRealData(const std::vector<std::string>& dirs) {
    std::printf("[real game data]\n");
    GameData gd;
    for (const std::string& d : dirs) CHECK(gd.mount(d) > 0);
    Inventory inv = gd.scan();
    std::printf("  files %zu | scenes %zu | stages %zu | tracks %zu | toc %zu\n", inv.files, inv.scenes.size(),
                inv.stages.size(), inv.music.size(), inv.tocFiles.size());
    CHECK_EQ(inv.scenes.size(), static_cast<size_t>(23));
    size_t views = 0;
    for (const SceneInfo& s : inv.scenes) {
        CHECK(s.error.empty());
        if (!s.error.empty()) std::fprintf(stderr, "    %s: %s\n", s.path.c_str(), s.error.c_str());
        views += s.gbs.views.size();
        CHECK_EQ(s.gbs.textureNames.size(), static_cast<size_t>(s.gbs.textureCount));
        CHECK(s.gbs.formatVersion > 0.94f && s.gbs.formatVersion < 0.96f);
    }
    CHECK_EQ(views, static_cast<size_t>(151));
    auto find = [&](const char* n) -> const SceneInfo* {
        for (const SceneInfo& s : inv.scenes)
            if (s.path.size() >= std::strlen(n) && s.path.compare(s.path.size() - std::strlen(n), std::strlen(n), n) == 0) return &s;
        return nullptr;
    };
    if (auto* m = find("Main.gbs")) { CHECK_EQ(m->gbs.views.size(), static_cast<size_t>(10)); CHECK_EQ(m->gbs.views[0].name, std::string("MainMenu_New")); }
    if (auto* m = find("InGame.gbs")) { CHECK_EQ(m->gbs.views.size(), static_cast<size_t>(17)); CHECK_EQ(m->gbs.tailSize, 136u); }
    if (auto* m = find("CutsceneCommon.gbs")) { CHECK_EQ(m->gbs.views.size(), static_cast<size_t>(21)); CHECK_EQ(m->gbs.tailSize, 1632u); }
    if (auto* m = find("OtterIsStupid.gbs")) CHECK(m->gbs.views.empty());

    CHECK_EQ(inv.stages.size(), static_cast<size_t>(32));
    CHECK_EQ(inv.music.size(), static_cast<size_t>(24));
    CHECK_EQ(inv.tocFiles.size(), static_cast<size_t>(6));
    CHECK_EQ(inv.compileJobs.size(), static_cast<size_t>(12));
    CHECK(inv.warnings.empty());
    for (const std::string& w : inv.warnings) std::fprintf(stderr, "    warning: %s\n", w.c_str());

    std::string text;
    CHECK(gd.vfs().readText("arcade/story/Filia.stm", text));
    StmFile f = parseStm(text);
    CHECK_EQ(f.charName, std::string("Filia"));
    CHECK_EQ(f.nodes.size(), static_cast<size_t>(17));
    CHECK_EQ(f.nodes.back().arg, std::string("Complete"));
    // every stm in the data parses to at least one node and every "FileName" it references exists
    size_t refs = 0, missing = 0;
    for (const std::string& p : gd.vfs().listByExt(".stm")) {
        std::vector<uint8_t> raw;
        if (!readFile(p, raw)) continue;
        const std::string t(raw.begin(), raw.end());
        StmFile sf = parseStm(t);
        for (const StmNode& n : sf.nodes)
            if (const std::string* fn = n.get("FileName")) {
                ++refs;
                if (!gd.vfs().exists(*fn)) ++missing;
            }
    }
    std::printf("  stm FileName references: %zu, unresolved: %zu\n", refs, missing);
    CHECK_EQ(refs, static_cast<size_t>(556));
    CHECK_EQ(missing, static_cast<size_t>(0));
}

// ============================================================ Steam downloader: pure logic
static bool has(const std::vector<std::string>& v, const std::string& s) {
    for (const auto& x : v) if (x == s) return true;
    return false;
}

static void testSteamLogic() {
    std::printf("[steam logic]\n");
    DownloadRequest req;
    req.installDir = "/data/sg";
    req.depots = {245171, 245172};
    req.fileListPath = "/data/files.txt";
    SteamLogin login;
    login.username = "tester";
    auto a = buildDepotDownloaderArgs(req, login);
    CHECK(has(a, "-app") && has(a, "245170"));
    CHECK(has(a, "-username") && has(a, "tester"));
    CHECK(has(a, "-remember-password"));
    CHECK(has(a, "-os") && has(a, "windows"));
    CHECK(has(a, "-dir") && has(a, "/data/sg"));
    CHECK(has(a, "-filelist"));
    CHECK(!has(a, "-password") && !has(a, "-pass"));  // secrets never go on the command line
    for (size_t i = 0; i + 2 < a.size(); ++i)
        if (a[i] == "-depot") { CHECK_EQ(a[i + 1], std::string("245171")); CHECK_EQ(a[i + 2], std::string("245172")); }
    login.rememberPassword = false;
    CHECK(!has(buildDepotDownloaderArgs(req, login), "-remember-password"));

    CHECK_EQ(makeFileListText({"data01\\levels.gfs", "a/b.gfs"}, {"\\.dds$"}), std::string("data01/levels.gfs\na/b.gfs\nregex:\\.dds$\n"));

    // output lines, in the exact formats used by DepotDownloader's source
    Event e = parseOutputLine(" 12.50% depots/245171/9/data01/levels.gfs");
    CHECK(e.type == EventType::Progress);
    CHECK(e.percent > 12.49 && e.percent < 12.51);
    CHECK_EQ(e.file, std::string("depots/245171/9/data01/levels.gfs"));
    e = parseOutputLine("100.00% x");
    CHECK(e.type == EventType::Progress && e.percent > 99.99);
    e = parseOutputLine("Got 12 licenses for account!");
    CHECK(e.type == EventType::Licenses && e.number == 12);
    e = parseOutputLine("Processing depot 245171");
    CHECK(e.type == EventType::DepotStart && e.number == 245171);
    e = parseOutputLine("Total downloaded: 123456 bytes (234567 bytes uncompressed) from 1 depots");
    CHECK(e.type == EventType::Finished && e.bytes == 123456);
    CHECK(parseOutputLine("Depot 245171 is not available from this account.").type == EventType::Error);
    CHECK(parseOutputLine("Error: -app not specified!").type == EventType::Error);
    CHECK(parseOutputLine("Using app branch: 'public'.").type == EventType::Log);
    CHECK(parseOutputLine("3.14 not a progress line").type == EventType::Log);
    CHECK(parseOutputLine("12.5% missing second decimal").type == EventType::Log);

    CHECK(classifyPrompt("Enter account password for \"tester\": ") == PromptKind::Password);
    CHECK(classifyPrompt("STEAM GUARD! Please enter your 2-factor auth code from your authenticator app: ") == PromptKind::GuardAppCode);
    CHECK(classifyPrompt("STEAM GUARD! Please enter the auth code sent to the email at a@b.c: ") == PromptKind::GuardEmailCode);
    CHECK(classifyPrompt("Enter account password for \"tester\"") == PromptKind::None);  // still being received
    CHECK(classifyPrompt("hello: ") == PromptKind::None);
    CHECK(isMobileConfirmLine("STEAM GUARD! Use the Steam Mobile App to confirm your sign in..."));
    CHECK(isWrongCodeLine("The previous 2-factor auth code you have provided is incorrect."));

    const char* manifest =
        "Content Manifest for Depot 245171 \n\nManifest ID / date     : 123 / 01/01/2026 \n"
        "Total number of files  : 4 \nTotal number of chunks : 10 \nTotal bytes on disk    : 3000 \n"
        "Total bytes compressed : 1500 \n\n\n"
        "          Size Chunks File SHA                                 Flags Name\n"
        "             0      0 0000000000000000000000000000000000000000    40 data01\n"
        "          2000      2 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa     0 data01/levels.gfs\n"
        "           500      1 bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb     0 Skullgirls.exe\n"
        "           500      1 cccccccccccccccccccccccccccccccccccccccc     0 data01/My File With Spaces.GFS\n";
    ManifestListing m;
    CHECK(parseManifestListing(manifest, m));
    CHECK_EQ(m.depot, 245171u);
    CHECK_EQ(m.totalFiles, static_cast<uint64_t>(4));
    CHECK_EQ(m.totalBytes, static_cast<uint64_t>(3000));
    CHECK_EQ(m.files.size(), static_cast<size_t>(3));  // directory entry skipped
    CHECK_EQ(m.files[2].name, std::string("data01/My File With Spaces.GFS"));
    auto gfs = selectByExtension(m, {".gfs"});
    CHECK_EQ(gfs.size(), static_cast<size_t>(2));
    CHECK_EQ(totalSize(m, gfs), static_cast<uint64_t>(2500));
    CHECK(!parseManifestListing("nothing useful here", m));
}

// ============================================================ Steam downloader: process runner against a fake DepotDownloader
static std::string writeScript(const std::string& dir, const std::string& name, const std::string& body) {
    const std::string p = dir + "/" + name;
    writeText(p, "#!/bin/sh\n" + body);
    chmod(p.c_str(), 0755);
    return p;
}

static void testSteamRunner() {
    std::printf("[steam runner (fake DepotDownloader)]\n");
    const std::string dir = makeTempDir("proc");
    const std::string argsFile = dir + "/args.txt";
    const std::string kPass = "hunter2-secret", kCode = "12345";

    // Mirrors the real program: prompts have no trailing newline; password prompt on stdout, Steam Guard on stderr.
    const std::string good = writeScript(dir, "dd_ok.sh", R"SH(
echo "$@" > "$FAKE_ARGS_FILE"
printf 'Enter account password for "tester": '
read -r pw
echo
if [ "$pw" != "hunter2-secret" ]; then echo "Error: wrong password"; exit 1; fi
echo "Got 3 licenses for account!"
printf 'STEAM GUARD! Please enter your 2-factor auth code from your authenticator app: ' >&2
read -r code
if [ "$code" != "12345" ]; then echo "The previous 2-factor auth code you have provided is incorrect." >&2; exit 1; fi
echo "STEAM GUARD! Use the Steam Mobile App to confirm your sign in..." >&2
echo "Processing depot 245171"
echo " 12.50% depots/data01/levels.gfs"
echo " 99.99% depots/data01/levels.gfs"
echo "100.00% depots/data01/core.gfs"
echo "debug: the password is $pw and the code is $code"
echo "Total downloaded: 987654 bytes (234567 bytes uncompressed) from 1 depots"
exit 0
)SH");

    {   // happy path
        std::vector<Event> events;
        std::vector<std::string> notices;
        std::vector<PromptKind> prompts;
        RunCallbacks cb;
        cb.onEvent = [&](const Event& e) { events.push_back(e); };
        cb.onNotice = [&](const std::string& l) { notices.push_back(l); };
        cb.onPrompt = [&](PromptKind k, std::string& ans) {
            prompts.push_back(k);
            ans = (k == PromptKind::Password) ? kPass : kCode;
            return true;
        };
        DownloadRequest req;
        req.installDir = dir + "/out";
        SteamLogin login;
        login.username = "tester";
        RunResult r = runDepotDownloader(good, buildDepotDownloaderArgs(req, login), {{"FAKE_ARGS_FILE", argsFile}},
                                         {kPass, kCode}, cb);
        CHECK(r.error.empty());
        CHECK_EQ(r.exitCode, 0);
        CHECK_EQ(prompts.size(), static_cast<size_t>(2));
        if (prompts.size() == 2) { CHECK(prompts[0] == PromptKind::Password); CHECK(prompts[1] == PromptKind::GuardAppCode); }
        CHECK_EQ(notices.size(), static_cast<size_t>(1));  // mobile-confirm line
        bool lic = false, fin = false, dep = false, leaked = false;
        double lastPct = -1;
        for (const Event& e : events) {
            if (e.type == EventType::Licenses && e.number == 3) lic = true;
            if (e.type == EventType::DepotStart && e.number == 245171) dep = true;
            if (e.type == EventType::Finished && e.bytes == 987654) fin = true;
            if (e.type == EventType::Progress) lastPct = e.percent;
            if (e.text.find(kPass) != std::string::npos || e.text.find(kCode) != std::string::npos) leaked = true;
        }
        CHECK(lic && dep && fin);
        if (!(lic && dep && fin))
            for (const Event& e : events) std::fprintf(stderr, "    event type=%d num=%u bytes=%llu text='%s'\n", static_cast<int>(e.type),
                                                       e.number, static_cast<unsigned long long>(e.bytes), e.text.c_str());
        CHECK(lastPct > 99.9);
        CHECK(!leaked);  // the script deliberately echoes the secrets; the runner must redact them
        bool sawRedacted = false;
        for (const Event& e : events) if (e.text.find("debug: the password is ***") != std::string::npos) sawRedacted = true;
        CHECK(sawRedacted);
        const std::vector<uint8_t> argRaw = slurp(argsFile);
        const std::string argv(argRaw.begin(), argRaw.end());
        CHECK(argv.find(kPass) == std::string::npos);  // never on the command line
        CHECK(argv.find("-username tester") != std::string::npos);
    }
    {   // account does not own the game
        const std::string notOwned = writeScript(dir, "dd_notowned.sh", R"SH(
printf 'Enter account password for "tester": '
read -r pw
echo
echo "Got 1 licenses for account!"
echo "Depot 245171 is not available from this account."
exit 1
)SH");
        std::vector<Event> events;
        RunCallbacks cb;
        cb.onEvent = [&](const Event& e) { events.push_back(e); };
        cb.onPrompt = [&](PromptKind, std::string& a) { a = kPass; return true; };
        RunResult r = runDepotDownloader(notOwned, {"-app", "245170"}, {}, {kPass}, cb);
        CHECK_EQ(r.exitCode, 1);
        bool err = false;
        for (const Event& e : events) if (e.type == EventType::Error) err = true;
        CHECK(err);
    }
    {   // user cancels the password dialog -> the child must be stopped, not left waiting
        const std::string waits = writeScript(dir, "dd_wait.sh", "printf 'Enter account password for \"t\": '\nread -r pw\nsleep 30\n");
        RunCallbacks cb;
        cb.onPrompt = [](PromptKind, std::string&) { return false; };
        const auto t0 = std::chrono::steady_clock::now();
        RunResult r = runDepotDownloader(waits, {}, {}, {}, cb);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(r.aborted);
        CHECK(secs < 10.0);
    }
    {   // cancel flag stops a long download
        const std::string slow = writeScript(dir, "dd_slow.sh", "echo \"Processing depot 1\"\nsleep 30\n");
        std::atomic<bool> cancel{false};
        RunCallbacks cb;
        std::thread t([&] { std::this_thread::sleep_for(std::chrono::milliseconds(400)); cancel = true; });
        const auto t0 = std::chrono::steady_clock::now();
        RunResult r = runDepotDownloader(slow, {}, {}, {}, cb, &cancel);
        t.join();
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(r.cancelled);
        CHECK(secs < 10.0);
    }
    {   // missing executable is a clean error, not a crash
        RunCallbacks cb;
        RunResult r = runDepotDownloader(dir + "/nope", {}, {}, {}, cb);
        CHECK(!r.error.empty());
    }
    fs::remove_all(dir);
}


// ============================================================ Steam downloader: worker thread + state machine
template <class F>
static bool waitUntil(F cond, double secs = 10.0) {
    const auto t0 = std::chrono::steady_clock::now();
    while (!cond()) {
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > secs) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return true;
}

static void testSteamTask() {
    std::printf("[steam task (worker thread)]\n");
    const std::string dir = makeTempDir("task");
    const std::string okScript = writeScript(dir, "dd_ok.sh", R"SH(
printf 'Enter account password for "tester": '
read -r pw
echo
echo "Got 3 licenses for account!"
printf 'STEAM GUARD! Please enter your 2-factor auth code from your authenticator app: ' >&2
read -r code
if [ "$code" != "AB123" ]; then echo "The previous 2-factor auth code you have provided is incorrect." >&2; exit 1; fi
echo "Processing depot 245171"
echo " 50.00% depots/data01/core.gfs"
echo "echo $pw"
echo "100.00% depots/data01/ui.gfs"
echo "Total downloaded: 987654 bytes (1 bytes uncompressed) from 1 depots"
)SH");
    SteamTaskConfig cfg;
    cfg.exe = okScript;
    cfg.installDir = dir + "/out";
    cfg.homeDir = dir + "/home";
    cfg.username = "tester";

    {   // full flow: password is sent automatically, the Guard code comes from the "UI"
        SteamTask t;
        std::string err;
        CHECK(t.start(cfg, "hunter2-secret", &err));
        CHECK(!t.start(cfg, "x", &err));  // second start while running is refused
        CHECK(waitUntil([&] { return t.snapshot().phase == SteamPhase::NeedGuardCode; }));
        SteamTaskState s = t.snapshot();
        CHECK(s.pendingPrompt == PromptKind::GuardAppCode);
        CHECK_EQ(s.licenses, 3u);
        t.submitCode("AB123");
        CHECK(waitUntil([&] { return !t.running(); }));
        t.join();
        s = t.snapshot();
        CHECK(s.phase == SteamPhase::Done);
        CHECK_EQ(s.exitCode, 0);
        CHECK(s.percent > 99.9);
        CHECK_EQ(s.depot, 245171u);
        CHECK_EQ(s.bytes, static_cast<uint64_t>(987654));
        CHECK_EQ(s.currentFile, std::string("depots/data01/ui.gfs"));
        bool leaked = false;
        for (const std::string& l : s.log) if (l.find("hunter2-secret") != std::string::npos) leaked = true;
        CHECK(!leaked);
        CHECK(fs::exists(dir + "/out"));   // install folder was created
    }
    {   // wrong Steam Guard code
        SteamTask t;
        CHECK(t.start(cfg, "pw12345"));
        CHECK(waitUntil([&] { return t.snapshot().phase == SteamPhase::NeedGuardCode; }));
        t.submitCode("WRONG");
        CHECK(waitUntil([&] { return !t.running(); }));
        t.join();
        SteamTaskState s = t.snapshot();
        CHECK(s.phase == SteamPhase::Failed);
        CHECK(s.wrongCode);
        CHECK_EQ(s.exitCode, 1);
    }
    {   // cancel while waiting for the code
        SteamTask t;
        CHECK(t.start(cfg, "pw12345"));
        CHECK(waitUntil([&] { return t.snapshot().phase == SteamPhase::NeedGuardCode; }));
        t.cancel();
        CHECK(waitUntil([&] { return !t.running(); }));
        t.join();
        CHECK(t.snapshot().phase == SteamPhase::Cancelled);
    }
    {   // account that does not own the game
        const std::string notOwned = writeScript(dir, "dd_notowned.sh", R"SH(
printf 'Enter account password for "tester": '
read -r pw
echo
echo "Got 1 licenses for account!"
echo "Depot 245171 is not available from this account."
exit 1
)SH");
        SteamTaskConfig c2 = cfg;
        c2.exe = notOwned;
        SteamTask t;
        CHECK(t.start(c2, "pw12345"));
        CHECK(waitUntil([&] { return !t.running(); }));
        t.join();
        SteamTaskState s = t.snapshot();
        CHECK(s.phase == SteamPhase::Failed);
        CHECK(s.message.find("does not own") != std::string::npos);
    }
    {   // config validation
        SteamTask t;
        std::string err;
        SteamTaskConfig bad = cfg;
        bad.username.clear();
        CHECK(!t.start(bad, "pw", &err));
        CHECK(!err.empty());
        bad = cfg;
        bad.exe.clear();
        CHECK(!t.start(bad, "pw", &err));
        bad = cfg;
        bad.exe = dir + "/does_not_exist";
        CHECK(t.start(bad, "pw", &err));  // spawn failure is reported through the state, not by crashing
        CHECK(waitUntil([&] { return !t.running(); }));
        t.join();
        CHECK(t.snapshot().phase == SteamPhase::Failed);
    }
    fs::remove_all(dir);
}

// Real character files (annie): the expected numbers were produced by tools/reference/sg_extract.py on the same files
// (tests/real_diff.py compares every record; here a few anchors keep the regression check in ctest).
static uint32_t crc32of(const std::vector<uint8_t>& d) {
    uint32_t c = 0xFFFFFFFFu;
    for (uint8_t b : d) {
        c ^= b;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return c ^ 0xFFFFFFFFu;
}

// Every real .foit under the data folders must give the fight runtime what it needs to play the character: the 18 normals, a jump and
// its landing, the locomotion states.  A character whose moves are named differently (Filia: Jab / Strong / Fierce / Short / Fwd / RH, one
// Jump_Start / JumpLand) used to come out with none of her attacks and no jump.
static void testRealMoveSets(const std::vector<std::string>& dirs) {
    std::printf("[real move sets: every .foit found]\n");
    size_t found = 0;
    for (const std::string& d : dirs) {
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(d, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file() || it->path().extension() != ".foit") continue;
            std::vector<uint8_t> bytes;
            if (!readFile(it->path().string(), bytes)) continue;
            auto ch = std::make_shared<foit::Character>();
            std::string err;
            const std::string who = it->path().filename().string();
            const bool parsed = foit::parse(bytes.data(), bytes.size(), *ch, &err);
            if (!parsed) std::printf("  %s: %s\n", who.c_str(), err.c_str());
            CHECK(parsed);
            if (!parsed) continue;
            fight::MoveSet ms;
            const bool built = ms.build(ch, &err);
            CHECK(built);
            if (!built) continue;
            // Boss rigs (BossMarie: 88 states, no hit records, no crouch / jump / hit reactions) are scripted by arcade Lua, not
            // played with the 18 normals: not a fighter, nothing to check here.
            if (ms.hitCount() == 0 && ms.id.crouch < 0 && ms.id.jumpStart[0] < 0) {
                std::printf("  %s: scripted rig (no hit data, no crouch / jump), skipped\n", who.c_str());
                continue;
            }
            ++found;
            int missing = 0;
            for (int p = 0; p < fight::kPostureCount; ++p)
                for (int b = 0; b < fight::kNormalCount; ++b) missing += ms.id.normal[p][b] < 0;
            if (missing) std::printf("  %s: %d of 18 normals do not resolve\n", who.c_str(), missing);
            CHECK_EQ(missing, 0);
            CHECK(ms.id.stand >= 0);
            CHECK(ms.id.crouch >= 0);
            CHECK(ms.id.walkFLoop >= 0);
            CHECK(ms.id.walkBLoop >= 0);
            CHECK(ms.id.dashF >= 0);
            CHECK(ms.id.jumpStart[0] >= 0);
            CHECK(ms.id.land[0] >= 0);
            CHECK(ms.id.hit[fight::kPStand][1] >= 0);
        }
    }
    if (!found) skip("no .foit under ZDROID_TEST_DATA");
}

static const fight::HubNormal* find2(const std::vector<fight::HubNormal>& h, int state) {
    for (const fight::HubNormal& e : h)
        if (e.state == state) return &e;
    return nullptr;
}

// The `Normals` hub of a character: which transitions are plain attacks, and which direction they need.
static void testHubNormalsSynthetic() {
    std::printf("[Normals hub -> attack table: synthetic]\n");
    foit::Object o;
    auto addState = [&](const char* n) {
        foit::State s;
        s.name = n;
        o.states.push_back(s);
        return static_cast<uint16_t>(o.states.size() - 1);
    };
    const uint16_t hub = addState("Normals");
    const uint16_t wp = addState("s.WP"), fmp = addState("s.F+MP"), mkB = addState("j.MK_B"), mkN = addState("j.MK_N"), mkF = addState("j.MK_F");
    const uint16_t thr = addState("s.Throw_Fwd"), odd = addState("s.Odd"), seq = addState("c.MotionThing"), bad = addState("x.Foo");
    auto addTr = [&](uint16_t target, int b0, std::initializer_list<int> pressed, size_t steps = 1) {
        foit::Transition t;
        t.target = target;
        for (size_t k = 0; k < steps; ++k) {
            foit::Step st;
            st.b0 = static_cast<uint8_t>(b0);
            for (int i : pressed) st.t[i] = 0;
            t.cmd.steps.push_back(st);
        }
        o.states[hub].transitions.push_back(t);
    };
    addTr(mkF, 0, {4});      // forward + MK, listed before the neutral one on purpose
    addTr(mkB, 4, {4});
    addTr(mkN, 9, {4});
    addTr(fmp, 0, {1});
    addTr(wp, 9, {0});
    addTr(thr, 9, {0, 3});   // a throw: two buttons
    addTr(odd, 9, {0, 1});   // two buttons, no "Throw" in the name
    addTr(seq, 2, {2}, 2);   // two steps: a motion, not a normal
    addTr(bad, 9, {2});      // not s. / c. / j.
    const uint16_t jd = addState("j.D+HK"), su = addState("s.U+WP"), cwp = addState("c.WP");
    addTr(jd, 2, {5});       // down in the air: a command of its own
    addTr(su, 6, {0});       // up + punch: listed, but not an input the duel has
    addTr(cwp, 2, {0});      // down on the ground = the crouching posture, a plain crouching attack
    const std::vector<fight::HubNormal> h = fight::hubNormals(o);
    CHECK_EQ(h.size(), static_cast<size_t>(8));
    if (const fight::HubNormal* e = find2(h, jd)) {
        CHECK_EQ(e->dir, 3);
    } else CHECK(false);
    if (const fight::HubNormal* e = find2(h, su)) {
        CHECK_EQ(e->dir, 4);
    } else CHECK(false);
    if (const fight::HubNormal* e = find2(h, cwp)) {
        CHECK_EQ(e->dir, 0);
    } else CHECK(false);
    auto find = [&](int state) -> const fight::HubNormal* {
        for (const fight::HubNormal& e : h)
            if (e.state == state) return &e;
        return nullptr;
    };
    if (const fight::HubNormal* e = find(wp)) {
        CHECK_EQ(e->posture, static_cast<int>(fight::kPStand));
        CHECK_EQ(e->button, 0);
        CHECK_EQ(e->dir, 0);
    } else CHECK(false);
    if (const fight::HubNormal* e = find(fmp)) {
        CHECK_EQ(e->button, 1);
        CHECK_EQ(e->dir, 1);
    } else CHECK(false);
    if (const fight::HubNormal* e = find(mkB)) {
        CHECK_EQ(e->posture, static_cast<int>(fight::kPAir));
        CHECK_EQ(e->button, 4);
        CHECK_EQ(e->dir, 2);
    } else CHECK(false);
    CHECK(find(thr) == nullptr);
    CHECK(find(odd) == nullptr);
    CHECK(find(seq) == nullptr);
    CHECK(find(bad) == nullptr);
    CHECK(fight::hubNormals(foit::Object{}).empty());   // no hub (boss rigs): nothing, no crash
}

// Every real character: the 18 normals resolve, the hub agrees with the names, the direction variants are other states of the same posture.
static void testHubNormalsReal(const std::string& dir) {
    std::printf("[Normals hub vs state names: every character in %s]\n", dir.c_str());
    size_t n = 0, variants = 0;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(dir, ec)) {
        if (!e.is_regular_file() || e.path().extension() != ".foit") continue;
        std::vector<uint8_t> bytes;
        if (!readFile(e.path().string(), bytes)) continue;
        auto ch = std::make_shared<foit::Character>();
        std::string err;
        if (!foit::parse(bytes.data(), bytes.size(), *ch, &err)) continue;
        fight::MoveSet ms;
        if (!ms.build(ch, &err)) continue;
        const std::string who = e.path().filename().string();
        const auto hub = fight::hubNormals(ch->body);
        // BossMarie's rig (scripted by the arcade Lua: no hit data, no crouch / jump) and HitSpark (no Normals hub) are not fighters
        if (hub.empty() || (ms.hitCount() == 0 && ms.id.crouch < 0 && ms.id.jumpStart[0] < 0)) {
            std::printf("  %s: not a fighter rig, skipped\n", who.c_str());
            continue;
        }
        ++n;
        int missing = 0;
        for (int p = 0; p < fight::kPostureCount; ++p)
            for (int b = 0; b < fight::kNormalCount; ++b) {
                const int s = ms.id.normal[p][b];
                missing += s < 0;
                const int f = ms.id.normalF[p][b], k = ms.id.normalB[p][b], d = ms.id.normalD[p][b];
                if (d >= 0) CHECK_EQ(p, static_cast<int>(fight::kPAir));   // down is a command only in the air
                for (const int v : {f, k, d}) {
                    if (v < 0) continue;
                    ++variants;
                    CHECK(v != s);
                    const char pre = p == fight::kPStand ? 's' : (p == fight::kPCrouch ? 'c' : 'j');
                    CHECK_EQ(ms.stateName(v)[0], pre);
                }
            }
        if (missing) std::printf("  %s: %d of 18 normals do not resolve\n", who.c_str(), missing);
        CHECK_EQ(missing, 0);
        // the table and the names tell the same story wherever both speak
        int disagree = 0, covered = 0;
        for (const fight::HubNormal& h : hub) {
            if (h.dir != 0) continue;
            ++covered;
            if (ms.id.normal[h.posture][h.button] != h.state) {
                ++disagree;
                std::printf("  %s: hub says %s for posture %d button %d, names say %s\n", who.c_str(), ms.stateName(h.state).c_str(), h.posture, h.button,
                            ms.stateName(ms.id.normal[h.posture][h.button]).c_str());
            }
        }
        CHECK_EQ(disagree, 0);
        CHECK(covered >= 18);   // each fighter lists all 18 plain normals in its hub
    }
    std::printf("  %zu characters, %zu direction variants\n", n, variants);
    CHECK(n >= 1);
}

// Every row of the real stages.ini must resolve from each name the game and the menus use for it.
static void testRealStages(const std::vector<std::string>& dirs) {
    std::printf("[real stages.ini rows -> .lvl]\n");
    GameData gd;
    for (const std::string& d : dirs) gd.mount(d);
    const Inventory inv = gd.scan();
    if (inv.stages.empty() || inv.levels.empty()) { skip("no stages.ini / .lvl in the mounted data"); return; }
    size_t rows = 0;
    for (const StageEntry& e : inv.stages) {
        bool have = false;
        for (const LevelInfo& l : inv.levels) have = have || l.name == e.level;
        if (!have) continue;   // a row whose .lvl the user did not unpack
        ++rows;
        std::string thumb = e.picture;
        if (thumb.compare(0, 6, "stage_") == 0) thumb = thumb.substr(6);
        for (const std::string& name : {e.level, e.locId, e.picture, thumb}) {
            const int i = resolveLevel(inv.levels, inv.stages, name);
            if (i < 0 || inv.levels[static_cast<size_t>(i)].name != e.level) std::printf("  '%s' (row %s) -> %s\n", name.c_str(), e.locId.c_str(), i < 0 ? "none" : inv.levels[static_cast<size_t>(i)].name.c_str());
            CHECK(i >= 0 && inv.levels[static_cast<size_t>(i)].name == e.level);
        }
    }
    const std::vector<int> pool = randomLevels(inv.levels, inv.stages);
    for (int i : pool) CHECK(inv.levels[static_cast<size_t>(i)].name.compare(0, 4, "test") != 0);
    std::printf("  %zu stages.ini rows with a .lvl, %zu of %zu levels in the Random pool\n", rows, pool.size(), inv.levels.size());
    CHECK(rows > 0);
}

static void testRealCharacter(const std::string& dir) {
    std::printf("[real character files: annie]\n");
    GameData gd;
    CHECK(gd.mount(dir) >= 4);
    const Inventory inv = gd.scan();
    const CharacterAssets* ca = nullptr;
    for (const CharacterAssets& c : inv.characters)
        if (c.name == "annie") ca = &c;
    CHECK(ca != nullptr);
    if (!ca) return;
    CHECK(!ca->artPath.empty());
    CHECK(!ca->artPtPath.empty());
    CHECK_EQ(ca->palettes.size(), static_cast<size_t>(1));
    ArtFile af;
    std::string err;
    CHECK(gd.loadCharacter(*ca, af, &err));
    CHECK_EQ(af.records().size(), static_cast<size_t>(1207));
    size_t cat[3] = {0, 0, 0}, flagged = 0, decoded = 0;
    for (size_t i = 0; i < af.records().size(); ++i) {
        const ArtRecord& r = af.records()[i];
        ++cat[static_cast<int>(r.category())];
        flagged += r.flagged();
        ArtPlanes pl;
        if (af.decode(i, pl) && pl.band.size() == static_cast<size_t>(r.pixelWidth()) * r.pixelHeight()) ++decoded;
    }
    CHECK_EQ(cat[0], static_cast<size_t>(225));  // sprites
    CHECK_EQ(cat[1], static_cast<size_t>(454));  // extras (.art-pt)
    CHECK_EQ(cat[2], static_cast<size_t>(528));  // silhouettes
    CHECK_EQ(flagged, static_cast<size_t>(175)); // tags 0x7A / 0xFA
    CHECK_EQ(decoded, static_cast<size_t>(1207));
    struct Anchor { size_t idx; uint32_t foitOff, off, bandCrc, gradCrc; uint8_t tag; };
    const Anchor anchors[] = {
        {0, 63849, 0, 0xa943e349u, 0x64778961u, 0x3a},
        {1, 63958, 956691, 0x81e81715u, 0x5e3575c9u, 0x3a},
        {407, 236991, 6839966, 0xf0c61b00u, 0xe5f106edu, 0x7a},  // flagged: odd tone rows interpolated (was 0xf8f09697 as stored)
        {699, 333850, 9806625, 0xc98a6a68u, 0x5648c6beu, 0xba},
        {1206, 426618, 19626444, 0x5506beadu, 0x6ba79af4u, 0xba},
    };
    for (const Anchor& a : anchors) {
        const ArtRecord& r = af.records()[a.idx];
        CHECK_EQ(r.foitOff, a.foitOff);
        CHECK_EQ(r.off, a.off);
        CHECK_EQ(static_cast<int>(r.tag), static_cast<int>(a.tag));
        ArtPlanes pl;
        CHECK(af.decode(a.idx, pl));
        CHECK_EQ(crc32of(pl.band), a.bandCrc);
        CHECK_EQ(crc32of(pl.grad), a.gradCrc);
    }
    Palette pal;
    CHECK(GameData::loadPalette(ca->palettes[0].second, pal, &err));
    CHECK_EQ(pal.tones, 16);
    CHECK_EQ(pal.bands, 52);
    // a real frame must produce visible, non-uniform pixels
    ArtPlanes pl;
    CHECK(af.decode(1, pl));
    const std::vector<uint8_t> rgba = toRgba(pl, buildRgbaTables(pal));
    size_t opaque = 0;
    for (size_t i = 3; i < rgba.size(); i += 4) opaque += rgba[i] == 255;
    CHECK(opaque > rgba.size() / 4 / 10);
}

// ---- .foit: frames / states / boxes (the grammar read from the executable) ------------------------------------
static void testFoitRobustness(const std::vector<uint8_t>& good) {
    foit::Character ch;
    std::string err;
    // garbage / truncated input must be rejected, never crash
    CHECK(!foit::parse(nullptr, 0, ch, &err));
    std::vector<uint8_t> junk(4096, 0x41);
    CHECK(!foit::parse(junk.data(), junk.size(), ch, &err));
    for (size_t cut : {size_t(12), size_t(6000), good.size() / 3, good.size() - 1}) {
        CHECK(!foit::parse(good.data(), cut, ch, &err));
    }
    // byte flips anywhere: any outcome is fine as long as it terminates without crashing
    uint32_t seed = 12345u;
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    for (int it = 0; it < 24; ++it) {
        std::vector<uint8_t> m = good;
        for (int k = 0; k < 12; ++k) m[rnd() % m.size()] = static_cast<uint8_t>(rnd());
        foit::parse(m.data(), m.size(), ch, &err);
    }
    CHECK(true);  // reaching this line is the check
}

static void testFoit(const std::string& dir) {
    std::printf("[.foit parser: annie]\n");
    std::vector<uint8_t> data;
    CHECK(readFile(dir + "/annie.foit", data));
    if (data.empty()) return;
    foit::Character ch;
    std::string err;
    CHECK(foit::parse(data.data(), data.size(), ch, &err));
    if (!err.empty()) std::printf("  error: %s\n", err.c_str());
    CHECK_EQ(ch.frames.size(), static_cast<size_t>(1549));
    CHECK_EQ(ch.body.states.size(), static_cast<size_t>(582));
    CHECK_EQ(ch.body.children.size(), static_cast<size_t>(37));
    CHECK_EQ(ch.body.name, std::string("Annie"));
    CHECK_EQ(ch.frameListPos, static_cast<size_t>(63835));
    CHECK(ch.body.slotDefaultsValid);
    CHECK_EQ(ch.body.slotDefaults[62], 0x40000000u);
    // state names (0-based ids)
    CHECK_EQ(ch.body.findState("Stand"), 21);
    CHECK_EQ(ch.body.findState("Crouch"), 22);
    CHECK_EQ(ch.body.findState("WalkFwd"), 27);
    CHECK_EQ(ch.body.findState("WalkFwd_Loop"), 28);
    CHECK_EQ(ch.body.findState("s.WP"), 118);
    // frame 1: standing pose - pivot between the feet, three hurtboxes, one push box
    const foit::Frame& f1 = ch.frames[1];
    CHECK_EQ(f1.pivotX, 409);
    CHECK_EQ(f1.pivotY, 511);
    CHECK_EQ(static_cast<int>(f1.src), 5);
    CHECK_EQ(f1.hurt.size(), static_cast<size_t>(3));
    CHECK_EQ(f1.push.size(), static_cast<size_t>(1));
    CHECK(!f1.img[0].placeholder());
    CHECK(f1.img[6].placeholder());
    CHECK_EQ(f1.img[0].off, 956691u);
    // frame 137 (standing light punch, active): one hitbox (top, bottom, left, right) relative to the pivot
    const foit::Frame& f137 = ch.frames[137];
    CHECK_EQ(f137.hit.size(), static_cast<size_t>(1));
    if (!f137.hit.empty()) {
        CHECK_EQ(f137.hit[0].top, -175);
        CHECK_EQ(f137.hit[0].bottom, -130);
        CHECK_EQ(f137.hit[0].left, 15);
        CHECK_EQ(f137.hit[0].right, 202);
    }
    // WalkFwd: three 3-tick frames (ids are 1-based in the nodes), then WalkFwd_Loop; header sets the walk speed
    const foit::State& walk = ch.body.states[27];
    int bodies = 0, ticks = 0;
    for (const foit::Node& n : walk.hdr.nodes)
        if (!n.isScript()) {
            ++bodies;
            ticks += n.ticks();
        }
    CHECK_EQ(bodies, 3);
    CHECK_EQ(ticks, 9);
    CHECK_EQ(static_cast<int>(walk.next), 28);
    CHECK(walk.hdr.hasChain);
    CHECK_EQ(walk.hdr.chain.size(), static_cast<size_t>(3));
    CHECK_EQ(static_cast<int>(walk.hdr.nodes[1].refs[1]), 45);
    CHECK_EQ(static_cast<int>(walk.hdr.nodes[3].refs[1]), 47);
    // Stand loops on itself, the light punch returns to Stand and carries hit data
    CHECK_EQ(static_cast<int>(ch.body.states[21].next), 21);
    CHECK_EQ(static_cast<int>(ch.body.states[118].next), 21);
    bool hasHit = false;
    for (const foit::Node& n : ch.body.states[118].hdr.nodes)
        for (const foit::NodeProps& p : n.props) hasHit = hasHit || p.hasHit;
    CHECK(hasHit);
    CHECK_EQ(ch.body.states[21].transitions.size(), static_cast<size_t>(144));

    // every frame image decodes; the old record scanner found 1207 of them, all present here (same file offsets)
    std::vector<uint8_t> art, pt;
    if (readFile(dir + "/annie.art", art) && readFile(dir + "/annie.art-pt", pt)) {
        size_t total = 0, decoded = 0;
        for (const foit::Frame& f : ch.frames) {
            const std::vector<uint8_t>& src = f.src == 5 ? art : pt;
            for (const foit::Image& im : f.img) {
                if (im.placeholder()) continue;
                ++total;
                decoded += foit::validateImage(im, src.data(), src.size());
            }
        }
        CHECK_EQ(total, static_cast<size_t>(1539));
        CHECK_EQ(decoded, total);
        ArtPlanes pl;
        const foit::Frame& f0 = ch.frames[0];
        const std::vector<uint8_t>& s0 = f0.src == 5 ? art : pt;
        CHECK(foit::decodeImage(f0.img[0], s0.data(), s0.size(), pl));
        CHECK_EQ(crc32of(pl.band), 0xa943e349u);
        CHECK_EQ(crc32of(pl.grad), 0x64778961u);
        ArtFile af;
        CHECK(af.load(data, art, pt));
        size_t found = 0;
        for (const ArtRecord& r : af.records()) {
            bool hit = false;
            for (const foit::Frame& f : ch.frames) {
                if ((r.src == ArtRecord::Src::Art) != (f.src == 5)) continue;
                for (const foit::Image& im : f.img) hit = hit || (im.external && im.off == r.off && im.size[0] == r.sizeNormal);
                if (hit) break;
            }
            found += hit;
        }
        CHECK_EQ(found, af.records().size());
    } else {
        skip("annie.art / annie.art-pt not found next to the .foit");
    }
    testFoitRobustness(data);
}

// every character file of the PC build must parse to the last byte (ZDROID_TEST_FOITS = folder with *.foit)
static void testFoitAll(const std::string& dir) {
    std::printf("[.foit parser: every character in %s]\n", dir.c_str());
    size_t n = 0;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(dir, ec)) {
        if (!e.is_regular_file() || e.path().extension() != ".foit") continue;
        std::vector<uint8_t> d;
        if (!readFile(e.path().string(), d)) continue;
        foit::Character ch;
        std::string err;
        const bool ok = foit::parse(d.data(), d.size(), ch, &err);
        if (!ok) std::printf("  %s: %s\n", e.path().filename().string().c_str(), err.c_str());
        CHECK(ok);
        if (ok) {
            CHECK(!ch.frames.empty());
            CHECK(!ch.body.states.empty());
            for (const foit::State& s : ch.body.states) CHECK(s.next < ch.body.states.size());
            // the layer object is the character's palette table: every palette has one colour per group slot
            CHECK(ch.hasLayer);
            CHECK(!ch.layer.palettes.empty());
            uint32_t sum = 0;
            for (const foit::ColorGroup& g : ch.layer.groups) sum += g.count;
            CHECK(sum == ch.layer.totalColors);
            for (const foit::Palette& p : ch.layer.palettes) CHECK(p.colors.size() == ch.layer.totalColors);
            CHECK(ch.layerPos < ch.tailPos && ch.tailPos < ch.frameListPos);
        }
        ++n;
    }
    CHECK(n > 0);
}


// ---- the script VM (synthetic chains) ---------------------------------------------------------------------
namespace {
foit::Operand opLit(float v) {
    foit::Operand o;
    o.desc = 0;
    std::memcpy(&o.raw, &v, 4);
    return o;
}
foit::Operand opSlot(int id, uint8_t prefix = 0) {
    foit::Operand o;
    o.desc = static_cast<uint8_t>(1 | prefix);
    o.raw = static_cast<uint32_t>(id);
    return o;
}
foit::Operand opFlag(int bit) {  // a flag bit (read or write)
    foit::Operand o;
    o.desc = 0x20;
    const float f = static_cast<float>(bit);
    std::memcpy(&o.raw, &f, 4);
    return o;
}
foit::Operand opConstHash(uint32_t h) {  // a literal with the descriptor the tag tests use
    foit::Operand o;
    o.desc = 0x24;
    o.raw = h;
    return o;
}
foit::ChainNode node(int op, foit::Operand a, foit::Operand b, foit::Operand c, bool boolean = false) {
    foit::ChainNode n;
    n.op = static_cast<uint8_t>(op);
    n.a = a;
    n.b = b;
    n.c = c;
    n.flags = boolean ? 1 : 0;
    return n;
}
struct TagHost : foit::vm::Host {
    uint32_t tag = 0;
    bool stateHasTag(const foit::vm::Ref&, uint32_t h) override { return h == tag; }
};
}  // namespace

static void testFoitVm() {
    std::printf("[foit script VM: synthetic chains]\n");
    using namespace foit;
    float slots[kSlotCount] = {};
    uint32_t flags[vm::kFlagWords] = {0, 0, 0};
    uint32_t defaults[kSlotCount] = {};
    const float def100 = 7.5f;
    std::memcpy(&defaults[100], &def100, 4);
    float enemySlots[kSlotCount] = {};
    uint32_t enemyFlags[vm::kFlagWords] = {0, 0, 0};
    float globals[8] = {};
    uint32_t seed = 12345;
    TagHost host;
    vm::Context cx;
    cx.self = {slots, flags, defaults, nullptr};
    cx.enemy = {enemySlots, enemyFlags, defaults, nullptr};
    cx.globals = globals;
    cx.numGlobals = 8;
    cx.rng = &seed;
    cx.host = &host;

    auto run = [&](std::initializer_list<ChainNode> nodes) {
        Chain c(nodes);
        return vm::runChain(c, cx);
    };
    auto test = [&](std::initializer_list<ChainNode> nodes) {
        Chain c(nodes);
        return vm::testChain(c, cx);
    };

    CHECK(run({node(vm::op::Set, opLit(5), opLit(0), opSlot(100))}) == 1);
    CHECK(slots[100] == 5.0f);
    run({node(vm::op::Add, opSlot(100), opLit(2.5f), opSlot(101))});
    CHECK(slots[101] == 7.5f);
    run({node(vm::op::Sub, opSlot(101), opSlot(100), opSlot(102)), node(vm::op::Mul, opSlot(102), opLit(4), opSlot(103))});
    CHECK(slots[102] == 2.5f && slots[103] == 10.0f);
    run({node(vm::op::Div, opSlot(103), opLit(4), opSlot(104))});
    CHECK(slots[104] == 2.5f);
    slots[105] = 99;
    run({node(vm::op::Div, opSlot(103), opLit(0), opSlot(105))});
    CHECK(slots[105] == 99.0f);  // division by zero writes nothing

    // engine owned slots (0..0x24) are read only for scripts
    slots[10] = 3;
    run({node(vm::op::Set, opLit(8), opLit(0), opSlot(10))});
    CHECK(slots[10] == 3.0f);

    run({node(vm::op::Max, opLit(3), opLit(9), opSlot(106)), node(vm::op::Min, opLit(3), opLit(9), opSlot(107)),
         node(vm::op::Abs, opLit(-4), opLit(0), opSlot(108)), node(vm::op::Trunc, opLit(3.9f), opLit(0), opSlot(109))});
    CHECK(slots[106] == 9.0f && slots[107] == 3.0f && slots[108] == 4.0f && slots[109] == 3.0f);

    // conditional sets: op 14 writes when B != 0, op 15 when B == 0
    slots[110] = slots[111] = 1;
    run({node(vm::op::SetIfNonZero, opLit(42), opLit(0), opSlot(110)), node(vm::op::SetIfZero, opLit(42), opLit(0), opSlot(111))});
    CHECK(slots[110] == 1.0f && slots[111] == 42.0f);

    // trig in degrees
    run({node(vm::op::Sin, opLit(90), opLit(0), opSlot(112)), node(vm::op::Cos, opLit(180), opLit(0), opSlot(113)),
         node(vm::op::Atan2, opLit(1), opLit(1), opSlot(114))});
    CHECK(std::fabs(slots[112] - 1.0f) < 1e-5f && std::fabs(slots[113] + 1.0f) < 1e-5f && std::fabs(slots[114] - 45.0f) < 1e-4f);

    // reset to the default / flag bits / global slots / other objects
    // op 8: C := the default value of the slot operand A names (read off the shipped engine: A is looked up, C is only the target)
    slots[100] = 1.0f;
    run({node(vm::op::Reset, opSlot(100), opLit(0), opSlot(140))});
    CHECK(slots[140] == 7.5f && slots[100] == 1.0f);
    slots[100] = 7.5f;
    run({node(vm::op::Set, opLit(1), opLit(0), opFlag(37))});
    CHECK((flags[1] & (1u << 5)) != 0);
    CHECK(test({node(vm::op::TEq, opFlag(37), opLit(0), opLit(1), true)}));
    run({node(vm::op::Set, opLit(0), opLit(0), opFlag(37))});
    CHECK((flags[1] & (1u << 5)) == 0);
    foit::Operand glob;
    glob.desc = 2;
    glob.raw = 3;
    run({node(vm::op::Set, opLit(6), opLit(0), glob)});
    CHECK(globals[3] == 6.0f);
    run({node(vm::op::Set, opLit(77), opLit(0), opSlot(120, 0x10))});  // E. : the enemy
    CHECK(enemySlots[120] == 77.0f && slots[120] == 0.0f);
    run({node(vm::op::Add, opSlot(120, 0x10), opLit(1), opSlot(121))});
    CHECK(slots[121] == 78.0f);

    // lerp / inverse lerp (C is read first): C := (1 - C) * A + C * B
    slots[122] = 0.25f;
    run({node(vm::op::Lerp, opLit(0), opLit(100), opSlot(122))});
    CHECK(std::fabs(slots[122] - 25.0f) < 1e-4f);
    slots[123] = 30;
    run({node(vm::op::InvLerp, opLit(10), opLit(50), opSlot(123))});
    CHECK(std::fabs(slots[123] - 0.5f) < 1e-5f);

    // boolean chains compare A with C (B is unused) and AND their tests; an empty chain holds
    slots[130] = 5;
    CHECK(test({}));
    CHECK(test({node(vm::op::TGt, opSlot(130), opLit(0), opLit(4), true), node(vm::op::TLe, opSlot(130), opLit(0), opLit(5), true)}));
    CHECK(!test({node(vm::op::TGt, opSlot(130), opLit(0), opLit(4), true), node(vm::op::TLt, opSlot(130), opLit(0), opLit(5), true)}));
    CHECK(test({node(vm::op::TNe, opSlot(130), opLit(0), opLit(6), true)}));
    CHECK(!test({node(vm::op::Set, opLit(1), opLit(0), opSlot(131))}));  // an arithmetic node is not a test
    // state tags (ops 0x20 / 0x21) ask the host
    host.tag = 0x11e97176u;
    CHECK(test({node(vm::op::TTag, opSlot(39), opLit(0), opConstHash(0x11e97176u), true)}));
    CHECK(!test({node(vm::op::TTag, opSlot(39), opLit(0), opConstHash(0x9f982536u), true)}));
    CHECK(test({node(vm::op::TNoTag, opSlot(39), opLit(0), opConstHash(0x9f982536u), true)}));

    // the random generator is a deterministic LCG and respects the range
    uint32_t s1 = 99, s2 = 99;
    bool inRange = true, varies = false;
    int first = vm::scriptRandom(s1, 10.0f);
    CHECK(first == vm::scriptRandom(s2, 10.0f));
    for (int i = 0; i < 200; ++i) {
        const int v = vm::scriptRandom(s1, 10.0f);
        inRange &= v >= 0 && v < 10;
        varies |= v != first;
    }
    CHECK(inRange && varies);
}

// ---- move set + duel runtime (Annie's real numbers) ------------------------------------------
static void testMoveSetAndDuel(const std::string& dir) {
    std::printf("[move set + duel runtime: annie]\n");
    std::vector<uint8_t> data;
    if (!readFile(dir + "/annie.foit", data)) { skip("annie.foit not found"); return; }
    auto ch = std::make_shared<foit::Character>();
    std::string err;
    CHECK(foit::parse(data.data(), data.size(), *ch, &err));
    fight::MoveSet ms;
    CHECK(ms.build(ch, &err));
    if (!ms.valid()) { std::printf("  moveset: %s\n", err.c_str()); return; }

    // physics read from the character's own scripts
    CHECK_EQ(ms.walkF, 5.5f);
    CHECK_EQ(ms.walkB, 4.0f);
    CHECK_EQ(ms.dashF, 14.0f);
    CHECK_EQ(ms.dashB, 11.0f);
    CHECK_EQ(ms.jumpVy, 32.0f);
    CHECK_EQ(ms.jumpVx, 7.0f);
    CHECK_EQ(ms.gravity, 1.5f);

    // the hand-driven states were all found
    CHECK(ms.id.stand >= 0 && ms.id.crouch >= 0 && ms.id.walkF >= 0 && ms.id.walkB >= 0);
    CHECK(ms.id.dashF >= 0 && ms.id.dashB >= 0);
    CHECK(ms.id.jumpStart[0] >= 0 && ms.id.jumpRise[0] >= 0 && ms.id.jumpFall[0] >= 0 && ms.id.land[0] >= 0);
    for (int b = 0; b < fight::kNormalCount; ++b)
        CHECK(ms.id.normal[fight::kPStand][b] >= 0 && ms.id.normal[fight::kPCrouch][b] >= 0 && ms.id.normal[fight::kPAir][b] >= 0);
    CHECK(ms.id.hit[fight::kPStand][1] >= 0 && ms.id.knockback >= 0 && ms.id.floorLie >= 0 && ms.id.wakeup >= 0);
    CHECK_EQ(ms.find("no such state"), -1);

    // s.WP: 23 ticks, hit data armed from tick 6 - damage 300, hitstop 7, hitstun 16, mid
    const int wp = ms.id.normal[fight::kPStand][0];
    CHECK_EQ(ms.stateName(wp), std::string("s.WP"));
    CHECK_EQ(ms.ticks(wp), 23);
    CHECK_EQ(ms.hitAt(wp, 5), -1);
    const int h = ms.hitAt(wp, 6);
    CHECK(h >= 0);
    if (h >= 0) {
        const fight::HitInfo& hi = ms.hit(h);
        CHECK_EQ(hi.damage, 300);
        CHECK_EQ(hi.hitstop, 7);
        CHECK_EQ(hi.hitstun, 16);
        CHECK_EQ(hi.level, 3);
        CHECK(!hi.launch);
        CHECK(hi.hitVx > 0.0f);  // pushes the defender away
    }
    CHECK_EQ(static_cast<int>(ms.frameAt(wp, 0)), 136);
    CHECK(ms.frameAt(wp, 9999) != fight::kNoFrame);  // clamped, never out of range
    CHECK(ms.frame(ms.frameAt(wp, 6)) != nullptr);
    // a heavy kick launches
    bool anyLauncher = false;
    for (size_t i = 0; i < ms.hitCount(); ++i) anyLauncher |= ms.hit(static_cast<int>(i)).launch;
    CHECK(anyLauncher);

    // ---- duel: 180 apart, one light punch
    fight::DuelContent content;
    content.ms[0] = &ms;
    content.ms[1] = &ms;
    CHECK(content.valid());
    auto fresh = [&](int gap) {
        fight::MatchState st{};
        fight::duelStartRound(st.p[0], ms, fight::toFixed(static_cast<float>(-gap / 2)), fight::Facing::kRight, 0);
        fight::duelStartRound(st.p[1], ms, fight::toFixed(static_cast<float>(gap / 2)), fight::Facing::kLeft, 0);
        st.roundTimer = 5940;
        return st;
    };
    {
        fight::MatchState st = fresh(180);
        for (int t = 0; t < 30; ++t) fight::advanceDuel(st, t == 2 ? fight::kLP : 0, 0, content);
        CHECK_EQ(st.p[1].health, 970);                 // 300 raw -> 30 of 1000
        CHECK_EQ(st.p[0].health, 1000);
        CHECK(st.p[1].x > fight::toFixed(90.0f));      // pushed away from the attacker
        CHECK(st.p[0].meter > 0);                      // the attacker built meter
    }
    {   // the same punch out of range whiffs
        fight::MatchState st = fresh(900);
        for (int t = 0; t < 40; ++t) fight::advanceDuel(st, t == 2 ? fight::kLP : 0, 0, content);
        CHECK_EQ(st.p[1].health, 1000);
    }
    {   // walking forward moves at the scripted speed; walking back is slower
        fight::MatchState st = fresh(900);
        for (int t = 0; t < 60; ++t) fight::advanceDuel(st, fight::kRight, fight::kRight, content);
        const float fwd = fight::fromFixed(st.p[0].x) + 450.0f;
        CHECK(fwd > 200.0f && fwd < 5.5f * 60.0f + 1.0f);
        fight::MatchState bk = fresh(900);
        for (int t = 0; t < 60; ++t) fight::advanceDuel(bk, fight::kLeft, 0, content);
        CHECK(-450.0f - fight::fromFixed(bk.p[0].x) > 100.0f);
    }
    {   // a neutral jump: leaves the floor, peaks around 300-345 px, comes back down and lands
        fight::MatchState st = fresh(900);
        float peak = 0;
        bool landed = false;
        for (int t = 0; t < 120; ++t) {
            fight::advanceDuel(st, t == 2 ? fight::kUp : 0, 0, content);
            peak = std::max(peak, fight::fromFixed(st.p[0].y));
            if (t > 20 && st.p[0].y == 0) landed = true;
        }
        CHECK(peak > 300.0f && peak < 345.0f);
        CHECK(landed);
    }
    {   // determinism: a snapshot round trip every tick never diverges, and the same inputs give the same checksum
        fight::MatchState a = fresh(150), b = fresh(150);
        uint32_t mismatches = 0;
        for (int t = 0; t < 240; ++t) {
            const uint16_t i1 = (t % 37 == 3) ? fight::kLP : (t % 53 == 5) ? fight::kHK : (t % 29 < 8 ? fight::kRight : 0);
            const uint16_t i2 = (t % 41 == 7) ? fight::kMP : (t % 31 < 6 ? fight::kLeft : 0);
            fight::advanceDuel(a, i1, i2, content);
            std::vector<uint8_t> snap = fight::saveState(b);
            fight::MatchState copy{};
            fight::loadState(copy, snap);
            fight::advanceDuel(copy, i1, i2, content);
            b = copy;
            if (a.checksum() != b.checksum()) ++mismatches;
        }
        CHECK_EQ(mismatches, 0u);
    }
    {   // KO ends the round
        fight::MatchState st = fresh(180);
        st.p[1].health = 20;
        for (int t = 0; t < 40; ++t) fight::advanceDuel(st, t == 2 ? fight::kLP : 0, 0, content);
        CHECK_EQ(static_cast<int>(st.roundOver), 1);
        CHECK(st.p[1].health <= 0);
    }
}

// ---- .dds ---------------------------------------------------------------------------------
static std::vector<uint8_t> makeDdsHeader(uint32_t w, uint32_t h, const char* fourcc, uint32_t pfFlags,
                                          uint32_t bpp, uint32_t mips, uint32_t caps) {
    std::vector<uint8_t> d(128, 0);
    auto put32 = [&](size_t off, uint32_t v) {
        for (int i = 0; i < 4; ++i) d[off + i] = static_cast<uint8_t>(v >> (8 * i));
    };
    put32(0, 0x20534444u);  // 'DDS '
    put32(4, 124);
    put32(8, 0x100F);  // caps | height | width | pitch | pixelformat | mipmapcount
    put32(12, h);
    put32(16, w);
    put32(20, 0);
    put32(24, 0);
    put32(28, mips);
    put32(76, 32);
    put32(80, pfFlags);
    for (int i = 0; i < 4; ++i) d[84 + i] = static_cast<uint8_t>(fourcc[i]);
    put32(88, bpp);
    put32(92, 0xFF);
    put32(96, 0xFF00);
    put32(100, 0xFF0000);
    put32(104, 0xFF000000);
    put32(108, caps);
    return d;
}

static void testDds() {
    std::string err;

    // header validation
    DdsInfo info;
    CHECK(!ddsHeader(reinterpret_cast<const uint8_t*>("nope"), 4, info, &err));
    std::vector<uint8_t> tiny(127, 0);
    CHECK(!ddsHeader(tiny.data(), tiny.size(), info, &err));
    CHECK_EQ(std::string(err), std::string("not a DDS file (too small)"));

    // --- DXT1, 4 colours: c0 = red, c1 = green, row 0 uses indices 0,1,2,3
    {
        std::vector<uint8_t> d = makeDdsHeader(4, 4, "DXT1", 0x4, 0, 1, 0x1000);
        const uint8_t block[8] = {0x00, 0xF8, 0xE0, 0x07, 0xE4, 0x00, 0x00, 0x00};
        d.insert(d.end(), block, block + 8);
        DdsImage img;
        CHECK(decodeDds(d, img, &err));
        CHECK_EQ(img.width, 4);
        CHECK_EQ(img.height, 4);
        CHECK_EQ(std::string(img.formatName()), std::string("DXT1"));
        const uint8_t* p = img.rgba.data();
        CHECK_EQ(p[0], 255); CHECK_EQ(p[1], 0);   CHECK_EQ(p[2], 0);   CHECK_EQ(p[3], 255);  // idx 0 -> c0
        CHECK_EQ(p[4], 0);   CHECK_EQ(p[5], 255); CHECK_EQ(p[6], 0);                          // idx 1 -> c1
        CHECK_EQ(p[8], 170); CHECK_EQ(p[9], 85);  CHECK_EQ(p[10], 0);                         // idx 2 -> (2c0+c1)/3
        CHECK_EQ(p[12], 85); CHECK_EQ(p[13], 170);                                            // idx 3 -> (c0+2c1)/3
        CHECK_EQ(p[16], 255);                                                                 // row 1: all idx 0
    }

    // --- a size that is not a multiple of 4 (6x5): the partial block row/column must not write out of the image
    {
        std::vector<uint8_t> d = makeDdsHeader(6, 5, "DXT1", 0x4, 0, 1, 0x1000);
        for (int b = 0; b < 4; ++b) {  // 2 x 2 blocks, all red
            const uint8_t block[8] = {0x00, 0xF8, 0x00, 0xF8, 0x00, 0x00, 0x00, 0x00};
            d.insert(d.end(), block, block + 8);
        }
        DdsImage img;
        CHECK(decodeDds(d, img, &err));
        CHECK_EQ(img.width, 6);
        CHECK_EQ(img.height, 5);
        CHECK_EQ(img.rgba.size(), static_cast<size_t>(6 * 5 * 4));
        CHECK_EQ(img.rgba[(4 * 6 + 5) * 4], 255);  // last pixel (bottom-right) decoded
    }

    // --- DXT1 3-colour block (c0 <= c1): index 3 is transparent black
    {
        std::vector<uint8_t> d = makeDdsHeader(4, 4, "DXT1", 0x4, 0, 1, 0x1000);
        const uint8_t block[8] = {0xE0, 0x07, 0x00, 0xF8, 0xFF, 0x00, 0x00, 0x00};  // all indices = 3
        d.insert(d.end(), block, block + 8);
        DdsImage img;
        CHECK(decodeDds(d, img, &err));
        CHECK_EQ(img.rgba[0], 0);
        CHECK_EQ(img.rgba[3], 0);
    }

    // --- DXT5 alpha ramp: a0 = 255, a1 = 0, indices 0,1,2,3 across row 0
    {
        std::vector<uint8_t> d = makeDdsHeader(4, 4, "DXT5", 0x4, 0, 1, 0x1000);
        const uint8_t block[16] = {0xFF, 0x00, 136, 6, 0, 0, 0, 0,       // alpha bytes
                                   0x00, 0xF8, 0xE0, 0x07, 0x00, 0x00, 0x00, 0x00};  // red/green, idx 0
        d.insert(d.end(), block, block + 16);
        DdsImage img;
        CHECK(decodeDds(d, img, &err));
        CHECK_EQ(img.rgba[0], 255);
        CHECK_EQ(img.rgba[3], 255);  // idx 0 -> a0
        CHECK_EQ(img.rgba[7], 0);    // idx 1 -> a1
        CHECK_EQ(img.rgba[11], 218); // idx 2 -> (6a0+a1)/7
        CHECK_EQ(img.rgba[15], 182); // idx 3 -> (5a0+2a1)/7
    }

    // --- DXT3: explicit 4-bit alpha, nibble y*4+x
    {
        std::vector<uint8_t> d = makeDdsHeader(4, 4, "DXT3", 0x4, 0, 1, 0x1000);
        uint8_t block[16] = {};
        // DXT3 packs 16 alpha nibbles in pixel order, low nibble of each byte first.
        block[0] = 0x0F;  // pixels 0,1 of row 0 -> 0xF, 0x0
        block[8] = 0x00; block[9] = 0xF8; block[10] = 0xE0; block[11] = 0x07;  // red / green
        d.insert(d.end(), block, block + 16);
        DdsImage img;
        CHECK(decodeDds(d, img, &err));
        CHECK_EQ(img.rgba[3], 255);  // 0xF * 17
        CHECK_EQ(img.rgba[7], 0);
    }

    // --- uncompressed 32 bpp BGRA with DDPF_ALPHAPIXELS (flags 0x41, what most tools write): must not be read as alpha-only
    {
        std::vector<uint8_t> d = makeDdsHeader(2, 1, "\0\0\0\0", 0x41, 32, 1, 0x1000);
        auto put32 = [&](size_t off, uint32_t v) {
            for (int i = 0; i < 4; ++i) d[off + i] = static_cast<uint8_t>(v >> (8 * i));
        };
        put32(92, 0x00FF0000u);
        put32(96, 0x0000FF00u);
        put32(100, 0x000000FFu);
        const uint8_t px[8] = {0x00, 0x00, 0xFF, 0x80, 0x00, 0xFF, 0x00, 0xFF};
        d.insert(d.end(), px, px + 8);
        DdsImage img;
        CHECK(decodeDds(d, img, &err));
        CHECK_EQ(std::string(img.formatName()), std::string("BGRA8"));
        CHECK_EQ(img.rgba[0], 255);  // B,G,R,A = 00,00,FF,80 -> red
        CHECK_EQ(img.rgba[3], 128);
    }

    // --- uncompressed 32 bpp BGRA
    {
        std::vector<uint8_t> d = makeDdsHeader(2, 1, "\0\0\0\0", 0x40, 32, 1, 0x1000);
        // makeDdsHeader writes R=0xFF,G=0xFF00,B=0xFF0000 (RGBA byte order); swap the
        // masks so the header really describes a BGRA file and exercise that path.
        auto put32 = [&](size_t off, uint32_t v) {
            for (int i = 0; i < 4; ++i) d[off + i] = static_cast<uint8_t>(v >> (8 * i));
        };
        put32(92, 0x00FF0000u);  // R mask
        put32(96, 0x0000FF00u);  // G mask
        put32(100, 0x000000FFu); // B mask
        const uint8_t px[8] = {0xFF, 0x00, 0x00, 0x80, 0x00, 0xFF, 0x00, 0xFF};
        d.insert(d.end(), px, px + 8);
        DdsImage img;
        CHECK(decodeDds(d, img, &err));
        CHECK_EQ(img.width, 2);
        CHECK_EQ(std::string(img.formatName()), std::string("BGRA8"));
        CHECK_EQ(img.rgba[0], 0);    // file is B,G,R,A so R = px[2] = 0
        CHECK_EQ(img.rgba[2], 255);  // B = px[0] = 255
        CHECK_EQ(img.rgba[3], 128);
    }

    // --- mip selection: 8x8 DXT1 with 2 levels, ask for a preview <= 4
    {
        std::vector<uint8_t> d = makeDdsHeader(8, 8, "DXT1", 0x4, 0, 2, 0x1000 | 0x8 | 0x400000);
        d.resize(128 + 32 + 8, 0);  // level 0 = 2x2 blocks * 8B, level 1 = 8B
        DdsImage full, small;
        CHECK(decodeDds(d, full, &err));
        CHECK_EQ(full.width, 8);
        CHECK(decodeDds(d, small, &err, 4));
        CHECK_EQ(small.width, 4);
        CHECK_EQ(small.height, 4);
    }

    // --- truncated file
    {
        std::vector<uint8_t> d = makeDdsHeader(4, 4, "DXT5", 0x4, 0, 1, 0x1000);
        DdsImage img;
        CHECK(!decodeDds(d, img, &err));
        CHECK_EQ(std::string(err), std::string("file is truncated"));
    }

    // --- resample / fit
    {
        const uint8_t src[16] = {0, 0, 0, 255, 255, 255, 255, 255, 100, 100, 100, 255, 50, 50, 50, 255};
        std::vector<uint8_t> one = resampleRgba(src, 2, 2, 1, 1);
        CHECK_EQ(one.size(), static_cast<size_t>(4));
        CHECK_EQ(one[0], 101);  // (0 + 255 + 100 + 50) / 4
        int w = 0, h = 0;
        fitInto(3750, 2000, 900, 500, w, h);
        CHECK_EQ(w, 900);
        CHECK_EQ(h, 480);
        fitInto(100, 100, 900, 500, w, h);
        CHECK_EQ(w, 100);  // never upscaled
    }
}

// ---- tile sprites (.spr.msb): a synthetic file first, then the user's real loading_<lang> when ZDROID_TEST_SPR is set
static void sprPutU64(std::vector<uint8_t>& v, uint64_t x) { for (int i = 7; i >= 0; --i) v.push_back(static_cast<uint8_t>(x >> (8 * i))); }
static void sprPutU32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 3; i >= 0; --i) v.push_back(static_cast<uint8_t>(x >> (8 * i))); }
static void sprPutStr(std::vector<uint8_t>& v, const std::string& s) { sprPutU64(v, s.size()); v.insert(v.end(), s.begin(), s.end()); }
static void sprPutF32(std::vector<uint8_t>& v, float f) { uint32_t b; std::memcpy(&b, &f, 4); sprPutU32(v, b); }

static std::vector<uint8_t> makeSpr() {
    std::vector<uint8_t> d;
    sprPutStr(d, "2.0");
    sprPutStr(d, "unit");
    sprPutU32(d, 0);
    sprPutStr(d, "unigned char tile_x, tile_y, tile_u, tile_v;");
    sprPutU64(d, 4);   // bytes per tile
    sprPutU64(d, 5);   // tiles
    sprPutU64(d, 2);   // frames
    sprPutU64(d, 2);
    sprPutU64(d, 4);   // 4x4 tiles
    sprPutU64(d, 4);
    const uint8_t tiles[5][4] = {{0, 0, 1, 0}, {1, 0, 0, 1}, {0, 1, 1, 1}, {0, 0, 0, 0}, {2, 1, 1, 0}};
    for (const auto& t : tiles) d.insert(d.end(), t, t + 4);
    sprPutU32(d, 0); sprPutU32(d, 3); sprPutU32(d, 4); sprPutF32(d, -10.0f); sprPutF32(d, 5.5f);
    sprPutU32(d, 3); sprPutU32(d, 2); sprPutU32(d, 4); sprPutF32(d, 1.0f); sprPutF32(d, 2.0f);
    sprPutStr(d, "first");  sprPutU32(d, 0); sprPutU32(d, 1); sprPutU64(d, 0);
    sprPutStr(d, "second"); sprPutU32(d, 1); sprPutU32(d, 1); sprPutU64(d, 1);
    return d;
}



// ============================================================ custom palettes + the mods/ folder
static palmod::PalFile makeFiliaPal() {
    palmod::PalFile p;
    p.character = "Filia";
    const std::vector<int>* rc = palmod::regionCounts("filia");
    size_t n = 0;
    for (int c : *rc) n += static_cast<size_t>(c);
    p.numColors = static_cast<uint32_t>(n + 1);
    for (size_t i = 0; i < n; ++i) p.colors.push_back(0xFF000000u | static_cast<uint32_t>((i * 2) << 16) | static_cast<uint32_t>(i));
    p.line = 0;
    p.shadow1 = 0xFFF2E3F0u;
    p.shadow2 = 0xFF30273Bu;
    return p;
}

static void testPalmod() {
    std::printf("[custom palettes: .pal parser + ramp]\n");
    CHECK_EQ(palmod::normaliseName("Ms. Fortune"), std::string("msfortune"));
    CHECK(palmod::regionCounts("MsFortune") != nullptr);
    CHECK(palmod::regionCounts("nobody") == nullptr);
    // every character of the table: the key colours of all regions add up to what a .pal of that character stores
    static const char* kAll[] = {"filia", "cerebella", "peacock", "parasoul", "msfortune", "painwheel", "valentine", "double", "squigly",
                                 "bigband", "eliza", "fukua", "beowulf", "robofortune", "annie", "umbrella", "blackdahlia", "marie"};
    for (const char* c : kAll) CHECK(palmod::regionCounts(c) != nullptr);
    CHECK_EQ(palmod::regionCounts("filia")->size(), size_t(24));

    const palmod::PalFile p = makeFiliaPal();
    CHECK_EQ(p.colors.size(), size_t(120));
    const std::vector<uint8_t> bytes = palmod::serialize(p);
    CHECK_EQ(bytes.size(), size_t(16 + 4 + 2 + 120 * 4 + 12));
    CHECK_EQ(bytes[16], uint8_t(121));
    palmod::PalFile q;
    std::string err;
    CHECK(palmod::parse(bytes.data(), bytes.size(), q, &err));
    CHECK_EQ(q.character, std::string("Filia"));
    CHECK_EQ(q.numColors, uint32_t(121));
    CHECK(q.colors == p.colors);
    CHECK_EQ(q.shadow2, p.shadow2);
    CHECK(!palmod::parse(bytes.data(), 30, q, &err));          // truncated
    CHECK(!palmod::parse(bytes.data(), bytes.size() - 4, q, &err));

    Palette pal;
    CHECK(palmod::toPalette(p, pal, &err));
    CHECK_EQ(pal.tones, 16);
    CHECK_EQ(pal.bands, 25);
    // region 0 holds keys 0..4 (light -> dark): tone 15 = key 0, tone 0 = key 4, tone 4 (15*1/4 = 3.75) is between keys 4 and 3
    CHECK_EQ(int(pal.at(1, 15)[2]), 0);   // key 0 = (i=0): R=0 G=0 B=0 ... colour bytes: R = (i*2), B = i
    CHECK_EQ(int(pal.at(1, 0)[2]), 4);    // key 4 -> B = 4
    CHECK_EQ(int(pal.at(1, 0)[0]), 8);    // key 4 -> R = 8
    CHECK(int(pal.at(1, 1)[2]) <= 4 && int(pal.at(1, 1)[2]) >= 3);  // between key 4 (B 4) and key 3 (B 3)
    for (int b = 1; b < pal.bands; ++b)
        for (int t = 0; t + 1 < 16; ++t) CHECK(std::abs(int(pal.at(b, t)[2]) - int(pal.at(b, t + 1)[2])) <= 1);  // smooth ramp
    // an unknown character / a palette of another build is refused
    palmod::PalFile bad = p;
    bad.character = "Nobody";
    CHECK(!palmod::toPalette(bad, pal, &err));
    bad = p;
    bad.colors.pop_back();
    bad.numColors -= 1;
    CHECK(!palmod::toPalette(bad, pal, &err));
    CHECK(err.find("another version") != std::string::npos);

    // the baked .dds is a normal palette texture again
    std::vector<uint8_t> dds;
    CHECK(palmod::toDds(p, dds, &err));
    CHECK_EQ(dds.size(), size_t(128 + 16 * 25 * 4));
    Palette back;
    CHECK(readDds(dds.data(), dds.size(), back, &err));
    CHECK_EQ(back.bands, 25);
    CHECK(back.rgb == pal.rgb);
    palmod::PalFile outline = p;
    outline.line = 0xFF336699u;
    CHECK(palmod::toPalette(outline, pal, &err));
    CHECK_EQ(int(pal.at(0, 3)[0]), 0x33);  // outline colour lands in row 0
    CHECK_EQ(int(pal.at(0, 3)[2]), 0x99);
}

// ============================================================ Claude palette generator (src/core/palgen.cpp)
static double palgenLuma(uint32_t argb) {
    return (0.299 * ((argb >> 16) & 0xFF) + 0.587 * ((argb >> 8) & 0xFF) + 0.114 * (argb & 0xFF)) / 255.0;
}

static void testPalgen() {
    std::printf("[palette generator: Claude theme]\n");
    CHECK(palgen::rampsValid());

    // a synthetic Filia palette (24 regions, 16 tones): every row is the base colour scaled from 55 % (tone 0) to 100 % (tone 15), the white one from 88 %
    auto rowColour = [](int row, int tone, int out[3]) {
        static const int base[][3] = {{0, 0, 0},       {150, 150, 150}, {40, 30, 60},   {235, 235, 225}, {255, 200, 160}, {200, 20, 30},
                                      {60, 160, 80},   {60, 160, 80},   {60, 160, 80},  {60, 160, 80},   {60, 160, 80},   {60, 160, 80},
                                      {60, 160, 80},   {60, 160, 80},   {60, 160, 80},  {60, 160, 80},   {200, 20, 30},   {60, 160, 80},
                                      {60, 160, 80},   {60, 160, 80},   {60, 160, 80},  {60, 160, 80},   {60, 160, 80},   {60, 160, 80},
                                      {60, 160, 80}};
        const double f = row == 3 ? 0.88 + 0.12 * tone / 15.0 : 0.55 + 0.45 * tone / 15.0;   // the white row stays light in every tone
        for (int k = 0; k < 3; ++k) out[k] = static_cast<int>(base[row][k] * f);
    };
    Palette src;
    src.tones = 16;
    src.bands = 25;
    src.rgb.assign(static_cast<size_t>(16) * 25 * 3, 0);
    for (int row = 1; row < 25; ++row)
        for (int t = 0; t < 16; ++t) {
            int c[3];
            rowColour(row, t, c);
            for (int k = 0; k < 3; ++k) src.rgb[(static_cast<size_t>(row) * 16 + t) * 3 + k] = static_cast<uint8_t>(c[k]);
        }
    auto classifyRow = [&](int row) { return palgen::classify(&src.rgb[static_cast<size_t>(row) * 16 * 3]); };
    CHECK(classifyRow(1) == palgen::Role::Cloth);   // a grey
    CHECK(classifyRow(2) == palgen::Role::Black);   // nearly black
    CHECK(classifyRow(3) == palgen::Role::White);   // nearly white
    CHECK(classifyRow(4) == palgen::Role::Skin);    // a skin tone
    CHECK(classifyRow(6) == palgen::Role::Cloth);   // a green

    const palgen::Hints fh = palgen::hintsFor("filia");
    CHECK_EQ(fh.eyes.size(), size_t(1));
    CHECK_EQ(fh.ties.size(), size_t(1));
    CHECK(palgen::hintsFor("nobody").eyes.empty());

    palmod::PalFile pal;
    palgen::Report rep;
    std::string err;
    CHECK(palgen::generate("Filia", src, palgen::Theme::Claude, pal, &rep, &err));
    CHECK_EQ(pal.character, std::string("Filia"));
    CHECK_EQ(rep.regions, 24);
    CHECK_EQ(rep.ties, 1);
    int total = 0;
    for (int n : rep.perRole) total += n;
    CHECK_EQ(total, 24);
    CHECK_EQ(rep.perRole[static_cast<int>(palgen::Role::Eye)], 1);

    // it is a real .pal: serialises, parses back and bakes with the region table
    const std::vector<uint8_t> bytes = palmod::serialize(pal);
    palmod::PalFile back;
    CHECK(palmod::parse(bytes.data(), bytes.size(), back, &err));
    CHECK(back.colors == pal.colors);
    Palette baked;
    CHECK(palmod::toPalette(back, baked, &err));
    CHECK_EQ(baked.bands, 25);
    CHECK(rep.meanErr < 3.0);

    // the keys of every region run light -> dark, and the roles keep their order: white above grey above black
    const std::vector<int>& counts = *palmod::regionCounts("filia");
    std::vector<size_t> first;   // index of each region's first key
    size_t at = 0;
    for (int c : counts) {
        first.push_back(at);
        for (int i = 0; i + 1 < c; ++i) CHECK(palgenLuma(pal.colors[at + i]) >= palgenLuma(pal.colors[at + i + 1]) - 1e-9);
        at += static_cast<size_t>(c);
    }
    const double black = palgenLuma(pal.colors[first[1]]);    // row 2
    const double cloth = palgenLuma(pal.colors[first[0]]);    // row 1 (row index = region + 1)
    const double white = palgenLuma(pal.colors[first[2]]);    // row 3
    CHECK(black < cloth);
    CHECK(cloth < white);
    CHECK(black < 0.30);
    CHECK(white > 0.85);
    // the iris (row 5) is orange: red > green > blue; the tie (row 16, red in the original) is black
    const uint32_t eye = pal.colors[first[4]];
    CHECK(((eye >> 16) & 0xFF) > ((eye >> 8) & 0xFF) && ((eye >> 8) & 0xFF) > (eye & 0xFF));
    CHECK(palgenLuma(pal.colors[first[15]]) < 0.30);
    // every key is opaque
    for (uint32_t c : pal.colors) CHECK_EQ(c >> 24, 0xFFu);

    // the dark variant is darker for the greys and the whites
    palmod::PalFile dark;
    CHECK(palgen::generate("Filia", src, palgen::Theme::ClaudeDark, dark, nullptr, &err));
    CHECK(palgenLuma(dark.colors[first[0]]) < cloth);
    CHECK(palgenLuma(dark.colors[first[2]]) < white);

    // refusals: a texture of another build, an unknown name
    Palette shortSrc = src;
    shortSrc.bands = 24;
    shortSrc.rgb.resize(static_cast<size_t>(16) * 24 * 3);
    CHECK(!palgen::generate("Filia", shortSrc, palgen::Theme::Claude, pal, nullptr, &err));
    CHECK(!palgen::generate("Nobody", src, palgen::Theme::Claude, pal, nullptr, &err));
    CHECK(std::string(palgen::themeLabel(palgen::Theme::ClaudeDark)) == "ClaudeDark");
}

static void testModsDir() {
    std::printf("[mods/ folder: layout, mount order, palettes]\n");
    const std::string game = makeTempDir("modsdir");
    CHECK(modsdir::ensure(game));
    const modsdir::Layout l = modsdir::layout(game);
    for (const std::string* d : {&l.pal, &l.custom, &l.stages, &l.jpVoice, &l.packs}) {
        CHECK(fs::is_directory(*d));
        CHECK(fs::exists(fs::path(*d) / "README.txt"));
    }
    CHECK(modsdir::ensure(game));   // idempotent
    writeText(game + "/data/x/hello.txt", "base");
    writeText(l.packs + "/zpack/x/hello.txt", "pack");
    writeText(l.packs + "/zpack/pack.ini", "name = Z pack\npriority = 50\n");
    writeText(l.custom + "/amod/x/hello.txt", "mod");
    writeText(l.custom + "/amod/x/only_mod.txt", "m");
    writeText(l.stages + "/arena/arena.lvl", "StageSize 100 100\n");
    writeText(l.stages + "/arena/stage.ini", "name = Cool Arena\n");
    writeText(l.jpVoice + "/UISound/JP_chars/v.wav", "jp");
    {   // the game folder is never indexed through mods/ (Vfs skips it)
        Vfs v;
        v.mount(game);
        std::string t;
        CHECK(v.readText("hello.txt", t));
        CHECK_EQ(t, std::string("base"));
        CHECK(!v.exists("arena.lvl"));
    }
    palmod::PalFile p = makeFiliaPal();
    const std::vector<uint8_t> pb = palmod::serialize(p);
    writeText(l.pal + "/sub/FiliaTest.pal", std::string(pb.begin(), pb.end()));
    palmod::PalFile other = p;
    other.character = "Cerebella";                       // right name, wrong number of colours
    const std::vector<uint8_t> ob = palmod::serialize(other);
    writeText(l.pal + "/Wrong.pal", std::string(ob.begin(), ob.end()));
    writeText(l.pal + "/junk.pal", "short");

    {
        Vfs v;
        v.mount(game);
        const modsdir::Report r = modsdir::mountAll(v, game, false);
        CHECK(r.layoutOk);
        CHECK_EQ(r.packs, size_t(1));
        CHECK_EQ(r.mods, size_t(1));
        CHECK_EQ(r.stages, size_t(1));
        CHECK_EQ(r.voiceFiles, size_t(0));
        CHECK_EQ(r.stageMods.size(), size_t(1));
        CHECK_EQ(r.stageMods[0].id, std::string("arena"));
        CHECK_EQ(r.stageMods[0].name, std::string("Cool Arena"));   // from stage.ini
        std::string t;
        CHECK(v.readText("x/hello.txt", t));
        CHECK_EQ(t, std::string("mod"));               // game < pack < mod
        CHECK(v.exists("arena.lvl"));
        CHECK(!v.exists("v.wav"));                     // Japanese voices are off
        CHECK_EQ(r.pal.entries.size(), size_t(1));     // the two bad files are skipped
        CHECK_EQ(r.pal.warnings.size(), size_t(2));
        CHECK_EQ(r.pal.entries[0].character, std::string("filia"));
        CHECK_EQ(r.pal.entries[0].label, std::string("FiliaTest"));
        CHECK(fs::exists(r.pal.entries[0].dds));
        Palette pal;
        CHECK(GameData::loadPalette(r.pal.entries[0].dds, pal));
        CHECK_EQ(pal.bands, 25);
    }
    {
        Vfs v;
        v.mount(game);
        const modsdir::Report r = modsdir::mountAll(v, game, true);
        CHECK_EQ(r.voiceFiles, size_t(1));
        CHECK(v.exists("UISound/JP_chars/v.wav"));
    }
    writeText(l.root + "/mods.ini", "disabled = amod\n");
    {
        Vfs v;
        v.mount(game);
        const modsdir::Report r = modsdir::mountAll(v, game, false);
        CHECK_EQ(r.mods, size_t(0));
        std::string t;
        CHECK(v.readText("x/hello.txt", t));
        CHECK_EQ(t, std::string("pack"));              // the pack shows through once the mod is off
    }
    writeText(l.root + "/mods.ini", "enabled = 0\n");
    {
        Vfs v;
        v.mount(game);
        const modsdir::Report r = modsdir::mountAll(v, game, false);
        CHECK_EQ(r.packs + r.mods + r.stages, size_t(0));
        CHECK(r.pal.entries.empty());
    }
    writeText(l.root + "/mods.ini", "japanese_voices = 1\n");
    {
        Vfs v;
        v.mount(game);
        CHECK_EQ(modsdir::mountAll(v, game, false).voiceFiles, size_t(1));   // forced on by the file
    }
    // GameData lists the baked palette after the game's own
    {
        const std::string g2 = makeTempDir("modsdir2");
        writeText(g2 + "/characters/filia.foit", "x");
        writeText(g2 + "/characters/filia_1p.dds", "x");
        const modsdir::Layout l2 = modsdir::layout(g2);
        modsdir::ensure(g2);
        writeText(l2.pal + "/Mine.pal", std::string(pb.begin(), pb.end()));
        GameData gd;
        gd.mount(g2);
        gd.mountModsDir(g2, false);
        const Inventory inv = gd.scan();
        CHECK_EQ(inv.characters.size(), size_t(1));
        CHECK_EQ(inv.characters[0].palettes.size(), size_t(2));
        CHECK_EQ(inv.characters[0].palettes[0].first, std::string("1p"));
        CHECK_EQ(inv.characters[0].palettes[1].first, std::string("mod:Mine"));
        fs::remove_all(g2);
    }
    fs::remove_all(game);
}

// ZDROID_TEST_PAL = folder with the community's FiliaInaccurateAfterSamson.pal and Filia's own filia_1p.dds
static void testPalmodReal(const std::string& dir) {
    std::printf("[custom palettes: real Filia .pal vs the game's filia_1p.dds]\n");
    std::vector<uint8_t> pb, db;
    if (!readFile(dir + "/FiliaInaccurateAfterSamson.pal", pb) || !readFile(dir + "/filia_1p.dds", db)) { skip("files missing in ZDROID_TEST_PAL"); return; }
    palmod::PalFile pf;
    std::string err;
    CHECK(palmod::parse(pb.data(), pb.size(), pf, &err));
    CHECK_EQ(pf.character, std::string("Filia"));
    CHECK_EQ(pf.numColors, uint32_t(121));
    CHECK_EQ(pf.colors.size(), size_t(120));
    CHECK(palmod::serialize(pf) == pb);        // byte-exact round trip
    Palette mine, game;
    CHECK(palmod::toPalette(pf, mine, &err));
    CHECK(readDds(db.data(), db.size(), game, &err));
    CHECK_EQ(mine.bands, game.bands);
    CHECK_EQ(mine.tones, game.tones);
    int exact = 0, total = 0, rowsOk = 0;
    for (int b = 1; b < game.bands; ++b) {
        int worst = 0;
        for (int t = 0; t < 16; ++t)
            for (int ch = 0; ch < 3; ++ch) {
                const int d = std::abs(int(mine.at(b, t)[ch]) - int(game.at(b, t)[ch]));
                worst = std::max(worst, d);
                ++total;
                exact += d == 0;
            }
        rowsOk += worst <= 1;
    }
    // 21 of the 24 regions are the stock colours (the community file is "inaccurate" in 3: skin, shirt, skirt) and match to +-1
    CHECK(rowsOk >= 21);
    CHECK(exact * 100 >= total * 85);
    std::printf("  regions matching the game's palette (+-1): %d / 24, identical channels: %d / %d\n", rowsOk, exact, total);
}

static void testModsItems() {
    std::printf("[mods/ folder: items list and on/off switch]\n");
    const std::string game = makeTempDir("modsitems");
    modsdir::ensure(game);
    const modsdir::Layout l = modsdir::layout(game);
    writeText(l.packs + "/zpack/x/a.txt", "pack");
    writeText(l.packs + "/zpack/pack.ini", "name = Z pack\nversion = 2\nauthor = me\n");
    writeText(l.custom + "/amod/x/b.txt", "mod");
    writeText(l.custom + "/offmod/x/c.txt", "mod");
    writeText(l.custom + "/offmod/mod.ini", "# keep this comment\nname = Off mod\nenabled = 0\n");
    writeText(l.stages + "/arena/arena.lvl", "StageSize 1 1\n");
    {
        Vfs v;
        const modsdir::Report r = modsdir::mountAll(v, game, false);
        CHECK_EQ(r.items.size(), size_t(4));   // packs, then mods, then stages: every folder, on or off
        CHECK_EQ(r.items[0].kind, std::string("pack"));
        CHECK_EQ(r.items[0].name, std::string("Z pack"));
        CHECK_EQ(r.items[0].version, std::string("2"));
        CHECK(r.items[0].enabled && r.items[0].mounted);
        bool sawOff = false;
        for (const modsdir::Item& it : r.items)
            if (it.id == "offmod") {
                sawOff = true;
                CHECK(!it.enabled && !it.mounted);
            }
        CHECK(sawOff);
        CHECK(r.allEnabled);
        CHECK_EQ(r.mods, size_t(1));
        for (const modsdir::Item& it : r.items)
            if (it.id == "amod") {
                CHECK(modsdir::setItemEnabled(game, it, false));   // goes to mods.ini disabled =
            }
    }
    {
        Vfs v;
        const modsdir::Report r = modsdir::mountAll(v, game, false);
        CHECK_EQ(r.mods, size_t(0));
        for (const modsdir::Item& it : r.items)
            if (it.id == "amod") CHECK(!it.enabled && !it.mounted);
        for (const modsdir::Item& it : r.items) {
            if (it.id == "amod") CHECK(modsdir::setItemEnabled(game, it, true));
            if (it.id == "offmod") CHECK(modsdir::setItemEnabled(game, it, true));   // its own mod.ini enabled = 0 goes back on
        }
    }
    {
        Vfs v;
        const modsdir::Report r = modsdir::mountAll(v, game, false);
        CHECK_EQ(r.mods, size_t(2));
        std::string ini;
        {
            std::ifstream f(l.custom + "/offmod/mod.ini", std::ios::binary);
            std::ostringstream ss;
            ss << f.rdbuf();
            ini = ss.str();
        }
        CHECK(!ini.empty());
        CHECK(ini.find("# keep this comment") != std::string::npos);   // the author's lines are kept
        CHECK(ini.find("enabled = 1") != std::string::npos);
    }
    writeText(l.root + "/mods.ini", "enabled = 0\n");
    {
        Vfs v;
        const modsdir::Report r = modsdir::mountAll(v, game, false);
        CHECK(!r.allEnabled);
        CHECK_EQ(r.items.size(), size_t(4));   // still listed, nothing mounted
        CHECK_EQ(r.packs + r.mods + r.stages, size_t(0));
    }
}

static void testPalOrganise() {
    std::printf("[mods/pal organised by character]\n");
    const fs::path dir = fs::temp_directory_path() / "zdroid_pal_org_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    auto writePal = [&](const char* name, const char* who) {
        palmod::PalFile pf;
        pf.character = who;
        pf.numColors = 3;
        pf.colors = {0xff000000u, 0xffffffffu};
        const std::vector<uint8_t> b = palmod::serialize(pf);
        std::ofstream(dir / name, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    };
    writePal("a.pal", "MsFortune");
    writePal("b.pal", "Filia");
    writePal("c.pal", "Nobody");
    { std::ofstream(dir / "junk.pal") << "not a palette"; }
    CHECK_EQ(palmod::organise(dir.string()), 2);
    CHECK(fs::exists(dir / "MsFortune" / "a.pal"));
    CHECK(fs::exists(dir / "Filia" / "b.pal"));
    CHECK(fs::exists(dir / "c.pal"));      // unknown character: stays
    CHECK(fs::exists(dir / "junk.pal"));   // not a palette: stays
    writePal("b.pal", "Filia");            // same name again: never overwritten
    CHECK_EQ(palmod::organise(dir.string()), 0);
    CHECK(fs::exists(dir / "b.pal"));
    CHECK_EQ(palmod::rosterFolders().size(), size_t(19));
    fs::remove_all(dir, ec);
}

static void testStoryVoice() {
    std::printf("[story voice-over file names]\n");
    auto v = storyVoiceFiles("Filia Intro 100", "en");
    CHECK_EQ(v.size(), size_t(1));
    CHECK_EQ(v[0], std::string("story_vo/lang_en/FiliaIntro100_EN.wav"));
    v = storyVoiceFiles("Filia Pre-Middle 100", "jp");   // '-' kept first, then the spelling without it
    CHECK_EQ(v.size(), size_t(2));
    CHECK_EQ(v[0], std::string("story_vo/lang_jp/FiliaPre-Middle100_JP.wav"));
    CHECK_EQ(v[1], std::string("story_vo/lang_jp/FiliaPreMiddle100_JP.wav"));
    v = storyVoiceFiles("Filia Intro 100", "en", 2);   // multi-file line: "_2_" instead of "_"
    CHECK_EQ(v[0], std::string("story_vo/lang_en/FiliaIntro100_2_EN.wav"));
    CHECK(storyVoiceFiles("_RAW_Whatever 1", "en").empty());
    CHECK(storyVoiceFiles("   ", "en").empty());
    CHECK(storyVoiceFiles("Filia Intro 100", "en", 4).empty());
}

static void testStorySynthetic() {
    std::printf("[story scripts: parser]\n");
    const CutScript s = parseCutscene(
        "Scene: CutsceneCommon \t\t\nView: Filia_Legacy_Intro\t\nResetAnim: OnDeactivate\n\n"
        "ShotTex: Shot_02 Filia intro10.dxt1   # trailing\n"
        "Msg #DIALOGUE_ON\tnote without a colon\n"
        "#Text: Filia Intro 600\n"
        "Anim: _LEGACY Intro\nText: Filia Intro 100\nHeader: Left Riccardo \nMsg: DIALOGUE_LEGACY_PLAY\n");
    CHECK_EQ(s.scene, std::string("CutsceneCommon"));
    CHECK_EQ(s.view, std::string("Filia_Legacy_Intro"));
    CHECK_EQ(s.cmds.size(), size_t(8));
    CHECK_EQ(s.cmds[3].key, std::string("ShotTex"));
    const auto w = splitWords(s.cmds[3].arg);
    CHECK_EQ(w.size(), size_t(3));
    CHECK_EQ(w[2], std::string("intro10.dxt1"));   // the inline comment is gone
    CHECK_EQ(s.cmds[5].arg, std::string("Filia Intro 100"));
    CHECK_EQ(s.cmds[6].arg, std::string("Left Riccardo"));

    const FightScript f = parseFightScript("Player 2\nFighter Parasoul\t\t#specifying Random ...\nColor 1\n\nWorld New_Meridian\nUseAI\t1\nNumRounds 1");
    CHECK_EQ(f.fighter(), std::string("Parasoul"));
    CHECK_EQ(f.world(), std::string("New_Meridian"));
    CHECK_EQ(f.get("NumRounds"), std::string("1"));
    CHECK_EQ(f.get("Nope", "x"), std::string("x"));

    const StmFile stm = parseStm("CharName Filia\n\nArcadeNode\nNodeType NewScene\nFileName arcade/story/A.ini\nTimelinePos 1\n\n"
                                 "ArcadeNode\nNodeType Fight\nFileName arcade/story/B.ini\nTimelinePos 2\nUnlockCondition Filia 1\nProgessionUnlock StoryFilia\n\n"
                                 "ArcadeNode\nNodeType Credits\nTimelinePos 3\nUnlockCondition Filia 2\n\nArcadeNode Complete");
    const auto nodes = storyNodes(stm);
    CHECK_EQ(nodes.size(), size_t(3));
    CHECK(nodes[0].type == StoryNode::Type::Scene);
    CHECK(nodes[1].type == StoryNode::Type::Fight);
    CHECK_EQ(nodes[1].unlockChar, std::string("Filia"));
    CHECK_EQ(nodes[1].unlockPos, 1);
    CHECK_EQ(nodes[1].progressionUnlock, std::string("StoryFilia"));
    CHECK(nodes[2].type == StoryNode::Type::Credits);
}

// Real data: every *.stm lists scripts that exist; every cutscene script parses and names a known animation; and the "legacy" cutscenes
// (a long animation of the view that waits at message cues for DIALOGUE_LEGACY_PLAY) have as many pause cues (ids eb030000 / eb030016) as the script has LEGACY_PLAY lines.
static void testStoryReal(const std::vector<std::string>& dirs) {
    std::printf("[story data on the real files]\n");
    GameData gd;
    for (const std::string& d : dirs) gd.mount(d);
    const Vfs& vfs = gd.vfs();
    std::vector<std::string> stms;
    for (const std::string& f : vfs.listByExt(".stm"))
        if (f.find("arcade/story") != std::string::npos || f.find("arcade\\story") != std::string::npos) stms.push_back(f);
    if (stms.empty()) { skip("no arcade/story/*.stm in the mounted data"); return; }
    std::vector<uint8_t> cc;
    GbsScene scene;
    const bool haveScene = vfs.read("CutsceneCommon.gbs", cc) && parseGbsScene(cc.data(), cc.size(), scene, nullptr);
    size_t scripts = 0, missing = 0, legacy = 0, legacyOk = 0, fights = 0;
    for (const std::string& f : stms) {
        std::string text;
        std::vector<uint8_t> raw;
        if (!readFile(f, raw)) continue;
        text.assign(raw.begin(), raw.end());
        const StmFile stm = parseStm(text);
        if (stm.charName.empty()) continue;   // (a *_CinematicsOnly.stm has none: skipped here)
        for (const StoryNode& n : storyNodes(stm)) {
            if (n.type == StoryNode::Type::Credits) continue;
            if (n.file.empty()) { CHECK(false); continue; }
            std::string body;
            if (!vfs.readText(n.file, body)) { ++missing; std::fprintf(stderr, "  %s: %s is not in the data\n", stm.charName.c_str(), n.file.c_str()); continue; }
            if (n.type == StoryNode::Type::Fight) {
                const FightScript fs = parseFightScript(body);
                CHECK(!fs.fighter().empty() || !fs.get("Player").empty());
                ++fights;
                continue;
            }
            const CutScript cs = parseCutscene(body);
            ++scripts;
            CHECK(!cs.cmds.empty());
            int plays = 0, texts = 0;
            std::string legacyAnim;
            for (const CutCmd& c : cs.cmds) {
                if (c.key == "Msg" && c.arg == "DIALOGUE_LEGACY_PLAY") ++plays;
                if (c.key == "Text") ++texts;
                if (c.key == "Anim") {
                    const auto w = splitWords(c.arg);
                    if (w.size() == 2 && w[0] == "_LEGACY") legacyAnim = w[1];
                }
            }
            CHECK(texts >= 0);
            if (!legacyAnim.empty() && haveScene) {
                const GbsSceneView* v = nullptr;
                for (const GbsSceneView& sv : scene.views) if (sv.name == cs.view) v = &sv;
                CHECK(v != nullptr);
                const GbsAnim* a = v ? v->anim(legacyAnim) : nullptr;
                CHECK(a != nullptr);
                if (!a) continue;
                int pauses = 0;
                for (const GbsSoundCue& m : a->messages) if (m.soundId == 0xeb030000u || m.soundId == 0xeb030016u) ++pauses;
                ++legacy;
                if (pauses == plays) ++legacyOk;
                else std::fprintf(stderr, "  %s: %d pause cues vs %d LEGACY_PLAY (the runner tolerates it)\n", n.file.c_str(), pauses, plays);
                CHECK(std::abs(pauses - plays) <= 1);
            }
        }
    }
    std::printf("  %zu cutscene scripts, %zu fight scripts, %zu missing, legacy cutscenes %zu (pause cues = LEGACY_PLAY in %zu)\n", scripts, fights, missing, legacy, legacyOk);
    CHECK(scripts > 100);
    CHECK_EQ(missing, size_t(0));
    CHECK(legacyOk * 8 >= legacy * 7);   // 14 of 16 match exactly, the other two are off by one
}

// tag bit 6: the grad strips only carry the tone bits of the even rows; the odd rows are rebuilt (Sprite Workshop decoder).
static void testArcadeLadder() {
    std::printf("[arcade ladder]\n");
    using arcade::Ladder;
    const std::vector<std::string> roster = {"filia", "cerebella", "peacock", "parasoul", "ms. fortune", "painwheel", "valentine",
                                             "double", "squigly", "bigband", "eliza", "fukua", "beowulf", "robofortune", "annie"};
    const std::vector<std::string> stages = {"new_meridian", "falling_wreckage", "marie_tower", "grand_opera", "the_bridge"};
    Ladder l;
    CHECK(!l.active());
    l.begin("filia", roster, stages, "BossMarie", "falling_wreckage", 1, 1234);
    CHECK(l.active());
    CHECK_EQ(l.total(), arcade::kRandomFights + 1);
    CHECK_EQ(l.index(), 0);
    CHECK(!l.atBoss());
    // the eight opponents never repeat, never the player, never the boss; stages never the boss's and never twice in a row
    std::vector<std::string> seen;
    std::string prevStage;
    Ladder walk = l;
    for (int i = 0; i < arcade::kRandomFights; ++i) {
        const arcade::Fight& f = walk.current();
        CHECK(!f.boss);
        CHECK(f.fighter != "filia" && f.fighter != "BossMarie");
        CHECK(std::find(seen.begin(), seen.end(), f.fighter) == seen.end());
        seen.push_back(f.fighter);
        CHECK(f.stage != "falling_wreckage" && !f.stage.empty());
        CHECK(f.stage != prevStage);
        prevStage = f.stage;
        CHECK_EQ(f.rounds, 0);
        CHECK(!walk.won());
    }
    CHECK(walk.atBoss());
    CHECK(walk.current().boss);
    CHECK(walk.current().fighter == "BossMarie");
    CHECK(walk.current().stage == "falling_wreckage");
    CHECK_EQ(walk.current().rounds, arcade::kBossRounds);
    CHECK_EQ(walk.current().difficulty, 2);
    CHECK(!walk.finished());
    CHECK(walk.won());          // the boss falls: the run is complete
    CHECK(walk.finished());
    CHECK(walk.won());          // asking again changes nothing
    CHECK_EQ(walk.index(), arcade::kRandomFights);

    // the same seed deals the same ladder, another seed another one
    Ladder again;
    again.begin("filia", roster, stages, "BossMarie", "falling_wreckage", 1, 1234);
    Ladder other;
    other.begin("filia", roster, stages, "BossMarie", "falling_wreckage", 1, 99);
    bool same = true, differs = false;
    Ladder a2 = again, o2 = other, l2 = l;
    for (int i = 0; i < arcade::kRandomFights; ++i) {
        if (a2.current().fighter != l2.current().fighter || a2.current().stage != l2.current().stage) same = false;
        if (o2.current().fighter != l2.current().fighter) differs = true;
        a2.won(); o2.won(); l2.won();
    }
    CHECK(same);
    CHECK(differs);

    // difficulty: one level up every three fights from (base - 1), never past hard
    CHECK_EQ(Ladder::difficultyFor(1, 0, false), 0);
    CHECK_EQ(Ladder::difficultyFor(1, 2, false), 0);
    CHECK_EQ(Ladder::difficultyFor(1, 3, false), 1);
    CHECK_EQ(Ladder::difficultyFor(1, 6, false), 2);
    CHECK_EQ(Ladder::difficultyFor(0, 7, false), 1);
    CHECK_EQ(Ladder::difficultyFor(2, 0, false), 1);
    CHECK_EQ(Ladder::difficultyFor(2, 7, false), 2);
    CHECK_EQ(Ladder::difficultyFor(0, 0, true), 2);

    // losing: try the same fight again, the index does not move
    Ladder c;
    c.begin("filia", roster, stages, "bossmarie", "falling_wreckage", 1, 7);
    c.won();
    const std::string third = [&] { Ladder t = c; t.won(); return t.current().fighter; }();
    const std::string now = c.current().fighter;
    c.retry();
    c.retry();
    CHECK_EQ(c.index(), 1);
    CHECK(c.current().fighter == now);
    CHECK_EQ(c.continuesUsed(), 2);
    c.won();
    CHECK(c.current().fighter == third);

    // a small roster: the player's own character fills in, and the same fighter never comes twice in a row
    Ladder small;
    small.begin("filia", {"filia", "peacock", "double"}, {"new_meridian"}, "BossMarie", "falling_wreckage", 1, 5);
    for (int i = 0; i < arcade::kRandomFights; ++i) {
        if (i > 0) {
            Ladder prev = small;
            CHECK(prev.current().fighter != "BossMarie");
        }
        small.won();
    }
    Ladder s2;
    s2.begin("filia", {"filia", "peacock", "double"}, {"new_meridian"}, "BossMarie", "falling_wreckage", 1, 5);
    std::string last;
    bool repeat = false;
    for (int i = 0; i < arcade::kRandomFights; ++i) {
        if (s2.current().fighter == last) repeat = true;
        last = s2.current().fighter;
        s2.won();
    }
    CHECK(!repeat);
    CHECK(s2.atBoss());

    // nothing to pick from: still a playable ladder (the boss at the end)
    Ladder none;
    none.begin("filia", {}, {}, "BossMarie", "falling_wreckage", 1, 3);
    CHECK_EQ(none.total(), arcade::kRandomFights + 1);
    CHECK(none.current().stage.empty());
}

static void testSavIni() {
    std::printf("[.savini save file]\n");
    const fs::path dir = fs::temp_directory_path() / "zdroid_savini_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    { std::ofstream(dir / "menu_settings.txt") << "OPT_STR_SPDIFFICULTY=Hard\nOPT_INT_MUSIC=40\n"; }
    { std::ofstream(dir / "zgirls_progress.txt") << "pos filia 7\ndone filia\nflag storydouble\n"; }

    SavIni a;
    a.open(dir.string());   // no .savini yet: the two older files are imported
    CHECK_EQ(a.get("options", "OPT_STR_SPDIFFICULTY"), std::string("Hard"));
    CHECK_EQ(a.getInt("options", "opt_int_music"), 40);   // keys are case-insensitive
    CHECK_EQ(a.getInt("story", "pos.filia"), 7);
    CHECK(a.isDone("StoryFilia"));
    CHECK(a.isDone("storydouble"));
    CHECK(fs::exists(dir / "zgirls.savini"));
    CHECK(!fs::exists(dir / "zgirls.savini.tmp"));

    // defaults from a ProgressionInit.txt text: letters -> [progression], digits -> [earned], anything else ignored; nothing overwritten
    const char* init = "StoryFilia = I\nStoryDouble = L\nStoryValentine = L\nFiliaWins = 0\nFiliaPals = 0000\nWeird = hello\n; c\n";
    CHECK_EQ(a.applyDefaults(init), 3);   // StoryFilia and StoryDouble were already there (imported)
    CHECK_EQ(a.get("progression", "StoryFilia"), std::string("C"));   // the save's own value wins over the default "I"
    CHECK_EQ(a.flagLetter("StoryValentine"), 'L');
    CHECK(!a.isDone("StoryValentine"));
    a.unlock("StoryValentine");
    CHECK_EQ(a.flagLetter("StoryValentine"), 'U');
    CHECK(a.isDone("StoryValentine"));
    CHECK_EQ(a.addCounter("FiliaWins"), 1);
    CHECK_EQ(a.addCounter("FiliaWins", 2), 3);
    CHECK_EQ(a.get("earned", "FiliaPals"), std::string("0000"));
    CHECK(!a.has("progression", "Weird") && !a.has("earned", "Weird"));
    bool spelled = false;   // the imported lower-case key took the game's spelling
    for (const SavIni::Entry& e : a.entries("progression")) spelled = spelled || e.key == "StoryDouble";
    CHECK(spelled);

    // a second run reads the same file back
    SavIni b;
    CHECK(b.load((dir / "zgirls.savini").string()));
    CHECK_EQ(b.counter("FiliaWins"), 3);
    CHECK_EQ(b.flagLetter("StoryValentine"), 'U');
    CHECK_EQ(b.get("options", "OPT_STR_SPDIFFICULTY"), std::string("Hard"));
    b.setAutosave(false);
    b.set("options", "OPT_X", "1");
    CHECK(b.erase("options", "OPT_X"));
    CHECK(!b.has("options", "OPT_X"));

    // text format: comments, blank lines, repeated key (last wins), keys outside a section ignored
    SavIni c;
    c.parse("stray=1\n; note\n[Options]\nA=1\nA=2\n\n# x\n[progression]\nStoryX = U\n");
    CHECK_EQ(c.getInt("options", "a"), 2);
    CHECK(c.isDone("storyx"));
    CHECK(!c.has("", "stray"));
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------------------- .lvl parser (mods may bring their own)
// The menus name a stage by its Character Select thumbnail ("rooftops_day", "casino"), the story scripts by its localisation id
// ("River_King"), the data by its .lvl ("rooftops_day_3d", "river_king_casino"): all of them must find the same level.
static void testResolveLevel() {
    std::printf("[stage id -> level]\n");
    std::vector<LevelInfo> lv;
    for (const char* n : {"arena", "arena_empty", "arena_Evo", "rooftops_day_3d", "river_king_casino", "training_3d", "maple_crest_3d",
                          "maple_crest_mare_3d", "bathhouse_3d", "bathhouseblood_3d", "atrium", "test1", "test2", "mymod"}) {
        LevelInfo l;
        l.name = n;
        lv.push_back(l);
    }
    const std::vector<StageEntry> table = parseStages(
        "Arena arena stage_arena\n"
        "Arena_Evo arena_Evo arena_Evo IsAlt>Arena ExcludeFromRandom\n"
        "_Empty_Arena arena_empty stage_arena_empty\n"
        "Meridian_Rooftops rooftops_day_3d  stage_rooftops_day\n"
        "River_King river_king_casino  stage_casino\n"
        "Training_Stage training_3d stage_training_stage\n"
        "Maple_Crest maple_crest_3d  stage_maple_crest\n"
        "Maple_Crest_Mare maple_crest_mare_3d stage_nightmare_crest\n"
        "Bathhouse bathhouse_3d stage_bathhouse\n"
        "Bathhouse_Sekh bathhouseblood_3d stage_bathhouse_sekh\n"
        "Glass_Canopy atrium stage_atrium\n");
    auto at = [&](const char* want) {
        const int i = resolveLevel(lv, table, want);
        return i < 0 ? std::string("(none)") : lv[static_cast<size_t>(i)].name;
    };
    CHECK_EQ(at("arena"), std::string("arena"));
    CHECK_EQ(at("arena_empty"), std::string("arena_empty"));
    CHECK_EQ(at("rooftops_day"), std::string("rooftops_day_3d"));         // thumbnail id
    CHECK_EQ(at("Meridian_Rooftops"), std::string("rooftops_day_3d"));    // localisation id
    CHECK_EQ(at("stage_rooftops_day"), std::string("rooftops_day_3d"));   // picture name
    CHECK_EQ(at("rooftops_day_3d"), std::string("rooftops_day_3d"));      // the .lvl
    CHECK_EQ(at("casino"), std::string("river_king_casino"));
    CHECK_EQ(at("River_King"), std::string("river_king_casino"));
    CHECK_EQ(at("training_stage"), std::string("training_3d"));
    CHECK_EQ(at("nightmare_crest"), std::string("maple_crest_mare_3d"));
    CHECK_EQ(at("Maple_Crest_Mare"), std::string("maple_crest_mare_3d"));
    CHECK_EQ(at("maple_crest"), std::string("maple_crest_3d"));            // not the Mare one
    CHECK_EQ(at("bathhouse"), std::string("bathhouse_3d"));
    CHECK_EQ(at("bathhouse_sekh"), std::string("bathhouseblood_3d"));
    CHECK_EQ(at("atrium"), std::string("atrium"));
    CHECK_EQ(at("MyMod"), std::string("mymod"));                           // a mod's stage: its folder / .lvl name
    CHECK_EQ(at("no_such_stage"), std::string("(none)"));
    CHECK_EQ(at(""), std::string("(none)"));
    // without a stages.ini the "_3d" suffix and an unambiguous fragment still find it
    CHECK_EQ(resolveLevel(lv, {}, "rooftops_day") >= 0 ? lv[static_cast<size_t>(resolveLevel(lv, {}, "rooftops_day"))].name : std::string("(none)"),
             std::string("rooftops_day_3d"));
    CHECK_EQ(resolveLevel(lv, {}, "crest") >= 0, false);                   // "crest" is in two levels: ambiguous, not a guess
    CHECK_EQ(resolveLevel(lv, {}, "rooftops") >= 0, true);                 // one level only: the fragment is enough
    // Random: only the stages the game lists as random-able, plus mods; never test1 / test2 / the Evo arena
    std::vector<std::string> pool;
    for (int i : randomLevels(lv, table)) pool.push_back(lv[static_cast<size_t>(i)].name);
    auto has = [&](const char* n) { return std::find(pool.begin(), pool.end(), std::string(n)) != pool.end(); };
    CHECK(has("arena") && has("casino") == false && has("river_king_casino") && has("mymod"));
    CHECK(!has("test1") && !has("test2") && !has("arena_Evo"));
    CHECK(!randomLevels(lv, {}).empty());
    CHECK(randomLevels({}, table).empty());
}

// A mod's stage with the same .lvl name as one of the game's replaces it: one level, the mod's.
static void testLevelShadowing() {
    std::printf("[level shadowing by a mod]\n");
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("zd_lvl_shadow_" + std::to_string(static_cast<long>(::getpid())));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "game" / "stages", ec);
    fs::create_directories(root / "mod" / "arena", ec);
    auto put = [](const fs::path& p, const char* text) { std::ofstream(p, std::ios::binary) << text; };
    put(root / "game" / "stages" / "arena.lvl", "StageSize: 3750 2000\nStart1: 1675\nStart2: 2075\n");
    put(root / "game" / "stages" / "other.lvl", "StageSize: 3750 2000\nStart1: 1675\nStart2: 2075\n");
    put(root / "mod" / "arena" / "arena.lvl", "StageSize: 5000 2000\nStart1: 2000\nStart2: 3000\n");
    GameData gd;
    gd.mount((root / "game").string());
    gd.mount((root / "mod").string());
    const Inventory inv = gd.scan();
    int arenas = 0, width = 0;
    for (const LevelInfo& l : inv.levels)
        if (l.name == "arena") ++arenas, width = l.width;
    CHECK_EQ(arenas, 1);
    CHECK_EQ(width, 5000);
    CHECK_EQ(inv.levels.size(), static_cast<size_t>(2));
    fs::remove_all(root, ec);
}

static void testLevelParser() {
    std::printf("[level parser]\n");
    const LevelInfo a = parseLevel("StageSize: 3750 2000\nStart1: 1675\nStart2: 2075\nShadowDist: 400\n"
                                   "Light: Amb 85 96 122  # ambient\nCAMERA 66\n2D stages/textures/2D/stage_x.dds\n", "x");
    CHECK_EQ(a.width, 3750);
    CHECK_EQ(a.start2, 2075);
    CHECK_EQ(a.shadowDist, 400);
    CHECK_EQ(a.background2D, std::string("stages/textures/2D/stage_x.dds"));
    CHECK(a.lights.size() == 1);
    // lines that carry no tokens (a lone comma used to erase() from an empty vector) and truncated keys must not crash or change anything
    for (const char* junk : {",\n", ",", " , \n,\n", ":\n", "Light:\n", "Light\n", "StageSize\nStart1:\n2D\nCAMERA\n", "\x01\x02,,,:::\n"}) {
        const LevelInfo b = parseLevel(junk, "j");
        CHECK_EQ(b.width, 3750);
        CHECK(b.background2D.empty());
    }
}

// ---------------------------------------------------------------------------------------- image scaling (sprite levels, enhanced backdrops)
static void testImgScale() {
    std::printf("[image scaling]\n");
    // a half-opaque red column next to transparent black: shrinking must not darken the red (premultiplied average)
    std::vector<uint8_t> px(4 * 4 * 4, 0);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 2; ++x) {
            uint8_t* p = &px[(static_cast<size_t>(y) * 4 + x) * 4];
            p[0] = 255; p[3] = 255;
        }
    int w = 0, h = 0;
    const std::vector<uint8_t> half = shrinkRgba(px.data(), 4, 4, 2, w, h);
    CHECK_EQ(w, 2);
    CHECK_EQ(h, 2);
    CHECK_EQ(static_cast<int>(half[0]), 255);       // the opaque side keeps its colour
    CHECK_EQ(static_cast<int>(half[3]), 255);
    CHECK_EQ(static_cast<int>(half[4 + 3]), 0);     // the transparent side stays transparent
    // an edge box covering one opaque and one transparent pixel: full red, half alpha (not 50 % red)
    std::vector<uint8_t> edge = {255, 0, 0, 255, 0, 0, 0, 0};
    const std::vector<uint8_t> e1 = shrinkRgba(edge.data(), 2, 1, 2, w, h);
    CHECK_EQ(w, 1);
    CHECK_EQ(static_cast<int>(e1[0]), 255);
    CHECK(e1[3] >= 127 && e1[3] <= 128);
    // odd sizes round up; factor 1 is a copy; nothing in, nothing out
    std::vector<uint8_t> odd(5 * 3 * 4, 200);
    shrinkRgba(odd.data(), 5, 3, 2, w, h);
    CHECK_EQ(w, 3);
    CHECK_EQ(h, 2);
    CHECK(shrinkRgba(odd.data(), 5, 3, 1, w, h) == odd);
    CHECK(shrinkRgba(nullptr, 5, 3, 2, w, h).empty());
    CHECK_EQ(w, 0);

    // 2x cubic: size doubles, a flat picture stays flat, a ramp stays monotonic, alpha is kept
    std::vector<uint8_t> flat(8 * 6 * 4);
    for (size_t i = 0; i < flat.size(); i += 4) { flat[i] = 90; flat[i + 1] = 120; flat[i + 2] = 200; flat[i + 3] = 255; }
    const std::vector<uint8_t> big = upscaleCubic2x(flat.data(), 8, 6);
    CHECK_EQ(big.size(), static_cast<size_t>(16 * 12 * 4));
    bool same = true;
    for (size_t i = 0; i < big.size(); i += 4) same = same && big[i] == 90 && big[i + 1] == 120 && big[i + 2] == 200 && big[i + 3] == 255;
    CHECK(same);
    std::vector<uint8_t> ramp(8 * 1 * 4);
    for (int x = 0; x < 8; ++x) { ramp[x * 4] = static_cast<uint8_t>(x * 30); ramp[x * 4 + 1] = ramp[x * 4 + 2] = 0; ramp[x * 4 + 3] = 255; }
    const std::vector<uint8_t> rbig = upscaleCubic2x(ramp.data(), 8, 1);
    bool mono = true;
    for (int x = 1; x < 16; ++x) mono = mono && rbig[static_cast<size_t>(x) * 4] >= rbig[static_cast<size_t>(x - 1) * 4];
    CHECK(mono);
    CHECK(upscaleCubic2x(flat.data(), 8, 6, 10).empty());   // over the pixel budget
    CHECK(upscaleCubic2x(nullptr, 8, 6).empty());

    // sharpen: flat stays flat, an edge gets more contrast, alpha untouched
    std::vector<uint8_t> f2 = flat;
    sharpenRgba(f2.data(), 8, 6, 0.8f);
    CHECK(f2 == flat);
    std::vector<uint8_t> step(8 * 3 * 4);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 8; ++x) {
            uint8_t* p = &step[(static_cast<size_t>(y) * 8 + x) * 4];
            p[0] = p[1] = p[2] = x < 4 ? 80 : 160;
            p[3] = 200;
        }
    sharpenRgba(step.data(), 8, 3, 1.0f);
    CHECK(step[(1 * 8 + 3) * 4] < 80);       // dark side of the edge gets darker
    CHECK(step[(1 * 8 + 4) * 4] > 160);      // bright side gets brighter
    CHECK_EQ(static_cast<int>(step[(1 * 8 + 3) * 4 + 3]), 200);

    // transparent pixels next to visible ones borrow their colour (alpha stays 0)
    std::vector<uint8_t> b(3 * 1 * 4, 0);
    b[0] = 10; b[1] = 20; b[2] = 30; b[3] = 255;
    bleedTransparent(b.data(), 3, 1, 2);
    CHECK_EQ(static_cast<int>(b[4]), 10);
    CHECK_EQ(static_cast<int>(b[5]), 20);
    CHECK_EQ(static_cast<int>(b[7]), 0);
    CHECK_EQ(static_cast<int>(b[8]), 10);    // two passes reach the second pixel
    CHECK_EQ(static_cast<int>(b[11]), 0);

    // the level a sprite is stored at
    CHECK_EQ(spriteLevelFor(1.95, 0), 0);    // a phone: every pixel
    CHECK_EQ(spriteLevelFor(1.0, 0), 0);
    CHECK_EQ(spriteLevelFor(0.60, 0), 0);
    CHECK_EQ(spriteLevelFor(0.50, 0), 1);
    CHECK_EQ(spriteLevelFor(0.30, 0), 1);
    CHECK_EQ(spriteLevelFor(0.25, 0), 2);
    CHECK_EQ(spriteLevelFor(0.01, 0), 3);
    CHECK_EQ(spriteLevelFor(1.95, 2), 2);    // Sprite quality = Low forces quarter size
    CHECK_EQ(spriteLevelFor(0.25, 1), 2);    // the floor never raises quality

    // the PC's flat stage backdrops are stored rotated (shader reads .argb): stored r,g,b,a = G,B,255,R
    {
        std::vector<uint8_t> rot = {20, 30, 255, 10,     // true colour R=10 G=20 B=30
                                    200, 100, 255, 90};  // true colour R=90 G=200 B=100
        CHECK(unrotateArgb(rot.data(), 2, 1));
        const std::vector<uint8_t> want = {10, 20, 30, 255, 90, 200, 100, 255};
        CHECK(rot == want);
        std::vector<uint8_t> normal = {10, 20, 30, 255, 90, 200, 100, 255};   // an ordinary picture is left alone
        const std::vector<uint8_t> copy = normal;
        CHECK(!unrotateArgb(normal.data(), 2, 1));
        CHECK(normal == copy);
        CHECK(!unrotateArgb(nullptr, 2, 1));
    }
}

// ---------------------------------------------------------------------------------------- Developer Options table
static void testDevOptions() {
    std::printf("[developer options]\n");
    using namespace devopt;
    std::vector<std::string> keys;
    for (int i = 0; i < kCount; ++i) {
        const Def& d = def(static_cast<Id>(i));
        CHECK(std::string(d.key).rfind("ZD_DEV_", 0) == 0);
        CHECK(std::find(keys.begin(), keys.end(), std::string(d.key)) == keys.end());   // unique keys
        keys.push_back(d.key);
        CHECK(d.nChoices >= 2 && d.nChoices <= kMaxChoices);
        CHECK(d.def >= 0 && d.def < d.nChoices);
        // every label, description and choice has English and Spanish text
        for (const bool es : {false, true}) {
            CHECK(text(std::string("dev.") + d.name, es) != nullptr);
            CHECK(text(std::string("dev.") + d.name + ".d", es) != nullptr);
            for (int c = 0; c < d.nChoices; ++c) CHECK(text(std::string("c.") + d.choices[c], es) != nullptr);
        }
        CHECK(std::string(text(std::string("dev.") + d.name, true)) != std::string(text(std::string("dev.") + d.name, false)));
    }
    CHECK(text("nope", false) == nullptr);

    // defaults
    const Settings d0 = resolve([](const char*) { return std::string(); });
    CHECK(d0.renderScale == 1.0);
    CHECK(d0.smooth);
    CHECK(d0.shadow == ShadowMode::Game);
    CHECK(d0.stage == StageSource::Auto);        // never the char-select pictures unless asked
    CHECK(d0.upscale == Upscale::Enhanced);
    CHECK(d0.preload == Preload::Standard);
    CHECK(!d0.fps && !d0.boxes && !d0.stageInfo);
    CHECK(!d0.gpuFx);                            // the GPU effects stay off until asked for
    CHECK(d0.roundIntro && d0.entrances);
    CHECK_EQ(d0.spriteMinLevel, 0);
    CHECK(!d0.spriteFullSize);
    CHECK(d0.spriteLevel(0.3) == 1 && d0.spriteLevel(1.5) == 0);
    // stored texts (case-insensitive); an unknown one falls back to the default
    std::map<std::string, std::string> save = {{"ZD_DEV_RES", "low"}, {"ZD_DEV_TEXQ", "Maximum"}, {"ZD_DEV_FILTER", "Pixelated"}, {"ZD_DEV_SHADOW", "Medium"},
                                               {"ZD_DEV_STAGE", "Portrait"}, {"ZD_DEV_UPSCALE", "Off"}, {"ZD_DEV_PRELOAD", "FULL"}, {"ZD_DEV_FPS", "On"},
                                               {"ZD_DEV_INTRO", "Off"}, {"ZD_DEV_INFO", "garbage"}, {"ZD_DEV_GPUFX", "on"}};
    const Settings d1 = resolve([&](const char* k) { auto it = save.find(k); return it == save.end() ? std::string() : it->second; });
    CHECK(d1.renderScale == 0.33);
    CHECK(d1.spriteFullSize && d1.spriteLevel(0.1) == 0);
    CHECK(!d1.smooth);
    CHECK(d1.shadow == ShadowMode::Medium);
    CHECK(d1.stage == StageSource::Portrait);
    CHECK(d1.upscale == Upscale::Off);
    CHECK(d1.preload == Preload::Full);
    CHECK(d1.fps && !d1.roundIntro && !d1.stageInfo);
    CHECK(d1.gpuFx);
    CHECK(d1.spriteCacheBytes > d0.spriteCacheBytes);
    save["ZD_DEV_TEXQ"] = "Low";
    CHECK_EQ(resolve([&](const char* k) { auto it = save.find(k); return it == save.end() ? std::string() : it->second; }).spriteMinLevel, 2);
    CHECK_EQ(indexOf(kShadows, "game"), 4);
    CHECK_EQ(indexOf(kShadows, ""), def(kShadows).def);

    // the menu page: one Setting per option, same keywords, defaults and choices as the table
    const MenuDef page = parseMenuIni(menuIni());
    CHECK_EQ(page.options.size(), static_cast<size_t>(kCount));
    for (size_t i = 0; i < page.options.size() && i < static_cast<size_t>(kCount); ++i) {
        const MenuOption& o = page.options[i];
        const Def& d = def(static_cast<Id>(i));
        CHECK(o.type == MenuOptType::Setting);
        CHECK_EQ(o.keyword, std::string(d.key));
        CHECK_EQ(o.choices.size(), static_cast<size_t>(d.nChoices));
        CHECK_EQ(o.defaultChoice, std::string(d.choices[d.def]));
        CHECK(o.text.rfind("@own:dev.", 0) == 0);
    }
}

// ---------------------------------------------------------------------------------------- HUD atlas rows + palettized portraits
static void testHudAtlasAndPortrait() {
    std::printf("[HUD atlas rows + palettized portraits]\n");
    GbsScene sc;
    GbsTexture atlas;
    atlas.id = 7; atlas.name = "Textures/TA_1001_1.png";   // a plain row names no atlas
    GbsTexture part;
    part.id = 8; part.name = "Textures/HUD/Main_Energy_Frame01.png"; part.atlasId = 7; part.u0 = 0.25f; part.v0 = 0.5f; part.u1 = 0.75f; part.v1 = 0.625f;
    GbsTexture self;
    self.id = 9; self.name = "Textures/Options/plain.png"; self.atlasId = 9;   // own id + full rect = not in an atlas
    GbsTexture bad;
    bad.id = 10; bad.name = "Textures/HUD/empty_rect.png"; bad.atlasId = 7; bad.u0 = 0.5f; bad.u1 = 0.5f;
    sc.textures = {atlas, part, self, bad};
    CHECK(!sc.textures[0].inAtlas());
    CHECK(sc.textures[1].inAtlas());
    CHECK(!sc.textures[2].inAtlas());
    CHECK(!sc.textures[3].inAtlas());                       // empty rectangle
    const GbsTexture* hit = sc.textureByName("textures/hud/main_energy_frame01.dds");   // case and .png/.dds are ignored
    CHECK(hit != nullptr && hit->id == 8u);
    CHECK(sc.textureByName("Textures/HUD/nope.png") == nullptr);
    CHECK(sc.textureById(7) != nullptr && sc.textureById(99) == nullptr);

    // a 4x2 8-bit RLE .pcx: row 0 = a run of four 3s, row 1 = 1,2,0,0
    std::vector<uint8_t> pcx(128, 0);
    pcx[0] = 0x0A; pcx[1] = 5; pcx[2] = 1; pcx[3] = 8;
    pcx[8] = 3; pcx[10] = 1;                                // xmax 3, ymax 1
    pcx[65] = 1; pcx[66] = 4;                               // one plane, 4 bytes per line
    for (uint8_t b : {0xC4, 3, 1, 2, 0xC2, 0}) pcx.push_back(static_cast<uint8_t>(b));
    hudart::Pcx img;
    std::string err;
    CHECK(hudart::decodePcx(pcx.data(), pcx.size(), img, &err));
    CHECK_EQ(img.w, 4);
    CHECK_EQ(img.h, 2);
    CHECK_EQ(static_cast<int>(img.idx[0]), 3);
    CHECK_EQ(static_cast<int>(img.idx[3]), 3);
    CHECK_EQ(static_cast<int>(img.idx[4]), 1);
    CHECK_EQ(static_cast<int>(img.idx[5]), 2);
    CHECK_EQ(static_cast<int>(img.idx[7]), 0);
    auto cut = pcx;
    cut.resize(130);
    CHECK(!hudart::decodePcx(cut.data(), cut.size(), img, &err));   // truncated
    cut = pcx;
    cut[2] = 0;
    CHECK(!hudart::decodePcx(cut.data(), cut.size(), img, &err));   // uncompressed is refused

    // composition: band = idx/5 + 1, tone from the shade layer, the outline layer darkens, background (0/255/255) stays clear
    Palette pal;
    pal.tones = 2; pal.bands = 3;
    pal.rgb.assign(static_cast<size_t>(pal.tones) * pal.bands * 3, 0);
    for (int b = 0; b < pal.bands; ++b)
        for (int t = 0; t < pal.tones; ++t) {
            uint8_t* c = const_cast<uint8_t*>(pal.at(b, t));
            c[0] = static_cast<uint8_t>(40 * b); c[1] = static_cast<uint8_t>(100 * t); c[2] = 10;
        }
    hudart::Pcx col, lin, shd;
    col.w = lin.w = shd.w = 2; col.h = lin.h = shd.h = 1;
    col.idx = {5, 0}; lin.idx = {255, 255}; shd.idx = {255, 255};   // pixel 0: band 2, brightest tone; pixel 1: background
    hudart::Image out;
    CHECK(hudart::composePortrait(col, lin, shd, pal, out));
    CHECK_EQ(static_cast<int>(out.rgba[3]), 255);
    CHECK_EQ(static_cast<int>(out.rgba[0]), 80);
    CHECK_EQ(static_cast<int>(out.rgba[1]), 100);
    CHECK_EQ(static_cast<int>(out.rgba[7]), 0);                      // transparent
    lin.idx[0] = 128;                                                // half outline
    CHECK(hudart::composePortrait(col, lin, shd, pal, out));
    CHECK(out.rgba[1] > 45 && out.rgba[1] < 55);
    shd.w = 3;
    CHECK(!hudart::composePortrait(col, lin, shd, pal, out));        // layers must agree in size
    CHECK_EQ(hudart::displayName("bossmarie"), std::string("BLOODY MARIE"));
    CHECK_EQ(hudart::displayName("MsFortune"), std::string("MS. FORTUNE"));
}

static void testOddToneRows() {
    std::printf("[flagged records: odd tone rows]\n");
    const size_t sw = 32;  // two cells
    std::vector<uint8_t> g(16 * sw, 0);
    for (size_t y = 0; y < 16; ++y)
        for (size_t x = 0; x < sw; ++x) g[y * sw + x] = static_cast<uint8_t>(((y & 1) ? 0x60 : 0x20) | (y % 2 == 0 ? (y * 2) : 0));
    const std::vector<uint8_t> before = g;
    interpolateOddToneRows(g, sw);
    for (size_t y = 0; y < 16; ++y)
        for (size_t x = 0; x < sw; ++x) {
            const uint8_t b = before[y * sw + x], a = g[y * sw + x];
            if (y == 15 || y % 2 == 0) {
                CHECK_EQ(static_cast<int>(a), static_cast<int>(b));  // stored rows untouched (row 15 is stored too)
            } else {
                CHECK_EQ(static_cast<int>(a & 0xE0), static_cast<int>(b & 0xE0));  // line level kept
                const int want = ((before[(y - 1) * sw + x] & 31) + (before[(y + 1) * sw + x] & 31)) / 2;
                CHECK_EQ(static_cast<int>(a & 31), want);
            }
        }
}

static void testSprTiles() {
    std::printf("[tile sprites]\n");
    SprTiles s;
    std::string err;
    CHECK(parseSprTiles(makeSpr(), s, &err));
    CHECK_EQ(s.name, std::string("unit"));
    CHECK_EQ(s.tiles.size(), static_cast<size_t>(5));
    CHECK_EQ(s.frames.size(), static_cast<size_t>(2));
    CHECK(s.frames[0].cols == 2 && s.frames[0].rows == 2 && s.frames[1].cols == 3 && s.frames[1].rows == 2);
    CHECK(s.frames[0].offsetX == -10.0f && s.frames[0].offsetY == 5.5f);
    CHECK(s.anim("second") && s.anim("second")->first == 1 && s.anim("first")->count == 1 && !s.anim("third"));
    // 8x8 atlas of 4x4 tiles: tile (u,v) is filled with the byte 10*u+v+1 in all channels
    std::vector<uint8_t> atlas(8 * 8 * 4);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            for (int c = 0; c < 4; ++c) atlas[static_cast<size_t>((y * 8 + x) * 4 + c)] = static_cast<uint8_t>(10 * (x / 4) + (y / 4) + 1);
    std::vector<uint8_t> px;
    int w = 0, h = 0;
    CHECK(composeSprFrame(s, 0, atlas.data(), 8, 8, px, w, h));
    CHECK(w == 8 && h == 8);
    CHECK(px[0] == 11 && px[(0 * 8 + 4) * 4] == 2 && px[(4 * 8 + 0) * 4] == 12);  // tiles (1,0) (0,1) (1,1) at cells (0,0) (1,0) (0,1)
    CHECK(px[(4 * 8 + 4) * 4 + 3] == 0);                                           // cell (1,1) has no tile: transparent
    CHECK(composeSprFrame(s, 1, atlas.data(), 8, 8, px, w, h) && w == 12 && h == 8);
    CHECK(!composeSprFrame(s, 2, atlas.data(), 8, 8, px, w, h));
    // damaged files fail cleanly
    std::vector<uint8_t> bad = makeSpr();
    bad.resize(60);
    CHECK(!parseSprTiles(bad, s, &err) && s.tiles.empty());
    std::vector<uint8_t> junk(300, 0xAB);
    CHECK(!parseSprTiles(junk, s, &err));
    bad = makeSpr();
    bad[bad.size() - 30] ^= 0xFF;  // a damaged animation tail only loses names
    CHECK(parseSprTiles(bad, s, &err) && s.frames.size() == 2);
}

static void testSprReal(const std::string& dir) {
    std::printf("[real loading sprites]\n");
    for (const char* lang : {"en", "es"}) {
        std::vector<uint8_t> msb, dds;
        const std::string base = dir + "/loading_" + lang;
        if (!readFile(base + ".spr.msb", msb) || !readFile(base + ".dds", dds)) {
            std::printf("  SKIP no loading_%s files\n", lang);
            ++g_skipped;
            continue;
        }
        SprTiles s;
        std::string err;
        CHECK(parseSprTiles(msb, s, &err));
        CHECK_EQ(s.frames.size(), static_cast<size_t>(28));
        CHECK(s.anim(std::string("bomb_") + lang) && s.anim(std::string("bomb_") + lang)->count == 9);   // "LOADING..." + the bomb, per language
        CHECK(s.anim("slideshow") && s.anim("slideshow")->count == 19);
        DdsImage img;
        CHECK(decodeDds(dds, img));
        bool inside = true;
        for (const SprTile& t : s.tiles) inside = inside && (t.u + 1) * s.tileW <= img.width && (t.v + 1) * s.tileH <= img.height;
        CHECK(inside);  // every tile reference lands inside the atlas
        std::vector<uint8_t> px;
        int w = 0, hh = 0;
        CHECK(composeSprFrame(s, 0, img.rgba.data(), img.width, img.height, px, w, hh) && w == 512 && hh == 240);
        CHECK(composeSprFrame(s, 9, img.rgba.data(), img.width, img.height, px, w, hh) && w == 688 && hh == 336);
        size_t lit = 0;
        for (size_t i = 0; i < px.size(); i += 4) lit += px[i] > 128;
        CHECK(lit > 2000);  // line art, not an empty frame
    }
}

// Decodes every .dds under a real data folder (optional: ZDROID_TEST_DDS).
static void testDdsReal(const std::string& dir) {
    Vfs vfs;
    const size_t mounted = vfs.mount(dir);
    CHECK(mounted > 0);
    const std::vector<std::string> files = vfs.listByExt(".dds");
    CHECK(!files.empty());
    size_t ok = 0, withAlpha = 0;
    int formats = 0;
    std::string err;
    const size_t limit = std::min<size_t>(files.size(), 40);
    for (size_t i = 0; i < limit; ++i) {
        std::vector<uint8_t> bytes;
        if (!readFile(files[i], bytes)) { CHECK(false); continue; }
        DdsImage img;
        if (!decodeDds(bytes, img, &err, 256)) {
            std::fprintf(stderr, "  dds decode failed: %s (%s)\n", files[i].c_str(), err.c_str());
            CHECK(false);
            continue;
        }
        ++ok;
        if (img.format == DdsFormat::DXT5 || img.format == DdsFormat::DXT3) ++formats;
        bool anyAlpha = false, anyColour = false;
        for (size_t p = 0; p < img.rgba.size(); p += 4) {
            if (img.rgba[p] || img.rgba[p + 1] || img.rgba[p + 2]) anyColour = true;
            if (img.rgba[p + 3] != 255) anyAlpha = true;
        }
        CHECK(anyColour);  // a real texture is not all black
        if (anyAlpha) ++withAlpha;
    }
    std::printf("  decoded %zu/%zu .dds (%d block-compressed, %zu with alpha)\n", ok, limit, formats, withAlpha);
    CHECK_EQ(ok, limit);
}


// ---- the data driven fight runtime (foit_rt): synthetic character, then the real data when it is around ------------------
// A two state character: Stand (script sets slot 100, carries the "free state" bit) and Walk (reached with forward held, runs the velocity).
static std::shared_ptr<foit::Character> makeRtChar() {
    auto ch = std::make_shared<foit::Character>();
    foit::Object& b = ch->body;
    b.v374 = 8;  // input ring size
    b.refBe = 0;
    auto frameNode = [&](uint16_t frame, uint8_t b3, foit::Chain chain) {
        foit::Node n;
        n.refs[1] = frame;
        foit::NodeProps p;
        p.b1 = 0x01;  // has duration
        p.dur = 1;
        p.b3 = static_cast<uint8_t>(b3 | (chain.empty() ? 0 : 0x80));
        p.chain = std::move(chain);
        n.props.push_back(p);
        return n;
    };
    foit::State stand;
    stand.name = "Stand";
    stand.next = 0;
    stand.hdr.nodes.push_back(frameNode(1, 0x20, {node(foit::vm::op::Set, opLit(7), opLit(0), opSlot(100))}));
    foit::State walk;
    walk.name = "Walk";
    walk.next = 1;
    walk.hdr.nodes.push_back(frameNode(2, 0, {node(foit::vm::op::Set, opLit(5), opLit(0), opSlot(158)), node(foit::vm::op::Set, opLit(0), opLit(0), opSlot(59))}));
    foit::Transition t;  // Stand -> Walk while forward is held
    t.target = 1;
    foit::Step st;
    st.b0 = foit::rt::DirRight;
    st.w = 1;
    t.cmd.steps.push_back(st);
    stand.transitions.push_back(t);
    foit::Transition back;  // Walk -> Stand as soon as forward is released (a step for neutral)
    back.target = 0;
    foit::Step sn;
    sn.b0 = foit::rt::DirNeutral;
    sn.w = 1;
    back.cmd.steps.push_back(sn);
    walk.transitions.push_back(back);
    b.states = {stand, walk};
    ch->frames.resize(2);
    return ch;
}

static void testFoitRuntimeSynthetic() {
    std::printf("[fight runtime: synthetic character, rollback snapshots]\n");
    using namespace foit::rt;
    auto ch = makeRtChar();
    Object o;
    init(o, *ch, ch->body, 0.0f, true);
    World w;
    w.objects = {&o};
    enterState(o, w, 0);
    Sample prev;
    auto step = [&](int dir) {
        Sample in = composeSample(prev, dir, 0);
        prev = in;
        tickAll(w, &in, 1);
        w.events.clear();
    };
    for (int i = 0; i < 3; ++i) step(DirNeutral);
    CHECK_EQ(o.state, 0);
    CHECK(o.slots[100] == 7.0f);  // the node's script ran
    for (int i = 0; i < 4; ++i) step(DirRight);
    CHECK_EQ(o.state, 1);         // forward held: Stand -> Walk
    CHECK(o.x > 0.0f);            // and it moves
    step(DirNeutral);
    step(DirNeutral);
    CHECK_EQ(o.state, 0);         // released: back to Stand

    // combo bookkeeping: a node of a free state (bit b3 & 0x20) resets the scale and the counters
    o.comboHits = 5;
    o.slots[83] = 0.5f;
    o.slots[12] = 4;
    step(DirNeutral);
    CHECK(o.slots[83] == 1.0f);
    CHECK_EQ(o.comboHits, 0);
    CHECK(o.slots[12] == 0.0f);

    // snapshot / restore / replay
    for (int i = 0; i < 5; ++i) step(DirRight);
    std::vector<uint8_t> snap;
    saveWorld(w, snap);
    const uint64_t h0 = hashWorld(w);
    const Sample savedPrev = prev;
    for (int i = 0; i < 6; ++i) step(i < 3 ? DirNeutral : DirRight);
    const uint64_t endHash = hashWorld(w);
    CHECK(endHash != h0);
    CHECK(loadWorld(w, {&o}, snap.data(), snap.size()));
    CHECK(hashWorld(w) == h0);
    prev = savedPrev;
    for (int i = 0; i < 6; ++i) step(i < 3 ? DirNeutral : DirRight);
    CHECK(hashWorld(w) == endHash);  // same inputs from the same snapshot: the same world

    // malformed streams are refused, never crash
    int accepted = 0;
    for (size_t cut = 0; cut < snap.size(); ++cut) accepted += loadWorld(w, {&o}, snap.data(), cut) ? 1 : 0;
    CHECK_EQ(accepted, 0);
    std::vector<uint8_t> bad = snap;
    bad[0] ^= 0xff;
    CHECK(!loadWorld(w, {&o}, bad.data(), bad.size()));
    CHECK(!loadWorld(w, {}, snap.data(), snap.size()));  // wrong number of characters
    for (size_t i = 8; i < snap.size(); i += 7) {        // random corruption must stay memory safe (the result may be accepted or not)
        std::vector<uint8_t> junk = snap;
        junk[i] = static_cast<uint8_t>(junk[i] ^ 0xa5);
        (void)loadWorld(w, {&o}, junk.data(), junk.size());
    }
}

// With the user's own Filia: her forward throw takes exactly 4 x 212 of the defender's life (the damage comes from the data), the throw
// ends with the defender released, and a snapshot taken in the middle of it replays identically.
static void testFoitRuntimeReal(const std::vector<std::string>& dirs) {
    std::printf("[fight runtime: real Filia throw + rollback]\n");
    std::shared_ptr<foit::Character> ch;
    for (const std::string& d : dirs) {
        std::vector<uint8_t> bytes;
        if (!readFile(d + "/characters/filia.foit", bytes) && !readFile(d + "/filia.foit", bytes)) continue;
        auto c = std::make_shared<foit::Character>();
        std::string err;
        if (foit::parse(bytes.data(), bytes.size(), *c, &err)) {
            ch = c;
            break;
        }
    }
    if (!ch) {
        skip("no filia.foit under ZDROID_TEST_DATA");
        return;
    }
    using namespace foit::rt;
    Object p1, p2;
    init(p1, *ch, ch->body, -40.0f, true);
    init(p2, *ch, ch->body, 40.0f, false);
    p2.slots[25] = 2;
    p1.enemy = &p2;
    p2.enemy = &p1;
    World w;
    w.objects = {&p1, &p2};
    enterState(p1, w, findState(ch->body, "Stand"));
    enterState(p2, w, findState(ch->body, "Stand"));
    Sample prev1, prev2;
    std::vector<uint8_t> snap;
    uint64_t hashAt[400] = {};
    Sample snapPrev1, snapPrev2;
    const int kSnap = 90;
    const float life0 = p2.life();
    bool heldSeen = false;
    for (int t = 0; t < 360; ++t) {
        // 60 ticks of nothing, forward + LP + LK for 2 ticks, then nothing
        const bool press = t >= 60 && t < 62;
        Sample in[2];
        in[0] = composeSample(prev1, press ? DirRight : DirNeutral, press ? 0x9u : 0u);
        in[1] = composeSample(prev2, DirNeutral, 0);
        prev1 = in[0];
        prev2 = in[1];
        tickAll(w, in, 2);
        w.events.clear();
        heldSeen = heldSeen || p2.slots[10] != 0.0f;  // THROWN
        hashAt[t] = hashWorld(w);
        if (t == kSnap) {
            saveWorld(w, snap);
            snapPrev1 = prev1;
            snapPrev2 = prev2;
        }
    }
    CHECK(heldSeen);                                     // the defender was held by the throw
    CHECK(p2.slots[10] == 0.0f);                         // and let go
    CHECK_EQ(static_cast<int>(life0 - p2.life()), 848);  // 4 x floor(212.5)
    CHECK(p1.life() == life0);
    // replay from the snapshot
    CHECK(loadWorld(w, {&p1, &p2}, snap.data(), snap.size()));
    prev1 = snapPrev1;
    prev2 = snapPrev2;
    bool same = true;
    for (int t = kSnap + 1; t < 360; ++t) {
        Sample in[2];
        in[0] = composeSample(prev1, DirNeutral, 0);
        in[1] = composeSample(prev2, DirNeutral, 0);
        prev1 = in[0];
        prev2 = in[1];
        tickAll(w, in, 2);
        w.events.clear();
        same = same && hashWorld(w) == hashAt[t];
    }
    CHECK(same);
}

// the engine's hit-apply numbers: what the defender is handed when a hit connects (damage, chip, stun, meter, flags)
static void testFoitHitInfoSynthetic() {
    std::printf("[fight runtime: hit info (stun table, flag byte)]\n");
    float t[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    const int n = foit::rt::parseStrengthTable("# Light, Medium, Heavy\n#\n# format:\n# [L/M/H] Value\n\nLight 15\nMedium 20\nHeavy 30\n", t);
    CHECK_EQ(n, 3);
    CHECK(t[0] == 0.0f && t[1] == 15.0f && t[2] == 20.0f && t[3] == 30.0f && t[4] == 0.0f);
    float u[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    CHECK_EQ(foit::rt::parseStrengthTable("h 7 junk", u), 1);   // first letter decides (any case); the next word is not a pair
    CHECK(u[3] == 7.0f);
    CHECK_EQ(foit::rt::parseStrengthTable("", u), 0);
    foit::HitData h;
    CHECK_EQ(static_cast<int>(h.flags19()), 0);
    h.fa = 0x20;                                                 // fa bit 5 -> flag bit 0 (cannot kill)
    CHECK_EQ(static_cast<int>(h.flags19()), 1);
    h.fa = 0xE3;                                                 // the chain-present bits (0..1) and f0a bit do not leak into the byte
    CHECK_EQ(static_cast<int>(h.flags19()), 7);
    h.fb = 0x01;                                                 // fb bit 0 -> flag bit 3 (no counter hit)
    CHECK_EQ(static_cast<int>(h.flags19()), 7 | 8);
    h.fb = 0x1F;
    CHECK_EQ(static_cast<int>(h.flags19()), 0xFF);
    h.fb = 0xE0;                                                 // fb bits 5..7 are not part of it
    h.fa = 0;
    CHECK_EQ(static_cast<int>(h.flags19()), 0);
}

// TOUCHSCREENLR is relative to the way the object faces (positive = edge in front, negative = edge behind); PUSHBACK shoves the attacker
static void testFoitScreenTouchAndPushback() {
    std::printf("[fight runtime: screen edge flag, corner pushback]\n");
    using namespace foit::rt;
    auto ch = makeRtChar();
    auto lrAt = [&](float x, bool facingRight) {
        Object o;
        init(o, *ch, ch->body, x, facingRight);
        World w;
        w.objects = {&o};
        enterState(o, w, 0);
        Sample in = composeSample(Sample(), DirNeutral, 0);
        tickAll(w, &in, 1);
        return o.slots[13];
    };
    CHECK(lrAt(0.0f, true) == 0.0f && lrAt(0.0f, false) == 0.0f);
    CHECK(lrAt(400.0f, true) == 1.5f);     // right edge, facing it: in front, a point character on the end of the stage
    CHECK(lrAt(400.0f, false) == -1.5f);   // right edge, facing away: behind
    CHECK(lrAt(-400.0f, true) == -1.5f);
    CHECK(lrAt(-400.0f, false) == 1.5f);
    CHECK(lrAt(399.0f, true) == 1.5f);     // within 2 pixels still counts as touching
    CHECK(lrAt(390.0f, true) == 0.0f);

    // the defender is cornered (edge behind it, SOLIDWALLS on): the attacker is moved by the whole-pixel speed, which decays each tick
    Object atk, def;
    init(atk, *ch, ch->body, 340.0f, true);
    init(def, *ch, ch->body, 400.0f, false);
    atk.enemy = &def;
    def.enemy = &atk;
    def.attacker = &atk;
    World w;
    w.objects = {&atk, &def};
    enterState(atk, w, 0);
    enterState(def, w, 0);
    atk.slots[163] = def.slots[163] = 1;
    def.slots[95] = 3;   // PUSHBACKTIME
    def.slots[96] = 6;   // PUSHBACK
    def.slots[97] = -2;  // PUSHBACKDELTA
    Sample in[2] = {composeSample(Sample(), DirNeutral, 0), composeSample(Sample(), DirNeutral, 0)};
    const float x0 = atk.x;
    tickAll(w, in, 2);  // pushback of 6 queued on the attacker (it already ran this tick: moved next tick)
    CHECK(def.slots[95] == 2.0f && def.slots[96] == 4.0f);
    tickAll(w, in, 2);
    CHECK(def.slots[95] == 1.0f && def.slots[96] == 2.0f);
    tickAll(w, in, 2);
    CHECK(def.slots[95] == 0.0f);
    tickAll(w, in, 2);  // the last push (2) lands on the attacker's next tick
    CHECK(x0 - atk.x >= 12.0f - 0.01f);  // 6 + 4 + 2 pixels away from the defender (the push boxes may add a little)
    const float xAfter = atk.x;
    tickAll(w, in, 2);
    CHECK(atk.x == xAfter);  // time is over: no more push
    // hit-stop stops the clock; a defender that is not cornered pushes nobody unless PUSHBACKALWAYS is set
    Object a2, d2;
    init(a2, *ch, ch->body, -40.0f, true);
    init(d2, *ch, ch->body, 0.0f, false);
    a2.enemy = &d2;
    d2.enemy = &a2;
    d2.attacker = &a2;
    World w2;
    w2.objects = {&a2, &d2};
    enterState(a2, w2, 0);
    enterState(d2, w2, 0);
    a2.slots[163] = d2.slots[163] = 1;
    d2.slots[95] = 2;
    d2.slots[96] = 5;
    const float a2x = a2.x;
    tickAll(w2, in, 2);
    tickAll(w2, in, 2);
    tickAll(w2, in, 2);
    CHECK(a2.x == a2x);  // open field: nothing
    d2.slots[95] = 2;
    d2.slots[96] = 5;
    d2.slots[98] = 1;   // PUSHBACKALWAYS
    d2.slots[52] = 2;   // hit-stop: frozen
    tickAll(w2, in, 2);
    const float frozen = d2.slots[95];
    d2.slots[52] = 0;
    tickAll(w2, in, 2);
    tickAll(w2, in, 2);
    CHECK(frozen == 2.0f);
    CHECK(a2.x < a2x);   // pushed away from the defender
}

// TETHEREDTO / TETHERPT: a helper stays on a named point of its leader's current frame (mode 7) or rides along with its parent (mode 1);
// the jump to a random stored cell (mode 4) lands on the sprite and untethers.  Umbrella's Hungern follows her through this.
static void testFoitTether() {
    std::printf("[fight runtime: tethers (helpers that follow their owner)]\n");
    using namespace foit::rt;
    auto ch = makeRtChar();
    const uint32_t hash = 0x4727890eu;
    foit::Frame& f0 = ch->frames[0];  // the frame Stand shows
    f0.pivotX = 10;
    f0.pivotY = 20;
    f0.entries.push_back({hash, 40u, static_cast<uint32_t>(-80)});  // 30 px in front of the pivot, 100 px above it (canvas y grows downwards)
    // a one-cell image 3x2 cells with cells 1 and 4 stored, drawn with its top-left at the pivot
    foit::Image& im = f0.img[1];
    im.hasData = true;
    im.n = 2;
    im.w = 3;
    im.h = 2;
    im.cells = 6;
    im.mask = {0x12};  // bits 1 and 4
    f0.xy[foit::slotXY(1)] = 10;
    f0.xy[foit::slotXY(1) + 1] = 20;
    foit::Object helper;
    helper.name = "Helper";
    foit::State wait;
    wait.name = "Wait";
    wait.next = 0;
    foit::Node n;
    n.refs[1] = 2;
    foit::NodeProps p;
    p.b1 = 0x01;
    p.dur = 1;
    n.props.push_back(p);
    wait.hdr.nodes.push_back(n);
    helper.states = {wait};
    ch->body.children.push_back(helper);

    Object parent;
    init(parent, *ch, ch->body, 100.0f, true);
    World w;
    w.objects = {&parent};
    enterState(parent, w, 0);
    Sample in = composeSample(Sample(), DirNeutral, 0);
    tickAll(w, &in, 1);
    Object* c = spawn(parent, w, 0, 0.0f, 0.0f, nullptr);
    CHECK(c != nullptr);
    if (!c) return;
    std::memcpy(&c->slots[45], &hash, sizeof hash);
    c->slots[57] = 7;
    tickAll(w, &in, 1);
    CHECK(c->x == parent.x + 30.0f && c->y == parent.y + 100.0f);
    parent.slots[56] = c->slots[56] = -1.0f;  // facing left (the helper's own facing counts): mirrored
    tickAll(w, &in, 1);
    CHECK(c->x == parent.x - 30.0f && c->y == parent.y + 100.0f);
    const uint32_t other = 0x1234u;  // a point the frame does not have: the pivot itself
    std::memcpy(&c->slots[45], &other, sizeof other);
    tickAll(w, &in, 1);
    CHECK(c->x == parent.x && c->y == parent.y);

    // mode 1: moves by what the parent moved
    c->slots[57] = 1;
    tickAll(w, &in, 1);
    const float cx = c->x, cy = c->y;
    parent.x += 50.0f;
    parent.y += 7.0f;
    tickAll(w, &in, 1);
    CHECK(c->x == cx + 50.0f && c->y == cy + 7.0f);
    c->slots[57] = 0;  // untethered: stays put
    parent.x += 20.0f;
    tickAll(w, &in, 1);
    CHECK(c->x == cx + 50.0f);

    // mode 4: lands on a stored cell of the leader's image (cell 1 = column 1, row 0 or cell 4 = column 1, row 1) and untethers
    parent.slots[56] = c->slots[56] = 1.0f;
    c->slots[57] = 4;
    tickAll(w, &in, 1);
    CHECK(c->slots[57] == 0.0f);
    const float ddx = c->x - parent.x, ddy = c->y - parent.y;
    CHECK(ddx == 10.0f - 10.0f + 24.0f);                    // drawX 0 + (1 + 0.5) * 16
    CHECK(ddy == -(0.0f + 8.0f) || ddy == -(0.0f + 24.0f));  // row 0 or row 1 (y up)

    // a snapshot keeps the bookkeeping: save, tick on, load, tick again -> the same world
    c->slots[57] = 1;
    tickAll(w, &in, 1);
    std::vector<uint8_t> snap;
    saveWorld(w, snap);
    parent.x += 5.0f;
    tickAll(w, &in, 1);
    const uint64_t h1 = hashWorld(w);
    CHECK(loadWorld(w, {&parent}, snap.data(), snap.size()));
    Object* c2 = w.objects.size() > 1 ? w.objects[1] : nullptr;
    CHECK(c2 != nullptr);
    parent.x += 5.0f;
    tickAll(w, &in, 1);
    CHECK(hashWorld(w) == h1);
}

// ---- round flow (match layer) with the real Annie -----------------------------------------------------------------------
static void testFoitMatchReal(const std::vector<std::string>& dirs) {
    std::printf("[fight runtime: round flow with real Annie (intro, fight, KO / time over, poses, rounds, rollback)]\n");
    std::shared_ptr<foit::Character> ch;
    for (const std::string& d : dirs) {
        for (const char* sub : {"/characters", ""}) {
            std::vector<uint8_t> bytes;
            if (!readFile(d + sub + "/annie.foit", bytes)) continue;
            auto c = std::make_shared<foit::Character>();
            std::string err;
            if (foit::parse(bytes.data(), bytes.size(), *c, &err)) {
                ch = c;
                break;
            }
        }
        if (ch) break;
    }
    if (!ch) {
        skip("no annie.foit under ZDROID_TEST_FOIT");
        return;
    }
    using namespace foit::rt;
    struct Run {
        Object p1, p2;
        World w;
        Sample prev1, prev2;
        std::vector<int> phases;  // Match::Phase of every MatchPhase event, in order
        int tick = 0;
    };
    auto setup = [&](Run& r, float life1, float life2, int seconds, int rounds) {
        init(r.p1, *ch, ch->body, -40.0f, true);
        init(r.p2, *ch, ch->body, 60.0f, false);
        r.p2.slots[25] = 2;
        r.p1.enemy = &r.p2;
        r.p2.enemy = &r.p1;
        r.p1.slots[64] = r.p1.slots[65] = life1;
        r.p2.slots[64] = r.p2.slots[65] = life2;
        r.w.objects = {&r.p1, &r.p2};
        enterState(r.p1, r.w, findState(ch->body, "Stand"));
        enterState(r.p2, r.w, findState(ch->body, "Stand"));
        startMatch(r.w, seconds, rounds);
    };
    auto step = [&](Run& r, bool punch) {
        Sample in[2];
        in[0] = composeSample(r.prev1, DirNeutral, punch ? 0x1u : 0u);
        in[1] = composeSample(r.prev2, DirNeutral, 0);
        r.prev1 = in[0];
        r.prev2 = in[1];
        tickAll(r.w, in, 2);
        for (const foit::rt::Event& e : r.w.events)
            if (e.kind == foit::rt::Event::MatchPhase) r.phases.push_back(static_cast<int>(e.hash));
        r.w.events.clear();
        ++r.tick;
    };

    // time over: the fighter with more life wins; both go through the intro poses and the win / lose poses on their own
    {
        static Run r;
        r = Run();
        setup(r, 9000.0f, 5000.0f, 2, 1);
        CHECK_EQ(r.w.match.phase, static_cast<int>(Match::Intro));
        CHECK(r.p1.slots[30] == -1.0f && r.p1.slots[135] == 1.0f);  // ROUNDOVER -1 + DOPOSE 1 = "play the intro"
        while (r.w.match.phase != Match::Over && r.tick < 3000) {
            step(r, false);
            if (r.w.match.phase == Match::Fight) CHECK(r.p1.slots[30] == 0.0f && r.p2.slots[30] == 0.0f);
        }
        CHECK_EQ(r.w.match.phase, static_cast<int>(Match::Over));
        CHECK_EQ(r.w.match.timeouts, 0);  // every pose handed control back by itself
        CHECK_EQ(r.w.match.matchWinner, 0);
        CHECK(r.w.match.wins[0] == 1 && r.w.match.wins[1] == 0);
        CHECK_EQ(r.w.match.reason, 2);  // time over
        CHECK(r.p1.slots[30] == 2.0f);
        CHECK(r.phases.size() == 4 && r.phases[0] == Match::Intro && r.phases[1] == Match::Fight && r.phases[2] == Match::Ending &&
              r.phases[3] == Match::Over);
        // the loser got the lose pose, the winner the win pose (both asked through DOPOSE, both written back to 0)
        CHECK(r.p1.slots[135] == 0.0f && r.p2.slots[135] == 0.0f);
    }
    // KO: P2 is one light punch from death; the round ends once it has landed and lies still; it is not asked for a pose, the winner is
    {
        static Run r;
        r = Run();
        setup(r, 14300.0f, 250.0f, 99, 1);
        bool sawSettle = false;
        while (r.w.match.phase != Match::Over && r.tick < 3000) {
            const bool punch = r.w.match.phase == Match::Fight && r.tick % 60 < 2;
            step(r, punch);
            if (r.w.match.phase == Match::Settle) {
                sawSettle = true;
                CHECK(r.p1.slots[30] == 0.0f);  // the killing blow is still flying: the round is not over yet
            }
        }
        CHECK(sawSettle);
        CHECK_EQ(r.w.match.phase, static_cast<int>(Match::Over));
        CHECK_EQ(r.w.match.reason, 1);
        CHECK_EQ(r.w.match.matchWinner, 0);
        CHECK_EQ(r.w.match.timeouts, 0);
        CHECK(r.p2.life() <= 0.0f);
        CHECK(r.p2.slots[135] == 0.0f);  // the fallen fighter was never asked for a pose
        CHECK(r.w.match.clock > 0 && r.w.match.clock < 99 * 60);
    }
    // best of 3 ending in draws: new round = fresh fighters at their start spots with full life; a snapshot taken at the start of round 2
    // replays to the same state (the match layer is part of the saved world)
    {
        static Run r;
        r = Run();
        setup(r, 14300.0f, 14300.0f, 1, 2);
        std::vector<uint8_t> snap;
        Sample sp1, sp2;
        int snapTick = -1;
        while (r.w.match.phase != Match::Over && r.tick < 6000) {
            step(r, false);
            if (snapTick < 0 && r.w.match.round == 2) {
                saveWorld(r.w, snap);
                sp1 = r.prev1;
                sp2 = r.prev2;
                snapTick = r.tick;
                CHECK(r.p1.x == -40.0f && r.p2.x == 60.0f);        // back at the start spots
                CHECK(r.p1.life() == 14300.0f && r.p2.life() == 14300.0f);
                CHECK(r.p1.slots[30] == -1.0f && r.p1.slots[135] == 1.0f);
                CHECK(r.w.objects.size() == 2u);                  // helpers of round 1 left with it
            }
        }
        CHECK_EQ(r.w.match.phase, static_cast<int>(Match::Over));
        CHECK_EQ(r.w.match.round, 2);
        CHECK(r.w.match.wins[0] == 2 && r.w.match.wins[1] == 2);  // a drawn round counts for both sides (assumed)
        CHECK_EQ(r.w.match.matchWinner, -1);
        CHECK(snapTick > 0);
        const uint64_t endHash = hashWorld(r.w);
        const int endTick = r.tick;
        CHECK(loadWorld(r.w, {&r.p1, &r.p2}, snap.data(), snap.size()));
        r.prev1 = sp1;
        r.prev2 = sp2;
        r.tick = snapTick;
        while (r.tick < endTick) step(r, false);
        CHECK(hashWorld(r.w) == endHash);
    }
}

// The data driven fight behind the app's GGPO-shaped layer (fight.h): FoitDuel with the real Annie.
static void testFoitDuelReal(const std::vector<std::string>& dirs) {
    std::printf("[fight runtime: FoitDuel through the GGPO-shaped sessions (synctest, deep rollback, round flow, training, tie-break, skip-intro)]\n");
    std::shared_ptr<foit::Character> ch;
    for (const std::string& d : dirs) {
        for (const char* sub : {"/characters", ""}) {
            std::vector<uint8_t> bytes;
            if (!readFile(d + sub + "/annie.foit", bytes)) continue;
            auto c = std::make_shared<foit::Character>();
            std::string err;
            if (foit::parse(bytes.data(), bytes.size(), *c, &err)) {
                ch = c;
                break;
            }
        }
        if (ch) break;
    }
    if (!ch) {
        skip("no annie.foit under ZDROID_TEST_FOIT");
        return;
    }
    using fight::FoitDuel;
    using fight::FoitDuelConfig;
    auto makeCfg = [&](int secs, int rounds, bool training, bool intro) {
        FoitDuelConfig c;
        c.ch[0] = c.ch[1] = ch;
        c.startX[0] = -40.0f;
        c.startX[1] = 60.0f;
        c.roundSeconds = secs;
        c.roundsToWin = rounds;
        c.training = training;
        c.introPoses = intro;
        c.tieBreak = false;
        return c;
    };
    // a held-for-6-ticks pseudo random pad: directions most of the time, an attack button now and then
    auto pad = [](int t, int side) -> uint16_t {
        uint32_t h = static_cast<uint32_t>(t / 6) * 2654435761u ^ (static_cast<uint32_t>(side) + 1u) * 0x9E3779B9u;
        h ^= h >> 15;
        h *= 2246822519u;
        h ^= h >> 13;
        uint16_t b = 0;
        switch ((h >> 3) % 8) {
            case 0: b |= fight::kRight; break;
            case 1: b |= fight::kLeft; break;
            case 2: b |= fight::kDown; break;
            case 3: b |= fight::kRight | fight::kDown; break;
            case 4: b |= fight::kUp; break;
            default: break;
        }
        if ((h >> 8) % 5 == 0) {
            const uint16_t atk[6] = {fight::kLP, fight::kMP, fight::kHP, fight::kLK, fight::kMK, fight::kHK};
            b |= atk[(h >> 12) % 6];
        }
        return b;
    };

    // input conversion: opposite directions cancel, the six buttons keep their order, macros expand
    {
        int dir = -1;
        uint32_t mask = 0;
        FoitDuel::splitInput(fight::kLeft | fight::kRight, &dir, &mask);
        CHECK_EQ(dir, static_cast<int>(foit::rt::DirNeutral));
        FoitDuel::splitInput(fight::kUp | fight::kDown, &dir, &mask);
        CHECK_EQ(dir, static_cast<int>(foit::rt::DirNeutral));
        FoitDuel::splitInput(fight::kRight | fight::kDown | fight::kLP, &dir, &mask);
        CHECK_EQ(dir, static_cast<int>(foit::rt::DirDownRight));
        CHECK_EQ(static_cast<int>(mask), 1);
        FoitDuel::splitInput(fight::kHK, &dir, &mask);
        CHECK_EQ(static_cast<int>(mask), 0x20);
        FoitDuel::splitInput(fight::kMacro1, &dir, &mask);
        CHECK_EQ(static_cast<int>(mask), 0x7);
        FoitDuel::splitInput(fight::kMacro2, &dir, &mask);
        CHECK_EQ(static_cast<int>(mask), 0x38);
    }

    // init projects the HUD numbers; a bad config is refused
    {
        FoitDuel d;
        FoitDuelConfig bad;
        CHECK(!d.init(bad));
        CHECK(d.init(makeCfg(99, 2, false, false)));
        fight::MatchState st;
        d.project(st);
        CHECK_EQ(st.p[0].health, 1000);
        CHECK_EQ(st.p[1].health, 1000);
        CHECK(st.roundTimer > 0);
    }

    // synctest: every tick is replayed from a snapshot and the checksums must agree (the checksum covers the real world)
    {
        FoitDuel d;
        CHECK(d.init(makeCfg(99, 2, false, false)));
        fight::MatchState st;
        d.project(st);
        fight::SyncTestSession sess(st, fight::foitDuelCallbacks(&d), 1);
        for (int t = 0; t < 900; ++t) {
            int sim = 0;
            sess.addLocalInput(pad(t, 0), pad(t, 1), &sim);
        }
        CHECK(sess.framesChecked() > 800u);
        CHECK(sess.mismatches() == 0u);
        CHECK(d.world().stats.hits + d.world().stats.blocks > 0u);   // the fight really happened
    }

    // deep rollback: save at tick K, run N ticks, load the save, run them again: every checksum and the final hash repeat
    {
        FoitDuel d;
        CHECK(d.init(makeCfg(99, 2, false, false)));
        fight::MatchState st;
        d.project(st);
        const fight::Callbacks cb = fight::foitDuelCallbacks(&d);
        const int K = 300, N = 240;
        std::vector<uint8_t> snap;
        std::vector<uint32_t> sums;
        bool hitShown = false;
        int firstHit = -1;
        for (int t = 0; t < K + N; ++t) {
            if (t == K) cb.save(st, snap, cb.user);
            cb.advance(st, pad(t, 0), pad(t, 1), cb.user);
            if (t >= K) sums.push_back(st.checksum());
            if (firstHit < 0 && d.world().stats.hits > 0) firstHit = t;
            // the defender's own script applies the damage a few ticks after the hit connects: it shows in the HUD numbers then
            if (firstHit >= 0 && st.p[0].health < 1000) hitShown = true;
            if (firstHit >= 0 && st.p[1].health < 1000) hitShown = true;
        }
        CHECK(firstHit >= 0 && hitShown);
        const uint64_t endHash = d.hash();
        CHECK(!snap.empty());
        CHECK(cb.load(st, snap, cb.user));
        bool same = true;
        for (int k = 0; k < N; ++k) {
            cb.advance(st, pad(K + k, 0), pad(K + k, 1), cb.user);
            if (st.checksum() != sums[static_cast<size_t>(k)]) same = false;
        }
        CHECK(same);
        CHECK(d.hash() == endHash);
        std::vector<uint8_t> junk = {1, 2, 3, 4};
        CHECK(!cb.load(st, junk, cb.user));   // a malformed snapshot is refused
    }

    // round flow: entrance poses -> fight -> round end -> match over, always in that order
    {
        FoitDuel d;
        FoitDuelConfig c = makeCfg(8, 1, false, true);
        c.readyTicks = 30;
        c.endHold = 40;
        CHECK(d.init(c));
        fight::MatchState st;
        d.project(st);
        const fight::Callbacks cb = fight::foitDuelCallbacks(&d);
        std::vector<int> seen;
        for (int t = 0; t < 9000 && !d.over(); ++t) {
            const bool fighting = d.phase() == foit::rt::Match::Fight;
            cb.advance(st, fighting ? static_cast<uint16_t>(fight::kRight | ((t / 10) % 2 ? fight::kLP : fight::kMK)) : 0, 0, cb.user);
            if (seen.empty() || seen.back() != static_cast<int>(d.phase())) seen.push_back(static_cast<int>(d.phase()));
        }
        CHECK(d.over());
        CHECK(!seen.empty() && seen.front() == static_cast<int>(foit::rt::Match::Intro));
        auto firstAt = [&](int ph) {
            for (size_t i = 0; i < seen.size(); ++i)
                if (seen[i] == ph) return static_cast<int>(i);
            return -1;
        };
        const int iIntro = firstAt(foit::rt::Match::Intro), iReady = firstAt(foit::rt::Match::Ready), iFight = firstAt(foit::rt::Match::Fight),
                  iEnd = firstAt(foit::rt::Match::Ending), iOver = firstAt(foit::rt::Match::Over);
        CHECK(iIntro == 0 && iReady > iIntro && iFight > iReady && iEnd > iFight && iOver > iEnd);
        CHECK(d.matchWinner() >= -1 && d.matchWinner() <= 1);
        CHECK(d.roundReason() >= 1 && d.roundReason() <= 3);
        d.project(st);
        CHECK(st.roundOver);   // the HUD / AI see the round as finished
    }

    // training: no clock, nobody dies, the match never ends
    {
        FoitDuel d;
        CHECK(d.init(makeCfg(99, 2, true, false)));
        fight::MatchState st;
        d.project(st);
        const fight::Callbacks cb = fight::foitDuelCallbacks(&d);
        bool alive = true;
        for (int t = 0; t < 1500; ++t) {
            cb.advance(st, (t % 24) < 2 ? fight::kHP : (t % 24) < 14 ? fight::kRight : 0, 0, cb.user);
            if (d.body(1).life() <= 0 || d.body(0).life() <= 0) alive = false;
        }
        CHECK(alive);
        CHECK(!d.over());
        CHECK_EQ(d.round(), 1);
        CHECK(d.world().stats.hits > 0u);
    }

    // a drawn round that reaches the target: the match ends as a draw, or (tie-break on) a deciding round is played
    for (int tb = 0; tb < 2; ++tb) {
        FoitDuel d;
        FoitDuelConfig c = makeCfg(1, 1, false, false);
        c.tieBreak = tb == 1;
        CHECK(d.init(c));
        fight::MatchState st;
        d.project(st);
        const fight::Callbacks cb = fight::foitDuelCallbacks(&d);
        for (int t = 0; t < 4000 && !d.over() && d.round() < 2; ++t) cb.advance(st, 0, 0, cb.user);
        if (tb == 0) {
            CHECK(d.over());
            CHECK_EQ(d.matchWinner(), -1);
            CHECK_EQ(d.round(), 1);
        } else {
            CHECK(!d.over());
            CHECK_EQ(d.round(), 2);
            CHECK_EQ(d.match().roundsToWin, 2);
        }
    }

    // an attack button pressed during the entrance cuts it short; one already held when it started does not
    {
        auto introLength = [&](uint16_t first, int pressAt, bool hold) {
            FoitDuel d;
            CHECK(d.init(makeCfg(99, 2, false, true)));
            fight::MatchState st;
            d.project(st);
            const fight::Callbacks cb = fight::foitDuelCallbacks(&d);
            int t = 0;
            for (; t < 3000 && d.phase() == foit::rt::Match::Intro; ++t) {
                uint16_t in = hold ? first : (t == pressAt ? first : 0);
                cb.advance(st, in, 0, cb.user);
            }
            return t;
        };
        const int plain = introLength(0, -1, false);
        const int pressed = introLength(fight::kLP, 10, false);
        const int held = introLength(fight::kLP, 0, true);
        CHECK(plain > 60 && plain < 3000);
        CHECK(pressed < plain);
        CHECK_EQ(held, plain);
    }
}

static void testFoitHitInfoReal(const std::vector<std::string>& dirs) {
    std::printf("[fight runtime: hit info with real Annie (damage, stun, chip, meter)]\n");
    std::shared_ptr<foit::Character> ch;
    std::string base;
    for (const std::string& d : dirs) {
        for (const char* sub : {"/characters", ""}) {
            std::vector<uint8_t> bytes;
            if (!readFile(d + sub + "/annie.foit", bytes)) continue;
            auto c = std::make_shared<foit::Character>();
            std::string err;
            if (foit::parse(bytes.data(), bytes.size(), *c, &err)) {
                ch = c;
                base = d + sub;
                break;
            }
        }
        if (ch) break;
    }
    if (!ch) {
        skip("no annie.foit under ZDROID_TEST_FOIT");
        return;
    }
    using namespace foit::rt;
    // stun per strength: the game's own table when it is next to the characters, else the documented values
    float table[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    std::vector<uint8_t> ini;
    if (readFile(base + "/_dizzy_values.ini", ini)) parseStrengthTable(std::string(ini.begin(), ini.end()), table);
    else parseStrengthTable("Light 15\nMedium 20\nHeavy 30\n", table);

    // a light punch on an idle opponent: DAMAGE 300, 15 stun (STUNDAMAGE = BASESTUN) added to TOTALSTUN by the defender's own script
    {
        Object p1, p2;
        init(p1, *ch, ch->body, -10.0f, true);
        init(p2, *ch, ch->body, 30.0f, false);
        p2.slots[25] = 2;
        p1.enemy = &p2;
        p2.enemy = &p1;
        World w;
        std::memcpy(w.strengthTable, table, sizeof table);
        w.objects = {&p1, &p2};
        enterState(p1, w, findState(ch->body, "Stand"));
        enterState(p2, w, findState(ch->body, "Stand"));
        const float stun0 = p2.slots[38];
        Sample prev1, prev2;
        for (int t = 0; t < 120; ++t) {
            const bool press = t >= 30 && t < 32;
            Sample in[2];
            in[0] = composeSample(prev1, DirNeutral, press ? 0x1u : 0u);
            in[1] = composeSample(prev2, DirNeutral, 0);
            prev1 = in[0];
            prev2 = in[1];
            tickAll(w, in, 2);
            w.events.clear();
        }
        CHECK_EQ(static_cast<int>(w.stats.hits), 1);
        CHECK(p2.slots[84] == 300.0f);                               // DAMAGE: whole(fc x proration) x DAMAGEMULT
        CHECK(p2.slots[85] == 300.0f);                               // the raw damage of the data
        CHECK(p2.life() == 14000.0f);
        CHECK(p2.slots[88] == table[1] && p2.slots[87] == table[1]); // BASESTUN / STUNDAMAGE of a light hit
        CHECK(p2.slots[38] == stun0 + table[1]);                     // TOTALSTUN took it
        CHECK(p2.slots[89] > 1.0f && p2.slots[89] == p2.slots[92]);  // METERGAIN = BASEMETERGAIN = the hit's meter value
        CHECK(p2.slots[86] == 0.0f);                                 // an unblocked light punch carries no chip
        CHECK(p2.slots[90] == 0.0f && p2.slots[21] == 0.0f);         // counter hits allowed, the hit can kill
    }
    // counter hit: the defender is in the startup of its own punch (its COUNTERHIT is set), the engine hands that over as WASCH and the
    // hit state's script takes 1.5 x the damage
    {
        Object p1, p2;
        init(p1, *ch, ch->body, -10.0f, true);
        init(p2, *ch, ch->body, 30.0f, false);
        p2.slots[25] = 2;
        p1.enemy = &p2;
        p2.enemy = &p1;
        World w;
        std::memcpy(w.strengthTable, table, sizeof table);
        w.objects = {&p1, &p2};
        enterState(p1, w, findState(ch->body, "Stand"));
        enterState(p2, w, findState(ch->body, "Stand"));
        Sample prev1, prev2;
        for (int t = 0; t < 120; ++t) {
            Sample in[2];
            in[0] = composeSample(prev1, DirNeutral, (t >= 30 && t < 32) ? 0x1u : 0u);
            in[1] = composeSample(prev2, DirNeutral, (t >= 32 && t < 34) ? 0x1u : 0u);
            prev1 = in[0];
            prev2 = in[1];
            tickAll(w, in, 2);
            w.events.clear();
        }
        CHECK_EQ(static_cast<int>(w.stats.hits), 1);
        CHECK(p2.slots[70] == 1.0f);                                 // WASCH
        CHECK(p2.slots[84] == 450.0f && p2.life() == 13850.0f);      // 300 x 1.5
    }
    // the same fighters, Annie's crescent-less melee special on a crouch-blocking opponent: chip = fe x DAMAGEMULT, taken off LIFEBAR and REDLIFE
    {
        Object p1, p2;
        init(p1, *ch, ch->body, -40.0f, true);
        init(p2, *ch, ch->body, 30.0f, false);
        p2.slots[25] = 2;
        p1.enemy = &p2;
        p2.enemy = &p1;
        World w;
        std::memcpy(w.strengthTable, table, sizeof table);
        w.objects = {&p1, &p2};
        enterState(p1, w, findState(ch->body, "Stand"));
        enterState(p2, w, findState(ch->body, "Stand"));
        Sample prev1, prev2;
        for (int t = 0; t < 140; ++t) {
            int d1 = DirNeutral;
            unsigned m1 = 0;
            if (t >= 30 && t < 33) d1 = DirDown;                     // d, db, b + LP
            else if (t >= 33 && t < 36) d1 = DirDownLeft;
            else if (t >= 36 && t < 38) { d1 = DirLeft; m1 = 0x1u; }
            Sample in[2];
            in[0] = composeSample(prev1, d1, m1);
            in[1] = composeSample(prev2, DirDownRight, 0);           // P2 faces left: down + back
            prev1 = in[0];
            prev2 = in[1];
            tickAll(w, in, 2);
            w.events.clear();
        }
        CHECK_EQ(static_cast<int>(w.stats.blocks), 1);
        CHECK_EQ(static_cast<int>(w.stats.hits), 0);
        CHECK(p2.slots[86] == 100.0f);                               // CHIPDAMAGE = fe x scale
        CHECK(p2.life() == 14200.0f && p2.slots[65] == 14200.0f);    // the block-stun script took it off LIFEBAR and REDLIFE
        CHECK(p1.slots[66] > 0.0f && p2.slots[66] > 0.0f);           // both fighters gained meter from the block
    }
}


// ============================================================ ERROR CODES, MOD CHECK, RAM PROBE
static std::vector<uint8_t> testWav(uint32_t dataBytes, uint32_t declared) {
    std::vector<uint8_t> w;
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) w.push_back(static_cast<uint8_t>(v >> (8 * i))); };
    auto u16 = [&](uint16_t v) { w.push_back(static_cast<uint8_t>(v)); w.push_back(static_cast<uint8_t>(v >> 8)); };
    const char* riff = "RIFF"; w.insert(w.end(), riff, riff + 4);
    u32(36 + declared);
    const char* wave = "WAVEfmt "; w.insert(w.end(), wave, wave + 8);
    u32(16); u16(1); u16(1); u32(22050); u32(44100); u16(2); u16(16);
    const char* data = "data"; w.insert(w.end(), data, data + 4);
    u32(declared);
    w.insert(w.end(), dataBytes, 0x11);
    return w;
}

static std::vector<uint8_t> testPng(bool withEnd) {
    std::vector<uint8_t> p = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13, 'I', 'H', 'D', 'R'};
    p.insert(p.end(), 13 + 4, 0);                     // IHDR payload + crc
    if (withEnd) { const uint8_t e[12] = {0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82}; p.insert(p.end(), e, e + 12); }
    else p.insert(p.end(), 12, 0x55);
    return p;
}

static void putFile(const std::string& path, const std::vector<uint8_t>& b) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

static void testErrCodes() {
    std::printf("[error codes: registry, display text, crash marker]\n");
    err::Registry r;
    CHECK(!r.any());
    CHECK_EQ(r.primary(), err::kNone);
    r.raise(err::kNoData, "no characters");
    r.raise(err::kCrash, "boom");
    r.raise(err::kNoData, "no characters");           // same detail: kept once
    r.raise(err::kNoData, "no scenes");
    r.raise(static_cast<err::Code>(9), "ignored");     // not a code
    r.raise(static_cast<err::Code>(0), "ignored");
    CHECK(r.any());
    CHECK_EQ(r.primary(), err::kCrash);                // the most severe (lowest number) is shown first
    CHECK_EQ(r.active().size(), static_cast<size_t>(2));
    CHECK_EQ(r.details(err::kNoData).size(), static_cast<size_t>(2));
    const uint32_t gen = r.generation();
    r.clear(err::kCrash);
    CHECK(r.generation() != gen);
    CHECK_EQ(r.primary(), err::kNoData);
    CHECK_EQ(err::displayText(err::kNoData), std::string("Err 3"));
    CHECK_EQ(err::displayText(err::kNone), std::string());
    CHECK(err::isFatal(err::kCrash) && !err::isFatal(err::kNoAudio));
    for (int c = 1; c <= err::kCodeCount; ++c) {
        CHECK(std::string(err::meaning(static_cast<err::Code>(c), true)).size() > 5);
        CHECK(std::string(err::meaning(static_cast<err::Code>(c), false)).size() > 5);
    }
    r.clearAll();
    CHECK(!r.any());

    // crash marker: clean exit leaves nothing, a recorded crash raises code 1 next run, a stale "running" does not
    const std::string dir = makeTempDir("errmark");
    const std::string marker = dir + "/state/session.mark";
    auto slurp = [&](const std::string& p) { std::ifstream f(p, std::ios::binary); std::string t((std::istreambuf_iterator<char>(f)), {}); return t; };
    {
        err::Registry reg;
        const err::SessionInfo si = err::beginSession(marker, reg);
        CHECK(!si.previousCrash && si.uncleanNote.empty() && !reg.any());
        CHECK_EQ(slurp(marker), std::string("running\n"));
        err::markBackground();
        CHECK_EQ(slurp(marker), std::string("background\n"));
        err::markForeground();
        CHECK_EQ(slurp(marker), std::string("running\n"));
        err::endSession();
        CHECK(!fs::exists(marker));
    }
    writeText(marker, "crash signal 11\n");
    {
        err::Registry reg;
        const err::SessionInfo si = err::beginSession(marker, reg);
        CHECK(si.previousCrash);
        CHECK_EQ(si.crashText, std::string("signal 11"));
        CHECK_EQ(reg.primary(), err::kCrash);
        err::endSession();
    }
    writeText(marker, "running\n");                    // e.g. the OS killed the app: not a crash the player saw
    {
        err::Registry reg;
        const err::SessionInfo si = err::beginSession(marker, reg);
        CHECK(!si.previousCrash && !si.uncleanNote.empty() && !reg.any());
        err::endSession();
    }
    writeText(marker, "background\n");
    {
        err::Registry reg;
        const err::SessionInfo si = err::beginSession(marker, reg);
        CHECK(!si.previousCrash && si.uncleanNote.empty() && !reg.any());
        err::endSession();
    }
#if defined(__unix__) || defined(__APPLE__)
    {   // a real fatal signal in a child process leaves "crash signal 11" for the next run
        const pid_t pid = fork();
        if (pid == 0) {
            err::Registry reg;
            err::beginSession(marker, reg);
            err::installCrashHandlers();
            std::raise(SIGSEGV);
            _exit(0);
        }
        int status = 0;
        CHECK(pid > 0 && waitpid(pid, &status, 0) == pid);
        CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV);   // the default action still went ahead
        CHECK_EQ(slurp(marker), std::string("crash signal 11\n"));
        err::Registry reg;
        const err::SessionInfo si = err::beginSession(marker, reg);
        CHECK(si.previousCrash && reg.has(err::kCrash));
        err::endSession();
    }
#endif
    fs::remove_all(dir);
}

static void testModCheck() {
    std::printf("[mod check: damaged (4), altering (5), conflicts, mount gate]\n");
    using modcheck::Severity;
    std::string why;
    // bytes level
    CHECK(modcheck::checkBytes(".wav", testWav(64, 64).data(), testWav(64, 64).size(), &why));
    {
        const auto w = testWav(10, 64);   // the header promises 64 bytes of audio, only 10 are there
        CHECK(!modcheck::checkBytes(".wav", w.data(), w.size(), &why));
        CHECK_EQ(why, std::string("WAV audio is cut short"));
    }
    {
        const auto p = testPng(true), q = testPng(false);
        CHECK(modcheck::checkBytes(".png", p.data(), p.size(), &why));
        CHECK(!modcheck::checkBytes(".png", q.data(), q.size(), &why));
    }
    {
        std::vector<uint8_t> d = makeDdsHeader(8, 8, "DXT5", 0x4, 0, 1, 0x1000);
        d.insert(d.end(), 16 * 4, 0);     // 2x2 blocks
        CHECK(modcheck::checkBytes(".dds", d.data(), d.size(), &why));
        d.resize(d.size() - 1);
        CHECK(!modcheck::checkBytes(".dds", d.data(), d.size(), &why));
    }
    CHECK(!modcheck::checkBytes(".foit", reinterpret_cast<const uint8_t*>("garbage"), 7, &why));
    CHECK(!modcheck::checkBytes(".gbs", reinterpret_cast<const uint8_t*>("garbage"), 7, &why));
    CHECK(!modcheck::checkBytes(".pal", reinterpret_cast<const uint8_t*>("garbage"), 7, &why));
    CHECK(modcheck::checkBytes(".lua", reinterpret_cast<const uint8_t*>("anything"), 8, &why));   // unknown kinds pass
    CHECK(modcheck::isCodeExtension(".dll") && modcheck::isCodeExtension(".so") && !modcheck::isCodeExtension(".dds"));

    const std::string root = makeTempDir("modcheck");
    // a good mod
    {
        std::vector<uint8_t> d = makeDdsHeader(4, 4, "DXT1", 0x4, 0, 1, 0x1000);
        d.insert(d.end(), 8, 0);
        putFile(root + "/good/data/characters/filia_1p.dds", d);
        putFile(root + "/good/sound/hit.wav", testWav(32, 32));
        putFile(root + "/good/art/logo.png", testPng(true));
        writeText(root + "/good/mod.ini", "name = Good\npriority = 10\n");
        writeText(root + "/good/README.txt", "");                   // an empty text file is fine
        const modcheck::Result r = modcheck::check(root + "/good");
        CHECK(!r.blocked());
        CHECK(r.issues.empty());
        CHECK_EQ(r.files, static_cast<size_t>(5));
        CHECK_EQ(r.firstProblem(), std::string());
    }
    // damaged: each kind of damage on its own
    struct Bad { const char* dir; const char* rel; std::vector<uint8_t> bytes; };
    std::vector<uint8_t> shortDds = makeDdsHeader(64, 64, "DXT5", 0x4, 0, 1, 0x1000);
    shortDds.insert(shortDds.end(), 16, 0);
    const std::vector<Bad> bad = {
        {"b_dds", "a/tex.dds", shortDds},
        {"b_wav", "a/v.wav", testWav(10, 64)},
        {"b_empty", "a/zero.wav", {}},
        {"b_png", "a/p.png", testPng(false)},
        {"b_ini", "mod.ini", {'n', 'a', 0, 'm', 'e'}},
        {"b_foit", "characters/x.foit", {'n', 'o', 'p', 'e'}},
        {"b_gbs", "ui/x.gbs", {'n', 'o', 'p', 'e'}},
        {"b_pal", "pal/x.pal", {'n', 'o', 'p', 'e'}},
    };
    for (const Bad& b : bad) {
        putFile(root + "/" + b.dir + "/" + b.rel, b.bytes);
        const modcheck::Result r = modcheck::check(root + "/" + b.dir);
        CHECK(r.damaged());
        CHECK(!r.altered());
        CHECK(r.blocked());
        CHECK(r.firstProblem().find(b.rel) == 0);
    }
    // altering: native code, scripts, a link that leaves the folder
    for (const char* rel : {"lib/libhook.so", "CoreMessaging.dll", "run.sh", "x.EXE"}) {
        putFile(root + "/alt/" + rel, {1, 2, 3});
    }
    {
        const modcheck::Result r = modcheck::check(root + "/alt");
        CHECK(r.altered());
        size_t n = 0;
        for (const auto& i : r.issues) n += i.sev == Severity::Altered;
        CHECK_EQ(n, static_cast<size_t>(4));
    }
#if defined(__unix__) || defined(__APPLE__)
    {
        std::error_code ec;
        fs::create_directories(root + "/link", ec);
        writeText(root + "/outside.txt", "secret");
        fs::create_symlink(root + "/outside.txt", root + "/link/escape.txt", ec);
        CHECK(!ec);
        CHECK(modcheck::check(root + "/link").altered());
        fs::create_directories(root + "/link2/real", ec);
        writeText(root + "/link2/real/a.txt", "x");
        fs::create_symlink(root + "/link2/real/a.txt", root + "/link2/alias.txt", ec);
        CHECK(!modcheck::check(root + "/link2").blocked());      // a link that stays inside the folder is fine
    }
#endif
    // damaged + altering together: the altering problem is reported first
    putFile(root + "/both/x.dll", {1});
    putFile(root + "/both/z.wav", {});
    CHECK(modcheck::check(root + "/both").firstProblem().find("x.dll") == 0);
    CHECK(!modcheck::check(root + "/nowhere").blocked());        // a missing folder is simply clean
    {
        err::Registry reg;
        modcheck::report("mod both", modcheck::check(root + "/both"), reg);
        CHECK(reg.has(err::kRuntimeAltered) && reg.has(err::kModDamaged));
        err::Registry reg2;
        modcheck::report("mod good", modcheck::check(root + "/good"), reg2);
        CHECK(!reg2.any());
    }

    // conflicts: later layer wins, suffix rule, unrelated files stay out
    {
        const std::string c = makeTempDir("modconf");
        writeText(c + "/a/data/characters/filia_1p.dds", "a");
        writeText(c + "/a/sound/x.wav", "a");
        writeText(c + "/a/a_only.png", "a");
        writeText(c + "/a/mod.ini", "name = a");
        writeText(c + "/b/characters/filia_1p.dds", "b");
        writeText(c + "/b/ui/x.wav", "b");
        writeText(c + "/b/mod.ini", "name = b");
        writeText(c + "/c/filia_1p.dds", "c");
        writeText(c + "/c/mod.ini", "name = c");
        const std::vector<modcheck::Conflict> cf = modcheck::conflicts({{"a", c + "/a"}, {"b", c + "/b"}, {"c", c + "/c"}});
        CHECK_EQ(cf.size(), static_cast<size_t>(1));
        if (cf.size() == 1) {
            CHECK_EQ(cf[0].path, std::string("data/characters/filia_1p.dds"));
            CHECK_EQ(cf[0].winner, std::string("c"));
            CHECK_EQ(cf[0].overridden.size(), static_cast<size_t>(2));
            CHECK_EQ(cf[0].overridden[0], std::string("a"));
            CHECK_EQ(cf[0].overridden[1], std::string("b"));
        }
        CHECK(modcheck::conflicts({{"a", c + "/a"}}).empty());
        fs::remove_all(c);
    }

    // the mount gate: mods/modscustom folders that fail the check are never mounted
    {
        err::global().clearAll();
        const std::string game = makeTempDir("modgate");
        const modsdir::Layout l = modsdir::layout(game);
        modsdir::ensure(game);
        writeText(game + "/data/x/hello.txt", "base");
        putFile(l.custom + "/fine/x/hello.txt", {'o', 'k'});
        putFile(l.custom + "/fine/x/shared.png", testPng(true));
        putFile(l.custom + "/fine2/x/shared.png", testPng(true));
        putFile(l.custom + "/broken/x/hello.txt", {'b'});
        putFile(l.custom + "/broken/x/tex.dds", {});
        putFile(l.custom + "/hooked/x/hello.txt", {'h'});
        putFile(l.custom + "/hooked/libpatch.so", {0x7f, 'E', 'L', 'F'});
        Vfs vfs;
        vfs.mount(game);
        const modsdir::Report rep = modsdir::mountAll(vfs, game, false);
        CHECK_EQ(rep.mods, static_cast<size_t>(2));                    // fine + fine2
        std::string t;
        CHECK(vfs.readText("hello.txt", t));
        CHECK_EQ(t, std::string("ok"));                                  // neither broken nor hooked got over the base
        size_t blocked = 0;
        for (const modsdir::Item& it : rep.items) {
            if (it.id == "broken" || it.id == "hooked") { CHECK(it.blocked && !it.mounted && !it.problem.empty()); ++blocked; }
            if (it.id == "fine") CHECK(it.mounted && !it.blocked);
        }
        CHECK_EQ(blocked, static_cast<size_t>(2));
        CHECK(err::global().has(err::kModDamaged));
        CHECK(err::global().has(err::kRuntimeAltered));
        CHECK_EQ(rep.conflicts.size(), static_cast<size_t>(1));          // shared.png: fine2 wins over fine
        if (!rep.conflicts.empty()) {
            CHECK(rep.conflicts[0].winner.find("fine2") != std::string::npos);
        }
        err::global().clearAll();
        fs::remove_all(game);
    }

    // the Lite diagnosis (codes 2 and 3)
    {
        Inventory inv;
        Vfs empty;
        err::Registry reg;
        diagnose(inv, empty, true, reg);
        CHECK(reg.has(err::kNoAudio) && reg.has(err::kNoData));
        CHECK(reg.details(err::kNoData).size() == 4);
        err::Registry reg2;
        diagnose(inv, empty, false, reg2);
        CHECK_EQ(reg2.details(err::kNoAudio).size(), static_cast<size_t>(3));   // + "no output device"
        inv.characters.push_back(CharacterAssets{});
        inv.scenes.push_back(SceneInfo{});
        inv.levels.push_back(LevelInfo{});
        inv.ddsFiles = 3;
        inv.music.push_back(MusicTrack{});
        const std::string d = makeTempDir("lite");
        putFile(d + "/sound/hit.wav", testWav(8, 8));
        Vfs v;
        v.mount(d);
        err::Registry reg3;
        diagnose(inv, v, true, reg3);
        CHECK(!reg3.any());
        fs::remove_all(d);
    }
    fs::remove_all(root);
}

static void testRamProbe() {
    std::printf("[ram probe: process figures, growth tracker, display strings]\n");
    const ram::Process p = ram::process();
#if defined(__linux__)
    CHECK(p.ok);
    CHECK(p.rssBytes > 1024ull * 1024);
    CHECK(p.peakBytes >= p.rssBytes);
    CHECK(p.virtBytes >= p.rssBytes);
#else
    (void)p;
#endif
    CHECK_EQ(ram::megabytes(5ull * 1024 * 1024 + 1), std::string("0005"));
    CHECK_EQ(ram::megabytes(412ull * 1024 * 1024), std::string("0412"));
    CHECK_EQ(ram::megabytes(123456ull * 1024 * 1024), std::string("9999"));
    CHECK_EQ(ram::megabytes(7ull * 1024 * 1024, 2), std::string("07"));
    CHECK_EQ(ram::hex(0xA3F09C12u), std::string("A3F09C12"));
    CHECK_EQ(ram::hex(0xFu, 4), std::string("000F"));
    ram::Tracker t;
    t.update(0, 100ull << 20);
    t.update(10000, 130ull << 20);          // window not over yet
    CHECK(!t.growing());
    t.update(60000, 220ull << 20);          // +120 MB in a minute
    CHECK(t.growing());
    CHECK(t.mbPerMin() > 100.0 && t.mbPerMin() < 140.0);
    CHECK_EQ(t.peakBytes(), 220ull << 20);
    t.update(120000, 220ull << 20);
    CHECK(!t.growing());
}


int main() {
    testVfs();
    testMusicLibrary();
    testBonusMusic();
    testTextFormats();
    testGbsSynthetic();
    testGbsAnimSynthetic();
    testGbsTextLayout();
    testGbsArc();
    testMenuListAnimSynthetic();
    testMenuData();
    testRle();
    testDds();
    testSteamLogic();
    testSteamRunner();
    testSteamTask();

    if (const char* o = std::getenv("ZDROID_TEST_ORACLE"); o && *o) testOracle(o);
    else { std::printf("[art decoder vs oracle]\n"); skip("set ZDROID_TEST_ORACLE (run tests/oracle/gen_oracle.py)"); }

    if (const char* d = std::getenv("ZDROID_TEST_DATA"); d && *d) { testRealData(splitColon(d)); testMenuDataReal(splitColon(d)); testGbsSceneReal(splitColon(d)); testGbsAnimReal(splitColon(d)); testGbsArcReal(splitColon(d)); testMenuListAnimReal(splitColon(d)); testStoryReal(splitColon(d)); testRealMoveSets(splitColon(d)); testRealStages(splitColon(d)); }
    else { std::printf("[real game data]\n"); skip("set ZDROID_TEST_DATA=<core dir>:<Win dir>:<ui dir>"); }

    if (const char* fd = std::getenv("ZDROID_TEST_FOIT"); fd && *fd) testFoitRuntimeReal(splitColon(fd));
    else { std::printf("[fight runtime: real Filia throw + rollback]\n"); skip("set ZDROID_TEST_FOIT=<folder with filia.foit (or characters/filia.foit)>"); }
    testFoitHitInfoSynthetic();
    testFoitScreenTouchAndPushback();
    testFoitTether();
    if (const char* fd = std::getenv("ZDROID_TEST_FOIT"); fd && *fd) testFoitHitInfoReal(splitColon(fd));
    if (const char* fd = std::getenv("ZDROID_TEST_FOIT"); fd && *fd) testFoitMatchReal(splitColon(fd));
    if (const char* fd = std::getenv("ZDROID_TEST_FOIT"); fd && *fd) testFoitDuelReal(splitColon(fd));
    else { std::printf("[fight runtime: hit info with real Annie]\n"); skip("set ZDROID_TEST_FOIT=<folder with annie.foit (or characters/annie.foit)>"); }

    testStorySynthetic();
    testStoryVoice();
    testModsItems();
    testPalOrganise();
    testPalmod();
    testPalgen();
    testModsDir();
    testErrCodes();
    testModCheck();
    testRamProbe();
    if (const char* pd = std::getenv("ZDROID_TEST_PAL"); pd && *pd) testPalmodReal(pd);
    else { std::printf("[custom palettes: real Filia .pal]\n"); skip("set ZDROID_TEST_PAL=<folder with FiliaInaccurateAfterSamson.pal + filia_1p.dds>"); }
    testArcadeLadder();
    testSavIni();
    testOddToneRows();
    testLevelParser();
    testResolveLevel();
    testLevelShadowing();
    testHubNormalsSynthetic();
    testFoitVm();
    testFoitRuntimeSynthetic();
    testImgScale();
    testDevOptions();
    testHudAtlasAndPortrait();
    testSprTiles();
    if (const char* sp = std::getenv("ZDROID_TEST_SPR"); sp && *sp) testSprReal(sp);
    else { std::printf("[real loading sprites]\n"); skip("set ZDROID_TEST_SPR=<folder with loading_en.dds + loading_en.spr.msb>"); }

    if (const char* c = std::getenv("ZDROID_TEST_CHAR"); c && *c) {
        testRealCharacter(c);
        testFoit(c);
        testMoveSetAndDuel(c);
    }
    if (const char* c = std::getenv("ZDROID_TEST_FOITS"); c && *c) {
        testFoitAll(c);
        testHubNormalsReal(c);
    }
    else { std::printf("[real character files: annie]\n"); skip("set ZDROID_TEST_CHAR=<folder with annie.foit/.art/.art-pt/_1p.dds>"); }

    if (const char* t = std::getenv("ZDROID_TEST_DDS"); t && *t) { std::printf("[real .dds textures]\n"); testDdsReal(t); }
    else { std::printf("[real .dds textures]\n"); skip("set ZDROID_TEST_DDS=<folder with .dds files>"); }

    std::printf("\n%d checks, %d failed, %d skipped\n", g_checks, g_fail, g_skipped);
    return g_fail ? 1 : 0;
}
