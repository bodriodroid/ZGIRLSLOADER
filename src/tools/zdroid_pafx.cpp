// zdroid_pafx - build, inspect and self-check the effects pack (.pafx).
//
//   zdroid_pafx pack  --core <Core folder> --out <file.pafx> [--key <key file>]
//   zdroid_pafx info  <file.pafx> [--key <key file>]
//   zdroid_pafx dump  <file.pafx> --dir <folder> [--key <key file>]       (writes the sealed entries as plain files, for debugging)
//   zdroid_pafx gen-check <folder> [--core <Core folder>]                 (cases for tools/fx_gles_check.py)
//
// The key file is the per-install secret (created on first use, default ./pafx.key).  A pack is made from the player's own Core on
// the player's own machine and is never distributed; see docs/PAFX.md.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "fx_glsl.h"
#include "fx_math.h"
#include "fx_pack.h"
#include "fx_store.h"

using namespace zengine;
using namespace zengine::fx;
namespace fs = std::filesystem;

namespace {

bool readFile(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}
bool writeFile(const fs::path& p, const void* d, size_t n) {
    std::ofstream f(p, std::ios::binary);
    if (!f) return false;
    f.write(static_cast<const char*>(d), static_cast<std::streamsize>(n));
    return static_cast<bool>(f);
}

// `--name value` lookup
const char* opt(int argc, char** argv, const char* name) {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], name) == 0) return argv[i + 1];
    return nullptr;
}

bool gatherCore(const fs::path& coreArg, CoreShaderInputs& in, std::string* err) { return gatherCoreShaders(coreArg.string(), in, err); }

bool getKey(int argc, char** argv, AeadKey& key) {
    std::string err;
    const char* kp = opt(argc, argv, "--key");
    if (!loadOrCreatePafxKey(kp ? kp : "pafx.key", key, &err)) {
        std::fprintf(stderr, "key: %s\n", err.c_str());
        return false;
    }
    return true;
}

int cmdPack(int argc, char** argv) {
    const char* core = opt(argc, argv, "--core");
    const char* out = opt(argc, argv, "--out");
    if (!core || !out) {
        std::fprintf(stderr, "usage: zdroid_pafx pack --core <Core folder> --out <file.pafx> [--key <file>]\n");
        return 2;
    }
    AeadKey key;
    if (!getKey(argc, argv, key)) return 1;
    CoreShaderInputs in;
    std::string err;
    if (!gatherCore(core, in, &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    std::array<uint8_t, 16> id;
    if (!secureRandom(id.data(), id.size())) {
        std::fprintf(stderr, "no secure random source\n");
        return 1;
    }
    std::vector<uint8_t> bytes;
    FxBuildReport rep;
    if (!buildFxPack(in, key, id, bytes, &rep, &err)) {
        std::fprintf(stderr, "build failed: %s\n", err.c_str());
        return 1;
    }
    if (!writeFile(out, bytes.data(), bytes.size())) {
        std::fprintf(stderr, "cannot write %s\n", out);
        return 1;
    }
    std::printf("wrote %s (%zu bytes): %d tuning group(s) read from Core, %d shader file(s) fingerprinted %s\n", out, bytes.size(),
                rep.tuningGroups, in.fileCount, in.fingerprint.c_str());
    for (const std::string& n : rep.notes) std::printf("  note: %s\n", n.c_str());
    return 0;
}

int cmdInfo(int argc, char** argv, bool dump) {
    if (argc < 3) return 2;
    AeadKey key;
    if (!getKey(argc, argv, key)) return 1;
    std::string raw;
    if (!readFile(argv[2], raw)) {
        std::fprintf(stderr, "cannot read %s\n", argv[2]);
        return 1;
    }
    PafxArchive a;
    std::string err;
    if (!a.open(reinterpret_cast<const uint8_t*>(raw.data()), raw.size(), key, &err)) {
        std::fprintf(stderr, "open: %s\n", err.c_str());
        return 1;
    }
    const char* dir = opt(argc, argv, "--dir");
    if (dump && !dir) {
        std::fprintf(stderr, "dump needs --dir\n");
        return 2;
    }
    for (const PafxEntryInfo& e : a.entries()) {
        std::printf("%-34s kind %u  %u bytes\n", e.name.c_str(), static_cast<unsigned>(e.kind), e.size);
        if (dump) {
            std::vector<uint8_t> b;
            if (!a.read(e.name, b, &err)) {
                std::fprintf(stderr, "%s\n", err.c_str());
                return 1;
            }
            const fs::path p = fs::path(dir) / e.name;
            fs::create_directories(p.parent_path());
            writeFile(p, b.data(), b.size());
        }
    }
    return 0;
}

// ---------------------------------------------------------------- check cases for the GLES harness
struct Lcg {
    uint32_t s;
    uint32_t next() { return s = s * 1664525u + 1013904223u; }
    float f() { return static_cast<float>(next() >> 8) / 16777216.0f; }
};

uint8_t q8(float x) { return static_cast<uint8_t>(std::lround(std::fmax(0.0f, std::fmin(1.0f, x)) * 255.0f)); }

std::string fl(float v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.9g", static_cast<double>(v));
    return b;
}
std::string v3(const float* v) { return fl(v[0]) + " " + fl(v[1]) + " " + fl(v[2]); }

int cmdGenCheck(int argc, char** argv) {
    if (argc < 3) return 2;
    const fs::path dir = argv[2];
    fs::create_directories(dir / "programs");
    for (const FxProgramSource& p : fxProgramSources()) {
        writeFile(dir / "programs" / (p.name + ".vert"), p.vertex.data(), p.vertex.size());
        writeFile(dir / "programs" / (p.name + ".frag"), p.fragment.data(), p.fragment.size());
    }

    // numbers: ours, or the ones read from a Core when --core is given
    FxTuning tun = neutralTuning();
    if (const char* core = opt(argc, argv, "--core")) {
        CoreShaderInputs in;
        std::string err;
        if (!gatherCore(core, in, &err)) {
            std::fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        const int n = extractTuning(in.tiledSpriteFp, in.otterVp, in.otterFp, tun);
        std::printf("tuning: %d group(s) from the Core\n", n);
    } else {
        // make the neutral numbers lively enough to exercise every term
        tun.sprite.look[1] = {{0.2f, 0.05f, 0.0f}, {1.0f, 0.7f, 0.4f}};
        tun.sprite.contrast = 1.4f;
        tun.sprite.brightness = 1.3f;
        tun.sprite.saturation = 0.1f;
        tun.ui.look[0] = {{0.1f, 0.1f, 0.3f}, {0.8f, 0.9f, 1.0f}};
        tun.ui.saturation = -0.15f;
        tun.shadowTint[0] = 0.3f; tun.shadowTint[1] = 0.35f; tun.shadowTint[2] = 0.4f;
    }

    const int W = 64, H = 64, TONES = 16, BANDS = 40;
    Lcg rnd{12345};
    Palette pal;
    pal.tones = TONES;
    pal.bands = BANDS;
    pal.rgb.resize(static_cast<size_t>(TONES) * BANDS * 3);
    for (uint8_t& b : pal.rgb) b = static_cast<uint8_t>(rnd.next() >> 24);
    std::vector<uint8_t> index(static_cast<size_t>(W) * H * 4), flat(static_cast<size_t>(W) * H * 4), cover(static_cast<size_t>(W) * H * 4);
    for (int i = 0; i < W * H; ++i) {
        const uint32_t r = rnd.next();
        const uint8_t band = (r >> 28) < 3 ? 0 : static_cast<uint8_t>(1 + (r >> 8) % (BANDS - 1));  // some outline layer, some empty
        const uint8_t grad = static_cast<uint8_t>(r >> 16);
        index[i * 4 + 0] = 0;
        index[i * 4 + 1] = grad;
        index[i * 4 + 2] = band;
        index[i * 4 + 3] = 255;
        for (int c = 0; c < 4; ++c) flat[i * 4 + c] = static_cast<uint8_t>(rnd.next() >> 24);
        const uint8_t cv = (rnd.next() >> 29) < 3 ? 255 : static_cast<uint8_t>(rnd.next() >> 24);  // coverage in R
        cover[i * 4 + 0] = cv; cover[i * 4 + 1] = 0; cover[i * 4 + 2] = 0; cover[i * 4 + 3] = 255;
    }
    std::vector<uint8_t> palTex(static_cast<size_t>(TONES) * BANDS * 4);
    for (int i = 0; i < TONES * BANDS; ++i) {
        palTex[i * 4 + 0] = pal.rgb[i * 3 + 0]; palTex[i * 4 + 1] = pal.rgb[i * 3 + 1]; palTex[i * 4 + 2] = pal.rgb[i * 3 + 2]; palTex[i * 4 + 3] = 255;
    }
    writeFile(dir / "index.rgba", index.data(), index.size());
    writeFile(dir / "flat.rgba", flat.data(), flat.size());
    writeFile(dir / "cover.rgba", cover.data(), cover.size());
    writeFile(dir / "palette.rgba", palTex.data(), palTex.size());

    std::ostringstream cs;
    int caseNo = 0;
    auto uni = [&](std::ostringstream& o, const char* type, const char* name, const std::string& v) { o << "uniform " << type << " " << name << " " << v << "\n"; };
    auto screenUniforms = [&](std::ostringstream& o, const ScreenFx& f) {
        uni(o, "f2", "uScreen", fl(f.shadowT) + " " + fl(f.flashT));
        uni(o, "f3", "uFlashLow", v3(f.look.low));
        uni(o, "f3", "uFlashHigh", v3(f.look.high));
        uni(o, "f3", "uFlashLuma", v3(f.lumaCoeff));
        const float curve[3] = {f.contrast, f.brightness, f.saturation};
        uni(o, "f3", "uFlashCurve", v3(curve));
        uni(o, "f3", "uShadowTint", v3(f.shadowTint));
        uni(o, "f3", "uShadowLuma", v3(f.shadowLuma));
    };
    auto spriteUniforms = [&](std::ostringstream& o, const SpriteParams& p) {
        uni(o, "f2", "uPaletteSize", std::to_string(TONES) + " " + std::to_string(BANDS));
        uni(o, "f1", "uAlpha", fl(p.alpha));
        uni(o, "f3", "uLine", v3(p.line));
        const float hue[3] = {p.hueCos, p.hueSin, p.hueAmount};
        uni(o, "f3", "uHue", v3(hue));
    };
    auto emit = [&](const std::string& prog, const std::string& header, const std::vector<uint8_t>& ref) {
        const std::string rn = "ref_" + std::to_string(caseNo) + ".rgba";
        writeFile(dir / rn, ref.data(), ref.size());
        cs << "case " << caseNo << "_" << prog << " " << prog << " " << W << " " << H << "\n" << header << "ref " << rn << "\nend\n";
        ++caseNo;
    };
    auto put = [&](std::vector<uint8_t>& ref, int x, int y, Rgba c) {
        uint8_t* o = &ref[(static_cast<size_t>(y) * W + x) * 4];
        o[0] = q8(c.r); o[1] = q8(c.g); o[2] = q8(c.b); o[3] = q8(c.a);
    };
    auto texel = [&](const std::vector<uint8_t>& t, int x, int y) { return &t[(static_cast<size_t>(y) * W + x) * 4]; };

    // ---- sprite_palette in several configurations
    struct Cfg { const char* tag; bool ui; int flashNum; float shadowT, flashT, alpha, hueA, hueAngle; float line[3]; };
    const Cfg cfgs[] = {
        {"plain", false, 1, 0, 0, 1, 0, 0, {0, 0, 0}},
        {"hue_line_alpha", false, 1, 0, 0, 0.6f, 0.8f, 0.7f, {0.1f, 0.0f, 0.2f}},
        {"flash_sprite", false, 2, 0.5f, 0.7f, 1, 0, 0, {0, 0, 0}},
        {"flash_ui", true, 1, 1.0f, 1.0f, 0.9f, 0.3f, 2.1f, {0.05f, 0.05f, 0.05f}},
    };
    for (const Cfg& c : cfgs) {
        SpriteParams p;
        p.alpha = c.alpha;
        p.hueAmount = c.hueA;
        p.hueCos = std::cos(c.hueAngle);
        p.hueSin = std::sin(c.hueAngle);
        std::memcpy(p.line, c.line, sizeof(p.line));
        p.screen = ScreenFx::make(tun, c.ui, c.flashNum, c.shadowT, c.flashT);
        std::ostringstream h;
        h << "tex uIndex index.rgba nearest\ntex uPalette palette.rgba nearest " << TONES << " " << BANDS << "\n";
        spriteUniforms(h, p);
        screenUniforms(h, p.screen);
        std::vector<uint8_t> ref(static_cast<size_t>(W) * H * 4);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) put(ref, x, y, spritePalette(pal, texel(index, x, y)[2], texel(index, x, y)[1], p));
        emit("sprite_palette", h.str(), ref);
    }

    // ---- super_outline
    {
        SpriteParams p;
        p.alpha = 0.8f;
        const float a[3] = {1.0f, 0.2f, 0.1f}, b[3] = {0.1f, 0.3f, 1.0f};
        const float m = 0.35f;
        std::ostringstream h;
        h << "tex uIndex index.rgba nearest\ntex uPalette palette.rgba nearest " << TONES << " " << BANDS << "\n";
        spriteUniforms(h, p);
        uni(h, "f3", "uOutlineA", v3(a));
        uni(h, "f3", "uOutlineB", v3(b));
        uni(h, "f1", "uOutlineMix", fl(m));
        screenUniforms(h, p.screen);
        std::vector<uint8_t> ref(static_cast<size_t>(W) * H * 4);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) put(ref, x, y, superOutline(pal, texel(index, x, y)[2], texel(index, x, y)[1], p, a, b, m));
        emit("super_outline", h.str(), ref);
    }

    // ---- floor_shadow: the height runs from z0 at the bottom row to z1 at the top row
    const struct { float fade, z0, z1; } floors[] = {{400.0f, -380.0f, -20.0f}, {400.0f, 100.0f, 300.0f}, {-250.0f, 10.0f, 240.0f}};
    for (const auto& fz : floors) {
        const float maxA = 0.75f;
        std::ostringstream h;
        h << "tex uIndex index.rgba nearest\ntex uPalette palette.rgba nearest " << TONES << " " << BANDS << "\n";
        SpriteParams p;
        spriteUniforms(h, p);
        screenUniforms(h, p.screen);
        uni(h, "f2", "uFloor", fl(fz.fade) + " " + fl(maxA));
        h << "z " << fl(fz.z0) << " " << fl(fz.z1) << "\n";
        std::vector<uint8_t> ref(static_cast<size_t>(W) * H * 4, 0);
        for (int y = 0; y < H; ++y) {
            const float z = fz.z0 + (fz.z1 - fz.z0) * ((y + 0.5f) / H);
            for (int x = 0; x < W; ++x) {
                const float a = floorShadowAlpha(texel(index, x, y)[2], texel(index, x, y)[1], z, fz.fade, maxA);
                put(ref, x, y, a < 0 ? Rgba{0, 0, 0, 0} : Rgba{0, 0, 0, a});
            }
        }
        emit("floor_shadow", h.str(), ref);
    }

    // ---- flat_tint (stage / UI card): texture * vertex colour, screen effect on top
    {
        const ScreenFx f = ScreenFx::make(tun, false, 3, 0.6f, 0.4f);
        const Rgba vc{0.9f, 0.8f, 1.0f, 0.75f};
        std::ostringstream h;
        h << "tex uTex flat.rgba nearest\ncolor " << fl(vc.r) << " " << fl(vc.g) << " " << fl(vc.b) << " " << fl(vc.a) << "\n";
        screenUniforms(h, f);
        std::vector<uint8_t> ref(static_cast<size_t>(W) * H * 4);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const uint8_t* t = texel(flat, x, y);
                put(ref, x, y, flatTint({t[0] / 255.0f, t[1] / 255.0f, t[2] / 255.0f, t[3] / 255.0f}, vc, f));
            }
        emit("flat_tint", h.str(), ref);
    }

    // ---- death portrait: the second sample is one texel to the left (the shift is exactly one texel, sampled at texel centres)
    {
        const float shift = -1.0f / static_cast<float>(W), va = 0.9f;
        std::ostringstream h;
        h << "tex uTex cover.rgba nearest\ncolor 1 1 1 " << fl(va) << "\n";
        uni(h, "f3", "uStroke", v3(tun.deathStroke));
        uni(h, "f3", "uFill", v3(tun.deathFill));
        uni(h, "f1", "uShift", fl(shift));
        std::vector<uint8_t> ref(static_cast<size_t>(W) * H * 4);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const float r0 = texel(cover, x, y)[0] / 255.0f, r1 = texel(cover, x > 0 ? x - 1 : 0, y)[0] / 255.0f;
                put(ref, x, y, deathPortrait(tun, r0, r1, va));
            }
        emit("death_portrait", h.str(), ref);
    }

    // ---- death background
    {
        std::ostringstream h;
        h << "color 1 1 1 1\n";
        std::string bg;
        for (int i = 0; i < 5; ++i) bg += (i ? " " : "") + v3(tun.deathBg[i]);
        uni(h, "f3", "uBg", bg);
        std::string st;
        for (int i = 0; i < 5; ++i) st += (i ? " " : "") + fl(tun.deathBgStop[i]);
        uni(h, "f1", "uBgStop", st);
        uni(h, "f1", "uAspect", fl(tun.deathBgAspect));
        std::vector<uint8_t> ref(static_cast<size_t>(W) * H * 4);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const Rgb c = deathBackground(tun, (x + 0.5f) / W, (y + 0.5f) / H);
                put(ref, x, y, {c.r, c.g, c.b, 1.0f});
            }
        emit("death_background", h.str(), ref);
    }

    writeFile(dir / "cases.txt", cs.str().data(), cs.str().size());
    std::printf("wrote %d case(s) and %zu program(s) to %s\n", caseNo, fxProgramSources().size(), dir.string().c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 2) {
        const std::string c = argv[1];
        if (c == "pack") return cmdPack(argc, argv);
        if (c == "info") return cmdInfo(argc, argv, false);
        if (c == "dump") return cmdInfo(argc, argv, true);
        if (c == "gen-check") return cmdGenCheck(argc, argv);
    }
    std::fprintf(stderr,
                 "usage:\n  zdroid_pafx pack --core <Core folder> --out <file.pafx> [--key <file>]\n  zdroid_pafx info <file.pafx> [--key <file>]\n"
                 "  zdroid_pafx dump <file.pafx> --dir <folder> [--key <file>]\n  zdroid_pafx gen-check <folder> [--core <Core folder>]\n");
    return 2;
}
