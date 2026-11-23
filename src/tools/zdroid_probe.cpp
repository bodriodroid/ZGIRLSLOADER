// zdroid_probe - inventory and sanity-check tool for the user's extracted game folders.
//
//   zdroid_probe <dir> [<dir>...]                       inventory of everything mounted
//   zdroid_probe <dir>... --scenes                      per-scene detail (views, texture names)
//   zdroid_probe <dir>... --missing                     per-scene: textures / sounds / fonts named by the scene and absent from disk
//   zdroid_probe <dir>... --char filia                  load a character, print record statistics
//   zdroid_probe <dir>... --char filia --png out/ [--max 20] [--pal 2p]
//                                                    also write the first frames as PNG
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "gamedata.h"
#include "gbs_scene.h"
#include "modsdir.h"

using namespace zengine;

static uint32_t crc32(const std::vector<uint8_t>& d) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (uint8_t b : d) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static const char* catName(ArtRecord::Category c) {
    switch (c) {
        case ArtRecord::Category::Sprite: return "sprites";
        case ArtRecord::Category::Extra: return "extras";
        default: return "silhouettes";
    }
}

int main(int argc, char** argv) {
    std::vector<std::string> dirs;
    std::string charName, pngDir, palLabel, treeSpec;
    bool scenes = false, records = false, onlyFlagged = false, keys = false, missing = false;
    int maxFrames = 20;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--scenes") scenes = true;
        else if (a == "--missing") missing = true;   // every UI scene: which textures / sounds / fonts it names are not on disk
        else if (a == "--keys") keys = true;   // with --tree: also every channel key of the animations named in KEYFILTER (env)
        else if (a == "--records") records = true;
        else if (a == "--tree" && i + 1 < argc) treeSpec = argv[++i];   // File.gbs:view  -> node tree + animations of that view
        else if (a == "--flagged") onlyFlagged = true;  // --png: only records whose tag has bit 6 set (0x7A/0xFA)
        else if (a == "--char" && i + 1 < argc) charName = argv[++i];
        else if (a == "--png" && i + 1 < argc) pngDir = argv[++i];
        else if (a == "--max" && i + 1 < argc) maxFrames = std::atoi(argv[++i]);
        else if (a == "--pal" && i + 1 < argc) palLabel = argv[++i];
        else if (a.rfind("--", 0) == 0) { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
        else dirs.push_back(a);
    }
    if (dirs.empty()) {
        std::fprintf(stderr, "usage: zdroid_probe <dir>... [--scenes] [--char name [--png outdir] [--max N] [--pal 2p] [--records]]\n");
        return 2;
    }

    GameData gd;
    for (const std::string& d : dirs) {
        size_t n = gd.mount(d);
        std::printf("mounted %s: %zu files%s\n", d.c_str(), n, n ? "" : "  (missing or empty!)");
    }
    {   // <first dir>/mods/{pal,modscustom,stages,sdvoice_japanese,resourcepacks}: created when missing (modsdir.h)
        const modsdir::Report mr = gd.mountModsDir(dirs[0], false);
        std::printf("mods folder %s: %s | packs %zu | mods %zu | stages %zu | custom palettes %zu\n", modsdir::layout(dirs[0]).root.c_str(),
                    mr.layoutOk ? "ok" : "cannot be created (read-only?)", mr.packs, mr.mods, mr.stages, mr.pal.entries.size());
        for (const std::string& l : mr.log) std::printf("  %s\n", l.c_str());
        for (const std::string& w : mr.warnings) std::printf("  warning: %s\n", w.c_str());
        for (size_t i = 0; i < mr.conflicts.size() && i < 20; ++i) {   // the same file in two mods: who wins
            std::string lost;
            for (const std::string& o : mr.conflicts[i].overridden) lost += (lost.empty() ? "" : ", ") + o;
            std::printf("  conflict: %s  -> %s wins over %s\n", mr.conflicts[i].path.c_str(), mr.conflicts[i].winner.c_str(), lost.c_str());
        }
        if (mr.conflicts.size() > 20) std::printf("  (+%zu more conflicts)\n", mr.conflicts.size() - 20);
    }
    Inventory inv = gd.scan();
    std::printf("\n== inventory (%zu files)\n", inv.files);
    std::printf("  ini %zu | lua %zu | stm %zu | seq %zu | glsl %zu | dds %zu | pcx %zu\n", inv.iniFiles, inv.luaFiles,
                inv.stmFiles, inv.seqFiles, inv.glslFiles, inv.ddsFiles, inv.pcxFiles);
    if (inv.gfsFiles)
        std::printf("  .gfs archives: %zu  (use zdroid_gfs unpack <file> <dir>, or Unpack .gfs in the viewer)\n", inv.gfsFiles);
    std::printf("  stages (stages.ini): %zu | music tracks (music.ini): %zu | arcade timelines (toc.ini): %zu\n",
                inv.stages.size(), inv.music.size(), inv.tocFiles.size());
    std::printf("  characters built by the game (compile.ini): %zu\n", inv.compileJobs.size());
    std::printf("  UI scenes (.gbs): %zu\n", inv.scenes.size());
    std::printf("  stages (.lvl): %zu\n", inv.levels.size());
    for (const LevelInfo& lv : inv.levels)
        std::printf("    %-24s %dx%d  start %d/%d  2D backdrop: %s\n", lv.name.c_str(), lv.width, lv.height, lv.start1,
                    lv.start2, lv.background2D.empty() ? "(none)" : lv.background2D.c_str());
    std::printf("  characters with sprite data on disk: %zu\n", inv.characters.size());
    for (const CharacterAssets& c : inv.characters)
        std::printf("    %-14s foit%s%s  palettes: %zu\n", c.name.c_str(), c.artPath.empty() ? "" : " + art",
                    c.artPtPath.empty() ? "" : " + art-pt", c.palettes.size());

    if (scenes) {
        std::printf("\n== scenes\n");
        for (const SceneInfo& s : inv.scenes) {
            const std::string b = std::filesystem::path(s.path).filename().string();
            if (!s.error.empty()) { std::printf("  %-26s ERROR %s\n", b.c_str(), s.error.c_str()); continue; }
            std::printf("  %-26s id %u | %u textures (%zu names found) | %zu views", b.c_str(), s.gbs.sceneId,
                        s.gbs.textureCount, s.gbs.textureNames.size(), s.gbs.views.size());
            if (s.gbs.tailSize) std::printf(" | tail %u B (x%u)", s.gbs.tailSize, s.gbs.count4);
            std::printf("\n");
            for (const GbsView& v : s.gbs.views) std::printf("      view '%s'  @%u  %u B\n", v.name.c_str(), v.offset, v.size);
        }
    }

    int rc = 0;
    if (missing) {
        // For every UI scene: which textures / fonts / sounds it names exist on disk.  Same lookup the UI uses (GameUi::resolveTexture):
        // "<name minus extension>.dds", or the atlas the texture lives in.
        auto stemOf = [](std::string s) {
            for (const char* ext : {".png", ".tga", ".jpg", ".dds"}) {
                const size_t n = std::strlen(ext);
                if (s.size() >= n) {
                    std::string tail = s.substr(s.size() - n);
                    for (char& c : tail) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    if (tail == ext) { s.resize(s.size() - n); break; }
                }
            }
            return s;
        };
        auto haveTex = [&](const std::string& name) { return !name.empty() && gd.vfs().exists(stemOf(name) + ".dds"); };
        std::printf("\n== missing (UI scenes vs the mounted folders)\n");
        size_t allTex = 0, allMissing = 0;
        for (const SceneInfo& si : inv.scenes) {
            const std::string b = std::filesystem::path(si.path).filename().string();
            std::vector<uint8_t> bytes;
            GbsScene sc;
            std::string err;
            if (!gd.vfs().read(b, bytes) || !parseGbsScene(bytes.data(), bytes.size(), sc, &err)) {
                std::printf("  %-26s cannot parse %s\n", b.c_str(), err.c_str());
                continue;
            }
            std::vector<std::string> lost, lostSnd, lostFont;
            for (const GbsTexture& t : sc.textures) {
                bool ok = haveTex(t.name);
                if (!ok && t.inAtlas())
                    if (const GbsTexture* at = sc.textureById(t.atlasId)) ok = haveTex(at->name);
                if (!ok) lost.push_back(t.name);
            }
            for (const GbsSound& s : sc.sounds)
                if (!s.name.empty() && !gd.vfs().exists(s.name)) lostSnd.push_back(s.name);
            for (const GbsFont& f : sc.fonts)
                if (!gd.vfs().exists("Fonts/" + f.name + "_0.dds")) lostFont.push_back(f.name);
            allTex += sc.textures.size();
            allMissing += lost.size();
            std::printf("  %-26s textures %3zu/%-3zu on disk | sounds %zu/%zu | fonts %zu/%zu\n", b.c_str(), sc.textures.size() - lost.size(),
                        sc.textures.size(), sc.sounds.size() - lostSnd.size(), sc.sounds.size(), sc.fonts.size() - lostFont.size(), sc.fonts.size());
            for (const std::string& n : lost) std::printf("      missing texture  %s\n", n.c_str());
            for (const std::string& n : lostSnd) std::printf("      missing sound    %s\n", n.c_str());
            for (const std::string& n : lostFont) std::printf("      missing font     %s\n", n.c_str());
        }
        std::printf("  total: %zu texture rows, %zu missing\n", allTex, allMissing);
    }
    if (!treeSpec.empty()) {
        const size_t colon = treeSpec.find(':');
        const std::string file = treeSpec.substr(0, colon), viewName = colon == std::string::npos ? "" : treeSpec.substr(colon + 1);
        std::vector<uint8_t> bytes;
        GbsScene sc;
        std::string err;
        if (!gd.vfs().read(file, bytes) || !parseGbsScene(bytes.data(), bytes.size(), sc, &err)) {
            std::printf("\n== tree: cannot read %s %s\n", file.c_str(), err.c_str());
            rc = 1;
        } else {
            std::printf("\n== tree %s: %zu fonts, %zu textures, %zu sounds\n", file.c_str(), sc.fonts.size(), sc.textures.size(), sc.sounds.size());
            for (const GbsSceneView& v : sc.views) {
                if (!viewName.empty() && v.name != viewName) continue;
                std::printf("view '%s' %.0fx%.0f, %zu animations\n", v.name.c_str(), v.width, v.height, v.anims.size());
                struct Dump {
                    const GbsScene& sc;
                    void go(const std::vector<GbsNode>& ns, int depth) const {
                        for (const GbsNode& n : ns) {
                            std::printf("%*s%s #%u '%s' %s (%.0f,%.0f %.0fx%.0f)", depth * 2 + 2, "", n.tag.c_str(), n.id, n.name.c_str(),
                                        n.type == GbsNode::Type::Sprite ? "sprite" : n.type == GbsNode::Type::Label ? "label" : "group", n.x, n.y, n.w, n.h);
                            if (n.type == GbsNode::Type::Sprite && n.texId != kGbsNoTexture) std::printf(" tex=%s", sc.textureName(n.texId).c_str());
                            if (n.type == GbsNode::Type::Label) {
                                const GbsFont* f = sc.fontById(n.fontId);
                                std::printf(" font=%s align=%d scale=%.2f", f ? f->name.c_str() : "?", n.hAlign, n.scaleX);
                            }
                            std::printf("\n");
                            go(n.kids, depth + 1);
                        }
                    }
                } d{sc};
                d.go(v.nodes, 0);
                for (const GbsAnim& a : v.anims) {
                    std::printf("  anim '%s' %d ticks%s | %zu channels | %zu sounds | %zu messages\n", a.name.c_str(), a.length,
                                a.loops() ? " (loops)" : "", a.channels.size(), a.sounds.size(), a.messages.size());
                    const char* kf = std::getenv("KEYFILTER");
                    if (keys && kf && std::string(kf).find(a.name) != std::string::npos)
                        for (const GbsSoundCue& m : a.messages) std::printf("      MSGA t=%d id=%u\n", m.t, m.soundId);
                    if (keys && kf && std::string(kf).find(a.name) != std::string::npos)
                        for (const GbsChannel& ch : a.channels)
                            for (const GbsKey& k : ch.keys)
                                std::printf("      node %u t=%d %c pos(%.0f,%.0f) size(%.0f,%.0f) argb=%08x scale(%.2f,%.2f) pivot(%.0f,%.0f)\n", ch.nodeId, k.t, k.kind, k.x, k.y, k.w, k.h, k.argb, k.scaleX, k.scaleY, k.pivotX, k.pivotY);
                }
            }
        }
    }
    if (!charName.empty()) {
        const CharacterAssets* ca = nullptr;
        for (const CharacterAssets& c : inv.characters)
            if (c.name == charName) ca = &c;
        std::printf("\n== character %s\n", charName.c_str());
        if (!ca) {
            std::printf("  not found (no %s.foit in the mounted folders)\n", charName.c_str());
            rc = 1;
        } else {
            ArtFile af;
            std::string err;
            if (!gd.loadCharacter(*ca, af, &err)) {
                std::printf("  load failed: %s\n", err.c_str());
                rc = 1;
            } else {
                size_t cnt[3] = {0, 0, 0};
                for (const ArtRecord& r : af.records()) ++cnt[static_cast<int>(r.category())];
                if (!af.hasArt()) std::printf("  note: no .art file - only .art-pt records can be decoded\n");
                if (!af.hasArtPt()) std::printf("  note: no .art-pt file - only .art records can be decoded\n");
                size_t flagged = 0;
                for (const ArtRecord& r : af.records()) flagged += r.flagged();
                std::printf("  records with tag bit 6 set (0x7A/0xFA): %zu\n", flagged);
                std::printf("  frames: sprites %zu | extras %zu | silhouettes %zu  (covers .art %.1f%%, .art-pt %.1f%%)\n",
                            cnt[0], cnt[1], cnt[2], af.coverageArt() * 100, af.coveragePt() * 100);
                if (records) {
                    // one line per record: used to diff against tools/reference/sg_extract.py (see tests/real_diff.py)
                    for (size_t i = 0; i < af.records().size(); ++i) {
                        const ArtRecord& r = af.records()[i];
                        ArtPlanes pl;
                        const bool ok = af.decode(i, pl);
                        std::printf("REC %s %02x %u %u %u %u %u %u %u %u %u crc %08x %08x%s\n",
                                    r.src == ArtRecord::Src::Art ? "art" : "art-pt", r.tag, r.w, r.h, r.n, r.A, r.B,
                                    r.foitOff, r.off, r.sizeNormal + r.sizeGrad + r.sizeBand, r.sizeNormal,
                                    ok ? crc32(pl.band) : 0u, ok ? crc32(pl.grad) : 0u, ok ? "" : " DECODE-FAILED");
                    }
                }
                if (!pngDir.empty()) {
                    Palette pal = fallbackPalette();
                    std::string used = "fallback (no .dds found)";
                    for (const auto& p : ca->palettes)
                        if (palLabel.empty() || p.first == palLabel) {
                            std::string perr;
                            if (GameData::loadPalette(p.second, pal, &perr)) { used = p.first + " = " + p.second; break; }
                            std::printf("  palette %s: %s\n", p.second.c_str(), perr.c_str());
                        }
                    std::printf("  palette: %s\n", used.c_str());
                    std::filesystem::create_directories(pngDir);
                    const std::vector<uint8_t> tables = buildRgbaTables(pal);
                    int written = 0;
                    for (size_t i = 0; i < af.records().size() && written < maxFrames; ++i) {
                        if (onlyFlagged && !af.records()[i].flagged()) continue;
                        ArtPlanes pl;
                        if (!af.decode(i, pl)) continue;
                        const std::vector<uint8_t> rgba = toRgba(pl, tables);
                        const std::vector<uint8_t> png = encodePngRgba(rgba.data(), pl.width, pl.height);
                        char fn[512];
                        std::snprintf(fn, sizeof fn, "%s/%s_%04zu_%s.png", pngDir.c_str(), charName.c_str(), i,
                                      catName(af.records()[i].category()));
                        if (std::FILE* f = std::fopen(fn, "wb")) {
                            std::fwrite(png.data(), 1, png.size(), f);
                            std::fclose(f);
                            ++written;
                        }
                    }
                    std::printf("  wrote %d PNG frames to %s\n", written, pngDir.c_str());
                }
            }
        }
    }

    if (!inv.warnings.empty()) {
        std::printf("\n== warnings (%zu)\n", inv.warnings.size());
        for (const std::string& w : inv.warnings) std::printf("  ! %s\n", w.c_str());
    }
    return rc;
}
