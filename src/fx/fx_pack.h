// fx_pack.h - build and read the effects pack (.pafx with the effect programs + tuning + manifest).
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "fx_glsl.h"
#include "fx_tuning.h"
#include "pafx.h"

namespace zengine {
namespace fx {

// What the packer sees of the player's Core: the text of the three shader sources it reads numbers from, plus a fingerprint of the
// whole shaders/ folder (so the loader can tell the pack was made from the Core that is installed now).
struct CoreShaderInputs {
    std::string tiledSpriteFp, otterVp, otterFp;
    std::string fingerprint;   // fnv1a-64 of "relative/path\0size\n" lines, sorted; "" when unknown
    int fileCount = 0;
};

// 16 hex digits.
std::string coreFingerprint(std::vector<std::pair<std::string, uint64_t>> filesAndSizes);

struct FxBuildReport {
    int tuningGroups = 0;
    std::vector<std::string> notes;
};

bool buildFxPack(const CoreShaderInputs& core, const AeadKey& key, const std::array<uint8_t, 16>& archiveId, std::vector<uint8_t>& out,
                 FxBuildReport* report = nullptr, std::string* err = nullptr);

class FxPack {
public:
    bool load(const uint8_t* data, size_t size, const AeadKey& key, std::string* err = nullptr);
    const FxTuning& tuning() const { return tuning_; }
    const std::vector<FxProgramSource>& programs() const { return programs_; }
    const FxProgramSource* program(const std::string& name) const;
    const std::string& coreFingerprint() const { return fingerprint_; }
    bool tuningFromCore() const { return tuning_.fromCore; }

private:
    FxTuning tuning_;
    std::vector<FxProgramSource> programs_;
    std::string fingerprint_;
};

}  // namespace fx
}  // namespace zengine
