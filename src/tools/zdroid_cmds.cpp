// zdroid_cmds - the input commands of a character's .foit, readable.
//
//   zdroid_cmds <a.foit> <state name>            transitions of one state: target, window, each step's directions and 15 button requirements
//   zdroid_cmds <a.foit> --hubs                  the transitions of Stand / Crouch / the jump hubs that need a single attack button
//
// Every state of a .foit has a table of transitions (target state + input command).  A command is a list of steps; a step names up to
// three direction ids (b0..b2), a window (w) and 15 button requirements t[0..14] (10 = "do not care").  This prints them raw, so the
// meaning of the values can be read off known moves instead of guessed.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "foit.h"

using namespace zengine;

static bool readAll(const std::string& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !out.empty();
}

static void printStep(const foit::Step& s) {
    std::printf("      dir %d/%d/%d  w=%-4u flags=%u  t=[", s.b0, s.b1, s.b2, s.w, s.flags);
    for (int i = 0; i < 15; ++i) {
        if (s.t[i] == 10) std::printf(" .");
        else std::printf(" %d", s.t[i]);
    }
    std::printf(" ]%s\n", s.chain.empty() ? "" : "  +condition chain");
}

static void printState(const foit::Object& o, int si) {
    const foit::State& st = o.states[static_cast<size_t>(si)];
    std::printf("state %d '%s': %zu transitions\n", si, st.name.c_str(), st.transitions.size());
    for (const foit::Transition& t : st.transitions) {
        const std::string tn = t.target < o.states.size() ? o.states[t.target].name : std::string("?");
        std::printf("  -> %-28s (cmd a=%u b=%u flags=%u, %zu step%s%s)\n", tn.c_str(), t.cmd.a, t.cmd.b, t.cmd.flags, t.cmd.steps.size(),
                    t.cmd.steps.size() == 1 ? "" : "s", t.cmd.hasDefault ? ", default step" : "");
        if (t.cmd.hasDefault) printStep(t.cmd.def);
        for (const foit::Step& s : t.cmd.steps) printStep(s);
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: zdroid_cmds <char.foit> <state name | --hubs>\n");
        return 2;
    }
    std::vector<uint8_t> bytes;
    foit::Character ch;
    std::string err;
    if (!readAll(argv[1], bytes) || !foit::parse(bytes.data(), bytes.size(), ch, &err)) {
        std::fprintf(stderr, "cannot parse %s (%s)\n", argv[1], err.c_str());
        return 1;
    }
    const foit::Object& o = ch.body;
    if (!std::strcmp(argv[2], "--hubs")) {
        for (const char* n : {"Stand", "Normals", "Crouch", "Crouching", "Jump_Rise_N", "JumpFall_Loop", "j.Normals"}) {
            const int si = o.findState(n);
            if (si >= 0) printState(o, si);
        }
        return 0;
    }
    if (!std::strcmp(argv[2], "--to") && argc > 3) {   // every transition, in any state, that enters the named state
        const int target = o.findState(argv[3]);
        if (target < 0) {
            std::fprintf(stderr, "no state '%s'\n", argv[3]);
            return 1;
        }
        for (size_t i = 0; i < o.states.size(); ++i)
            for (const foit::Transition& t : o.states[i].transitions)
                if (t.target == target) {
                    std::printf("from state %zu '%s':\n", i, o.states[i].name.c_str());
                    std::printf("  -> %s (cmd a=%u b=%u flags=%u, %zu step%s)\n", argv[3], t.cmd.a, t.cmd.b, t.cmd.flags, t.cmd.steps.size(), t.cmd.steps.size() == 1 ? "" : "s");
                    if (t.cmd.hasDefault) printStep(t.cmd.def);
                    for (const foit::Step& st : t.cmd.steps) printStep(st);
                }
        return 0;
    }
    const int si = o.findState(argv[2]);
    if (si < 0) {
        std::fprintf(stderr, "no state '%s'\n", argv[2]);
        return 1;
    }
    printState(o, si);
    return 0;
}
