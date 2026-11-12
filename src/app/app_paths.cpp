// app_paths.cpp - see app_paths.h
#include "app_paths.h"

#include <SDL.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace zengine {
namespace apppaths {

namespace {

std::string sdlPref(const char* org, const char* app) {
    std::string out;
    if (char* p = SDL_GetPrefPath(org, app)) {
        out = p;
        SDL_free(p);
    }
    return out;
}

// Files that older builds kept in the previous folder.  Never overwrites; a failure only means the file is not carried over.
void migrate(const std::string& from, const std::string& to) {
    if (from.empty() || to.empty() || from == to) return;
    static const char* kFiles[] = {"dataroot.txt", "touch_layout.txt", "menu_settings.txt", "zgirls_flags.txt", "zgirls_progress.txt"};
    std::error_code ec;
    for (const char* f : kFiles) {
        const fs::path src = fs::path(from) / f, dst = fs::path(to) / f;
        if (fs::is_regular_file(src, ec) && !fs::exists(dst, ec)) fs::copy_file(src, dst, ec);
    }
}

}  // namespace

const std::string& prefDir() {
    static const std::string dir = [] {
        std::string now = sdlPref("bodriodroid", "zgirls");
        if (!now.empty()) {
            // the previous folder sits next to the new one (<base>/larpbase/sg_loader); looked up by path so that it is never created
            fs::path cur = fs::path(now);
            if (cur.filename().empty()) cur = cur.parent_path();          // drop the trailing separator
            const fs::path base = cur.parent_path().parent_path();         // <base>/bodriodroid/zgirls -> <base>
            migrate((base / "larpbase" / "sg_loader").string(), now);
        }
        return now;
    }();
    return dir;
}

std::string prefFile(const std::string& name) {
    const std::string& d = prefDir();
    return d.empty() ? std::string() : d + name;
}

std::string coreStoreDir() { return prefFile("fx"); }
std::string importedDataDir() { return prefFile("data"); }
std::string coreImportDir() { return prefFile("core_import"); }

}  // namespace apppaths
}  // namespace zengine
