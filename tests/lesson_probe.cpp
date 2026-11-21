// lesson_probe.cpp - runs one of the game's lesson scripts on a real fight (two characters from the player's own game data) for N ticks and
// reports what the script did: Lua errors, missing natives, HUD calls, the objectives it raised.  Not part of the test suite (needs game data).
//
//   zdroid_lesson_probe <characters dir> <game data root> <script> [char1 [char2 [ticks]]]
//   e.g. zdroid_lesson_probe gd_chars/data/characters gamedata/core training/beginner/basicmovement.lua filia peacock 3000
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "foit.h"
#include "foit_names.h"
#include "lesson.h"
#include "vfs.h"

using namespace zengine;

namespace {

struct LogSink : script::UiSink {
    int calls = 0;
    void note(const std::string& s) {
        ++calls;
        std::printf("  ui: %s\n", s.c_str());
    }
    void loadCatalog(const std::string& n) override { note("loadcatalog " + n); }
    void clearAll() override { note("clearall"); }
    void setMax(int r) override { note("setmax " + std::to_string(r)); }
    void add(const std::string& k, int p, int m) override { note("add '" + k + "' " + std::to_string(p) + "/" + std::to_string(m)); }
    void update(int i, const std::string& k, int p, int m) override {
        note("update #" + std::to_string(i) + " '" + k + "' " + std::to_string(p) + "/" + std::to_string(m));
    }
    void complete(int i, bool d) override { note("complete #" + std::to_string(i) + (d ? " yes" : " no")); }
    void setExplanation(const std::string& k) override { note("explanation '" + k + "'"); }
    void showExplanation(bool on) override {
        note(std::string("showexplanation ") + (on ? "on" : "off"));
        explaining = on;
    }
    bool explanationActive() override { return explaining; }
    void setDetails(const std::string& k) override { note("details '" + k + "'"); }
    void endMission() override { note("endmission"); }
    void p1Disable(bool off) override { note(std::string("p1disable ") + (off ? "on" : "off")); }
    bool explaining = false;
};

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(static_cast<size_t>(n));
    const size_t got = n > 0 ? std::fread(out.data(), 1, out.size(), f) : 0;
    std::fclose(f);
    return got == out.size();
}

std::shared_ptr<foit::Character> loadChar(const std::string& dir, const std::string& name) {
    std::vector<uint8_t> b;
    if (!readFile(dir + "/" + name + ".foit", b)) {
        std::fprintf(stderr, "cannot read %s/%s.foit\n", dir.c_str(), name.c_str());
        return nullptr;
    }
    auto ch = std::make_shared<foit::Character>();
    std::string err;
    if (!foit::parse(b.data(), b.size(), *ch, &err)) {
        std::fprintf(stderr, "%s.foit: %s\n", name.c_str(), err.c_str());
        return nullptr;
    }
    return ch;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s <characters dir> <game data root> <script> [char1 [char2 [ticks]]]\n", argv[0]);
        return 2;
    }
    const std::string charDir = argv[1];
    const std::string root = argv[2];
    const std::string script = argv[3];
    const std::string c1 = argc > 4 ? argv[4] : "filia";
    const std::string c2 = argc > 5 ? argv[5] : "peacock";
    const int ticks = argc > 6 ? std::atoi(argv[6]) : 1800;

    Vfs vfs;
    std::printf("mounted %zu files from %s\n", vfs.mount(root), root.c_str());

    fight::FoitDuelConfig cfg;
    cfg.ch[0] = loadChar(charDir, c1);
    cfg.ch[1] = loadChar(charDir, c2);
    if (!cfg.ch[0] || !cfg.ch[1]) return 1;
    cfg.training = true;
    cfg.introPoses = false;
    cfg.roundSeconds = 0;
    fight::FoitDuel duel;
    if (!duel.init(cfg)) {
        std::fprintf(stderr, "duel init failed\n");
        return 1;
    }

    LogSink sink;
    script::Lesson lesson;
    std::vector<std::string> missing;
    auto read = [&](const std::string& name, std::string& out) {
        for (const std::string& cand : {name, name + ".lua"}) {
            if (vfs.readText(cand, out)) return true;
            const size_t sl = cand.find_last_of("/\\");
            std::string real;
            if (vfs.pick(sl == std::string::npos ? cand : cand.substr(sl + 1), &real)) {
                std::vector<uint8_t> b;
                if (vfs.read(real, b)) {
                    out.assign(b.begin(), b.end());
                    return true;
                }
            }
        }
        missing.push_back(name);
        return false;
    };
    if (!lesson.begin(&duel, &sink, read, script)) {
        std::printf("LOAD FAILED: %s\n", lesson.host().lastError().c_str());
        for (const auto& m : missing) std::printf("  missing file: %s\n", m.c_str());
        return 1;
    }
    std::printf("script loaded; running %d ticks\n", ticks);
    int shown = 0;
    int explainAge = 0;
    const bool bot = std::getenv("PROBE_BOT") != nullptr;
    uint32_t rng = 12345;
    uint16_t pad = 0;
    for (int t = 0; t < ticks; ++t) {
        if (sink.explaining && ++explainAge >= 40) {
            sink.explaining = false;   // "press any button"
            explainAge = 0;
        }
        if (!sink.explaining) explainAge = 0;
        if (bot && t % 6 == 0) {   // a button masher: a new random pad every 6 ticks
            rng = rng * 1664525u + 1013904223u;
            const uint16_t dirs[] = {0, fight::kLeft, fight::kRight, fight::kDown, static_cast<uint16_t>(fight::kDown | fight::kRight),
                                     static_cast<uint16_t>(fight::kDown | fight::kLeft), fight::kUp, static_cast<uint16_t>(fight::kUp | fight::kRight)};
            pad = dirs[(rng >> 8) % 8];
            if (((rng >> 16) & 3) != 0) pad = static_cast<uint16_t>(pad | (1u << (4 + ((rng >> 20) % 6))));
        }
        lesson.step(bot ? pad : 0);
        if (!lesson.host().loaded()) {
            std::printf("script stopped at tick %d: %s\n", t, lesson.host().lastError().c_str());
            break;
        }
        // print new log lines as they appear
        const auto& log = lesson.host().log();
        while (shown < static_cast<int>(log.size()) && shown < 400) {
            const std::string& l = log[shown++];
            if (l.rfind("WARNING - Set a global variable", 0) == 0) continue;   // the libraries define their helpers as globals
            std::printf("  [%d] %s\n", t, l.c_str());
        }
        if (t % 150 == 0) {
            const auto& w = duel.world();
            std::printf("  (t=%d objects=%zu p1.x=%.0f p2.x=%.0f p1.hitstun=%.0f)\n", t, w.objects.size(), duel.body(0).x, duel.body(1).x,
                        duel.body(0).slots[foit::slot::HITSTUN]);
        }
        if (lesson.host().missionComplete()) {
            std::printf("mission complete at tick %d\n", t);
            break;
        }
    }
    std::printf("done: %llu script ticks, %d ui calls, fighting=%d\n", static_cast<unsigned long long>(lesson.host().ticks()), sink.calls,
                duel.fighting() ? 1 : 0);
    for (const auto& m : missing) std::printf("  missing file: %s\n", m.c_str());
    return lesson.host().loaded() ? 0 : 1;
}
