#include "fx_pack.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace zengine {
namespace fx {

std::string coreFingerprint(std::vector<std::pair<std::string, uint64_t>> files) {
    std::sort(files.begin(), files.end());
    uint64_t h = 1469598103934665603ull;
    auto feed = [&h](const std::string& s) {
        for (unsigned char c : s) {
            h ^= c;
            h *= 1099511628211ull;
        }
    };
    for (const auto& f : files) {
        feed(f.first);
        feed(std::string(1, '\0'));
        feed(std::to_string(f.second));
        feed("\n");
    }
    char b[24];
    std::snprintf(b, sizeof(b), "%016llx", static_cast<unsigned long long>(h));
    return b;
}

bool buildFxPack(const CoreShaderInputs& core, const AeadKey& key, const std::array<uint8_t, 16>& id, std::vector<uint8_t>& out,
                 FxBuildReport* report, std::string* err) {
    FxTuning t = neutralTuning();
    FxBuildReport rep;
    rep.tuningGroups = extractTuning(core.tiledSpriteFp, core.otterVp, core.otterFp, t, &rep.notes);

    PafxWriter w;
    std::ostringstream man;
    man << "format=1\ngenerator=zdroid_pafx\n";
    man << "coreFingerprint=" << core.fingerprint << "\ncoreShaderFiles=" << core.fileCount << "\n";
    man << "tuningGroups=" << rep.tuningGroups << "\ntuningFromCore=" << (t.fromCore ? 1 : 0) << "\nprograms=";
    const auto& progs = fxProgramSources();
    for (size_t i = 0; i < progs.size(); ++i) man << (i ? "," : "") << progs[i].name;
    man << "\n";
    w.addText("fx/manifest.ini", PafxKind::Text, man.str());
    w.addText("fx/tuning.ini", PafxKind::Text, tuningToIni(t));
    for (const FxProgramSource& p : progs) {
        w.addText("fx/" + p.name + ".vert", PafxKind::GlslVertex, p.vertex);
        w.addText("fx/" + p.name + ".frag", PafxKind::GlslFragment, p.fragment);
    }
    if (report) *report = rep;
    return w.build(key, id, out, err);
}

bool FxPack::load(const uint8_t* data, size_t size, const AeadKey& key, std::string* err) {
    tuning_ = neutralTuning();
    programs_.clear();
    fingerprint_.clear();
    PafxArchive a;
    if (!a.open(data, size, key, err)) return false;
    std::string man, tun;
    if (!a.readText("fx/manifest.ini", man, err) || !a.readText("fx/tuning.ini", tun, err)) return false;
    if (!tuningFromIni(tun, tuning_)) {
        if (err) *err = "fx/tuning.ini is malformed";
        return false;
    }
    const size_t p = man.find("coreFingerprint=");
    if (p != std::string::npos) fingerprint_ = man.substr(p + 16, man.find('\n', p) - p - 16);
    const size_t pp = man.find("programs=");
    if (pp == std::string::npos) {
        if (err) *err = "manifest has no program list";
        return false;
    }
    std::string list = man.substr(pp + 9, man.find('\n', pp) - pp - 9);
    std::stringstream ss(list);
    std::string name;
    while (std::getline(ss, name, ',')) {
        FxProgramSource s;
        s.name = name;
        if (!a.readText("fx/" + name + ".vert", s.vertex, err) || !a.readText("fx/" + name + ".frag", s.fragment, err)) return false;
        programs_.push_back(std::move(s));
    }
    return !programs_.empty();
}

const FxProgramSource* FxPack::program(const std::string& name) const {
    for (const FxProgramSource& p : programs_)
        if (p.name == name) return &p;
    return nullptr;
}

}  // namespace fx
}  // namespace zengine
