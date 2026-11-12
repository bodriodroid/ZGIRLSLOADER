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

// The app's private folder for sealed material: the effects pack, the per-install key and the Core vault live here (vault.h, fx_store.h).
std::string coreStoreDir();
// The rest of the game's data (ui, stages, textures, characters ...) as the Android setup screen copied it out of the folder the player
// picked, so the app needs no storage permission.  Mounted after the Core vault, before the app's own data folder.
std::string importedDataDir();
// Where the Android shell leaves the copy of the folder the player picked, until the loader has sealed it into the vault and deleted it.
std::string coreImportDir();

}  // namespace apppaths
}  // namespace zengine
