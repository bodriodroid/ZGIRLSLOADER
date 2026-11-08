// zdroid_msb - inspect Reverge engine "msb" assets.
//
//   zdroid_msb <file.msb>...            summarise each file
//   zdroid_msb --strings <file.msb>     list every length-prefixed string with offsets
//   zdroid_msb --joints <file.sgs.msb>  dump the skeleton hierarchy
//   zdroid_msb --track <file.sga.msb>   dump animation tracks
//   zdroid_msb --dir <folder>           summarise every *.msb under a folder tree
//
// The on-disk format is MSB (big-endian, integers widened to 8 bytes); see
// docs/BINARY_RE.md for how that was recovered from SkullGirls.exe.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "vfs.h"
#include "warp.h"

using namespace zengine;

namespace {

void printSummary(const std::string& path, const std::vector<uint8_t>& d) {
    const std::string ext = [&] {
        const auto p = path.rfind('.');
        if (p == std::string::npos) return std::string{};
        const auto q = path.rfind('.', p - 1);
        return (q == std::string::npos) ? path.substr(p) : path.substr(q);
    }();

    const auto hits = warp::scanStrings(d);
    std::printf("%-52s %8zu  %s\n", path.c_str(), d.size(), ext.c_str());
    if (!hits.empty()) std::printf("    version: %s\n", hits[0].value.c_str());

    if (ext == ".sgs.msb" || path.find(".sgs.") != std::string::npos) {
        const warp::Skeleton sk = warp::readSkeleton(d);
        if (!sk.ok) {
            std::printf("    parse error: %s\n", sk.error.c_str());
            return;
        }
        std::printf("    skeleton: declared=%llu parsed=%zu\n",
                    static_cast<unsigned long long>(sk.declaredJoints), sk.joints.size());
        for (const warp::Joint& j : sk.joints) {
            std::printf("      #%-2zu %-24s parent=%-3d matrix=%-2zu extra=%-3zu @0x%04zX\n",
                        &j - &sk.joints[0], j.name.c_str(), j.parent, j.matrix.size(),
                        j.extra.size(), j.offset);
        }
    } else if (ext == ".sga.msb" || path.find(".sga.") != std::string::npos) {
        const warp::Animation an = warp::readAnimation(d);
        if (!an.ok) {
            std::printf("    parse error: %s\n", an.error.c_str());
            return;
        }
        std::printf("    animation: %zu track(s)\n", an.tracks.size());
        for (const warp::Track& t : an.tracks) {
            std::printf("      %-24s %6zu keys @0x%04zX  first=", t.name.c_str(), t.keys.size(),
                        t.offset);
            for (std::size_t i = 0; i < t.keys.size() && i < 4; ++i)
                std::printf("%s%.4f", i ? ", " : "", t.keys[i]);
            std::printf("\n");
        }
    } else {
        const warp::MeshInfo mi = warp::readMeshInfo(d);
        std::printf("    strings: %zu ->", mi.names.size());
        for (std::size_t i = 0; i < mi.names.size() && i < 8; ++i)
            std::printf(" %s", mi.names[i].c_str());
        std::printf("\n");
    }
}

int run(int argc, char** argv) {
    if (argc < 3) {
        std::printf(
            "usage: zdroid_msb <file.msb>...\n"
            "       zdroid_msb --strings <file.msb>\n"
            "       zdroid_msb --joints <file.sgs.msb>\n"
            "       zdroid_msb --track <file.sga.msb>\n"
            "       zdroid_msb --dir <folder>\n");
        return 2;
    }
    const std::string mode = argv[1];

    if (mode == "--dir") {
        const std::string root = argv[2];
        Vfs vfs;
        if (vfs.mount(root) == 0) {
            std::printf("no files under %s\n", root.c_str());
            return 1;
        }
        int n = 0;
        for (const std::string& f : vfs.listByExt(".msb")) {
            std::vector<uint8_t> d;
            if (!readFile(f, d)) continue;
            printSummary(f, d);
            ++n;
        }
        std::printf("(%d msb file%s)\n", n, n == 1 ? "" : "s");
        return n ? 0 : 1;
    }

    std::vector<uint8_t> d;
    if (!readFile(argv[2], d)) {
        std::printf("cannot read %s\n", argv[2]);
        return 1;
    }

    if (mode == "--strings") {
        for (const warp::StringHit& h : warp::scanStrings(d))
            std::printf("  @0x%04zX len=%-3zu %s\n", h.offset, h.value.size(), h.value.c_str());
        return 0;
    }
    if (mode == "--joints" || mode == "--track") {
        printSummary(argv[2], d);
        return 0;
    }

    // Plain mode: every argument is a file.
    for (int i = 1; i < argc; ++i) {
        std::vector<uint8_t> f;
        if (!readFile(argv[i], f)) {
            std::printf("cannot read %s\n", argv[i]);
            continue;
        }
        printSummary(argv[i], f);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: zdroid_msb [--strings|--joints|--track|--dir] <path>\n");
        return 2;
    }
    if (std::strcmp(argv[1], "--") == 0 || argv[1][0] != '-') {
        // Plain listing of one or more files.
        std::vector<std::string> args;
        args.push_back("--plain");
        for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
        std::vector<char*> raw;
        for (auto& a : args) raw.push_back(const_cast<char*>(a.c_str()));
        return run(static_cast<int>(raw.size()), raw.data());
    }
    return run(argc, argv);
}
