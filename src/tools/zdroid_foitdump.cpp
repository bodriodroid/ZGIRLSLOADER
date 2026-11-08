// zdroid_foitdump - everything the .foit says about one state (or all of them), as text.
//
//   zdroid_foitdump <char.foit> --state NAME [--child N]     header script, every node (frame refs, duration, scripts, hit data, spawns)
//   zdroid_foitdump <char.foit> --id N [--child N]            same, by state id
//   zdroid_foitdump <char.foit> --all [--child N]             every state of the object
//   zdroid_foitdump <char.foit> --trans NAME [--child N]      the transition table of a state: target + input steps
//
// --child N looks into the Nth child object (projectile / effect) of the body.  Scripts print with slot names (foit_names.h).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "foit.h"

using namespace zengine;

static const char* kDir[] = {"F", "DF", "D", "DB", "B", "UB", "U", "UF", "?8", "N", "*"};
static const char* kBtn[] = {"LP", "MP", "HP", "LK", "MK", "HK", "b6", "b7", "b8", "b9", "b10", "b11", "b12", "b13", "b14"};

static std::string stepText(const foit::Step& s) {
    std::string t;
    auto dir = [&](uint8_t d) { return d < 11 ? kDir[d] : "??"; };
    t += std::string("dir ") + dir(s.b0);
    if (s.b1 != 10 || s.b2 != 10) t += std::string("/") + dir(s.b1) + "/" + dir(s.b2);
    char buf[40];
    std::snprintf(buf, sizeof buf, " w%u fl%u", s.w, s.flags);
    t += buf;
    std::string b;
    for (int i = 0; i < 15; ++i)
        if (s.t[i] != 10) {
            std::snprintf(buf, sizeof buf, " %s=%u", kBtn[i], s.t[i]);
            b += buf;
        }
    if (!b.empty()) t += " [" + b.substr(1) + "]";
    if (!s.chain.empty()) t += "  if{" + foit::describeChain(s.chain) + "}";
    return t;
}

static void printCommand(const foit::Command& c, const char* indent) {
    std::printf("%scmd a=%u b=%u fl=%u steps=%zu%s\n", indent, c.a, c.b, c.flags, c.steps.size(), c.hasDefault ? " (+default)" : "");
    if (c.hasDefault) std::printf("%s  default: %s\n", indent, stepText(c.def).c_str());
    for (size_t i = 0; i < c.steps.size(); ++i) std::printf("%s  step %zu: %s\n", indent, i, stepText(c.steps[i]).c_str());
}

static void printHit(const foit::HitData& h) {
    std::printf("      HIT dmg(fc)=%u hitstop(f10)=%u hitstun(f8)=%u level(f13)=%u strength(f18)=%u  fe=%u f4=%u f2c=%u f0=%u f12=%u f14..17=%u,%u,%u,%u fa=%u fb=%u f0a=%u\n", h.fc, h.f10, h.f8, h.f13, h.f18, h.fe, h.f4, h.f2c, h.f0, h.f12,
                h.f14, h.f15, h.f16, h.f17, h.fa, h.fb, h.f0a);
    for (int i = 0; i < 4; ++i)
        if (h.hasChain[i]) std::printf("        chain[%d]: %s\n", i, foit::describeChain(h.chains[i]).c_str());
}

static void printProps(const foit::NodeProps& p, const char* ind) {
    std::printf("%sprops b1=%u b2=%u b3=%u dur=%u f2=%u x1a=%u\n", ind, p.b1, p.b2, p.b3, p.dur, p.f2, p.x1a);
    if (!p.chain.empty()) std::printf("%s  script: %s\n", ind, foit::describeChain(p.chain).c_str());
    if (p.hasHit) printHit(p.hit);
    for (const foit::SubItem& s : p.sub) {
        std::printf("%s  sub a=%u b=%u c=%u fl=%u", ind, s.a, s.b, s.c, s.flags);
        if (!s.chain.empty()) std::printf("  chain: %s", foit::describeChain(s.chain).c_str());
        std::printf("\n");
    }
    if (!p.u32s.empty()) {
        std::printf("%s  u32s:", ind);
        for (uint32_t v : p.u32s) std::printf(" %u", v);
        std::printf("\n");
    }
    if (p.hasCmd) printCommand(p.cmd, (std::string(ind) + "  ").c_str());
}

static void printState(const foit::Object& o, int id, bool trans) {
    const foit::State& s = o.states[id];
    std::printf("state %d '%s'  next -> %s  f=%u b14=%u v18=%s%u v1c=%u hashes=%zu  nodes=%zu ticks=%d transitions=%zu\n", id, s.name.c_str(),
                s.next < o.states.size() ? o.states[s.next].name.c_str() : "?", s.f, s.b14, s.hasV18 ? "" : "-", s.v18, s.v1c, s.hashes.size(), s.hdr.nodes.size(),
                s.totalTicks(), s.transitions.size());
    if (s.hdr.hasChain) std::printf("  header script: %s\n", foit::describeChain(s.hdr.chain).c_str());
    if (s.hdr.hasCmd) printCommand(s.hdr.cmd, "  header ");
    for (const foit::NodeProps& p : s.hdr.cmdNode) printProps(p, "  header-cmd-node ");
    for (size_t k = 0; k < s.hdr.nodes.size(); ++k) {
        const foit::Node& n = s.hdr.nodes[k];
        std::printf("  node %zu: refs %u,%u,%u%s\n", k, n.refs[0], n.refs[1], n.refs[2], n.isScript() ? "  (script node)" : "");
        for (const foit::NodeProps& p : n.props) printProps(p, "    ");
    }
    if (trans) {
        for (size_t i = 0; i < s.transitions.size(); ++i) {
            const foit::Transition& t = s.transitions[i];
            std::printf("  transition %zu -> %u '%s'\n", i, t.target, t.target < o.states.size() ? o.states[t.target].name.c_str() : "?");
            printCommand(t.cmd, "    ");
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: zdroid_foitdump <char.foit> (--state NAME | --id N | --all | --trans NAME) [--child N]\n");
        return 2;
    }
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    foit::Character ch;
    std::string err;
    if (!foit::parse(bytes.data(), bytes.size(), ch, &err)) {
        std::printf("%s: %s\n", argv[1], err.c_str());
        return 1;
    }
    int child = -1;
    std::string state, mode;
    int id = -1;
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--child") && i + 1 < argc) child = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--state") && i + 1 < argc) { state = argv[++i]; mode = "state"; }
        else if (!std::strcmp(argv[i], "--trans") && i + 1 < argc) { state = argv[++i]; mode = "trans"; }
        else if (!std::strcmp(argv[i], "--id") && i + 1 < argc) { id = std::atoi(argv[++i]); mode = "state"; }
        else if (!std::strcmp(argv[i], "--all")) mode = "all";
    }
    const foit::Object* o = &ch.body;
    if (child >= 0) {
        if (static_cast<size_t>(child) >= ch.body.children.size()) {
            std::printf("no child %d (the body has %zu)\n", child, ch.body.children.size());
            return 1;
        }
        o = &ch.body.children[static_cast<size_t>(child)];
        std::printf("child %d '%s': %zu states\n", child, o->name.c_str(), o->states.size());
    }
    if (mode == "all") {
        for (size_t i = 0; i < o->states.size(); ++i) printState(*o, static_cast<int>(i), false);
    } else if (mode == "state" || mode == "trans") {
        if (id < 0) id = o->findState(state);
        if (id < 0 || static_cast<size_t>(id) >= o->states.size()) {
            std::printf("no such state\n");
            return 1;
        }
        printState(*o, id, mode == "trans");
    } else {
        std::printf("object '%s': %zu states, %zu children\n", o->name.c_str(), o->states.size(), o->children.size());
        for (size_t i = 0; i < o->children.size(); ++i) std::printf("  child %zu '%s' (%zu states)\n", i, o->children[i].name.c_str(), o->children[i].states.size());
    }
    return 0;
}
