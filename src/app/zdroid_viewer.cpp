// zdroid_viewer - SDL2 front end of the Skullgirls 2nd Encore loader (desktop and Android).
//
// Five screens, all drawn with SDL_Renderer at a 640x360 logical size (touch input arrives as mouse events):
//   Menu    title + main menu (the game's own menu .ini files and localisation; the start screen)
//   Home    data folder, what was found in it, what the Steam download put on disk
//   Browse  every character's frames, decoded from <name>.foit/.art/.art-pt with the <name>_<N>p.dds palettes
//   Steam   download the game with DepotDownloader using the user's OWN Steam account
//   Play    an actual playable match on the fight runtime, with the fighters' real sprites
//
// Headless smoke test (SDL_VIDEODRIVER=dummy):
//   zdroid_viewer <dir> --smoke out.png [--screen menu|splash|attract|charselect|home|browse|steam|play] [--attract-shot N --attract-tick T] [--lang es] [--skip-title | --accept-notice] [--char annie] [--frame N] [--cat all|sprites|extras|sil]
//              [--pal 1p] [--zoom fit|1|2] [--bg 0|1|2] [--click x,y]... [--smoke-phase guard|downloading|done|failed|...]
//              [--exe <DepotDownloader>] [--steam-user U] [--steam-pass-env VAR] [--steam-code C] [--steam-autostart]
#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#ifdef __ANDROID__
#include <jni.h>
#endif

#include "app_paths.h"
#include "audio.h"
#include "music_lib.h"
#include "music_player.h"
#include "gamedata.h"
#include "zd_features.h"
#include "menu.h"
#include "modloader.h"
#include "modsdir.h"
#include "play.h"
#include "credits.h"
#include "gallery.h"
#include "story_mode.h"
#include "steam_task.h"
#include "ui.h"  // Col / Rect / Input / Ui: shared with the Play screen

namespace fs = std::filesystem;
using namespace zengine;

namespace {

constexpr int kW = 640, kH = 360;


std::string humanBytes(uint64_t b) {
    char buf[64];
    if (b >= (1ull << 30)) std::snprintf(buf, sizeof buf, "%.2f GB", b / double(1ull << 30));
    else if (b >= (1ull << 20)) std::snprintf(buf, sizeof buf, "%.1f MB", b / double(1ull << 20));
    else if (b >= 1024) std::snprintf(buf, sizeof buf, "%.1f KB", b / 1024.0);
    else std::snprintf(buf, sizeof buf, "%llu B", static_cast<unsigned long long>(b));
    return buf;
}

void wipe(std::string& s) {
    std::fill(s.begin(), s.end(), '\0');
    s.clear();
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}


// ---------------------------------------------------------------------------------------- application state
enum class Screen { Home, Browse, Steam, Play, Menu, Story, Gallery, Credits };   // Story after Menu: the top bar maps tabs 0..3

struct Options {
    std::vector<std::string> dirs;
    std::string story, fight, csP2, smokePng, screen, character, cat, pal, zoom, exe, steamUser, steamPassEnv, steamCode, smokePhase, lang;
    int frame = 0, bg = -1, smokeFrames = 4, loadDelayMs = 0, smokeScale = 1, fixedDtMs = 0;
    bool steamAutostart = false, skipTitle = false, acceptNotice = false, smokeLoading = false, gallery = false, credits = false, contributors = false;
    int csStep = 0, csPal = 0;
    bool storyAudit = false;                 // --story-audit: check every story node, print the problems, exit (smoke)
    bool modsView = false;                   // --mods-view: open the Mods section of Browse (smoke)
    bool csStory = false;                    // --cs-step 0|1|2 (size / fighter / stage), --cs-story: Character Select smoke runs
    int attractShot = 0, attractTick = 0;   // --screen attract: which shot (1-based on the command line) and tick to start from
    std::vector<std::pair<int, int>> clicks;
    int clickAfter = 1;   // first frame the scripted clicks may happen on (a match needs a while to load)
};

#ifdef __ANDROID__
std::string androidNativeLibDir() {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jobject act = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !act) return {};
    std::string out;
    jclass cls = env->GetObjectClass(act);
    jmethodID gai = env->GetMethodID(cls, "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;");
    jobject ai = gai ? env->CallObjectMethod(act, gai) : nullptr;
    if (ai) {
        jclass aic = env->GetObjectClass(ai);
        jfieldID f = env->GetFieldID(aic, "nativeLibraryDir", "Ljava/lang/String;");
        jstring s = f ? static_cast<jstring>(env->GetObjectField(ai, f)) : nullptr;
        if (s) {
            const char* c = env->GetStringUTFChars(s, nullptr);
            if (c) out = c;
            env->ReleaseStringUTFChars(s, c);
            env->DeleteLocalRef(s);
        }
        env->DeleteLocalRef(aic);
        env->DeleteLocalRef(ai);
    }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(act);
    return out;
}
#endif

bool onPath(const std::string& name, std::string* found) {
    if (name.find('/') != std::string::npos) {
        std::error_code ec;
        if (fs::exists(name, ec)) {
            if (found) *found = name;
            return true;
        }
        return false;
    }
    const char* p = std::getenv("PATH");
    if (!p) return false;
    std::string path = p;
    size_t i = 0;
    while (i <= path.size()) {
        size_t j = path.find(':', i);
        if (j == std::string::npos) j = path.size();
        const std::string dir = path.substr(i, j - i);
        std::error_code ec;
        if (!dir.empty() && fs::exists(fs::path(dir) / name, ec)) {
            if (found) *found = (fs::path(dir) / name).string();
            return true;
        }
        i = j + 1;
    }
    return false;
}

// Everything the loader indexes from the user's own folders.  Built on a worker thread (the splash is
// on screen meanwhile) and applied on the main thread, so no UI object ever sees a half-built GameData.
struct LoadResult {
    GameData gd;
    Inventory inv;
    size_t modCount = 0;
    std::vector<std::string> modLog;
    modsdir::Report modsRep;      // what mods/ holds, for the Mods section of Browse
    size_t dlGfs = 0, dlFiles = 0;
    uint64_t dlBytes = 0;
};

void scanDownloadDir(const std::string& steamDir, size_t& gfs, size_t& files, uint64_t& bytes) {
    gfs = files = 0;
    bytes = 0;
    std::error_code ec;
    fs::recursive_directory_iterator it(steamDir, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        ++files;
        bytes += static_cast<uint64_t>(it->file_size(e2));
        if (lower(it->path().extension().string()) == ".gfs") ++gfs;
    }
}

std::unique_ptr<LoadResult> buildData(const std::string& dataRoot, const std::vector<std::string>& extraDirs, int delayMs, bool jpVoices = false) {
    auto r = std::make_unique<LoadResult>();
    if (delayMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    r->gd.mount(dataRoot);
    for (const std::string& d : extraDirs) r->gd.mount(d);
    // SKGLoader/mods/* override the base game: mounted last, higher priority last.
    r->modCount = r->gd.mountMods(dataRoot, &r->modLog);
    // <gameDir>/mods/{pal,modscustom,stages,sdvoice_japanese,resourcepacks}: created if missing, then mounted (modsdir.h)
    const modsdir::Report mrep = r->gd.mountModsDir(dataRoot, jpVoices);
    r->modCount += mrep.packs + mrep.mods + mrep.stages;
    r->modsRep = mrep;
    for (const std::string& l : mrep.log) r->modLog.push_back(l);
    for (const std::string& w : mrep.warnings) std::printf("mods: %s\n", w.c_str());
    if (!mrep.pal.entries.empty()) std::printf("mods: %zu custom palette(s)\n", mrep.pal.entries.size());
    r->inv = r->gd.scan();
    scanDownloadDir((fs::path(dataRoot) / "SKGSTEAM").string(), r->dlGfs, r->dlFiles, r->dlBytes);
    return r;
}

struct App {
    // ---- configuration
    Options opt;
    std::string dataRoot;         // editable on the Home screen
    std::string dataRootEdit;
    std::string prefsDir;         // persistent per-app folder (data root choice, Steam login token)
    std::string exePath;          // DepotDownloader
    bool exeFound = false;
    Screen screen = Screen::Home;

    // ---- game data
    GameData gd;
    Inventory inv;
    size_t dlGfs = 0, dlFiles = 0;
    uint64_t dlBytes = 0;

    // ---- browser
    int charIdx = -1;
    ArtFile art;
    std::string loadErr;
    std::vector<std::pair<std::string, std::string>> palettes;
    int palIdx = 0;
    Palette pal;
    std::vector<uint8_t> tables;
    int cat = 0;  // 0 all, 1 sprites, 2 extras, 3 silhouettes
    std::vector<int> filtered;
    int fpos = 0;
    bool frameDirty = true;
    SDL_Texture* tex = nullptr;
    int texW = 0, texH = 0;
    std::string frameErr;
    int bg = 0, zoom = 0;  // zoom: 0 fit, 1 = 1x, 2 = 2x
    float listScroll = 0;
    bool listDrag = false;
    int listDragStartY = 0;

    // ---- play (real match on the fight runtime) and the main menu in front of it
    play::PlayScreen play;
    menu::MenuScreen menu;
    gallery::GalleryScreen gallery;   // Extras -> Art Gallery
    credits::CreditsScreen credits;   // Extras -> Credits / Contributors
    story::StoryScreen story;   // Story Mode (Visual Novel): cutscenes + fights, front door is the menu
    Audio audio;  // menu theme + UI sounds (silent when there is no device or no wav)
    MusicLibrary musicLib;   // every theme the data has (music.ini + the .wav files), and which screen plays which
    MusicPlayer musicPlayer;

    // ---- steam
    SteamTask task;
    std::string user, pw, code;
    bool remember = true;
    std::string steamHome;
    std::string steamErr;
    bool codeAutoSent = false;

    ~App() {
        if (loader.joinable()) loader.join();
        task.cancel();
        task.join();
        if (tex) SDL_DestroyTexture(tex);
        wipe(pw);
    }

    std::string steamDir() const { return (fs::path(dataRoot) / "SKGSTEAM").string(); }

    void resolveExe() {
        std::vector<std::string> cands;
        if (const char* e = std::getenv("ZDROID_DEPOTDOWNLOADER"); e && *e) cands.push_back(e);
        if (const char* d = std::getenv("ZDROID_NATIVE_LIB_DIR"); d && *d) cands.push_back(std::string(d) + "/libdepotdownloader.so");
        if (!opt.exe.empty()) cands.push_back(opt.exe);
        cands.push_back("DepotDownloader");
        exePath = cands.front();
        exeFound = false;
        for (const std::string& c : cands) {
            std::string f;
            if (onPath(c, &f)) {
                exePath = f;
                exeFound = true;
                return;
            }
        }
    }

    void scanDownload() { scanDownloadDir(steamDir(), dlGfs, dlFiles, dlBytes); }

    std::vector<std::string> extraDirs() const {
        std::vector<std::string> v;
        for (size_t i = 1; i < opt.dirs.size(); ++i) v.push_back(opt.dirs[i]);
        return v;
    }

    // Main thread only.
    void applyData(LoadResult&& r) {
        gd = std::move(r.gd);
        inv = std::move(r.inv);
        modCount = r.modCount;
        modLog = std::move(r.modLog);
        modsRep = std::move(r.modsRep);
        dlGfs = r.dlGfs;
        dlFiles = r.dlFiles;
        dlBytes = r.dlBytes;
        if (modCount) {
            std::printf("SKGLoader: %zu mod(s) applied\n", modCount);
            for (const std::string& l : modLog) std::printf("  %s\n", l.c_str());
        }
        charIdx = -1;
        art = ArtFile{};
        filtered.clear();
        frameDirty = true;
        loadErr.clear();
        // Hand the freshly indexed roster + stages to the Play screen and the menu.
        play.setGameData(&gd);
        play.setLanguage(opt.lang);
        play.setRoster(inv.characters);
        play.setLevels(inv.levels);
        play.setStageTable(inv.stages);
        menu.setGameData(&gd, &inv);
        story.setGameData(&gd);
        gallery.setGameData(&gd);
        credits.setGameData(&gd);
        musicLib.scan(gd.vfs());
        musicPlayer.setup(&audio, &musicLib, &gd.vfs());
        std::printf("play: %zu character(s), %zu stage(s)\n", inv.characters.size(), inv.levels.size());
    }

    // Blocking (Home screen buttons, tests).
    void rescan() { applyData(std::move(*buildData(dataRoot, extraDirs(), 0, opt.lang == "jp"))); }

    // Non-blocking start-up load: the splash animates while a worker thread indexes the folders.
    void startAsyncLoad() {
        loading = true;
        loadDone = false;
        loader = std::thread([this, root = dataRoot, extra = extraDirs(), delay = opt.loadDelayMs, jp = opt.lang == "jp"] {
            loaded = buildData(root, extra, delay, jp);
            loadDone = true;
        });
    }
    // Call every frame; applies the result on the main thread when the worker is done.
    void pollLoad() {
        if (!loading || !loadDone) return;
        loader.join();
        loading = false;
        applyData(std::move(*loaded));
        loaded.reset();
    }

    bool loading = false;
    std::thread loader;
    std::unique_ptr<LoadResult> loaded;
    std::atomic<bool> loadDone{false};

    size_t modCount = 0;
    std::vector<std::string> modLog;
    modsdir::Report modsRep;
    bool modsView = false;        // Browse -> Mods
    int modsTab = 0;              // 0 packs, 1 mods, 2 stages, 3 palettes, 4 info
    float modsScroll = 0;
    bool modsDrag = false;
    int modsDragStartY = 0;

    void savePrefs() const {
        if (prefsDir.empty()) return;
        std::ofstream f(fs::path(prefsDir) / "dataroot.txt", std::ios::trunc);
        if (f) f << dataRoot << "\n";
    }

    // ---- browser
    void applyFilter() {
        filtered.clear();
        for (size_t i = 0; i < art.records().size(); ++i) {
            const auto c = art.records()[i].category();
            const bool ok = cat == 0 || (cat == 1 && c == ArtRecord::Category::Sprite) ||
                            (cat == 2 && c == ArtRecord::Category::Extra) || (cat == 3 && c == ArtRecord::Category::Silhouette);
            if (ok) filtered.push_back(static_cast<int>(i));
        }
        fpos = std::clamp(fpos, 0, std::max(static_cast<int>(filtered.size()) - 1, 0));
        frameDirty = true;
    }

    void loadPalette() {
        pal = fallbackPalette();
        if (!palettes.empty()) {
            palIdx = ((palIdx % static_cast<int>(palettes.size())) + static_cast<int>(palettes.size())) % static_cast<int>(palettes.size());
            std::string err;
            if (!GameData::loadPalette(palettes[palIdx].second, pal, &err)) {
                pal = fallbackPalette();
                frameErr = "palette: " + err;
            }
        }
        tables = buildRgbaTables(pal);
        frameDirty = true;
    }

    void selectChar(int i, const std::string& palLabel = {}) {
        if (i < 0 || i >= static_cast<int>(inv.characters.size())) return;
        charIdx = i;
        const CharacterAssets& c = inv.characters[i];
        loadErr.clear();
        frameErr.clear();
        art = ArtFile{};
        if (!gd.loadCharacter(c, art, &loadErr)) {
            filtered.clear();
            frameDirty = true;
            return;
        }
        palettes = c.palettes;
        palIdx = 0;
        for (size_t k = 0; k < palettes.size(); ++k)
            if (palettes[k].first == palLabel) palIdx = static_cast<int>(k);
        fpos = 0;
        loadPalette();
        applyFilter();
    }

    bool loadFrame(SDL_Renderer* r) {
        if (tex) {
            SDL_DestroyTexture(tex);
            tex = nullptr;
        }
        frameErr.clear();
        frameDirty = false;
        if (filtered.empty()) return false;
        ArtPlanes pl;
        if (!art.decode(static_cast<size_t>(filtered[fpos]), pl)) {
            frameErr = "frame failed to decode";
            return false;
        }
        const std::vector<uint8_t> rgba = toRgba(pl, tables);
        tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, pl.width, pl.height);
        if (!tex) {
            frameErr = std::string("texture: ") + SDL_GetError();
            return false;
        }
        SDL_UpdateTexture(tex, nullptr, rgba.data(), pl.width * 4);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        texW = pl.width;
        texH = pl.height;
        return true;
    }

    // ---- steam
    bool steamActive(const SteamTaskState& s) const {
        return s.phase == SteamPhase::Connecting || s.phase == SteamPhase::NeedGuardCode || s.phase == SteamPhase::Downloading;
    }

    void startSteam() {
        steamErr.clear();
        SteamTaskConfig cfg;
        cfg.exe = exePath;
        cfg.installDir = steamDir();
        cfg.username = user;
        cfg.rememberPassword = remember;
        cfg.homeDir = steamHome;
        std::string copy = pw;  // start() wipes its own copy once the password has been sent
        if (task.start(cfg, std::move(copy), &steamErr)) {
            wipe(pw);
            codeAutoSent = false;
            code.clear();
        }
    }
};

// ---------------------------------------------------------------------------------------- screens
void drawTopBar(Ui& ui, App& app) {
    ui.fill(Rect{0, 0, kW, 28}, kPanel);
    ui.fill(Rect{0, 27, kW, 1}, kLine);
#ifdef __ANDROID__
    ui.text(8, 10, "ZGIRLSMOBILE", kAccent);
#else
    ui.text(8, 10, "ZGIRLS LOADER", kAccent);
#endif
    // No "Play" tab any more: matches start from the menu (Versus / Training -> Character Select), not from a debug chooser.
    const struct { const char* name; Screen screen; } tabs[4] = {
        {"Home", Screen::Home}, {"Browse", Screen::Browse}, {"Steam", Screen::Steam}, {"Menu", Screen::Menu}};
    for (int i = 0; i < 4; ++i)
        if (ui.button(10 + i, Rect{kW - 4 * 76 + i * 74, 3, 72, 22}, tabs[i].name, true, app.screen == tabs[i].screen))
            app.screen = tabs[i].screen;
}

void drawHome(Ui& ui, App& app) {
    ui.text(20, 38, "Data folder (extracted game files: .foit .art .art-pt .dds .gbs .ini ...)", kDim);
    ui.field(20, Rect{20, 50, 420, 24}, app.dataRootEdit, "/path/to/unpacked");
    if (ui.button(21, Rect{446, 50, 84, 24}, "Apply")) {
        app.dataRoot = app.dataRootEdit;
        app.savePrefs();
        app.rescan();
    }
    if (ui.button(22, Rect{536, 50, 84, 24}, "Rescan")) app.rescan();

    const Inventory& inv = app.inv;
    int y = 92;
    auto line = [&](const std::string& k, const std::string& v, Col c = kText) {
        ui.text(20, y, k, kDim);
        ui.text(300, y, Ui::fit(v, kW - 320), c);
        y += 14;
    };
    line("Files indexed", std::to_string(inv.files));
    line("Characters with sprite data", std::to_string(inv.characters.size()),
         inv.characters.empty() ? kWarn : kGood);
    line("Characters listed in compile.ini", std::to_string(inv.compileJobs.size()));
    line("Stages / music tracks / timelines",
         std::to_string(inv.stages.size()) + " / " + std::to_string(inv.music.size()) + " / " + std::to_string(inv.tocFiles.size()));
    line("UI scenes (.gbs)", std::to_string(inv.scenes.size()));
    line("Palettes (.dds) / Lua scripts", std::to_string(inv.ddsFiles) + " / " + std::to_string(inv.luaFiles));
    y += 6;
    {
        std::string v = std::to_string(app.dlFiles) + " files, " + humanBytes(app.dlBytes) + ", " + std::to_string(app.dlGfs) + " .gfs";
        line("Steam download (SKGSTEAM)", v, app.dlFiles ? kGood : kDim);
        if (app.dlGfs || inv.gfsFiles) {
            ui.text(20, y, Ui::fit(".gfs archives are packed. Unpacking them is NOT implemented yet:", kW - 40), kWarn);
            y += 12;
            ui.text(20, y, Ui::fit("extract them (e.g. with SkullMod) and point the data folder at the result.", kW - 40), kWarn);
            y += 14;
        }
    }
    if (!inv.warnings.empty()) {
        y += 4;
        ui.text(20, y, "Notes", kDim);
        y += 12;
        for (size_t i = 0; i < inv.warnings.size() && i < 4; ++i) {
            ui.text(20, y, Ui::fit("- " + inv.warnings[i], kW - 40), kWarn);
            y += 12;
        }
        if (inv.warnings.size() > 4) ui.text(20, y, "  (+" + std::to_string(inv.warnings.size() - 4) + " more)", kDim);
    }
    if (ui.button(23, Rect{20, 318, 150, 30}, "Browse sprites", !inv.characters.empty())) {
        app.screen = Screen::Browse;
        if (app.charIdx < 0) app.selectChar(0);
    }
    if (ui.button(24, Rect{180, 318, 150, 30}, "Steam download")) app.screen = Screen::Steam;
}

// Browse -> Mods: everything the <data>/mods folder holds (resource packs, mods, stages, custom palettes by character) with an
// on/off switch per folder.  A switch writes mods.ini and rescans, so the change shows up at once.
void drawMods(Ui& ui, App& app, SDL_Renderer* r) {
    const modsdir::Report& rep = app.modsRep;
    static const char* const kTabs[5] = {"Packs", "Mods", "Stages", "Palettes", "Info"};
    static const char* const kKinds[3] = {"pack", "mod", "stage"};
    size_t counts[5] = {0, 0, 0, rep.pal.entries.size(), 0};
    for (const modsdir::Item& it : rep.items)
        for (int k = 0; k < 3; ++k)
            if (it.kind == kKinds[k]) ++counts[k];
    for (int i = 0; i < 5; ++i) {
        const std::string lab = i < 4 ? std::string(kTabs[i]) + " " + std::to_string(counts[i]) : std::string(kTabs[i]);
        if (ui.button(80 + i, Rect{4 + i * 108, 32, 104, 22}, lab, true, app.modsTab == i)) {
            app.modsTab = i;
            app.modsScroll = 0;
        }
    }

    struct Row {
        int h = 12;
        int kind = 0;   // 0 text, 1 character header, 2 folder (switch), 3 palette
        std::string a, b;
        int item = -1;
        Col col = kText;
    };
    std::vector<Row> rows;
    const int tab = app.modsTab;
    if (tab < 3) {
        for (size_t i = 0; i < rep.items.size(); ++i) {
            if (rep.items[i].kind != kKinds[tab]) continue;
            Row w;
            w.h = 38;
            w.kind = 2;
            w.item = static_cast<int>(i);
            rows.push_back(w);
        }
        if (rows.empty()) {
            static const char* const kWhere[3] = {"resourcepacks", "modscustom", "stages"};
            Row a;
            a.a = std::string("Nothing here yet. Drop one folder per ") + (tab == 0 ? "pack" : tab == 1 ? "mod" : "stage") + " into:";
            a.col = kDim;
            rows.push_back(a);
            Row b;
            b.a = "  " + rep.root + "/" + kWhere[tab];
            rows.push_back(b);
        }
    } else if (tab == 3) {
        std::string lastChar;
        for (size_t i = 0; i < rep.pal.entries.size(); ++i) {
            const palmod::Entry& e = rep.pal.entries[i];
            if (e.character != lastChar) {
                lastChar = e.character;
                size_t n = 0;
                for (const palmod::Entry& o : rep.pal.entries) n += o.character == lastChar;
                Row h;
                h.kind = 1;
                h.h = 18;
                std::string nm = lastChar;
                if (!nm.empty()) nm[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(nm[0])));
                h.a = nm + "  (" + std::to_string(n) + ")";
                rows.push_back(h);
            }
            Row p;
            p.kind = 3;
            p.h = 16;
            p.a = e.label;
            p.b = e.file.substr(e.file.find_last_of("/\\") == std::string::npos ? 0 : e.file.find_last_of("/\\") + 1);
            rows.push_back(p);
        }
        if (rows.empty()) {
            Row a;
            a.a = "No .pal files yet. Put them in:";
            a.col = kDim;
            rows.push_back(a);
            Row b;
            b.a = "  " + rep.root + "/pal/<Character>/";
            rows.push_back(b);
        }
    } else {
        auto line = [&](const std::string& t, Col c = kText) {
            Row w;
            w.a = t;
            w.col = c;
            rows.push_back(w);
        };
        line("Folder: " + (rep.root.empty() ? std::string("(none)") : rep.root));
        line(rep.allEnabled ? "Mods: on" : "Mods: OFF (mods.ini enabled = 0)", rep.allEnabled ? kGood : kWarn);
        line("Mounted: " + std::to_string(rep.packs) + " pack(s), " + std::to_string(rep.mods) + " mod(s), " + std::to_string(rep.stages) + " stage(s)");
        line("Custom palettes: " + std::to_string(rep.pal.entries.size()));
        line("Japanese voice files: " + std::to_string(rep.voiceFiles) + "  (only while the voice language is Japanese)");
        if (!rep.warnings.empty()) {
            line("");
            line("Warnings:", kWarn);
            for (const std::string& w : rep.warnings) line("  " + w, kWarn);
        }
        if (!rep.log.empty()) {
            line("");
            line("Log:", kDim);
            for (const std::string& l : rep.log) line("  " + l, kDim);
        }
    }

    const Rect list{4, 58, 632, 270};
    ui.fill(list, kPanel);
    ui.outline(list, kLine);
    int total = 4;
    for (const Row& w : rows) total += w.h + (w.kind == 2 ? 2 : 0);
    const float maxScroll = std::max(0.f, float(total - list.h));
    if (ui.in.pressed && list.contains(ui.in.pmx, ui.in.pmy)) {
        app.modsDrag = false;
        app.modsDragStartY = ui.in.pmy;
    }
    if (ui.in.down && list.contains(ui.in.pmx, ui.in.pmy)) {
        if (std::abs(ui.in.my - app.modsDragStartY) > 6) app.modsDrag = true;
        if (app.modsDrag) app.modsScroll -= static_cast<float>(ui.in.dragDy);
    }
    if (list.contains(ui.in.mx, ui.in.my)) app.modsScroll -= static_cast<float>(ui.in.wheel * 24);
    app.modsScroll = std::clamp(app.modsScroll, 0.f, maxScroll);

    int toggle = -1;
    SDL_Rect clip{list.x + 1, list.y + 1, list.w - 2, list.h - 2};
    SDL_RenderSetClipRect(r, &clip);
    int y = list.y + 2 - static_cast<int>(app.modsScroll);
    for (const Row& w : rows) {
        const int rh = w.h + (w.kind == 2 ? 2 : 0);
        if (y + rh >= list.y && y <= list.y + list.h) {
            const int x = list.x + 6;
            if (w.kind == 1) {
                ui.fill(Rect{list.x + 2, y, list.w - 4, w.h - 2}, kPanel2);
                ui.text(x + 4, y + 4, w.a, kAccent);
            } else if (w.kind == 3) {
                ui.text(x + 14, y + 4, Ui::fit(w.a, 330), kText);
                ui.text(x + 360, y + 4, Ui::fit(w.b, 250), kDim);
            } else if (w.kind == 2) {
                const modsdir::Item& it = rep.items[static_cast<size_t>(w.item)];
                ui.fill(Rect{list.x + 2, y, list.w - 4, w.h}, kPanel2);
                std::string head = it.name;
                if (!it.version.empty() && it.version != "0.1") head += "  v" + it.version;
                if (!it.author.empty()) head += "  by " + it.author;
                ui.text(x + 4, y + 5, Ui::fit(head, 470), it.enabled ? kText : kDim);
                std::string sub = it.id + "  -  " + std::to_string(it.files) + " file(s)  -  prio " + std::to_string(it.priority);
                if (!it.description.empty()) sub += "  -  " + it.description;
                Col sc = kDim;
                if (it.enabled && !it.mounted) {
                    sub = (rep.allEnabled ? "no files to mount  -  " : "mods are off in mods.ini  -  ") + sub;
                    sc = kWarn;
                }
                ui.text(x + 4, y + 21, Ui::fit(sub, 470), sc);
                const Rect pill{list.x + list.w - 84, y + 8, 72, 22};
                ui.fill(pill, it.enabled ? Col{40, 110, 66, 255} : Col{70, 40, 48, 255});
                ui.outline(pill, it.enabled ? kGood : kBad);
                ui.textIn(pill, it.enabled ? "ON" : "OFF", kText, true);
                if (ui.in.released && !app.modsDrag && pill.contains(ui.in.mx, ui.in.my) && pill.contains(ui.in.pmx, ui.in.pmy) &&
                    list.contains(ui.in.mx, ui.in.my))
                    toggle = w.item;
            } else if (!w.a.empty()) {
                ui.text(x, y, Ui::fit(w.a, list.w - 16), w.col);
            }
        }
        y += rh;
    }
    SDL_RenderSetClipRect(r, nullptr);
    if (ui.in.released) app.modsDrag = false;

    bool reload = false;
    if (ui.button(85, Rect{4, 334, 110, 22}, "< Browse")) app.modsView = false;
    if (ui.button(86, Rect{120, 334, 100, 22}, "Reload")) reload = true;
    ui.text(228, 340, Ui::fit("data/mods: " + (rep.root.empty() ? app.dataRoot + "/mods" : rep.root), 404), kDim);

    // after every read of `rep`: both actions replace the report
    if (toggle >= 0) {
        const modsdir::Item it = rep.items[static_cast<size_t>(toggle)];
        if (!modsdir::setItemEnabled(app.dataRoot, it, !it.enabled)) std::printf("mods: could not write mods.ini in %s\n", app.dataRoot.c_str());
        reload = true;
    }
    if (reload) app.rescan();
}

void drawBrowse(Ui& ui, App& app, SDL_Renderer* r) {
    if (app.modsView) {
        drawMods(ui, app, r);
        return;
    }
    const Inventory& inv = app.inv;
    const Rect list{4, 32, 160, 324};
    ui.fill(list, kPanel);
    ui.outline(list, kLine);
    const std::string modsLab = "Mods " + std::to_string(app.modsRep.items.size() + app.modsRep.pal.entries.size());
    if (ui.button(87, Rect{538, 334, 98, 22}, modsLab)) {
        app.modsView = true;
        app.modsScroll = 0;
    }

    if (inv.characters.empty()) {
        ui.text(176, 44, "No characters found.", kWarn);
        ui.text(176, 60, Ui::fit("Mount a folder that holds <name>.foit/.art/.art-pt files:", 450), kDim);
        ui.text(176, 72, "Home -> Data folder.", kDim);
        return;
    }

    // character list: drag to scroll, tap to select
    const int rowH = 24, n = static_cast<int>(inv.characters.size());
    const float maxScroll = std::max(0.f, float(n * rowH - (list.h - 4)));
    if (ui.in.pressed && list.contains(ui.in.pmx, ui.in.pmy)) {
        app.listDrag = false;
        app.listDragStartY = ui.in.pmy;
    }
    if (ui.in.down && list.contains(ui.in.pmx, ui.in.pmy)) {
        if (std::abs(ui.in.my - app.listDragStartY) > 6) app.listDrag = true;
        if (app.listDrag) app.listScroll -= static_cast<float>(ui.in.dragDy);
    }
    if (list.contains(ui.in.mx, ui.in.my)) app.listScroll -= static_cast<float>(ui.in.wheel * rowH);
    app.listScroll = std::clamp(app.listScroll, 0.f, maxScroll);
    SDL_Rect clip{list.x + 1, list.y + 1, list.w - 2, list.h - 2};
    SDL_RenderSetClipRect(r, &clip);
    for (int i = 0; i < n; ++i) {
        const int y = list.y + 2 + i * rowH - static_cast<int>(app.listScroll);
        if (y + rowH < list.y || y > list.y + list.h) continue;
        const Rect row{list.x + 2, y, list.w - 4, rowH - 2};
        const bool sel = i == app.charIdx;
        ui.fill(row, sel ? Col{150, 48, 86, 255} : kPanel2);
        const CharacterAssets& c = inv.characters[i];
        ui.text(row.x + 6, row.y + 4, Ui::fit(c.name, 96), kText);
        const std::string tag = std::to_string(c.palettes.size()) + "p";
        ui.text(row.x + row.w - Ui::textW(tag) - 6, row.y + 8, tag, kDim);
        const char* parts = c.artPath.empty() ? "pt only" : (c.artPtPath.empty() ? "art only" : "");
        if (*parts) ui.text(row.x + 6, row.y + 13, parts, kWarn);
        if (ui.in.released && !app.listDrag && row.contains(ui.in.mx, ui.in.my) && list.contains(ui.in.pmx, ui.in.pmy) &&
            row.contains(ui.in.pmx, ui.in.pmy))
            app.selectChar(i);
    }
    SDL_RenderSetClipRect(r, nullptr);
    if (ui.in.released) app.listDrag = false;

    // preview panel
    const Rect pv{170, 32, 466, 270};
    if (app.bg == 0) {
        ui.fill(pv, Col{150, 150, 150, 255});
        SDL_Rect pclip{pv.x, pv.y, pv.w, pv.h};
        SDL_RenderSetClipRect(r, &pclip);
        ui.color(Col{110, 110, 110, 255});
        for (int ty = 0; ty < pv.h; ty += 8)
            for (int tx = ((ty / 8) % 2) * 8; tx < pv.w; tx += 16) {
                SDL_Rect q{pv.x + tx, pv.y + ty, 8, 8};
                SDL_RenderFillRect(r, &q);
            }
        SDL_RenderSetClipRect(r, nullptr);
    } else {
        ui.fill(pv, app.bg == 1 ? Col{8, 8, 12, 255} : Col{236, 236, 236, 255});
    }
    ui.outline(pv, kLine);

    if (app.charIdx < 0) {
        ui.text(pv.x + 12, pv.y + 12, "Pick a character on the left.", Col{20, 20, 20, 255});
    } else if (!app.loadErr.empty()) {
        ui.text(pv.x + 12, pv.y + 12, Ui::fit("Cannot load: " + app.loadErr, pv.w - 24), kBad);
    } else {
        if (app.frameDirty) app.loadFrame(r);
        if (app.tex) {
            double s = 1;
            if (app.zoom == 0) {
                s = std::min(double(pv.w - 8) / app.texW, double(pv.h - 8) / app.texH);
                if (s >= 1) s = std::floor(s);
            } else {
                s = app.zoom;
            }
            const int dw = static_cast<int>(app.texW * s), dh = static_cast<int>(app.texH * s);
            SDL_Rect dst{pv.x + (pv.w - dw) / 2, pv.y + (pv.h - dh) / 2, dw, dh};
            SDL_Rect pclip{pv.x + 1, pv.y + 1, pv.w - 2, pv.h - 2};
            SDL_RenderSetClipRect(r, &pclip);
            SDL_RenderCopy(r, app.tex, nullptr, &dst);
            SDL_RenderSetClipRect(r, nullptr);
        } else if (!app.frameErr.empty()) {
            ui.text(pv.x + 12, pv.y + 12, Ui::fit(app.frameErr, pv.w - 24), kBad);
        } else {
            ui.text(pv.x + 12, pv.y + 12, "No frames in this category.", Col{20, 20, 20, 255});
        }
        if (!app.filtered.empty()) {
            const ArtRecord& rec = app.art.records()[static_cast<size_t>(app.filtered[app.fpos])];
            char info[160];
            std::snprintf(info, sizeof info, "#%d %s tag %02X  %dx%d px  %u cells%s", app.filtered[app.fpos],
                          rec.src == ArtRecord::Src::Art ? ".art" : ".art-pt", rec.tag, rec.pixelWidth(), rec.pixelHeight(),
                          rec.n, rec.flagged() ? "  [bit6: scanline rows]" : "");
            const std::string t = Ui::fit(info, pv.w - 12);
            ui.fill(Rect{pv.x + 3, pv.y + 3, Ui::textW(t) + 6, 12}, Col{0, 0, 0, 160});
            ui.text(pv.x + 6, pv.y + 5, t, kText);
        }
    }

    // controls
    const int cy = 308;
    const int total = static_cast<int>(app.filtered.size());
    const bool have = total > 0;
    if (ui.button(30, Rect{170, cy, 32, 22}, "<", have) || (ui.in.left && have)) {
        app.fpos = (app.fpos + total - 1) % total;
        app.frameDirty = true;
    }
    {
        char lab[48];
        std::snprintf(lab, sizeof lab, "%d / %d", have ? app.fpos + 1 : 0, total);
        ui.fill(Rect{206, cy, 104, 22}, kPanel);
        ui.textIn(Rect{206, cy, 104, 22}, lab, kText, true);
    }
    if (ui.button(31, Rect{314, cy, 32, 22}, ">", have) || (ui.in.right && have)) {
        app.fpos = (app.fpos + 1) % total;
        app.frameDirty = true;
    }
    const char* catNames[4] = {"All", "Sprites", "Extras", "Silh."};
    for (int i = 0; i < 4; ++i)
        if (ui.button(32 + i, Rect{354 + i * 70, cy, 66, 22}, catNames[i], true, app.cat == i)) {
            app.cat = i;
            app.fpos = 0;
            app.applyFilter();
        }
    const int cy2 = 334;
    {
        const bool canPal = !app.palettes.empty();
        std::string lab = canPal ? "Palette " + app.palettes[static_cast<size_t>(app.palIdx)].first : "Palette: default";
        if (ui.button(40, Rect{170, cy2, 150, 22}, lab, canPal && app.palettes.size() > 1)) {
            ++app.palIdx;
            app.loadPalette();
        }
    }
    if (ui.button(41, Rect{326, cy2, 100, 22}, app.bg == 0 ? "BG: checker" : app.bg == 1 ? "BG: dark" : "BG: light")) app.bg = (app.bg + 1) % 3;
    if (ui.button(42, Rect{432, cy2, 100, 22}, app.zoom == 0 ? "Zoom: fit" : app.zoom == 1 ? "Zoom: 1x" : "Zoom: 2x")) app.zoom = (app.zoom + 1) % 3;
}

void drawSteam(Ui& ui, App& app) {
    const SteamTaskState st = app.task.snapshot();
    const bool active = app.steamActive(st);

    ui.text(20, 38, "Download from Steam", kText, 2);
    ui.text(20, 58, Ui::fit("Uses YOUR Steam account via DepotDownloader; Steam decides if you own it.", kW - 40), kDim);
    ui.text(20, 70, Ui::fit("Password goes only to DepotDownloader's stdin: never in arguments or logs.", kW - 40), kDim);
    ui.text(20, 86, "DepotDownloader:", kDim);
    ui.text(20 + 17 * 8, 86, Ui::fit(app.exePath, kW - 180), app.exeFound ? kGood : kBad);
    if (!app.exeFound) {
#ifdef __ANDROID__
        ui.text(20, 98, Ui::fit("Steam download works on PC only for now. Copy your extracted game folder to the app's folder instead.", kW - 40), kBad);
#else
        ui.text(20, 98, Ui::fit("Not found: set ZDROID_DEPOTDOWNLOADER or bundle libdepotdownloader.so.", kW - 40), kBad);
#endif
    }
    else
        ui.text(20, 98, Ui::fit("Download to: " + app.steamDir(), kW - 40), kDim);

    // output box (always visible)
    const Rect logBox{300, 116, 320, 150};
    ui.fill(logBox, Col{10, 10, 14, 255});
    ui.outline(logBox, kLine);
    {
        const int maxLines = (logBox.h - 8) / 11;
        const int first = std::max(0, static_cast<int>(st.log.size()) - maxLines);
        int y = logBox.y + 5;
        for (int i = first; i < static_cast<int>(st.log.size()); ++i, y += 11)
            ui.text(logBox.x + 5, y, Ui::fit(st.log[static_cast<size_t>(i)], logBox.w - 10), kDim);
        if (st.log.empty()) ui.text(logBox.x + 5, y, "(DepotDownloader output appears here)", Col{80, 80, 96, 255});
    }

    if (!active) {
        ui.text(20, 116, "Steam username", kDim);
        ui.field(50, Rect{20, 127, 260, 24}, app.user, "username");
        ui.text(20, 158, "Password (or remembered login)", kDim);
        ui.field(51, Rect{20, 169, 260, 24}, app.pw, "password", true);
        ui.check(52, Rect{20, 200, 260, 20}, "Remember login on this device", app.remember);
        if (ui.button(53, Rect{20, 228, 130, 30}, "Start download", app.exeFound && !app.user.empty())) app.startSteam();
        ui.text(160, 238, Ui::fit("~ several GB", 130), kDim);
        // result of the previous run
        if (st.phase == SteamPhase::Done) ui.text(20, 270, Ui::fit(st.message + ". " + humanBytes(st.bytes), kW - 40), kGood);
        else if (st.phase == SteamPhase::Failed) ui.text(20, 270, Ui::fit(st.message, kW - 40), kBad);
        else if (st.phase == SteamPhase::Cancelled) ui.text(20, 270, "Cancelled.", kWarn);
        if (!app.steamErr.empty()) ui.text(20, 284, Ui::fit(app.steamErr, kW - 40), kBad);
        if (st.phase == SteamPhase::Done) {
            ui.text(20, 296, Ui::fit("Next: .gfs archives must be unpacked (not implemented yet, see README).", kW - 40), kWarn);
            if (ui.button(54, Rect{20, 318, 150, 30}, "Back to Home")) {
                app.rescan();
                app.screen = Screen::Home;
            }
        }
        return;
    }

    // --- running
    const uint32_t dots = (ui.now / 400) % 4;
    const std::string spin(dots, '.');
    switch (st.phase) {
        case SteamPhase::Connecting:
            ui.text(20, 116, "Signing in to Steam" + spin, kText);
            ui.text(20, 130, Ui::fit("Waiting for DepotDownloader...", 270), kDim);
            break;
        case SteamPhase::NeedGuardCode: {
            ui.text(20, 116, "Steam Guard", kWarn, 2);
            const bool mail = st.pendingPrompt == PromptKind::GuardEmailCode;
            ui.text(20, 138, Ui::fit(mail ? "Enter the code Steam emailed you:" : "Enter the code from your Steam", 270), kText);
            if (!mail) ui.text(20, 150, "Guard authenticator app:", kText);
            if (st.wrongCode) ui.text(20, 162, "That code was rejected.", kBad);
            const bool entered = ui.field(60, Rect{20, 176, 150, 26}, app.code, "code");
            if (ui.focus == 0) ui.focus = 60;  // keep the keyboard open while waiting for the code
            if ((ui.button(61, Rect{178, 176, 100, 26}, "Submit", !app.code.empty()) || entered) && !app.code.empty()) {
                app.task.submitCode(app.code);
                app.code.clear();
            }
            if (st.mobileConfirm) ui.text(20, 210, Ui::fit("Or approve the sign-in in your Steam app.", 270), kGood);
            if (!app.opt.steamCode.empty() && !app.codeAutoSent) {
                app.codeAutoSent = true;
                app.task.submitCode(app.opt.steamCode);
            }
            break;
        }
        default:
            ui.text(20, 116, st.licenses ? "Signed in (" + std::to_string(st.licenses) + " licenses)" : "Downloading", kGood);
            if (st.depot) ui.text(20, 130, "Depot " + std::to_string(st.depot), kDim);
            break;
    }
    if (ui.button(62, Rect{20, 228, 130, 30}, "Cancel")) app.task.cancel();

    char pct[32];
    std::snprintf(pct, sizeof pct, "%5.1f%%", st.percent);
    ui.bar(Rect{20, 284, 560, 14}, st.percent, kAccent);
    ui.text(588, 287, pct, kText);
    ui.text(20, 304, Ui::fit(st.currentFile, kW - 40), kDim);
}

// ---------------------------------------------------------------------------------------- startup
Options parseArgs(int argc, char** argv) {
    Options o;
    auto val = [&](int& i) -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--smoke") o.smokePng = val(i);
        else if (a == "--smoke-frames") o.smokeFrames = std::atoi(val(i).c_str());
        else if (a == "--dt") o.fixedDtMs = std::clamp(std::atoi(val(i).c_str()), 0, 100);  // fixed frame time (captures: 1 frame = N ms)
        else if (a == "--smoke-scale") o.smokeScale = std::clamp(std::atoi(val(i).c_str()), 1, 4);  // capture at N x 640x360
        else if (a == "--smoke-phase") o.smokePhase = val(i);
        else if (a == "--screen") o.screen = val(i);
        else if (a == "--char") o.character = val(i);
        else if (a == "--frame") o.frame = std::atoi(val(i).c_str());
        else if (a == "--cat") o.cat = val(i);
        else if (a == "--pal") o.pal = val(i);
        else if (a == "--zoom") o.zoom = val(i);
        else if (a == "--bg") o.bg = std::atoi(val(i).c_str());
        else if (a == "--exe") o.exe = val(i);
        else if (a == "--steam-user") o.steamUser = val(i);
        else if (a == "--steam-pass-env") o.steamPassEnv = val(i);
        else if (a == "--steam-code") o.steamCode = val(i);
        else if (a == "--steam-autostart") o.steamAutostart = true;
        else if (a == "--lang") o.lang = val(i);
        else if (a == "--skip-title") o.skipTitle = true;
        else if (a == "--accept-notice") o.acceptNotice = true;
        else if (a == "--cs-step") o.csStep = std::clamp(std::atoi(val(i).c_str()), 0, 2);
        else if (a == "--cs-story") o.csStory = true;
        else if (a == "--cs-p2") o.csP2 = val(i);   // charselect, player 2's pass (player 1 already picked this id)
        else if (a == "--mods-view") o.modsView = true;
        else if (a == "--story-audit") o.storyAudit = true;
        else if (a == "--gallery") o.gallery = true;   // open the Art Gallery (smoke)
        else if (a == "--credits") o.credits = true;   // open the credits roll (smoke; ZDROID_CREDITS_PAGE=n starts on page n, ZDROID_CREDITS_THANKS=1 jumps to the card)
        else if (a == "--contributors") o.credits = o.contributors = true;
        else if (a == "--story") o.story = val(i);   // "filia" / "random" / "list": open Story Mode (smoke)
        else if (a == "--fight") o.fight = val(i);   // "annie:beowulf:new_meridian": start a match like the Character Select would (smoke)
        else if (a == "--cs-pal") o.csPal = std::max(0, std::atoi(val(i).c_str()));
        else if (a == "--attract-shot") o.attractShot = std::max(1, std::atoi(val(i).c_str())) - 1;
        else if (a == "--attract-tick") o.attractTick = std::max(0, std::atoi(val(i).c_str()));
        else if (a == "--smoke-loading") o.smokeLoading = true;      // capture while the splash is still loading
        else if (a == "--load-delay-ms") o.loadDelayMs = std::atoi(val(i).c_str());  // artificial delay (tests)
        else if (a == "--click-after") o.clickAfter = std::max(1, std::atoi(val(i).c_str()));
        else if (a == "--click") {
            int x = 0, y = 0;
            if (std::sscanf(val(i).c_str(), "%d,%d", &x, &y) == 2) o.clicks.emplace_back(x, y);
        } else if (a.rfind("--", 0) == 0) {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
        } else {
            o.dirs.push_back(a);
        }
    }
    return o;
}

bool phaseMatches(const std::string& want, SteamPhase p) {
    if (want.empty()) return true;
    static const struct { const char* n; SteamPhase p; } map[] = {
        {"idle", SteamPhase::Idle}, {"connecting", SteamPhase::Connecting}, {"guard", SteamPhase::NeedGuardCode},
        {"downloading", SteamPhase::Downloading}, {"done", SteamPhase::Done}, {"failed", SteamPhase::Failed},
        {"cancelled", SteamPhase::Cancelled}};
    for (const auto& m : map)
        if (want == m.n) return m.p == p;
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    App app;
    app.opt = parseArgs(argc, argv);
    const Options& opt = app.opt;
    const bool smoke = !opt.smokePng.empty();

    // ---- where things live
    app.prefsDir = apppaths::prefDir();
    if (!opt.dirs.empty()) {
        app.dataRoot = opt.dirs[0];
    } else if (const char* e = std::getenv("ZDROID_DATA"); e && *e) {
        app.dataRoot = e;
    } else {
        if (!app.prefsDir.empty()) {
            std::ifstream f(fs::path(app.prefsDir) / "dataroot.txt");
            std::string l;
            if (f && std::getline(f, l) && !l.empty()) app.dataRoot = l;
        }
#ifdef __ANDROID__
        if (app.dataRoot.empty())
            if (const char* ext = SDL_AndroidGetExternalStoragePath()) app.dataRoot = ext;
#endif
        if (app.dataRoot.empty()) app.dataRoot = ".";
    }
    app.dataRootEdit = app.dataRoot;
    app.steamHome = !app.prefsDir.empty() ? (fs::path(app.prefsDir) / "steam_home").string() : (fs::path(app.dataRoot) / ".sgl_home").string();
#ifdef __ANDROID__
    if (!std::getenv("ZDROID_NATIVE_LIB_DIR")) {
        const std::string d = androidNativeLibDir();
        if (!d.empty()) setenv("ZDROID_NATIVE_LIB_DIR", d.c_str(), 1);
    }
#endif
    app.resolveExe();
    app.user = opt.steamUser;
    if (!opt.steamPassEnv.empty())
        if (const char* p = std::getenv(opt.steamPassEnv.c_str())) app.pw = p;

    // ---- SDL
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    // The game is landscape only. On Android SDL otherwise picks the orientation from the window shape and can
    // override the manifest's sensorLandscape (it rotates to portrait on a resizable window).
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    Uint32 wflags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#ifdef __ANDROID__
    wflags |= SDL_WINDOW_FULLSCREEN;
#endif
    if (smoke) wflags = SDL_WINDOW_HIDDEN;
#ifdef __ANDROID__
    const char* kAppTitle = "ZGirlsMobile";
#else
    const char* kAppTitle = "ZGirls Loader";
#endif
    SDL_Window* win = SDL_CreateWindow(kAppTitle, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       smoke ? kW * opt.smokeScale : 1280, smoke ? kH * opt.smokeScale : 720, wflags);
    if (!win) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Renderer* ren = nullptr;
    if (!smoke) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) {
        std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return 1;
    }
    SDL_RenderSetLogicalSize(ren, kW, kH);
    SDL_RendererInfo rinfo{};
    SDL_GetRendererInfo(ren, &rinfo);
    const bool vsync = (rinfo.flags & SDL_RENDERER_PRESENTVSYNC) != 0;

    Ui ui;
    if (!ui.init(ren)) {
        std::fprintf(stderr, "font atlas: %s\n", SDL_GetError());
        return 1;
    }

    // ---- data + requested start state
#ifdef __ANDROID__
    app.menu.setPlatform("ANDROID");
#else
    app.menu.setPlatform("STEAM");
#endif
    app.menu.setPrefsDir(app.prefsDir);
    if (!opt.lang.empty()) app.menu.setLanguageOverride(opt.lang);
    app.audio.open();   // false = silent run; nothing else depends on it
    app.menu.setAudio(&app.audio);
    app.play.setAudio(&app.audio);
    app.story.setPrefsDir(app.prefsDir);
    app.gallery.setPrefsDir(app.prefsDir);
    app.story.setAudio(&app.audio);
    app.play.setMusic(&app.musicPlayer);
    app.story.setMusic(&app.musicPlayer);
    app.gallery.setMusic(&app.musicPlayer);
    app.credits.setMusic(&app.musicPlayer);
    // Normal start: show the splash at once and index the data on a worker thread.  The scripted
    // screenshot runs (and any --screen shortcut that needs the data up front) load synchronously.
    const bool asyncLoad = smoke ? opt.smokeLoading : opt.screen.empty();
    if (asyncLoad) {
        app.menu.setLoading(true);
        app.startAsyncLoad();
    } else {
        app.rescan();
    }
    app.screen = Screen::Menu;  // the menu is the front door; --screen picks another start
    if (opt.skipTitle) app.menu.skipTitle();
    else if (opt.acceptNotice) app.menu.acceptNotice();
    if (const char* pg = std::getenv("ZDROID_MENU_PAGE"); pg && *pg) app.menu.openPageForCapture(pg);   // smoke: a menu page by file name ("zd_dev.ini")
    if (opt.screen == "charselect") {
        app.menu.skipTitle();
        if (!opt.csP2.empty()) app.menu.requestCharSelectP2(opt.csP2);
        else app.menu.requestCharSelect(opt.csStory, opt.csStep, opt.csPal);
    }
    if (opt.screen == "attract") app.menu.showAttract(opt.attractShot, opt.attractTick);   // the opening montage (smoke / screenshots)
    if (opt.screen == "splash") app.menu.showSplash();   // the game's own loading screen (smoke / screenshots)
    if (opt.screen == "home") app.screen = Screen::Home;
    else if (opt.screen == "browse") app.screen = Screen::Browse;
    else if (opt.screen == "steam") app.screen = Screen::Steam;
    else if (opt.screen == "play") {
        if (kFightEnabled) app.screen = Screen::Play;
        else std::fprintf(stderr, "--screen play: fight mode is switched off (zd_features.h / -DZDROID_FIGHT_MODE=ON)\n");
    }
    if (opt.modsView) {
        app.screen = Screen::Browse;
        app.modsView = true;
    }
    if (!opt.fight.empty() && kFightEnabled) {
        std::vector<std::string> w;
        size_t from = 0;
        for (;;) {
            const size_t c = opt.fight.find(':', from);
            w.push_back(opt.fight.substr(from, c == std::string::npos ? std::string::npos : c - from));
            if (c == std::string::npos) break;
            from = c + 1;
        }
        w.resize(3);
        app.play.enterMatch(ren, std::getenv("ZDROID_PLAY_ARCADE") ? play::Mode::Arcade : (std::getenv("ZDROID_PLAY_TRAINING") ? play::Mode::Training : play::Mode::VsCpu), app.menu.rules(), w[0], w[1], w[2], "");
        app.screen = Screen::Play;
    }
    if (opt.gallery) {
        app.gallery.begin(opt.lang.empty() ? std::string("en") : opt.lang);
        app.screen = Screen::Gallery;
    }
    if (opt.credits) {
        app.credits.begin(opt.lang.empty() ? std::string("en") : opt.lang, opt.contributors, std::getenv("ZDROID_CREDITS_THANKS") != nullptr);
        app.screen = Screen::Credits;
    }
    if (!opt.story.empty()) {
        app.story.begin(opt.story == "list" ? std::string() : opt.story, opt.lang.empty() ? std::string("en") : opt.lang);
        app.screen = Screen::Story;
    }
    if (opt.bg >= 0) app.bg = opt.bg % 3;
    if (opt.zoom == "1") app.zoom = 1;
    else if (opt.zoom == "2") app.zoom = 2;
    if (opt.cat == "sprites") app.cat = 1;
    else if (opt.cat == "extras") app.cat = 2;
    else if (opt.cat == "sil") app.cat = 3;
    if (!opt.character.empty() || app.screen == Screen::Browse) {
        int idx = 0;
        for (size_t i = 0; i < app.inv.characters.size(); ++i)
            if (app.inv.characters[i].name == opt.character) idx = static_cast<int>(i);
        app.selectChar(idx, opt.pal);
        app.fpos = opt.frame;
        app.applyFilter();
    }

    // ---- main loop
    bool running = true, wantText = false;
    int auditProblems = 0;
    int frameNo = 0, prevMy = 0;
    Uint32 prevTick = 0;
    // Live touch points (a fighting game needs two fingers at once: move + attack), keyed by
    // (touchId, fingerId).  SDL synthesises a single mouse pointer from touch, which is not enough.
    std::vector<std::tuple<uint64_t, int, int>> fingers;
    size_t clickDone = 0;
    int lastClickFrame = -1;
    const auto t0 = std::chrono::steady_clock::now();
    int exitCode = 0;
    while (running) {
        Input& in = ui.in;
        in.pressed = in.released = false;
        in.text.clear();
        in.backspace = in.enter = in.tab = in.left = in.right = in.back = in.keyUp = in.keyDown = false;
        in.wheel = 0;
        in.dragDy = 0;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT: running = false; break;
                case SDL_MOUSEMOTION:
                    in.mx = e.motion.x;
                    in.my = e.motion.y;
                    in.touchPointer = e.motion.which == SDL_TOUCH_MOUSEID;
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    in.touchPointer = e.button.which == SDL_TOUCH_MOUSEID;
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        in.mx = in.pmx = e.button.x;
                        in.my = in.pmy = e.button.y;
                        in.pressed = in.down = true;
                        prevMy = in.my;
                    }
                    break;
                case SDL_MOUSEBUTTONUP:
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        in.mx = e.button.x;
                        in.my = e.button.y;
                        in.released = true;
                        in.down = false;
                    }
                    break;
                case SDL_MOUSEWHEEL: in.wheel += e.wheel.y; break;
                case SDL_FINGERDOWN:
                case SDL_FINGERMOTION:
                case SDL_FINGERUP: {
                    in.touchPointer = true;
                    const uint64_t key = (static_cast<uint64_t>(e.tfinger.touchId) << 32) | static_cast<uint64_t>(e.tfinger.fingerId);
                    const int fx = static_cast<int>(e.tfinger.x * kW), fy = static_cast<int>(e.tfinger.y * kH);
                    auto it = std::find_if(fingers.begin(), fingers.end(), [&](const auto& f) { return std::get<0>(f) == key; });
                    if (e.type == SDL_FINGERUP) {
                        if (it != fingers.end()) fingers.erase(it);
                    } else if (it != fingers.end()) {
                        std::get<1>(*it) = fx;
                        std::get<2>(*it) = fy;
                    } else {
                        fingers.emplace_back(key, fx, fy);
                    }
                    break;
                }
                case SDL_TEXTINPUT: in.text += e.text.text; break;
                case SDL_KEYDOWN:
                    switch (e.key.keysym.sym) {
                        case SDLK_BACKSPACE: in.backspace = true; break;
                        case SDLK_RETURN:
                        case SDLK_KP_ENTER: in.enter = true; break;
                        case SDLK_TAB: in.tab = true; break;
                        case SDLK_LEFT: in.left = true; break;
                        case SDLK_RIGHT: in.right = true; break;
                        case SDLK_UP: in.keyUp = true; break;
                        case SDLK_DOWN: in.keyDown = true; break;
                        case SDLK_SPACE: in.enter = in.enter || ui.focus == 0; break;
                        case SDLK_ESCAPE:
                        case SDLK_AC_BACK: in.back = true; break;
                        default: break;
                    }
                    break;
                default: break;
            }
        }
        // scripted clicks for the smoke test: two frames per click (press, then release)
        if (smoke && clickDone < opt.clicks.size() * 2 && frameNo >= opt.clickAfter) {
            const auto& c = opt.clicks[clickDone / 2];
            if (clickDone % 2 == 0) {
                in.mx = in.pmx = c.first;
                in.my = in.pmy = c.second;
                in.pressed = in.down = true;
                prevMy = in.my;
            } else {
                in.mx = c.first;
                in.my = c.second;
                in.released = true;
                in.down = false;
            }
            ++clickDone;
            lastClickFrame = frameNo;
        }
        if (in.down) in.dragDy = in.my - prevMy;
        prevMy = in.my;
        ui.now = SDL_GetTicks();
        in.touches.clear();
        in.touchIds.clear();
        for (const auto& f : fingers) {
            in.touches.emplace_back(std::get<1>(f), std::get<2>(f));
            in.touchIds.push_back(static_cast<int64_t>(std::get<0>(f) & 0x7FFFFFFFFFFFFFFFull));
        }
        if (in.down && fingers.empty()) {  // the desktop mouse counts as one finger (not when it is a finger's echo)
            in.touches.emplace_back(in.mx, in.my);
            in.touchIds.push_back(-1);
        }

        const Uint32 tick = SDL_GetTicks();
        int dtMs = prevTick ? static_cast<int>(std::min<Uint32>(tick - prevTick, 100u)) : 16;
        if (opt.fixedDtMs > 0) dtMs = opt.fixedDtMs;
        prevTick = tick;

        app.pollLoad();
        if (in.back) {
            if (ui.focus)
                ui.focus = 0;
            else if (app.screen == Screen::Story && app.story.onBack()) { /* closed something inside the story */ }
            else if (app.screen == Screen::Gallery && app.gallery.onBack()) { /* closed the full-screen picture */ }
            else if (app.screen == Screen::Browse && app.modsView) app.modsView = false;
            else if (app.screen == Screen::Menu) {
                if (!app.menu.onBack()) running = false;  // back on the title screen leaves the app
            } else if (app.screen == Screen::Play && app.play.onBack()) { /* the match handled it */ }
            else {
                if (app.screen == Screen::Play) app.menu.reloadSettings();
                app.screen = Screen::Menu;  // every other screen returns to the menu
            }
        }
        if (smoke && opt.steamAutostart && frameNo == 1 && !opt.steamUser.empty()) app.startSteam();
        if (smoke && opt.storyAudit && !app.loading) {
            auditProblems = app.story.audit(ren, opt.lang);
            running = false;
            continue;
        }

        // ---- draw
        // the menu, the match, the story, the gallery and the credits choose their own theme; the other screens are silent
        if (app.screen == Screen::Home || app.screen == Screen::Browse || app.screen == Screen::Steam) app.audio.playMusic(false);
        ui.color(kBg);
        SDL_RenderClear(ren);
        ui.beginFrame();
        switch (app.screen) {
            case Screen::Home: drawHome(ui, app); break;
            case Screen::Browse: drawBrowse(ui, app, ren); break;
            case Screen::Steam: drawSteam(ui, app); break;
            case Screen::Play:
                app.play.update(ui, dtMs);
                app.play.render(ui, ren);
                if (app.play.inStory() && app.play.storyOutcome() != play::PlayScreen::StoryOutcome::None) {   // a story fight ended
                    const auto o = app.play.storyOutcome();
                    app.play.leaveStory();
                    app.menu.reloadSettings();   // the match's Options page writes the same [options] as the menu's Settings
                    app.story.fightResult(o == play::PlayScreen::StoryOutcome::Won ? 1 : o == play::PlayScreen::StoryOutcome::Quit ? -1 : 0);
                    app.screen = Screen::Story;
                } else if (app.play.takeCredits()) {   // Arcade: the boss fell - the credits, then the Thanks card, then the menu
                    app.play.takeExit();
                    app.menu.reloadSettings();
                    app.credits.begin(app.menu.language(), false, true);
                    app.screen = Screen::Credits;
                } else if (app.play.takeExit()) {
                    app.menu.reloadSettings();
                    app.screen = Screen::Menu;   // match over: back to the menu
                }
                break;
            case Screen::Story: {
                const story::Out so = app.story.frame(ui, ren, dtMs);
                if (so.kind == story::Out::Kind::Fight) {
                    app.play.enterStory(ren, so.fight.p1, so.fight.p2, so.fight.level, so.fight.rounds, app.menu.rules().difficulty);
                    app.screen = Screen::Play;
                } else if (so.kind == story::Out::Kind::Back) {
                    app.screen = Screen::Menu;
                }
                break;
            }
            case Screen::Gallery:
                if (!app.gallery.frame(ui, ren, dtMs)) app.screen = Screen::Menu;
                break;
            case Screen::Credits:
                if (!app.credits.frame(ui, ren, dtMs)) app.screen = Screen::Menu;
                break;
            case Screen::Menu: {
                const menu::Result mr = app.menu.frame(ui, ren, dtMs);
                if (mr.kind == menu::Result::Kind::Play) {
                    app.play.enterMatch(ren, mr.mode, app.menu.rules(), mr.p1, mr.p2, mr.stage, mr.p1PalPath, mr.p2PalPath);
                    app.screen = Screen::Play;
                } else if (mr.kind == menu::Result::Kind::Story) {
                    app.story.begin(mr.storyChar, app.menu.language());
                    app.screen = Screen::Story;
                } else if (mr.kind == menu::Result::Kind::Gallery) {
                    app.gallery.begin(app.menu.language());
                    app.screen = Screen::Gallery;
                } else if (mr.kind == menu::Result::Kind::Credits) {
                    app.credits.begin(app.menu.language(), mr.contributors, false);
                    app.screen = Screen::Credits;
                } else if (mr.kind == menu::Result::Kind::Home) {
                    app.screen = Screen::Home;
                } else if (mr.kind == menu::Result::Kind::Quit) {
                    running = false;
                }
                break;
            }
        }
        // last, so it sits above scrolled content; the menu and a running match are full-screen (the HUD needs the top)
        const bool inMatch = app.screen == Screen::Play && app.play.phase() != play::Phase::Select;
        if (app.screen != Screen::Menu && app.screen != Screen::Story && app.screen != Screen::Gallery && app.screen != Screen::Credits && !inMatch) drawTopBar(ui, app);
        ui.endFrame();

        // keep the soft keyboard in step with the focused field
        if ((ui.focus != 0) != wantText) {
            wantText = ui.focus != 0;
            if (wantText) SDL_StartTextInput();
            else SDL_StopTextInput();
        }

        // ---- smoke capture
        if (smoke) {
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            const SteamTaskState st = app.task.snapshot();
            const bool ready = (opt.smokeLoading || !app.loading) && frameNo >= opt.smokeFrames && clickDone >= opt.clicks.size() * 2 && frameNo > lastClickFrame && phaseMatches(opt.smokePhase, st.phase);
            if (ready || secs > 20) {
                const int cw = kW * opt.smokeScale, ch = kH * opt.smokeScale;
                std::vector<uint8_t> px(static_cast<size_t>(cw) * ch * 4);
                int w = 0, h = 0;
                SDL_GetRendererOutputSize(ren, &w, &h);
                if (w != cw || h != ch) std::fprintf(stderr, "warning: renderer output is %dx%d, expected %dx%d\n", w, h, cw, ch);
                if (SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_RGBA32, px.data(), cw * 4) != 0) {
                    std::fprintf(stderr, "SDL_RenderReadPixels: %s\n", SDL_GetError());
                    exitCode = 2;
                } else {
                    const std::vector<uint8_t> png = encodePngRgba(px.data(), cw, ch);
                    if (std::FILE* f = std::fopen(opt.smokePng.c_str(), "wb")) {
                        std::fwrite(png.data(), 1, png.size(), f);
                        std::fclose(f);
                        std::printf("smoke: wrote %s (%dx%d) after %d frames, steam phase %d\n", opt.smokePng.c_str(), cw, ch, frameNo,
                                    static_cast<int>(st.phase));
                        if (app.audio.isOpen())
                            std::printf("smoke: audio mixed %llu frames, music %s at frame %u (gain %.2f)\n",
                                        static_cast<unsigned long long>(app.audio.framesMixed()), app.audio.musicInIntro() ? "in the intro" : "in the loop",
                                        app.audio.musicPosition(), static_cast<double>(app.audio.musicGain()));
                    } else {
                        std::fprintf(stderr, "cannot write %s\n", opt.smokePng.c_str());
                        exitCode = 2;
                    }
                }
                if (!ready) {
                    std::fprintf(stderr, "smoke: timed out waiting for steam phase '%s'\n", opt.smokePhase.c_str());
                    exitCode = 3;
                }
                running = false;
            }
            SDL_Delay(8);
        } else {
            SDL_RenderPresent(ren);
            if (!vsync) SDL_Delay(8);
        }
        ++frameNo;
    }

    if (app.tex) {
        SDL_DestroyTexture(app.tex);
        app.tex = nullptr;
    }
    SDL_DestroyTexture(ui.font);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return exitCode;
}
