// fx_store.h - the effects pack on the player's device: find the Core's shader sources, build the pack from them the first time,
// keep it (sealed with a per-install key) in the app's private folder, rebuild it when it no longer fits.
//
// Nothing here is shipped: the pack and its key are made on the device, from the player's own Core, and stay in the app's private
// storage (docs/PAFX.md).  No SDL and no GL in this file, so tests and the packer tool use it as it is.
#pragma once
#include <string>

#include "fx_pack.h"

namespace zengine {
namespace fx {

constexpr const char* kFxPackFile = "fx.pafx";
constexpr const char* kFxKeyFile = "pafx.key";

// Reads the three shader sources the tuning comes from and fingerprints every file of the shaders folder.  `dir` is the Core folder
// (the one that holds shaders/) or the shaders folder itself.  False + *err when there are no shader sources in it.
bool gatherCoreShaders(const std::string& dir, CoreShaderInputs& in, std::string* err = nullptr);

struct FxStoreInfo {
    bool built = false;        // the pack was (re)built by this call
    bool persisted = false;    // it is on disk (false: no private folder, it lives in memory only)
    bool fromCore = false;     // its numbers were read from the player's Core (false: neutral numbers of our own)
    std::string why;           // "created", "the Core changed", "built before this version", "unreadable"; "" when it was loaded as it was
    std::string fingerprint;   // of the Core it was made from ("" for neutral)
};

// `storeDir`: the app's private folder ("" = keep nothing: a pack is built in memory with a throw-away key).  `coreDir` as in
// gatherCoreShaders ("" = no Core found: neutral numbers).
// An existing fx.pafx is used when it opens with the key, has every program of this build and was made from the Core that is there
// now (a Core that cannot be found right now never replaces a pack that was made from one).  Otherwise it is built and written
// (through a temporary file, so a crash never leaves half a pack).
bool loadOrBuildFxPack(const std::string& storeDir, const std::string& coreDir, FxPack& out, FxStoreInfo* info = nullptr, std::string* err = nullptr);

}  // namespace fx
}  // namespace zengine
