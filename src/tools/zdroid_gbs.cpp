// zdroid_gbs - what a .gbs UI scene contains, for working out which parts the game code drives.
//
//   zdroid_gbs <file.gbs>                          views: node / animation / message counts
//   zdroid_gbs <file.gbs> --view <name>            node tree of one view (name, type, texture)
//   zdroid_gbs <file.gbs> --anims [<view>]         every animation: length, loop, channels, sound cues, game messages
//   zdroid_gbs <file.gbs> --keys <view> <anim> [rgb] the channels of one animation: node name and the keys (tick, position, size, alpha)
//   zdroid_gbs <file.gbs> --messages               every message id the animations send to the game code, with the animation names
//   zdroid_gbs <file.gbs> --textures               the texture table
//
// The message cues ("MSGA", id) are the only place a scene talks to the game code: the code reacts to the id (spawn a sprite,
// start a sound, advance a state), which is why a view that looks like plain art can still change how a fight plays.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "gbs_scene.h"

using namespace zengine;

static void tree(const GbsScene& sc, const GbsNode& n, int depth) {
    std::printf("%*s%-3s %-34s %5.0f,%-5.0f %4.0fx%-4.0f", depth * 2, "", n.tag.c_str(), n.name.c_str(), n.x, n.y, n.w, n.h);
    if (n.type == GbsNode::Type::Sprite && n.texId != kGbsNoTexture) std::printf("  %s", sc.textureName(n.texId).c_str());
    std::printf("\n");
    for (const GbsNode& k : n.kids) tree(sc, k, depth + 1);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: zdroid_gbs <file.gbs> [--view <name> | --anims [<view>] | --messages | --textures]\n");
        return 2;
    }
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    GbsScene sc;
    std::string err;
    if (!parseGbsScene(bytes.data(), bytes.size(), sc, &err)) {
        std::printf("%s: %s\n", argv[1], err.c_str());
        return 1;
    }
    const std::string mode = argc > 2 ? argv[2] : "";
    const std::string arg = argc > 3 ? argv[3] : "";
    if (mode == "--view") {
        const GbsSceneView* v = sc.view(arg);
        if (!v) { std::printf("no view '%s'\n", arg.c_str()); return 1; }
        for (const GbsNode& n : v->nodes) tree(sc, n, 0);
    } else if (mode == "--anims") {
        for (const GbsSceneView& v : sc.views) {
            if (!arg.empty() && v.name != arg) continue;
            std::printf("view '%s': %zu animations\n", v.name.c_str(), v.anims.size());
            for (const GbsAnim& a : v.anims) {
                std::printf("  %-34s %5d ticks  loop %d..%d  ch %-3zu snd %-2zu msg", a.name.c_str(), a.length, a.loopStart, a.loopEnd, a.channels.size(), a.sounds.size());
                for (const GbsSoundCue& m : a.messages) std::printf(" %u@%d", m.soundId, m.t);
                std::printf("\n");
            }
        }
    } else if (mode == "--keys") {
        const GbsSceneView* v = sc.view(arg);
        const GbsAnim* a = v ? v->anim(argc > 4 ? argv[4] : "") : nullptr;
        if (!a) { std::printf("no such view / animation\n"); return 1; }
        const bool rgb = argc > 5 && std::string(argv[5]) == "rgb";   // also print the colour of the keys that tint (not plain white)
        std::printf("%s / %s: %d ticks, loop %d..%d\n", v->name.c_str(), a->name.c_str(), a->length, a->loopStart, a->loopEnd);
        for (const GbsChannel& ch : a->channels) {
            const GbsNode* n = nullptr;
            std::function<const GbsNode*(const GbsNode&)> find = [&](const GbsNode& x) -> const GbsNode* {
                if (x.id == ch.nodeId) return &x;
                for (const GbsNode& k : x.kids) if (const GbsNode* r = find(k)) return r;
                return nullptr;
            };
            for (const GbsNode& x : v->nodes) if (!n) n = find(x);
            std::printf("  %-32s", n ? n->name.c_str() : "?");
            for (const GbsKey& k : ch.keys) std::printf("  [%d %c %.0f,%.0f %.0fx%.0f a%u%s]", k.t, k.kind, k.x, k.y, k.w, k.h, k.hasColour() ? (k.argb >> 24) : 255u, k.modeB > 0 ? "~" : "");
            if (rgb) for (const GbsKey& k : ch.keys) if (k.hasColour() && (k.argb & 0xFFFFFFu) != 0xFFFFFFu) std::printf(" {t%d rgb%06X}", k.t, k.argb & 0xFFFFFFu);
            std::printf("\n");
        }
        for (const GbsSoundCue& c : a->sounds) std::printf("  sound %u @%d\n", c.soundId, c.t);
        for (const GbsSoundCue& m : a->messages) std::printf("  message %u @%d\n", m.soundId, m.t);
    } else if (mode == "--messages") {
        std::map<uint32_t, std::set<std::string>> by;
        for (const GbsSceneView& v : sc.views)
            for (const GbsAnim& a : v.anims)
                for (const GbsSoundCue& m : a.messages) by[m.soundId].insert(v.name + "/" + a.name);
        for (const auto& kv : by) {
            std::printf("message %u:", kv.first);
            for (const std::string& s : kv.second) std::printf(" %s", s.c_str());
            std::printf("\n");
        }
    } else if (mode == "--textures") {
        for (const GbsTexture& t : sc.textures) std::printf("%5u %s\n", t.id, t.name.c_str());
    } else {
        std::printf("%s: scene %u, %zu fonts, %zu textures, %zu sounds, %zu views\n", argv[1], sc.sceneId, sc.fonts.size(), sc.textures.size(), sc.sounds.size(), sc.views.size());
        for (const GbsSceneView& v : sc.views) {
            size_t nodes = 0, msgs = 0;
            for (const GbsNode& n : v.nodes) nodes += countNodes(n);
            for (const GbsAnim& a : v.anims) msgs += a.messages.size();
            std::printf("  %-28s %4zu nodes  %3zu anims  %3zu messages\n", v.name.c_str(), nodes, v.anims.size(), msgs);
        }
    }
    return 0;
}
