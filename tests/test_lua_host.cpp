// test_lua_host.cpp - the Lua host of the lessons (src/core/lua_host.*): motions, sprites (slots / flags / tags), callbacks, coroutines, the dummy's
// pad, saveState, require, ui.*.  A fake fight with two bare objects, no game data.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "foit_names.h"
#include "lesson_data.h"
#include "lesson_rules.h"
#include "lua_host.h"
#include "palfx.h"
#include "vfs.h"

using namespace zengine;

static int g_checks = 0, g_fail = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(c)) {                                                           \
            ++g_fail;                                                         \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
        }                                                                     \
    } while (0)

struct Fake : script::FightAccess {
    foit::rt::World w;
    foit::rt::Object o[3];
    int saves = 0, loads = 0;
    std::vector<uint8_t> last;
    Fake() {
        for (auto& x : o) {
            for (float& s : x.slots) s = 0;
            for (uint32_t& f : x.flags) f = 0;
        }
        o[0].id = 1;
        o[1].id = 2;
        o[2].id = 3;
        w.objects = {&o[0], &o[1], &o[2]};
        o[2].leader = &o[0];   // a child of P1
    }
    foit::rt::World* world() override { return &w; }
    foit::rt::Object* character(int i) override { return i == 0 ? &o[0] : i == 3 ? &o[1] : nullptr; }
    bool saveState(std::vector<uint8_t>& out) override {
        ++saves;
        out = {static_cast<uint8_t>(saves), 7, 7};
        return true;
    }
    bool loadState(const std::vector<uint8_t>& in) override {
        ++loads;
        last = in;
        return true;
    }
};

struct Rec : script::UiSink {
    std::vector<std::string> ev;
    bool expl = false;
    void clearAll() override { ev.push_back("clear"); }
    void setMax(int n) override { ev.push_back("max" + std::to_string(n)); }
    void add(const std::string& k, int p, int m) override { ev.push_back("add:" + k + ":" + std::to_string(p) + "/" + std::to_string(m)); }
    void update(int i, const std::string& k, int p, int m) override { ev.push_back("upd:" + std::to_string(i) + ":" + k + ":" + std::to_string(p) + "/" + std::to_string(m)); }
    void complete(int i, bool d) override { ev.push_back(std::string("done:") + std::to_string(i) + (d ? "T" : "F")); }
    void setExplanation(const std::string& k) override { ev.push_back("expl:" + k); }
    void showExplanation(bool on) override { ev.push_back(on ? "showE" : "hideE"); }
    bool explanationActive() override { return expl; }
    void p1Disable(bool off) override { ev.push_back(off ? "p1off" : "p1on"); }
    void endMission() override { ev.push_back("end"); }
    void screenFade(float a, int d, float al) override { ev.push_back("fade:" + std::to_string(static_cast<int>(a * 10)) + ":" + std::to_string(d) + ":" + std::to_string(static_cast<int>(al * 10))); }
    bool has(const std::string& s) const {
        for (const auto& e : ev)
            if (e == s) return true;
        return false;
    }
};

static void testMotions() {
    using script::InputFrame;
    auto m = script::parseMotion("D,DF,F+HP");
    CHECK(m.size() == 7);
    CHECK(m[0].dir == 2 && m[0].buttons == 0);
    CHECK(m[2].dir == 1);
    CHECK(m[4].dir == 0 && m[4].buttons == 4);
    m = script::parseMotion("N+Jab+Strong");
    CHECK(m.size() == 3 && m[0].dir == 8 && m[0].buttons == 3);
    m = script::parseMotion("UF,VALID=4");
    CHECK(m.size() == 3 && m[0].dir == 7);
    m = script::parseMotion("D(L)+Short");
    CHECK(!m.empty() && m[0].dir == 2 && m[0].buttons == 8);
    m = script::parseMotion("!D(L)+Fierce");
    CHECK(!m.empty() && m[0].dir == 8 && m[0].buttons == 4);
    m = script::parseMotion("N+~Fierce");
    CHECK(!m.empty() && m[0].buttons == 0);
    m = script::parseMotion("X+HP");
    CHECK(!m.empty() && m[0].dir == 8 && m[0].buttons == 4);
    m = script::parseMotion("D,DB,B+Short+Fwd");
    CHECK(m.back().dir == 4 && m.back().buttons == (8 | 16));
    m = script::parseMotion("N+3K");
    CHECK(!m.empty() && m[0].buttons == (8 | 16 | 32));
    CHECK(script::parseMotion("").empty());
    CHECK(script::parseMotion("zzz").empty());
}

static void testHost() {
    Fake f;
    Rec ui;
    std::map<std::string, std::string> files;
    files["Mod.lua"] = "return { answer = 42 }\n";
    files["Lib.lua"] = "libcount = (libcount or 0) + 1\nfunction wait() coroutine.yield() end\n";
    files["main.lua"] = R"LUA(
require("Lib")
require("Lib")
ui.add("libcount" .. libcount, 0, 0)
local mod = require("Mod")
ui.add("mod" .. mod.answer .. tostring(require("Mod") == mod) .. tostring(require("Lib")), 0, 0)
function getP1() return getChar(0) end   -- LuaLib.lua defines these on top of getChar
function getP2() return getChar(3) end
local p1, p2 = getP1(), getP2()
local mt = getmetatable("")
mt.__mod = function(a, b) if type(b) == "table" then return string.format(a, unpack(b)) else return string.format(a, b) end end
ui.add("%d-%s" % { 5, "x" }, 0, 0)
ui.add("h" .. tostring(hash("Filia")), 0, 0)
setInit(function() initcalls = (initcalls or 0) + 1; ui.add("init" .. initcalls, 0, 0) end)
local ticks = 0
local co = coroutine.create(function() wait(); wait(); ui.complete(1, true) end)
setUpdate(function()
  ticks = ticks + 1
  if coroutine.status(co) == "suspended" then coroutine.resume(co) end
  if ticks == 1 then
    p1.numhits = 3
    p1.flag5 = 1
    p1.slot7 = 9
    ui.add("hits" .. p1.numhits .. "/" .. p1.NumHits .. "/f" .. p1.flag5 .. p1.flag6 .. "/s" .. p1.slot7, 0, 0)
    ui.add("same" .. tostring(p1 == getChar(0)) .. tostring(p1 == p2) .. tostring(getChar(1) == nil) .. #p1:getChildren(), 0, 0)
    ui.add("tag" .. tostring(p1:hasTag("Idle")), 0, 0)
    p1:addForbiddenTag("Taunt", "Super")
    p2:blockingAllowed(false)
  elseif ticks == 2 then
    addInput(0, 2, 5)
  elseif ticks == 3 then
    doMotion("D,DF,F+HP")
  elseif ticks == 4 then
    state = saveState()
    ui.p1disable(true)
  elseif ticks == 5 then
    state()
    ui.endmission()
    local x = nil
    x.boom = 1
  end
end)
)LUA";
    script::LuaHost h;
    const bool ok = h.load(&f, &ui, [&](const std::string& n, std::string& out) {
        auto it = files.find(n);
        if (it == files.end()) return false;
        out = it->second;
        return true;
    }, "main.lua");
    CHECK(ok);
    CHECK(h.lastError().empty());
    CHECK(ui.has("add:libcount1:0/0"));     // a module is run once
    CHECK(ui.has("add:mod42truetrue:0/0")); // require gives what the module returns (and true when it returns nothing)
    CHECK(ui.has("add:5-x:0/0"));           // the % operator of LuaLib on strings
    CHECK(ui.has("add:h" + std::to_string(palfx::nameHash("Filia")) + ":0/0"));
    h.start();
    h.start();
    CHECK(ui.has("add:init1:0/0") && !ui.has("add:init2:0/0"));
    h.tick();   // 1
    CHECK(ui.has("add:hits3/3/f10/s9:0/0"));
    CHECK(f.o[0].slots[foit::slot::NUMHITS] == 3 && f.o[0].slots[7] == 9);
    CHECK((f.o[0].flags[0] & (1u << 5)) != 0);
    CHECK(ui.has("add:sametruefalsetrue1:0/0"));
    CHECK(ui.has("add:tagfalse:0/0"));
    CHECK(h.forbiddenTags(0).size() == 2 && h.forbiddenTags(0)[0] == palfx::nameHash("Taunt") && h.forbiddenTags(3).empty());
    CHECK(!h.blockingAllowed(3) && h.blockingAllowed(0));   // p2:blockingAllowed(false): the dummy, not the player
    bool scripted = true;
    script::InputFrame in = h.takeInput(&scripted);
    CHECK(!scripted && in.dir == 8 && in.buttons == 0);
    h.tick();   // 2: addInput(f, HP + b5)
    in = h.takeInput(&scripted);
    CHECK(scripted && in.dir == 0 && in.buttons == ((1u << 2) | (1u << 5)));
    CHECK(!ui.has("done:1T"));
    h.tick();   // 3: a motion starts; the coroutine finishes after its two waits
    CHECK(ui.has("done:1T"));
    in = h.takeInput(&scripted);
    CHECK(scripted && in.dir == 2);
    int n = 1;
    while (true) {
        in = h.takeInput(&scripted);
        if (!scripted) break;
        ++n;
    }
    CHECK(n == 7);
    h.tick();   // 4
    CHECK(h.p1Disabled() && ui.has("p1off"));
    CHECK(f.saves == 1);
    h.tick();   // 5: restore, end, then an error in the script
    CHECK(f.loads == 1 && f.last.size() == 3 && f.last[0] == 1);
    CHECK(h.missionComplete() && ui.has("end"));
    CHECK(!h.lastError().empty() || !h.log().empty());   // the error was logged, the host survived
    bool logged = false;
    for (const auto& l : h.log()) logged = logged || l.find("ERROR") != std::string::npos;
    CHECK(logged);
    h.tick();
    CHECK(h.ticks() == 6);
}


// a .seq record: direction, 0a 0a, three zero bytes, 12 button states
static std::string seqBytes(const std::vector<std::pair<int, std::vector<int>>>& recs) {
    std::string b;
    b += '\x01';
    const uint32_t n = static_cast<uint32_t>(recs.size() - 1);
    for (int i = 0; i < 4; ++i) b += static_cast<char>((n >> (8 * i)) & 0xff);
    for (const auto& r : recs) {
        b += static_cast<char>(r.first);
        b += "\x0a\x0a";
        b.append(3, '\0');
        for (int i = 0; i < 12; ++i) b += static_cast<char>(i < static_cast<int>(r.second.size()) ? r.second[i] : 4);
    }
    return b;
}

static void testSequences() {
    // crouching HP: D + HP pressed, then neutral with HP released (the layout of the game's cHP.seq)
    const std::string cHP = seqBytes({{2, {4, 4, 0, 4, 4, 4, 4, 0}}, {8, {4, 4, 1, 4, 4, 4, 4, 1}}});
    std::vector<script::InputFrame> fr;
    CHECK(script::parseSequence(cHP, fr));
    CHECK(fr.size() == 2 && fr[0].dir == 2 && fr[0].buttons == 4 && fr[1].dir == 8 && fr[1].buttons == 0);
    const std::string held = seqBytes({{0, {3, 3, 3}}});   // 3 = held
    CHECK(script::parseSequence(held, fr) && fr[0].buttons == 7);
    CHECK(!script::parseSequence("", fr));
    CHECK(!script::parseSequence(std::string(40, '\x02'), fr));   // not version 1
    CHECK(!script::parseSequence(cHP.substr(0, 20), fr));          // truncated

    Fake f;
    std::map<std::string, std::string> files;
    files["s/cHP.seq"] = cHP;
    files["m.lua"] = R"LUA(
loadSequence({ "s/cHP.seq", "s/none.seq" })
done0 = sequenceComplete()
setUpdate(function()
  t = (t or 0) + 1
  if t == 1 then playSequence("s/cHP.seq"); started = not sequenceComplete() end
  if t == 5 then finished = sequenceComplete() end
end)
)LUA";
    script::LuaHost h;
    CHECK(h.load(&f, nullptr, [&](const std::string& n, std::string& out) {
        auto it = files.find(n);
        if (it == files.end()) return false;
        out = it->second;
        return true;
    }, "m.lua"));
    h.start();
    bool scripted = false;
    h.takeInput(&scripted);
    CHECK(!scripted);
    h.tick();   // 1: playSequence
    script::InputFrame a = h.takeInput(&scripted);
    CHECK(scripted && a.dir == 2 && a.buttons == 4);
    h.tick();
    a = h.takeInput(&scripted);
    CHECK(scripted && a.dir == 8 && a.buttons == 0);
    h.tick();
    h.takeInput(&scripted);
    CHECK(!scripted);   // the sequence is over: the dummy is idle again
    h.tick();
    h.tick();   // 5: sequenceComplete() is true
    bool sawMissing = false;
    for (const auto& l : h.log()) sawMissing = sawMissing || l.find("none.seq") != std::string::npos;
    CHECK(sawMissing);   // a missing sequence is logged, not fatal
    CHECK(h.loaded());
}

// ---- lesson data: fight inis, the TOC + .stm chapters, challenges.ini
static void testLessonData() {
    lessons::FightIni fi;
    CHECK(lessons::parseFightIni("TrainingMode 1\n\nPlayer 1\nFighter Filia\nColor 2\n\nPlayer 2\nFighter Peacock\nFighter Parasoul\nColor 1\n\n"
                                 "UseAI 1\nLuaFile training/beginner/basicmovement.lua\n\nWorld Class_Notes \t\t#case insensitive", fi));
    CHECK(fi.fighter[0] == "Filia" && fi.fighter[1] == "Peacock" && fi.color[0] == 2 && fi.color[1] == 1);   // a team lists more: the first
    CHECK(fi.training && fi.useAI && fi.luaFile == "training/beginner/basicmovement.lua" && fi.world == "Class_Notes");
    CHECK(!lessons::parseFightIni("Player 1\nFighter Filia\n", fi));   // one fighter: nothing to start

    CHECK(lessons::formatText("Dodge ({1}/{2}) items") == "Dodge (0/0) items");
    CHECK(lessons::formatText("Dodge ({1}/{2})", 2, 3) == "Dodge (2/3)");
    CHECK(lessons::formatText("s.{lk} {seqs} {qcf}+{HP} {unknown} { open") == "s.LK > QCF+HP {unknown} { open");
    CHECK(lessons::formatText("{d}{df}{f} {lplk}") == "DDFF LP+LK");

    // a folder with a TOC and two maps (the second one has a scene node and nodes out of order)
    const std::string dir = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/zg_lesson_data_test";
    std::system(("rm -rf '" + dir + "' && mkdir -p '" + dir + "/training'").c_str());
    auto put = [&](const std::string& name, const std::string& text) { std::ofstream(dir + "/" + name, std::ios::binary) << text; };
    put("training/trainingTOC.ini", "# comment\nFile: One.stm\nFile: Missing.stm\nFile: Two.stm\n");
    put("training/One.stm", "CharName beginner\nCharIcon Textures/Tutorials/ch_num01.png\n\n"
                            "StoryNode\nNodeType Fight\nFileName training/beginner/b.ini\nDisplayName B Title\nDisplayText B\nTimelinePos 2\n\n"
                            "StoryNode\nNodeType Fight\nFileName training/beginner/a.ini\nDisplayName A Title\nDisplayText A\nTimelinePos 1\n"
                            "NodeUnlockedImg Textures/Tutorials/tut_num01.png\nNodeCompletedImg Textures/Tutorials/tut_num_complete01.png\n");
    put("training/Two.stm", "CharName empty\n\nStoryNode\nNodeType NewScene\nFileName scene.ini\nTimelinePos 1\n");
    Vfs vfs;
    vfs.mount(dir);
    const std::vector<lessons::Chapter> ch = lessons::loadChapters(vfs, "trainingTOC.ini");
    CHECK(ch.size() == 1);   // the missing map and the one without a fight are left out
    if (ch.size() == 1) {
        CHECK(ch[0].stem == "One" && ch[0].name == "beginner" && ch[0].entries.size() == 2);
        CHECK(ch[0].entries[0].file == "training/beginner/a.ini" && ch[0].entries[0].title == "A Title" && ch[0].entries[1].pos == 2);   // by TimelinePos
        CHECK(ch[0].entries[0].icon == "Textures/Tutorials/tut_num01.png" && !ch[0].entries[0].doneIcon.empty());
        CHECK(ch[0].progressKey(ch[0].entries[0]) == "lesson.training/beginner/a.ini");
    }
    CHECK(lessons::loadChapters(vfs, "nothing.ini").empty());
    std::system(("rm -rf '" + dir + "'").c_str());

    const std::vector<lessons::Challenge> c = lessons::parseChallenges(
        "####  Basic\nChalName: Challenge1\nTimeLimit: 99\nWinCond: _KO_\nWorld: Arena\nPlayerTeam: Any\n#PlayerAssist:\n"
        "EnemyTeam: Filia, Cerebella, Peacock\nEnemyColor: 1, 2, 3\nEnemyAssist: Filia_Updo_AssistH, Cerebella_Cerecopter\n"
        "Health: 80, _NONE_, 100, _REGEN_\nMeter: 5, _NONE_, 1, _HOLD_\nPlayerLocked: None\nPlayerAllowDamage: All\nEnemyLocked: None\n"
        "AIFile: AI/ArcadeMode/Random.lua\nAILevel: 5\n\n"
        "ChalName: Challenge2\nTimeLimit: 45\nWinCond: _TIME_\nWorld: Medici_Tower\nPlayerTeam: Squigly\nPlayerLocked: Normal, Jump\n"
        "PlayerAllowDamage: Special, ComboOver2\nEnemyTeam: BigBand\nAILevel: 6\n");
    CHECK(c.size() == 2);
    if (c.size() == 2) {
        CHECK(c[0].name == "Challenge1" && c[0].timeLimit == 99 && c[0].winCond == "_KO_" && c[0].world == "Arena" && c[0].freePick());
        CHECK(c[0].enemyTeam.size() == 3 && c[0].enemyTeam[2] == "Peacock" && c[0].enemyColor.size() == 3 && c[0].enemyColor[2] == 3);
        CHECK(c[0].enemyAssist.size() == 2 && c[0].playerAssist.empty());
        CHECK(c[0].health.playerPct == 80 && c[0].health.enemyPct == 100 && c[0].health.enemyMod == "_REGEN_" && c[0].health.playerMod == "_NONE_");
        CHECK(c[0].meter.playerPct == 5 && c[0].meter.enemyPct == 1 && c[0].meter.enemyMod == "_HOLD_");
        CHECK(c[0].playerLocked.empty() && c[0].playerAllowDamage.empty() && c[0].aiFile == "AI/ArcadeMode/Random.lua" && c[0].aiLevel == 5);
        CHECK(c[1].name == "Challenge2" && !c[1].freePick() && c[1].playerTeam[0] == "Squigly" && c[1].winCond == "_TIME_" && c[1].timeLimit == 45);
        CHECK(c[1].playerLocked.size() == 2 && c[1].playerLocked[1] == "Jump");
        CHECK(c[1].playerAllowDamage.size() == 2 && c[1].playerAllowDamage[1] == "ComboOver2" && c[1].aiLevel == 6);
    }
}

static void testErrors() {
    Fake f;
    script::LuaHost h;
    CHECK(!h.load(&f, nullptr, [](const std::string&, std::string&) { return false; }, "nope.lua"));
    CHECK(!h.lastError().empty());
    script::LuaHost h2;
    CHECK(!h2.load(&f, nullptr, [](const std::string&, std::string& o) { o = "this is not lua"; return true; }, "bad.lua"));
    script::LuaHost h3;
    CHECK(!h3.load(&f, nullptr, [](const std::string& n, std::string& o) { if (n != "m.lua") return false; o = "require('missing')"; return true; }, "m.lua"));
    CHECK(h3.lastError().find("missing") != std::string::npos);
}

static void testChallengeRules() {
    using script::Rules;
    CHECK(Rules::modOf("_REGEN_") == Rules::Mod::Regen);
    CHECK(Rules::modOf("_DRAIN_") == Rules::Mod::Drain);
    CHECK(Rules::modOf("_HOLD_") == Rules::Mod::Hold);
    CHECK(Rules::modOf("_NONE_") == Rules::Mod::None);
    CHECK(Rules::modOf("") == Rules::Mod::None);
    // the first block of the shipped challenges.ini: life 80% / 100% with a regenerating enemy, 5 bars for the player, 1 for the enemy
    const auto cs = lessons::parseChallenges(
        "ChalName: Challenge1\nTimeLimit: 99\nWinCond: _KO_\nWorld: Arena\nPlayerTeam: Any\nEnemyTeam: Filia, Cerebella, Peacock\n"
        "Health: 80, _NONE_, 100, _REGEN_\nMeter: 5, _NONE_, 1, _NONE_\nPlayerLocked: Jump, Super\nEnemyLocked: None\nAIFile: AI/ArcadeMode/Random.lua\nAILevel: 5\n");
    CHECK(cs.size() == 1);
    if (!cs.empty()) {
        const lessons::Challenge& c = cs[0];
        CHECK(c.freePick());
        CHECK(c.enemyTeam.size() == 3);
        CHECK(c.health.playerPct == 80 && c.health.enemyPct == 100);
        CHECK(Rules::modOf(c.health.enemyMod) == Rules::Mod::Regen);
        CHECK(c.meter.playerPct == 5 && c.meter.enemyPct == 1);
        CHECK(c.playerLocked.size() == 2 && c.playerLocked[1] == "Super");
        CHECK(c.aiLevel == 5);
    }
}

int main() {
    testChallengeRules();
    testMotions();
    testHost();
    testSequences();
    testLessonData();
    testErrors();
    std::printf("lua_host tests: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
