// app_paths.h - where the app keeps its own small files (settings, the .savini save, debug flags, touch layout, data folder choice).
//
// One place decides the folder, so a rename (organisation / app name) never has to be chased through the code again.  The folder
// is SDL's per-user pref path of ("bodriodroid", "zgirls"); the first call copies the few files an older build left under the
// previous name ("larpbase", "sg_loader") when the new folder does not have them yet, so nothing the player set is lost.
#pragma once
#include <string>

namespace zengine {
namespace apppaths {

// "<pref folder>/" with a trailing separator, "" when SDL has no per-user folder (then nothing is saved).  Cached after the first call.
const std::string& prefDir();

// prefDir() + name ("" when there is no folder).
std::string prefFile(const std::string& name);

}  // namespace apppaths
}  // namespace zengine
