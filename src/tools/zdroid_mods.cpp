// zdroid_mods - inspect and drive the SKGLoader mod folder.
//
//   zdroid_mods --list <gameDir>                  show SKGLoader/mods and their state
//   zdroid_mods --create <gameDir> <id> [name]    scaffold a new mod
//   zdroid_mods --enable <gameDir> <id>
//   zdroid_mods --disable <gameDir> <id>
//   zdroid_mods --resolve <gameDir> <gamePath>    which real file (and mod) wins
//
// Mods are plain folders with a mod.ini; see src/core/modloader.h for the layout.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "modloader.h"
#include "vfs.h"

using namespace zengine;

namespace {

void list(const std::string& gameDir) {
    const mods::ModSet set = mods::scan(gameDir);
    std::printf("SKGLoader root : %s\n", set.root.c_str());
    std::printf("mods enabled   : %s\n", set.globalEnabled ? "yes" : "no (mods.ini)");
    for (const std::string& w : set.warnings) std::printf("  ! %s\n", w.c_str());
    if (set.mods.empty()) {
        std::printf("no mods found (create one with --create)\n");
        return;
    }
    std::printf("\n%-20s %-8s %-6s %-7s %-6s %s\n", "id", "version", "prio", "state", "files",
                "name");
    for (const mods::ModInfo& m : set.mods) {
        std::printf("%-20s %-8s %-6d %-7s %-6zu %s\n", m.id.c_str(), m.version.c_str(),
                    m.priority, m.enabled ? "on" : "off", m.fileCount, m.name.c_str());
        if (!m.author.empty()) std::printf("%-20s by %s\n", "", m.author.c_str());
        if (!m.description.empty()) std::printf("%-20s %s\n", "", m.description.c_str());
        for (const std::string& o : m.overrides) {
            const auto slash = o.find_last_of("/\\");
            std::printf("%-20s -> %s\n", "",
                        (slash == std::string::npos ? o : o.substr(slash + 1)).c_str());
        }
    }
    const std::vector<mods::ModInfo> order = mods::loadOrder(set);
    std::printf("\nload order (later wins):");
    for (const mods::ModInfo& m : order) std::printf(" %s", m.id.c_str());
    std::printf("%s\n", order.empty() ? " (none active)" : "");
}

int resolve(const std::string& gameDir, const std::string& gamePath) {
    const mods::Resolution r = mods::resolve(gameDir, gamePath);
    if (!r.found) {
        std::printf("%s: not found in %s\n", gamePath.c_str(), gameDir.c_str());
        return 1;
    }
    std::printf("%s\n  -> %s\n", gamePath.c_str(), r.realPath.c_str());
    std::printf("  supplied by: %s\n", r.modId.empty() ? "base game" : ("mod " + r.modId).c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf(
            "usage: zdroid_mods --list    <gameDir>\n"
            "       zdroid_mods --create  <gameDir> <id> [name]\n"
            "       zdroid_mods --enable  <gameDir> <id>\n"
            "       zdroid_mods --disable <gameDir> <id>\n"
            "       zdroid_mods --resolve <gameDir> <gamePath>\n");
        return 2;
    }
    const std::string mode = argv[1];
    const std::string gameDir = argv[2];

    if (mode == "--list") {
        list(gameDir);
        return 0;
    }
    if (argc < 4) {
        std::printf("%s needs a mod id\n", mode.c_str());
        return 2;
    }
    const std::string id = argv[3];

    if (mode == "--create") {
        const std::string name = (argc > 4) ? argv[4] : id;
        if (!mods::createSkeleton(gameDir, id, name)) {
            std::printf("cannot create mod '%s' under %s\n", id.c_str(), gameDir.c_str());
            return 1;
        }
        std::printf("created %s/SKGLoader/mods/%s (mod.ini + README.txt + data/)\n",
                    gameDir.c_str(), id.c_str());
        return 0;
    }
    if (mode == "--enable" || mode == "--disable") {
        const bool on = (mode == "--enable");
        if (!mods::setEnabled(gameDir, id, on)) {
            std::printf("cannot write mod.ini for '%s'\n", id.c_str());
            return 1;
        }
        std::printf("mod '%s' %s\n", id.c_str(), on ? "enabled" : "disabled");
        return 0;
    }
    if (mode == "--resolve") return resolve(gameDir, id);

    std::printf("unknown mode %s\n", mode.c_str());
    return 2;
}
