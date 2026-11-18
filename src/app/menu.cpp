// menu.cpp - the main menu (see menu.h).
#include "imgscale.h"
#include "menu.h"

#include "zd_features.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

#include "art.h"
#include "colour_edit.h"
#include "dds.h"
#include "hud_art.h"
#include "palmod.h"
#include "vfs.h"
#include "dev_options.h"
#include "savini.h"
#include "zg_progress.h"

namespace zengine {
namespace menu {

namespace {

constexpr int kW = 640, kH = 360;
constexpr int kWarningMs = 6200;   // PhotosensativeWarning runs 360 ticks (6 s)
constexpr int kSplashMs = 2600;   // the original loading screen is shown this long before the title
constexpr int kListX = 24, kListY = 98, kListH = 196;
constexpr int kDescY = 300, kDescH = 42;

Col withAlpha(Col c, int a) { return Col{c.r, c.g, c.b, static_cast<uint8_t>(std::clamp(a, 0, 255))}; }

std::string baseName(const std::string& p) {
    const size_t i = p.find_last_of("/\\");
    return i == std::string::npos ? p : p.substr(i + 1);
}

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

// Text with a 1px drop shadow, so it reads on top of a photo-like backdrop.
void shadowText(Ui& ui, int x, int y, const std::string& s, Col c, int scale = 1) {
    ui.text(x + scale, y + scale, s, Col{0, 0, 0, static_cast<uint8_t>(c.a * 3 / 4)}, scale);
    ui.text(x, y, s, c, scale);
}

std::vector<std::string> wrap(const std::string& s, int maxGlyphs) {
    std::vector<std::string> lines;
    std::string cur;
    std::istringstream in(s);
    std::string word;
    while (in >> word) {
        if (!cur.empty() && Ui::glyphCount(cur) + 1 + Ui::glyphCount(word) > maxGlyphs) {
            lines.push_back(cur);
            cur.clear();
        }
        if (!cur.empty()) cur += ' ';
        cur += word;
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

bool isNumber(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

// ---- built-in fallbacks: used only when the game's own .ini is not in the mounted data ----
// (the real files always win).  Labels double as their own localisation keys.
const char* builtinIni(const std::string& name) {
    if (name == "zd_dev.ini") {   // ZDroid's own page: Settings > Developer Options (core/dev_options.cpp is the table)
        static const std::string dev = devopt::menuIni();
        return dev.c_str();
    }
    if (name == "singlePlayer.ini")
        return "TopText: Single Player\n"
               "Option:\nType: Advance\nText: Arcade Mode\nDescription: Fight a CPU opponent.\nKeyword: ACT_ARCADE\n"
               "Option:\nType: Advance\nText: CPU Quick Match\nDescription: A quick battle against the opponent of your choosing.\nKeyword: ACT_QUICKMATCH\n"
               "Option:\nType: Setting\nText: Difficulty\nDescription: null\nDefaultChoice: Normal\nChoice: Hard\nChoice: Ridiculous\nChoice: Nightmare\nChoice: Sleepwalk\nKeyword: OPT_STR_SPDIFFICULTY\n";
    if (name == "versusSTEAM.ini")
        return "TopText: Versus\n"
               "Option:\nDefaultCursorPos\nType: Advance\nText: Local Versus\nDescription: Challenge the person sitting next to you.\nKeyword: ACT_LOCAL_MP\n";
    if (name == "training.ini")
        return "TopText: Training\n"
               "Option:\nType: Advance\nText: Training Room\nDescription: Practice moves and combos.\nKeyword: ACT_PRACTICE_OPT\n";
    if (name == "extras.ini")
        return "TopText: Extras\n"
               "Option:\nType: Advance\nText: Art Gallery\nDescription: null\nKeyword: ACT_EXTRAS_GALLERY\n"
               "Option:\nType: Advance\nText: Credits\nDescription: null\nKeyword: ACT_CREDITSNORMAL\n"
               "Option:\nType: Advance\nText: Contributors\nDescription: null\nKeyword: ACT_CREDITSCONT\n";
    if (name == "helpSTEAM.ini")
        return "TopText: Help\n"
               "Option:\nType: Advance\nText: Controls\nDescription: null\nKeyword: ACT_BUTTONCONFIG\n"
               "Option:\nType: Advance\nText: Settings\nDescription: null\nKeyword: ACT_OPTIONS\n";
    if (name == "options.ini")
        return "TopText: Settings\n"
               "Option:\nType: Setting\nText: Number of Rounds\nDescription: null\nChoice: 1\nDefaultChoice: 3\nChoice: 5\nChoice: 7\nKeyword: OPT_INT_NUM_ROUNDS\n"
               "Option:\nType: Setting\nText: Round Time Limit\nDescription: null\nChoice: 30\nChoice: 60\nDefaultChoice: 99\nChoice: Infinity\nKeyword: OPT_STR_TIME_LIMIT\n"
               "Option:\nType: Setting\nText: Language\nDescription: null\nDefaultChoice: Default\nChoice: English\nChoice: Spanish\nChoice: German\nChoice: French\nChoice: Italian\nChoice: Portuguese\nKeyword: OPT_STR_GAME_LANGUAGE\n"
               "Option:\nType: Advance\nText: Restore Defaults\nDescription: null\nKeyword: ACT_RESTORE_DEFAULTS\n";
    return nullptr;
}

struct Lang {
    const char* name;  // the Choice text in options.ini
    const char* code;  // <code>_loc.json
};
const Lang kLangs[] = {{"English", "en"},    {"Spanish", "es"}, {"German", "de"},
                       {"French", "fr"},     {"Italian", "it"}, {"Portuguese", "pt"},
                       {"Japanese", "jp"}};

}  // namespace

// ---------------------------------------------------------------------------------------- strings

std::string MenuScreen::own(const std::string& key) const {
    const bool es = langCode_ == "es";
    struct S {
        const char* key;
        const char* en;
        const char* es;
    };
    static const S t[] = {
        {"touch", "TOUCH THE SCREEN", "TOCA LA PANTALLA"},
        {"warn.text",
         "PHOTOSENSITIVITY WARNING\n\nThis game contains certain intensities of flashing light that may trigger seizures or blackouts in individuals with photosensitive epilepsy, even with no prior history of seizures. Please cease play immediately if you experience dizziness, disorientation, altered vision, eye or muscle twitching, loss of consciousness, or any other signs of distress.\n\nHold Any Button to Skip",
         "ADVERTENCIA DE FOTOSENSIBILIDAD\n\nEste juego contiene luces intermitentes de cierta intensidad que pueden provocar convulsiones o pérdidas del conocimiento en personas con epilepsia fotosensible, incluso sin antecedentes de convulsiones. Deja de jugar de inmediato si notas mareos, desorientación, alteraciones de la vista, temblores en los ojos o los músculos, pérdida del conocimiento o cualquier otro signo de malestar.\n\nMantén pulsado cualquier botón para saltar"},
        {"enter", "PRESS ENTER OR CLICK", "PULSA ENTER O HAZ CLIC"},
        {"gamedata", "Game Data", "Datos del juego"},
        {"gamedata.desc", "Pick the folder with your extracted game files, or download the game from your own Steam account.",
         "Elige la carpeta con los archivos extraídos de tu juego, o descárgalo desde tu propia cuenta de Steam."},
        {"notimpl", "Not available in ZDroid yet.", "Todavía no está disponible en ZDroid."},
        {"fightoff", "Fight mode is switched off while it gets polished.", "El modo de pelea está desactivado mientras se pule."},
        {"csconfirm", "%s picked - fight mode is switched off, nothing starts yet.", "%s elegido: el modo de pelea está desactivado, aún no empieza nada."},
        {"cslocked", "That character is locked.", "Ese personaje está bloqueado."},
        {"online", "Online play is not part of ZDroid.", "El juego en línea no forma parte de ZDroid."},
        {"approx", "ZDroid plays one match here; the original mode is not implemented yet.",
         "ZDroid juega un solo combate; el modo original aún no está implementado."},
        {"dim", "Dim entries are saved but ZDroid does not use them yet.",
         "Las entradas en gris se guardan, pero ZDroid aún no las usa."},
        {"hint.keys", "UP/DOWN select   ENTER ok   LEFT/RIGHT change   ESC back",
         "ARRIBA/ABAJO elegir   ENTER aceptar   IZQ/DER cambiar   ESC atrás"},
        {"hint.touch", "tap to select, tap again to confirm   (back = Android back)",
         "toca para elegir, toca otra vez para confirmar   (atrás = botón atrás)"},
        {"empty", "Nothing here yet.", "Aún no hay nada aquí."},
        {"jp", "Japanese needs the game's font atlas, which ZDroid cannot use yet: showing English.",
         "El japonés necesita el atlas de fuentes del juego, que ZDroid aún no puede usar: se muestra inglés."},
        {"restored", "Defaults restored.", "Valores restaurados."},
        {"nodata", "No game data mounted: using ZDroid's own labels.", "Sin datos del juego: usando textos propios de ZDroid."},
        {"controls", "Controls", "Controles"},
        {"c1", "Keyboard P1: WASD move, J K L = LP MP HP, U I O = LK MK HK",
         "Teclado J1: WASD mover, J K L = LP MP HP, U I O = LK MK HK"},
        {"c2", "Keyboard P2 (Versus): arrows move, 1 2 3 = LP MP HP, 4 5 6 = LK MK HK",
         "Teclado J2 (Versus): flechas mover, 1 2 3 = LP MP HP, 4 5 6 = LK MK HK"},
        {"c3", "Touch: d-pad on the left, six attack buttons on the right, ST = start / retry",
         "Táctil: cruceta a la izquierda, seis botones de ataque a la derecha, ST = start / reintentar"},
        {"c4", "Back / Esc leaves the match.  Remapping is not implemented yet.",
         "Atrás / Esc sale del combate.  Aún no se pueden reasignar los controles."},
        {"close", "OK", "Aceptar"},
        {"unofficial", "ZDroid - unofficial engine reimplementation. Needs your own copy of the game.",
         "ZDroid - reimplementación no oficial del motor. Necesita tu propia copia del juego."},
        {"dis.title", "NOTICE", "AVISO"},
        {"dev", "Developer: Destroyer And Nitro", "Desarrollador: Destroyer And Nitro"},
        // The notice text comes from the project owner (Spanish original; the English is a translation).
        {"dis.1", "This is an UNOFFICIAL Skullgirls port attempt. Material that is not authorised, for example elements, drawings, images, voices, etc., is not being distributed or shared.",
         "Este es un intento de port de Skullgirls NO OFICIAL. El material que no está autorizado, por ejemplo elementos, dibujos, imágenes, voces, etc., no se distribuye ni se comparte."},
        {"dis.2", "ZGirls Loader is a loader that reads the files of your copy of the original game. ZDroid-Engine is an unofficial, independent reimplementation that is compatible with the game's file formats; it contains no code or material from the original. The material must not be backed up or distributed together with a version that includes the original files. All materials belong to Lab Zero and their respective authors.",
         "ZGirls Loader es un cargador que lee los archivos de tu copia del juego original. ZDroid-Engine es una reimplementación no oficial e independiente, compatible con los formatos de archivo del juego; no contiene código ni material del original. El material no debe respaldarse ni distribuirse junto con una versión que incluya los archivos originales. Todos los materiales pertenecen a Lab Zero y a sus respectivos autores."},
        {"dis.h", "By entering the Loader, YOU accept that:", "Al ingresar al Loader, USTED acepta que:"},
        {"dis.a", "- I own a legal, NON-pirated copy of the game (Steam edition or another official edition).",
         "- Tengo una copia legal y NO pirata del juego (edición de Steam u otra edición oficial)."},
        {"dis.b", "- I will NOT distribute the game's files, nor altered copies of them (mods or hacks that include game material).",
         "- NO distribuiré los archivos del juego ni copias alteradas de ellos (mods o hacks que incluyan material del juego)."},
        {"dis.c", "- I will not use corrupted files, nor damage the APK to hurt performance or make it easy to hack.",
         "- No usaré archivos corruptos ni dañaré el APK para afectar el rendimiento o volverlo fácilmente hackeable."},
        {"dis.5", "If you do NOT own a copy of the game you must NOT have this Loader: buy Skullgirls 2nd Encore on Steam (or spend more on its \"mobile\" version) and play it the legal way.",
         "Si NO tienes una copia del juego NO debes poseer este Loader: debes comprar Skullgirls 2nd Encore en Steam (o gastar más dinero en su versión \"mobile\") y jugarlo por la vía legal."},
        {"dis.go", "Continue", "Continuar"},
        {"dis.exit", "Exit", "Salir"},
        {"loading", "Loading game data...", "Cargando datos del juego..."},
    };
    for (const S& s : t)
        if (key == s.key) return es ? s.es : s.en;
    if (const char* d = devopt::text(key, es)) return d;   // Developer Options labels / choices
    return key;
}

std::string MenuScreen::L(const std::string& key) const {
    if (startsWith(key, "@own:")) return own(key.substr(5));
    const std::string t = loc_.tr(key);
    if (t == key) {   // not in the game's strings: the Developer Options choices ("Native", "Smooth" ...) have their own
        const std::string c = own("c." + key);
        if (c != "c." + key) return c;
    }
    return t;
}

void MenuScreen::say(const std::string& msg) {
    toast_ = msg;
    toastMs_ = 2600;
}

// ---------------------------------------------------------------------------------------- data

const MenuDef& MenuScreen::def(const std::string& file) {
    auto it = defs_.find(file);
    if (it != defs_.end()) return it->second;
    std::string text;
    bool have = gd_ && gd_->vfs().readText(file, text);
    MenuDef d;
    if (have) d = parseMenuIni(text);
    if (!have || d.options.empty()) {
        if (const char* b = builtinIni(file)) d = parseMenuIni(b);
    }
    for (const MenuOption& o : d.options)
        if (o.type == MenuOptType::Setting && !o.keyword.empty())
            defaults_.emplace(o.keyword, o.defaultChoice.empty() && !o.choices.empty() ? o.choices.front() : o.defaultChoice);
    return defs_.emplace(file, std::move(d)).first->second;
}

MenuScreen::Page MenuScreen::makePage(const std::string& file) {
    Page p;
    p.file = file;
    p.def = def(file);
    for (size_t i = 0; i < p.def.options.size(); ++i)
        if (p.def.options[i].visibleOn(platform_)) p.vis.push_back(static_cast<int>(i));
    for (size_t k = 0; k < p.vis.size(); ++k)
        if (p.def.options[static_cast<size_t>(p.vis[k])].defaultCursor) p.sel = static_cast<int>(k);
    // The original closes a page with a "BACK" row at the end of the list (the .ini files do not carry it); on a touch
    // screen it is also the only visible way out.
    if (file == "helpSTEAM.ini") {   // ZDroid's own entry (data folder / Steam download) lives with the game's help & options page
        MenuOption gdopt;
        gdopt.type = MenuOptType::Advance;
        gdopt.text = "@own:gamedata";
        gdopt.description = "@own:gamedata.desc";
        gdopt.keyword = "@data";
        p.def.options.push_back(gdopt);
        p.vis.push_back(static_cast<int>(p.def.options.size()) - 1);
    }
    if (file == "options.ini") {   // ZDroid's own page of switches: resolution, sprite quality, shadows, stage ...
        MenuOption dev;
        dev.type = MenuOptType::Advance;
        dev.text = "@own:dev";
        dev.description = "@own:dev.desc";
        dev.keyword = "@page:zd_dev.ini";
        p.def.options.push_back(dev);
        p.vis.push_back(static_cast<int>(p.def.options.size()) - 1);
    }
    MenuOption back;
    back.type = MenuOptType::Advance;
    back.text = "BackOption";
    back.keyword = "@back";
    p.def.options.push_back(back);
    p.vis.push_back(static_cast<int>(p.def.options.size()) - 1);
    p.dense = p.vis.size() > 6;
    return p;
}

MenuScreen::Page MenuScreen::makeRoot() {
    Page p;
    p.root = true;
    p.file = "(root)";
    auto add = [&](const char* text, const char* desc, const char* kw) {
        MenuOption o;
        o.type = MenuOptType::Advance;
        o.text = text;
        o.description = desc;
        o.keyword = kw;
        p.def.options.push_back(o);
    };
    // The order and the keys come from SkullGirls.exe (the list builder at 0x64a780): Download Content, Achievements,
    // Leaderboards, Extras, Single Player, Versus, Training, Help & Options, Exit Game.  The game opens with Single Player
    // highlighted (index 4).  The first three need the store / online services, which ZDroid does not have: they stay in
    // the list (so the screen looks like the original one) but dimmed.
    add("Download", "DownloadDesc", "@download");
    add("Achievements", "AchievementsDesc", "@achievements");
    add("Leaderboards", "LeaderboardsDesc", "@leaderboards");
    add("Extras", "ExtrasDesc", "@page:extras.ini");
    add("Single Player", "SPDesc", "@page:singlePlayer.ini");
    add("VERSUS", "VersusDesc", "@page:versusSTEAM.ini");
    add("Training", "TrainingDesc", "@page:training.ini");
    add("Options", "Optionsdesc", "@page:helpSTEAM.ini");
    add("Exit", "ExitDesc", "@exit");
    for (size_t i = 0; i < p.def.options.size(); ++i) p.vis.push_back(static_cast<int>(i));
    p.dense = false;
    p.sel = 4;
    return p;
}

void MenuScreen::push(const std::string& file) {
    stack_.push_back(makePage(file));
}

void MenuScreen::rebuildStack() {
    // Keep the user's place (page files + selections) across a rescan / language change.
    std::vector<std::pair<std::string, int>> keep;
    for (const Page& p : stack_) keep.emplace_back(p.file, p.sel);
    defs_.clear();
    stack_.clear();
    stack_.push_back(makeRoot());
    for (size_t i = 0; i < keep.size(); ++i) {
        if (i == 0) {
            stack_[0].sel = std::min(keep[0].second, static_cast<int>(stack_[0].vis.size()) - 1);
            continue;
        }
        Page p = makePage(keep[i].first);
        p.sel = std::clamp(keep[i].second, 0, std::max(0, static_cast<int>(p.vis.size()) - 1));
        stack_.push_back(std::move(p));
    }
    if (stack_[0].sel < 0) stack_[0].sel = 0;
}

void MenuScreen::loadLocalization() {
    // Language: command-line override > the Language setting > the system locale > English.
    std::string code;
    jpFallback_ = false;
    std::string want = langOverride_;
    if (want.empty()) {
        const std::string v = settingValue("OPT_STR_GAME_LANGUAGE");
        if (v != "Default") {
            for (const Lang& l : kLangs)
                if (v == l.name) want = l.code;
        }
    }
    if (want.empty()) {
        if (SDL_Locale* loc = SDL_GetPreferredLocales()) {
            if (loc[0].language) {
                const std::string lang = loc[0].language;
                for (const Lang& l : kLangs)
                    if (lang == l.code || (lang == "ja" && std::string(l.code) == "jp")) want = l.code;
            }
            SDL_free(loc);
        }
    }
    if (want.empty()) want = "en";
    if (want == "jp" && !uiPackFonts()) {  // the 8x8 Latin font cannot draw kana/kanji; the game's own fonts can
        jpFallback_ = true;
        want = "en";
    }
    code = want;
    loc_.clear();
    std::string json;
    if (gd_ && gd_->vfs().readText(code + "_loc.json", json)) {
        std::string err;
        if (!loc_.parseJson(json, &err)) std::printf("[menu] %s_loc.json: %s (using what was read)\n", code.c_str(), err.c_str());
    } else if (code != "en" && gd_ && gd_->vfs().readText("en_loc.json", json)) {
        loc_.parseJson(json);
        code = "en";
    }
    langCode_ = code;
    std::printf("[menu] language %s, %zu localised strings\n", langCode_.c_str(), loc_.size());
}

bool MenuScreen::uiPackFonts() const {
    return gd_ && gd_->vfs().exists("Main.gbs") && gd_->vfs().exists("Fonts/Dialogue Text_JP_0.dds");
}

bool MenuScreen::optionUsable(const MenuOption& o) const {
    bool usable = true;
    if (o.type == MenuOptType::Setting) usable = settingImplemented(o.keyword);
    else if (o.type == MenuOptType::Advance) usable = actionImplemented(o.keyword.rfind("@page:", 0) == 0 ? "@page" : o.keyword);
    else if (o.type == MenuOptType::Submenu) usable = false;  // e.g. voice settings: nothing to configure without audio
    if (startsWith(o.keyword, "ACT_ONLINE") || startsWith(o.keyword, "ACT_LOBBY")) usable = false;
    return usable;
}

std::vector<std::pair<std::string, int>> MenuScreen::descriptionLines(const Page& p) const {
    std::vector<std::pair<std::string, int>> lines;
    if (toastMs_ > 0) {
        lines.emplace_back(toast_, 1);
        return lines;
    }
    if (p.vis.empty()) return lines;
    const MenuOption& o = p.def.options[static_cast<size_t>(p.vis[static_cast<size_t>(std::clamp(p.sel, 0, static_cast<int>(p.vis.size()) - 1))])];
    if (o.hasDescription()) {
        const std::string d = o.isMove ? commandToText(L(o.description)) : L(o.description);
        if (d != o.description || !loc_.size() || o.description.find(' ') != std::string::npos)  // never print a bare missing key
            lines.emplace_back(d, 0);
    }
    if (o.isMove) return lines;   // a move is read, not run: no "not available" note
    const std::string kw = startsWith(o.keyword, "@page:") ? "@page" : o.keyword;
    if (isFightAction(o.keyword) && !kFightEnabled)
        lines.emplace_back(own("fightoff"), 1);
    else if (o.type == MenuOptType::Advance && !actionImplemented(kw) && !startsWith(o.keyword, "ACT_ONLINE") && !startsWith(o.keyword, "ACT_LOBBY"))
        lines.emplace_back(own("notimpl"), 1);
    else if (startsWith(o.keyword, "ACT_ONLINE") || startsWith(o.keyword, "ACT_LOBBY"))
        lines.emplace_back(own("online"), 1);
    else if (o.type == MenuOptType::Setting && !settingImplemented(o.keyword))
        lines.emplace_back(own("dim"), 2);
    return lines;
}

void MenuScreen::layoutRows(Page& p) {
    rows_.clear();
    listBox_ = Rect{0, 0, 0, 0};
    if (sceneOk_) {
        if (p.root) sceneLayoutRoot(p);
        else sceneLayoutPage(p);
        return;
    }
    const int n = static_cast<int>(p.vis.size());
    const int rowH = rowHeight(p);
    const int rowW = p.dense ? 372 : 304;
    rowPx_ = rowH;
    visibleRows_ = kListH / rowH;
    listBox_ = Rect{kListX, kListY, rowW, kListH};
    for (int k = 0; k < n; ++k) {
        const int y = kListY + static_cast<int>(std::lround((k - p.scroll) * rowH));
        if (y + rowH < kListY || y > kListY + kListH) continue;
        rows_.push_back({k, Rect{kListX, y, rowW, rowH - (p.dense ? 1 : 3)}});
    }
}

void MenuScreen::setGameData(const GameData* gd, const Inventory* inv) {
    gd_ = gd;
    inv_ = inv;
    loading_ = false;
    assetsDirty_ = true;
    splash_.reset();
    splashTried_ = false;
    defs_.clear();
    defaults_.clear();
    loadLocalization();
    rebuildStack();
    loadAudioAssets();
}

void MenuScreen::loadAudioAssets() {
    sfxMove_ = sfxEnter_ = sfxBack_ = sfxNope_ = sfxStart_ = -1;
    if (!audio_ || !gd_) return;
    // The cues are picked by the file names the game ships in UISound/; which one the original plays where is not decoded.
    musicTrack_ = audio_->loadTrack(gd_->vfs(), "menu_loop.wav", "menu_intro.wav", true);
    selectTrack_ = audio_->loadTrack(gd_->vfs(), "select_loop.wav", "select_intro.wav", true);   // Character Select theme
    if (selectTrack_ < 0) selectTrack_ = musicTrack_;                                    // not in the data: keep the menu theme
    sfxMove_ = audio_->loadSfx(gd_->vfs(), "menu.select.wav");
    sfxEnter_ = audio_->loadSfx(gd_->vfs(), "submenu.select.wav");
    sfxBack_ = audio_->loadSfx(gd_->vfs(), "stageselect.whoosh.cancel.wav");
    sfxNope_ = audio_->loadSfx(gd_->vfs(), "ui.nope.wav");
    sfxStart_ = audio_->loadSfx(gd_->vfs(), "menu.select.intro.wav");
}

void MenuScreen::applyAudio() {
    if (!audio_) return;
    auto vol = [&](const char* key) {
        const std::string v = settingValue(key);
        return v.empty() ? 1.0f : std::clamp(static_cast<float>(std::atoi(v.c_str())) / 100.0f, 0.0f, 1.0f);
    };
    audio_->setVolumes(vol("OPT_INT_MASTER_VOL"), vol("OPT_INT_MUSIC"), vol("OPT_INT_EFFECTS"));
}

void MenuScreen::setPrefsDir(const std::string& dir) {
    prefsDir_ = dir;
    prefsFile_ = dir.empty() ? std::string() : dir + "/zgirls.savini";   // [options] of the .savini (core/savini.h)
    if (!dir.empty()) SavIni::shared().open(dir);                       // also imports an older menu_settings.txt once
    loadSettings();
    loadLocalization();
    rebuildStack();
}

void MenuScreen::setLanguageOverride(const std::string& code) {
    langOverride_ = code;
    loadLocalization();
    rebuildStack();
}

void MenuScreen::loadSettings() {
    settings_.clear();
    if (prefsFile_.empty()) return;
    for (const SavIni::Entry& e : SavIni::shared().entries("options")) settings_[e.key] = e.value;
}

void MenuScreen::saveSettings() const {
    if (prefsFile_.empty()) return;
    SavIni& ini = SavIni::shared();
    ini.setAutosave(false);
    const std::vector<SavIni::Entry> old = ini.entries("options");   // a copy: erase() reshuffles the section
    for (const SavIni::Entry& e : old)
        if (!settings_.count(e.key)) ini.erase("options", e.key);   // "restore defaults" empties the map
    for (const auto& kv : settings_) ini.set("options", kv.first, kv.second);
    ini.setAutosave(true);
    ini.save();
}

std::string MenuScreen::settingValue(const std::string& keyword) const {
    auto it = settings_.find(keyword);
    if (it != settings_.end()) return it->second;
    auto d = defaults_.find(keyword);
    if (d != defaults_.end()) return d->second;
    return keyword == "OPT_STR_GAME_LANGUAGE" ? "Default" : std::string();
}

play::Rules MenuScreen::rules() const {
    // The .ini defaults are only known once their pages were parsed; make sure they were.
    auto* self = const_cast<MenuScreen*>(this);
    self->def("singlePlayer.ini");
    self->def("options.ini");
    play::Rules r;
    const std::string d = settingValue("OPT_STR_SPDIFFICULTY");
    r.difficulty = (d == "Sleepwalk" || d == "Easy") ? 0 : (d == "Normal" || d.empty()) ? 1 : 2;
    const std::string n = settingValue("OPT_INT_NUM_ROUNDS");
    r.rounds = isNumber(n) ? std::clamp(std::atoi(n.c_str()), 1, 7) : 3;
    const std::string t = settingValue("OPT_STR_TIME_LIMIT");
    r.timeLimitSec = t == "Infinity" ? 0 : (isNumber(t) ? std::atoi(t.c_str()) : 99);
    return r;
}

bool MenuScreen::settingImplemented(const std::string& k) const {
    return k.rfind("ZD_DEV_", 0) == 0 || k == "OPT_STR_SPDIFFICULTY" || k == "OPT_INT_NUM_ROUNDS" || k == "OPT_STR_TIME_LIMIT" || k == "OPT_STR_GAME_LANGUAGE" ||
           k == "OPT_INT_MASTER_VOL" || k == "OPT_INT_MUSIC" || k == "OPT_INT_EFFECTS";
}

bool MenuScreen::isFightAction(const std::string& k) {
    return k == "ACT_ARCADE" || k == "ACT_QUICKMATCH" || k == "ACT_LOCAL_MP" || k == "ACT_PRACTICE_OPT";
}

bool MenuScreen::actionImplemented(const std::string& k) const {
    if (k == "ACT_EXTRAS_GALLERY") return true;   // Extras -> Art Gallery (gallery.cpp)
    if (k == "ACT_CREDITSNORMAL" || k == "ACT_CREDITSCONT") return true;   // Extras -> Credits / Contributors (credits.cpp)
    if (k == "ACT_STORY") return true;   // the Character Select (no stage, no 2nd fighter) or the story screen's own list
    if (isFightAction(k)) return kFightEnabled || charSel_.ready();
    if (k == "ACT_MOVELISTS" || k == "ACT_MOVELIST") return true;   // Help & Options -> Command Lists -> a character's moves (movelist_<Name>.ini)
    return k == "ACT_OPTIONS" || k == "ACT_BUTTONCONFIG" || k == "ACT_RESTORE_DEFAULTS" || k == "@page" || k == "@data" || k == "@exit" || k == "@back";
}

const char* MenuScreen::stateName() const {
    return state_ == State::Disclaimer ? "notice" : state_ == State::Warning ? "warning" : state_ == State::Splash ? "splash" : state_ == State::Attract ? "attract" : state_ == State::Title ? "title" : state_ == State::CharSelect ? "charselect" : "menu";
}

std::map<std::string, std::vector<CharSelect::PaletteChoice>> MenuScreen::buildPaletteChoices() {
    std::map<std::string, std::vector<CharSelect::PaletteChoice>> pm;
    favsByChar_.clear();
    if (!gd_) return pm;
    if (!favs_.enabled() && !prefsDir_.empty()) favs_ = coledit::Favourites(prefsDir_ + "/favcolours");
    for (int i = 0; i < CharSelect::kRoster; ++i) {
        const char* rn = CharSelect::rosterName(i);
        if (!rn || !*rn) continue;
        const std::string name = rn;
        std::vector<CharSelect::PaletteChoice>& v = pm[name];
        for (int n = 1; n <= 40; ++n) {
            std::string real;
            if (gd_->vfs().resolve(name + "_" + std::to_string(n) + "p.dds", &real)) v.push_back({std::to_string(n) + "p", real});
        }
        for (const palmod::Entry* e : gd_->customPalettes().forCharacter(name)) v.push_back({"mod:" + e->label, e->dds});
        if (v.empty()) {
            pm.erase(name);
            continue;
        }
        if (favs_.enabled()) {   // the character's own palette gives a favourite its lit flags when its texture has to be baked again
            Palette own;
            std::vector<uint8_t> bytes;
            const bool have = readFile(v[0].path, bytes) && readDds(bytes.data(), bytes.size(), own);
            std::vector<coledit::Fav> list = favs_.list(name, have ? &own : nullptr);
            for (const coledit::Fav& f : list) v.push_back({"fav:" + f.name, f.dds});
            favsByChar_[name] = std::move(list);
        }
    }
    return pm;
}

void MenuScreen::openColourEditor(const std::string& name, int palIdx) {
    const auto it = palChoices_.find(name);
    if (!gd_ || it == palChoices_.end() || it->second.empty() || name.empty() || name == "random") return;
    const int idx = std::max(0, std::min(palIdx, static_cast<int>(it->second.size()) - 1));
    const CharSelect::PaletteChoice& cur = it->second[static_cast<size_t>(idx)];
    auto loadPal = [&](const std::string& path, Palette& out) {
        std::vector<uint8_t> bytes;
        return readFile(path, bytes) && readDds(bytes.data(), bytes.size(), out) && out.bands >= 2;
    };
    Palette base, own;
    if (!loadPal(cur.path, base)) {
        say(langCode_ == "es" ? "No se pudo leer esa paleta." : "That palette could not be read.");
        return;
    }
    ColourEditor::Setup su;
    su.character = name;
    su.title = hudart::displayName(name);
    su.lang = langCode_;
    if (!coledit::fromPalette(base, palmod::regionCounts(name), su.scheme)) return;
    su.scheme.character = name;
    if (loadPal(it->second[0].path, own) && own.bands == base.bands && own.alpha.size() == static_cast<size_t>(own.bands) * own.tones)
        su.scheme.litAlpha = own.alpha;   // the character's own palette says which colours take the stage's light
    if (cur.label.rfind("fav:", 0) == 0) {
        for (const coledit::Fav& f : favsByChar_[name])
            if (f.dds == cur.path) su.editing = f;
    }
    su.name = !su.editing.file.empty() ? su.editing.name
                                       : std::string(langCode_ == "es" ? "Mi color " : "My colour ") + std::to_string(favsByChar_[name].size() + 1);
    if (!colourEd_.open(gfx_, favs_, std::move(su))) {
        say(langCode_ == "es" ? "No hay sprite de este personaje en tus datos." : "Your data has no sprite for this character.");
        return;
    }
    sfx(sfxEnter_);
}

void MenuScreen::finishColourEdit(const ColourEditor::Result& r) {
    using K = ColourEditor::Result::Kind;
    if (r.kind == K::None) return;
    if (r.kind == K::Cancelled) {
        sfx(sfxBack_);
        return;
    }
    const std::string name = r.fav.character;
    auto pm = buildPaletteChoices();
    palChoices_ = pm;
    charSel_.setPalettes(std::move(pm));
    int at = 0;
    if (r.kind == K::Saved) {
        const auto it = palChoices_.find(name);
        if (it != palChoices_.end())
            for (size_t i = 0; i < it->second.size(); ++i)
                if (it->second[i].path == r.fav.dds) at = static_cast<int>(i);
    }
    charSel_.setPaletteIndex(at);
    sfx(sfxEnter_);
    say(r.kind == K::Saved ? (langCode_ == "es" ? "Color guardado: " : "Colour saved: ") + r.fav.name : std::string(langCode_ == "es" ? "Color borrado." : "Colour deleted."));
}

bool MenuScreen::showCharSelect(bool story, int firstStep) {
    if (!sceneOk_ || !charSel_.ready()) return false;
    state_ = State::CharSelect;
    csStory_ = story;
    charSel_.setLang(langCode_);
    charSel_.setTeamSizeLocked(true);
    charSel_.setPlayer(pickingP2_ ? 1 : 0);
    charSel_.setSkipStage(pickingP2_);   // player 2's pass only picks the fighter: the stage was chosen in player 1's
    if (pickingP2_) charSel_.setFirstPick(pick1_, pickPal1_);
    else charSel_.setFirstPick("", "");
    if (gd_) {   // who starts locked (ProgressionInit.txt "StoryDouble = L"), minus what the player already opened
        std::string prog;
        std::set<std::string> locked;
        if (gd_->vfs().readText("ProgressionInit.txt", prog)) locked = CharSelect::parseLocked(prog);
        ZgProgress zp;
        zp.load(prefsDir_);
        for (auto it = locked.begin(); it != locked.end();) it = (zp.storyUnlocked(*it) || zgOption("story_all")) ? locked.erase(it) : std::next(it);
        charSel_.setLocked(locked);
    }
    if (gd_) {   // stages dropped into mods/stages
        std::vector<CharSelect::CustomStage> cst;
        for (const modsdir::StageMod& s : gd_->customStages()) cst.push_back({s.id, s.name});
        charSel_.setCustomStages(std::move(cst));
    }
    if (gd_) {   // the colours of every roster character: the game's own <name>_Np.dds, the custom ones from mods/pal ("mod:<file>"), then the player's favourites ("fav:<name>")
        std::map<std::string, std::vector<CharSelect::PaletteChoice>> pm = buildPaletteChoices();
        palChoices_ = pm;
        charSel_.setPalettes(std::move(pm));
    }
    charSel_.enterAt(timeMs_, story, firstStep == 2 ? CharSelect::Step::Stage : firstStep == 1 ? CharSelect::Step::Fighter : CharSelect::Step::Size);
    if (pendingPal_ > 0) charSel_.setPaletteIndex(pendingPal_);   // --cs-pal (smoke runs)
    pendingPal_ = 0;
    confirmExit_ = controls_ = false;
    return true;
}

bool MenuScreen::ensureAttract() {
    if (attractTried_) return attractOk_;
    if (!sceneOk_ || !gd_) return false;
    attractTried_ = true;
    attract_.setText([this](const std::string& key, const std::string& fb) { return loc_.trOr(key, fb); });
    attractOk_ = attract_.load(gfx_);
    gfx_.loadScene("Main.gbs");   // load() switched the current scene
    if (attractOk_ && audio_) attractTrack_ = audio_->loadTrack(gd_->vfs(), "sg_attract_c_loop.wav", "sg_attract_c.wav");
    return attractOk_;
}

void MenuScreen::beginAttract() {
    if (!ensureAttract()) {
        state_ = State::Menu;
        return;
    }
    state_ = State::Attract;
    attractT0_ = timeMs_;
    attract_.start();
    if (attractJumpShot_ >= 0) {
        attract_.jump(attractJumpShot_, attractJumpTick_);
        attractJumpShot_ = -1;
    }
}

void MenuScreen::showAttract(int shot, int tick) {
    attractJumpShot_ = shot;
    attractJumpTick_ = tick;
    confirmExit_ = controls_ = false;
    state_ = State::Attract;   // the scene is loaded on the first drawn frame (beginAttract is called there)
    attractT0_ = timeMs_;
    attractPending_ = true;
}

void MenuScreen::stepAttract() {
    attract_.update(frameDt_);
    if (!audio_ || !gd_) return;
    for (const auto& snd : attract_.takeSounds()) {
        auto it = sfxByName_.find(snd.first);
        if (it == sfxByName_.end()) {
            const int id = audio_->loadSfx(gd_->vfs(), snd.first);
            it = sfxByName_.emplace(snd.first, id >= 0 ? id : -2).first;
        }
        if (it->second >= 0) audio_->playSfx(it->second, snd.second);
    }
}

void MenuScreen::showSplash() {
    state_ = State::Splash;
    splashT0_ = timeMs_;
    confirmExit_ = controls_ = false;
}

std::string MenuScreen::pageTitle() const {
    if (stack_.empty() || stack_.back().root) return {};
    return L(stack_.back().def.topText);
}

void MenuScreen::showTitleImpl() {   // there is no title screen: the root menu is what comes after the warning
    state_ = State::Menu;
    confirmExit_ = controls_ = false;
    rebuildStack();
    stack_.resize(1);
}

void MenuScreen::showWarning() {
    state_ = State::Warning;
    warnT0_ = timeMs_;
    warnHold_ = 0;
    warnBound_ = false;
    confirmExit_ = controls_ = false;
}

void MenuScreen::endWarning() {
    state_ = State::Menu;
    if (audio_) sfx(sfxStart_);
}

// ---------------------------------------------------------------------------------------- logic

void MenuScreen::stepSetting(const MenuOption& o, int dir) {
    if (o.choices.empty()) return;
    const std::string cur = settingValue(o.keyword);
    int idx = 0;
    for (size_t i = 0; i < o.choices.size(); ++i)
        if (o.choices[i] == cur) idx = static_cast<int>(i);
    const int n = static_cast<int>(o.choices.size());
    if (n > 30) idx = std::clamp(idx + dir * 5, 0, n - 1);   // 0..100 volume lists: 5 per press, no wrap-around
    else idx = ((idx + dir) % n + n) % n;
    settings_[o.keyword] = o.choices[static_cast<size_t>(idx)];
    saveSettings();
    sfx(sfxMove_);
    if (o.keyword == "OPT_STR_GAME_LANGUAGE") {
        loadLocalization();
        rebuildStack();
        if (jpFallback_) say(own("jp"));
    }
}

Result MenuScreen::runAction(const MenuOption& o) {
    Result res;
    const std::string& k = o.keyword;
    if (k == "ACT_EXTRAS_GALLERY") {
        res.kind = Result::Kind::Gallery;
    } else if (k == "ACT_CREDITSNORMAL" || k == "ACT_CREDITSCONT") {
        res.kind = Result::Kind::Credits;
        res.contributors = k == "ACT_CREDITSCONT";
    } else if (k == "ACT_STORY") {
        if (!showCharSelect(true)) res.kind = Result::Kind::Story;   // no Charselect.gbs: the story screen lists the characters itself
    } else if (isFightAction(k)) {   // the game's own Character Select picks the fighters; Confirm starts the match
        fightMode_ = k == "ACT_LOCAL_MP" ? play::Mode::Versus : k == "ACT_PRACTICE_OPT" ? play::Mode::Training : k == "ACT_ARCADE" ? play::Mode::Arcade : play::Mode::VsCpu;
        pickingP2_ = false;
        twoPicks_ = k != "ACT_ARCADE";
        if (!showCharSelect(false, 1)) say(own("fightoff"));   // team size is fixed at 1 for now: straight to the fighters
    } else if (k == "ACT_MOVELISTS") {
        push("movelists.ini");
    } else if (k == "ACT_MOVELIST") {
        push("movelist_" + o.text + ".ini");
    } else if (k == "ACT_OPTIONS") {
        push("options.ini");
    } else if (k == "ACT_BUTTONCONFIG") {
        controls_ = true;
    } else if (k == "ACT_RESTORE_DEFAULTS") {
        settings_.clear();
        saveSettings();
        loadLocalization();
        rebuildStack();
        say(own("restored"));
    } else if (startsWith(k, "ACT_ONLINE") || startsWith(k, "ACT_LOBBY")) {
        say(own("online"));
    } else {
        say(own("notimpl"));
    }
    return res;
}

Result MenuScreen::activate(Page& p, int visIdx, int tapX, const Rect& row) {
    Result res;
    if (visIdx < 0 || visIdx >= static_cast<int>(p.vis.size())) return res;
    const MenuOption o = p.def.options[static_cast<size_t>(p.vis[static_cast<size_t>(visIdx)])];  // copy: push() may move pages
    const std::string& k = o.keyword;
    if (o.isMove) return res;   // a line of a move list: selecting it shows its command, there is nothing to run
    if (o.type == MenuOptType::Advance && !optionUsable(o)) sfx(sfxNope_);   // dimmed entry
    if (startsWith(k, "@page:")) {
        push(k.substr(6));
        return res;
    }
    if (k == "@back") {
        if (stack_.size() > 1) stack_.pop_back();   // `p` dangles from here on: nothing below may touch it
        return res;
    }
    if (k == "@data") {
        res.kind = Result::Kind::Home;
        return res;
    }
    if (k == "@exit") {
        confirmExit_ = true;
        modalSel_ = 1;
        return res;
    }
    switch (o.type) {
        case MenuOptType::Setting:
            stepSetting(o, (tapX >= 0 && tapX < (tapSplitX_ > 0 ? tapSplitX_ : row.x + row.w * 2 / 5)) ? -1 : +1);
            return res;
        case MenuOptType::Submenu: push(k); return res;
        case MenuOptType::Confirm: say(own("notimpl")); return res;
        default: break;
    }
    return runAction(o);
}

bool MenuScreen::onBack() {
    if (colourEd_.isOpen()) {   // Android back inside the colour editor closes it without saving
        colourEd_.cancel();
        return true;
    }
    if (controls_) {
        controls_ = false;
        return true;
    }
    if (confirmExit_) {
        confirmExit_ = false;
        return true;
    }
    if (state_ == State::CharSelect) {
        state_ = State::Menu;
        sfx(sfxBack_);
        return true;
    }
    if (state_ == State::Attract) {   // back skips the montage
        attract_.skip();
        return true;
    }
    if (state_ != State::Menu) return false;  // back on the notice or the title leaves the app
    if (stack_.size() > 1) {
        stack_.pop_back();
        return true;
    }
    confirmExit_ = true;  // back on the root list asks before leaving
    modalSel_ = 1;
    return true;
}

Result MenuScreen::handleInput(Ui& ui) {
    Result res;
    Input& in = ui.in;
    if (colourEd_.isOpen()) return res;   // the colour editor (drawn over the selector) owns the pointer and the keys
    const bool anyKey = in.enter || in.keyUp || in.keyDown || in.left || in.right;

    // ---- modals
    if (confirmExit_ && useDialog()) {
        switch (dlg_.input(in)) {
            case ConfirmDialog::Answer::Yes: res.kind = Result::Kind::Quit; [[fallthrough]];
            case ConfirmDialog::Answer::No:
                confirmExit_ = false;
                dlg_.close();
                break;
            case ConfirmDialog::Answer::Pending: break;
        }
        return res;
    }
    if (confirmExit_) {
        const Rect yes{196, 214, 110, 28}, no{334, 214, 110, 28};
        if (in.left || in.right || in.keyUp || in.keyDown) modalSel_ = 1 - modalSel_;
        if (in.enter) {
            if (modalSel_ == 0) res.kind = Result::Kind::Quit;
            confirmExit_ = false;
        }
        if (in.released) {
            if (yes.contains(in.mx, in.my) && yes.contains(in.pmx, in.pmy)) res.kind = Result::Kind::Quit;
            if ((yes.contains(in.mx, in.my) && yes.contains(in.pmx, in.pmy)) || (no.contains(in.mx, in.my) && no.contains(in.pmx, in.pmy)))
                confirmExit_ = false;
        }
        return res;
    }
    if (controls_) {
        if (in.enter || in.released) controls_ = false;
        return res;
    }

    // ---- legal notice: only an explicit "Continue" moves on (no "any key"), "Exit" leaves the app
    if (state_ == State::Disclaimer) {
        const Rect go{136, 308, 170, 30}, quit{334, 308, 170, 30};
        if (loading_) {
            // data is still being indexed: the notice stays up (that is the point of the splash); only Exit works
            const Rect lq{250, 330, 140, 24};
            if (in.released && lq.contains(in.mx, in.my) && lq.contains(in.pmx, in.pmy)) res.kind = Result::Kind::Quit;
            return res;
        }
        if (in.left || in.right || in.keyUp || in.keyDown) noticeSel_ = 1 - noticeSel_;
        if (in.enter) {
            if (noticeSel_ == 0) showWarning();
            else res.kind = Result::Kind::Quit;
        }
        if (in.released) {
            if (go.contains(in.mx, in.my) && go.contains(in.pmx, in.pmy)) showWarning();
            else if (quit.contains(in.mx, in.my) && quit.contains(in.pmx, in.pmy)) res.kind = Result::Kind::Quit;
        }
        return res;
    }

    // ---- photosensitivity warning: it runs its own animation (6 s); holding any button / finger for a moment skips it
    if (state_ == State::Warning) {
        warnHold_ = in.down ? warnHold_ + frameDt_ : 0;
        const int age = timeMs_ - warnT0_;
        if (warnHold_ >= 700 || (anyKey && age >= 800) || age >= kWarningMs) endWarning();
        return res;
    }

    // ---- loading screen: a fixed time (the animation has to be seen once), a tap may cut it short after a second
    if (state_ == State::Splash) {
        const int age = timeMs_ - splashT0_;
        if (age >= kSplashMs || (age >= 1000 && (in.pressed || anyKey))) beginAttract();
        return res;
    }

    // ---- opening montage (AttractMode.gbs): any tap / key skips to the title, the end of the montage leads there too
    if (state_ == State::Attract) {
        if (attractPending_) return res;   // not loaded yet (showAttract from a smoke run): the first draw starts it
        stepAttract();
        if (timeMs_ - attractT0_ >= 400 && (in.pressed || anyKey)) attract_.skip();
        if (attract_.finished()) state_ = State::Menu;
        return res;
    }

    // ---- title
    if (state_ == State::Title) {
        if (in.pressed || anyKey) state_ = State::Menu;
        return res;
    }

    // ---- character select
    if (state_ == State::CharSelect) {
        const CharSelect::Out o = charSel_.update(in, timeMs_);
        if (audio_ && gd_) {   // the sounds the screen's own animations cross (CharSelectSnd_*, stage arrows ...)
            for (const auto& snd : charSel_.takeSounds()) {
                auto it = sfxByName_.find(snd.first);
                if (it == sfxByName_.end()) {
                    const int id = audio_->loadSfx(gd_->vfs(), snd.first);
                    if (id < 0) std::printf("[charselect] sound not found: %s\n", snd.first.c_str());
                    it = sfxByName_.emplace(snd.first, id >= 0 ? id : -2).first;
                } 
                if (it->second >= 0) audio_->playSfx(it->second, snd.second);
            }
        }
        switch (o.kind) {
            case CharSelect::Out::Kind::Back:
                if (pickingP2_) {   // back to player 1's pick
                    pickingP2_ = false;
                    showCharSelect(false, 1);
                } else {
                    state_ = State::Menu;
                }
                break;
            case CharSelect::Out::Kind::Moved: sfx(sfxMove_); break;
            case CharSelect::Out::Kind::Step: sfx(sfxEnter_); break;
            case CharSelect::Out::Kind::Edit: openColourEditor(o.name, o.palette); break;
            case CharSelect::Out::Kind::Locked:
                sfx(sfxNope_);
                say(own("cslocked"));
                break;
            case CharSelect::Out::Kind::Confirm: {
                sfx(sfxEnter_);
                if (csStory_) {   // Story Mode: one fighter, no stage; the story screen takes it from here
                    res.kind = Result::Kind::Story;
                    res.storyChar = o.name;
                    state_ = State::Menu;
                    break;
                }
                if (kFightEnabled && !charSel_.story()) {
                    std::string pal;   // real path of the colours picked (the game's own or a mods/pal one)
                    if (const auto it = palChoices_.find(o.name); it != palChoices_.end() && o.palette >= 0 && o.palette < static_cast<int>(it->second.size()))
                        pal = it->second[static_cast<size_t>(o.palette)].path;
                    if (twoPicks_ && !pickingP2_) {   // the same selector again for P2
                        pick1_ = o.name;
                        pickStage_ = o.stage;
                        pickPal1_ = pal;
                        pickingP2_ = true;
                        showCharSelect(false, 1);   // starts at the fighter step: team size and stage are already chosen
                        break;
                    }
                    res.kind = Result::Kind::Play;
                    res.mode = fightMode_;
                    if (pickingP2_) {
                        res.p1 = pick1_;
                        res.p2 = o.name;
                        res.p2PalPath = pal;
                        res.stage = pickStage_;
                        res.p1PalPath = pickPal1_;
                    } else {
                        res.p1 = o.name;
                        res.p2 = "random";
                        res.stage = o.stage;
                        res.p1PalPath = pal;
                    }
                    if (res.p2 == "random") res.p2.clear();
                    pickingP2_ = false;
                    state_ = State::Menu;
                    break;
                }
                char buf[160];
                std::string nm = o.name;
                if (!nm.empty()) nm[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(nm[0])));
                if (!o.stage.empty()) nm += " / " + o.stage;
                if (o.teamSize > 1) nm += " x" + std::to_string(o.teamSize);
                if (o.palette > 0 && !o.paletteLabel.empty()) nm += " [" + (o.paletteLabel.rfind("mod:", 0) == 0 ? o.paletteLabel.substr(4) : o.paletteLabel) + "]";
                std::snprintf(buf, sizeof buf, own("csconfirm").c_str(), nm.c_str());
                say(buf);
                break;
            }
            case CharSelect::Out::Kind::None: break;
        }
        return res;
    }

    // ---- lists
    Page& p = stack_.back();
    const int n = static_cast<int>(p.vis.size());
    layoutRows(p);
    const int rowH = std::max(1, rowPx_);
    const int visibleRows = visibleRows_;
    const float maxScroll = static_cast<float>(std::max(0, n - visibleRows));

    if (n > 0) {
        if (in.keyUp) p.sel = (p.sel + n - 1) % n;
        if (in.keyDown) p.sel = (p.sel + 1) % n;
        p.sel = std::clamp(p.sel, 0, n - 1);
        // keep the selection on screen
        if (p.sel < p.scroll) p.scroll = static_cast<float>(p.sel);
        if (p.sel > p.scroll + visibleRows - 1) p.scroll = static_cast<float>(p.sel - visibleRows + 1);
    }
    p.scroll -= static_cast<float>(in.wheel);

    auto rowAt = [&](int x, int y) {
        if (listBox_.w > 0 && !listBox_.contains(x, y)) return -1;
        for (const RowGeom& g : rows_)
            if (g.r.contains(x, y)) return g.vis;
        return -1;
    };
    auto rectOf = [&](int vis) {
        for (const RowGeom& g : rows_)
            if (g.vis == vis) return g.r;
        return Rect{0, 0, 1, 1};
    };

    // drag-scroll (touch / mouse) on long pages
    if (in.pressed) {
        pressRow_ = rowAt(in.pmx, in.pmy);
        dragged_ = false;
    }
    if (in.down && maxScroll > 0 && listBox_.w > 0 && listBox_.contains(in.pmx, in.pmy)) {
        if (std::abs(in.my - in.pmy) > 8) dragged_ = true;
        if (dragged_) p.scroll -= static_cast<float>(in.dragDy) / rowH;
    }
    p.scroll = std::clamp(p.scroll, 0.0f, maxScroll);

    // hover selects with a real mouse; a finger has no hover, so it needs tap-to-select first
    const bool sceneRoot = sceneOk_ && p.root;  // the list re-flows around the pointer, so hover must not select
    if (!in.touchPointer && !sceneRoot && (in.mx != lastMx_ || in.my != lastMy_)) {
        const int r = rowAt(in.mx, in.my);
        if (r >= 0) p.sel = r;
    }
    lastMx_ = in.mx;
    lastMy_ = in.my;

    if (in.left && n > 0 && p.def.options[static_cast<size_t>(p.vis[static_cast<size_t>(p.sel)])].type == MenuOptType::Setting)
        stepSetting(p.def.options[static_cast<size_t>(p.vis[static_cast<size_t>(p.sel)])], -1);
    else if (in.right && n > 0 && p.def.options[static_cast<size_t>(p.vis[static_cast<size_t>(p.sel)])].type == MenuOptType::Setting)
        stepSetting(p.def.options[static_cast<size_t>(p.vis[static_cast<size_t>(p.sel)])], +1);
    else if (in.enter && n > 0)
        res = activate(p, p.sel, -1, rectOf(p.sel));

    if (in.released && !dragged_ && pressRow_ >= 0) {
        const int r = rowAt(in.mx, in.my);
        if (r == pressRow_) {
            if ((in.touchPointer || sceneRoot) && r != p.sel) {
                p.sel = r;  // first tap only selects (so the description can be read)
            } else {
                p.sel = r;
                res = activate(p, r, in.mx, rectOf(r));
            }
        }
        pressRow_ = -2;
    }
    return res;
}

// ---------------------------------------------------------------------------------------- drawing

void MenuScreen::ensureAssets(SDL_Renderer* r) {
    ren_ = r;
    if (!assetsDirty_) return;
    assetsDirty_ = false;
    bg_.reset(r);
    showcase_.reset(r);
    sceneOk_ = false;
    vBg_ = vMenu_ = vSplash_ = nullptr;
    if (!gd_) return;

    // The game's own UI scenes + textures + fonts, when the UI pack is mounted: they replace the stand-in art below.
    if (loadScenes(r)) return;

    // Backdrop: the first stage whose "2D <texture>" decodes (asg_labs first - verified path).
    std::vector<const LevelInfo*> order;
    if (inv_) {
        for (const LevelInfo& lv : inv_->levels)
            if (lv.name.find("asg_labs_3d") != std::string::npos) order.push_back(&lv);
        for (const LevelInfo& lv : inv_->levels)
            if (std::find(order.begin(), order.end(), &lv) == order.end()) order.push_back(&lv);
    }
    for (const LevelInfo* lv : order) {
        // The .lvl names one texture; when that file is not in the mounted data, fall back to the
        // stage's own backdrop names (same list the Play screen uses).
        std::string stem = lv->name;
        if (const size_t p3d = stem.find("_3d"); p3d != std::string::npos) stem = stem.substr(0, p3d);
        const std::string candidates[] = {baseName(lv->background2D), stem + "_bg.dds", "stage_" + stem + ".dds", stem + ".dds"};
        bool done = false;
        for (const std::string& want : candidates) {
            std::string real;
            if (want.empty() || !gd_->vfs().pick(want, &real)) continue;
            std::vector<uint8_t> data;
            DdsImage img;
            if (!readFile(real, data) || !decodeDds(data, img, nullptr, 4096)) continue;
            {   // stages/textures/2D/*.dds: rotated channels (see unrotateArgb); the menu only needs a screen-sized copy
                std::string lreal = real;
                for (char& c : lreal)
                    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
                if (lreal.find("/2d/") != std::string::npos || lreal.find("\\2d\\") != std::string::npos) unrotateArgb(img.rgba.data(), img.width, img.height);
                if (img.width > 1280) {
                    int sw = 0, sh = 0;
                    std::vector<uint8_t> small = shrinkRgba(img.rgba.data(), img.width, img.height, (img.width + 1279) / 1280, sw, sh);
                    if (!small.empty()) {
                        img.rgba = std::move(small);
                        img.width = sw;
                        img.height = sh;
                    }
                }
            }
            if (bg_.set(r, img)) {
                std::printf("[menu] backdrop: %s\n", real.c_str());
                done = true;
                break;
            }
        }
        if (done) break;
    }
    // Showcase fighter: the first character with real sprite data.
    if (inv_ && !inv_->characters.empty()) {
        std::string e;
        if (!showcase_.load(*gd_, inv_->characters.front(), 0, r, &e)) std::printf("[menu] showcase: %s\n", e.c_str());
    }
}

void MenuScreen::drawBackdrop(Ui& ui, SDL_Renderer* r) {
    // vertical gradient base (also what you see with no data)
    for (int y = 0; y < kH; y += 4) {
        const double t = y / static_cast<double>(kH);
        ui.fill(Rect{0, y, kW, 4}, Col{static_cast<uint8_t>(18 + 22 * t), static_cast<uint8_t>(14 + 10 * t),
                                       static_cast<uint8_t>(28 + 26 * t), 255});
    }
    if (bg_.tex) {
        const double s = std::max(kW / static_cast<double>(bg_.w), kH / static_cast<double>(bg_.h));
        const int dw = static_cast<int>(bg_.w * s), dh = static_cast<int>(bg_.h * s);
        const double pan = std::sin(timeMs_ / 5200.0) * 0.5 + 0.5;  // slow drift across the overflow
        SDL_Rect dst{static_cast<int>(-(dw - kW) * pan), -(dh - kH) / 2, dw, dh};
        SDL_RenderCopy(r, bg_.tex, nullptr, &dst);
    }
    // darken the left (menu) side and the bottom (description), keep the right side lively
    for (int x = 0; x < 400; x += 8) ui.fill(Rect{x, 0, 8, kH}, Col{6, 6, 12, static_cast<uint8_t>(190 - x * 190 / 400)});
    ui.fill(Rect{0, kDescY - 6, kW, kH - kDescY + 6}, Col{6, 6, 12, 200});
    ui.fill(Rect{0, 0, kW, 3}, kAccent);
}

void MenuScreen::drawShowcase(Ui& ui, SDL_Renderer* r) {
    if (!showcase_.hasMoves()) return;
    // the character's own "Stand" animation, played at the game's 60 ticks per second
    const fight::MoveSet& ms = showcase_.moves();
    const int total = ms.ticks(ms.id.stand);
    if (total <= 0) return;
    const int tick = static_cast<int>((static_cast<long long>(timeMs_) * 60 / 1000) % total);
    const uint16_t fi = ms.frameAt(ms.id.stand, tick);
    if (fi == fight::kNoFrame) return;
    const double s = 250.0 / 330.0;  // ~330 sprite pixels is a standing fighter
    ui.disc(500, 292, static_cast<int>(95 * s), 7, Col{0, 0, 0, 110});
    showcase_.drawFrame(r, fi, 500, 292, s, true);
}

void MenuScreen::drawDisclaimer(Ui& ui) {
    // Splash: black -> wine-red gradient with a slow moving glow, red text.  It is drawn from the very first
    // frame (before any game data is loaded), so it only uses the built-in font.
    for (int y = 0; y < kH; y += 2) {
        const double t = y / static_cast<double>(kH);
        const double glow = 0.5 + 0.5 * std::sin(timeMs_ / 1400.0 - t * 4.0);
        const int r = static_cast<int>(10 + 70 * t * t + 14 * glow * t);
        const int g = static_cast<int>(2 + 6 * t);
        const int b = static_cast<int>(8 + 16 * t);
        ui.fill(Rect{0, y, kW, 2}, Col{static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b), 255});
    }
    const Col red{255, 84, 84, 255}, hot{255, 52, 52, 255}, warm{255, 142, 118, 255};
    ui.fill(Rect{0, 0, kW, 3}, hot);
    shadowText(ui, 24, 12, own("dis.title"), hot, 2);
    {
        const std::string dev = own("dev");   // who made this loader (also the name on the app's signing certificate)
        ui.text(kW - 24 - Ui::textW(dev), 18, dev, warm);
    }
    ui.fill(Rect{24, 34, kW - 48, 1}, Col{255, 52, 52, 140});

    int y = 44;
    auto para = [&](const std::string& s, Col c, int width, int indent, int gap) {
        for (const std::string& l : wrap(s, width)) {
            ui.text(24 + indent, y, l, c);
            y += 11;
        }
        y += gap;
    };
    para(own("dis.1"), red, 74, 0, 6);
    para(own("dis.2"), red, 74, 0, 7);
    para(own("dis.h"), warm, 74, 0, 2);
    for (const char* k : {"dis.a", "dis.b", "dis.c"}) para(own(k), red, 70, 10, 1);
    y += 5;
    para(own("dis.5"), warm, 74, 0, 4);
    const std::string copy = loc_.trOr("Copyright", "");
    if (!copy.empty()) ui.text(24, 292, Ui::fit(copy, kW - 48), Col{200, 70, 70, 255});

    if (loading_) {
        // indeterminate bar + status; Continue appears once the data has been indexed
        const Rect bar{140, 314, 360, 8};
        ui.fill(bar, Col{30, 4, 8, 255});
        const int seg = 110;
        const int x = bar.x + static_cast<int>(std::fmod(timeMs_ / 5.0, bar.w + seg)) - seg;
        const int x0 = std::max(bar.x, x), x1 = std::min(bar.x + bar.w, x + seg);
        if (x1 > x0) ui.fill(Rect{x0, bar.y, x1 - x0, bar.h}, hot);
        ui.outline(bar, Col{255, 52, 52, 160});
        const std::string msg = own("loading");
        ui.text((kW - Ui::textW(msg)) / 2, 296 + 0, msg, warm);
        const Rect quit{250, 330, 140, 24};
        ui.fill(quit, Col{40, 8, 12, 230});
        ui.outline(quit, Col{255, 52, 52, 160});
        const std::string lab = own("dis.exit");
        ui.text(quit.x + (quit.w - Ui::textW(lab)) / 2, quit.y + 8, lab, red);
        return;
    }
    const Rect go{136, 308, 170, 30}, quit{334, 308, 170, 30};
    for (int i = 0; i < 2; ++i) {
        const Rect b = i == 0 ? go : quit;
        ui.fill(b, noticeSel_ == i ? Col{200, 28, 40, 255} : Col{40, 8, 12, 230});
        ui.outline(b, noticeSel_ == i ? Col{255, 190, 190, 255} : Col{255, 52, 52, 160});
        const std::string lab = own(i == 0 ? "dis.go" : "dis.exit");
        ui.text(b.x + (b.w - Ui::textW(lab, 2)) / 2, b.y + 7, lab, noticeSel_ == i ? Col{255, 240, 240, 255} : red, 2);
    }
}

void MenuScreen::drawTitle(Ui& ui) {
    shadowText(ui, kListX, 96, "SKULLGIRLS", kText, 5);
    shadowText(ui, kListX, 140, "2ND ENCORE", kAccent, 3);
    if ((timeMs_ / 520) % 2 == 0) {
        // the game's own prompt ("* PRESS ANY BUTTON TO START *"); own text only when its data is missing
        const bool touch = SDL_GetNumTouchDevices() > 0;
        const std::string go = loc_.trOr("Press Any Button to Start", own(touch ? "touch" : "enter"));
        shadowText(ui, kListX, 236, Ui::fit(go, 380), kText, 1 + (Ui::glyphCount(go) * 16 <= 380 ? 1 : 0));
    }
    const std::string copy = loc_.trOr("Copyright", "");
    const std::string foot = own("unofficial");
    ui.text(kListX, copy.empty() ? 336 : 322, Ui::fit(foot, kW - 48), kDim);
    if (!copy.empty()) ui.text(kListX, 336, Ui::fit(copy, kW - 48), kDim);
}

void MenuScreen::drawList(Ui& ui, Page& p) {
    const int rowH = rowHeight(p);
    const int rowW = p.dense ? 372 : 304;
    const int n = static_cast<int>(p.vis.size());
    const bool big = !p.dense;

    // header: logo (small) + page title
    shadowText(ui, kListX, 12, "SKULLGIRLS", kText, 4);
    shadowText(ui, kListX, 48, "2ND ENCORE", kAccent, 2);
    const std::string title = pageTitle();
    if (!title.empty()) shadowText(ui, kListX, 78, Ui::fit(title, 300), kWarn);

    if (n == 0) {
        ui.text(kListX, kListY + 8, own("empty"), kDim);
        return;
    }
    SDL_Rect clip{kListX - 4, kListY, rowW + 8, kListH};
    SDL_RenderSetClipRect(ui.r, &clip);
    for (int k = 0; k < n; ++k) {
        const int y = kListY + static_cast<int>(std::lround((k - p.scroll) * rowH));
        if (y + rowH < kListY || y > kListY + kListH) continue;
        const MenuOption& o = p.def.options[static_cast<size_t>(p.vis[static_cast<size_t>(k)])];
        const bool sel = k == p.sel;
        const bool usable = optionUsable(o);

        const Rect row{kListX, y, rowW, rowH - (big ? 3 : 1)};
        if (sel) {
            ui.fill(row, withAlpha(kAccent, 215));
            ui.fill(Rect{row.x, row.y, 4, row.h}, kText);
        } else {
            ui.fill(row, Col{10, 10, 18, 120});
        }
        const Col fg = !usable ? Col{128, 128, 148, 255} : kText;
        int scale = big ? 2 : 1;
        {
            std::string probe = L(o.text);
            if (o.type == MenuOptType::Setting) probe += "< " + L(settingValue(o.keyword)) + " >  ";
            if (scale == 2 && Ui::glyphCount(probe) * 16 > row.w - 28) scale = 1;
        }
        const int ty = row.y + (row.h - 8 * scale) / 2;
        if (o.type == MenuOptType::Setting) {
            std::string val = L(settingValue(o.keyword));
            if (val.empty() && !o.choices.empty()) val = L(o.choices.front());
            const std::string right = "< " + val + " >";
            const int rw = Ui::textW(right);
            ui.text(row.x + row.w - rw - 8, row.y + (row.h - 8) / 2, right, sel ? kText : kWarn);
            ui.text(row.x + 12, ty, Ui::fit(L(o.text), row.w - rw - 36 - (big ? 0 : 0)), fg, scale);
        } else {
            std::string label = L(o.text);
            if (o.type == MenuOptType::Submenu && !(label.size() >= 3 && label.compare(label.size() - 3, 3, "...") == 0)) label += "...";
            ui.text(row.x + 12, ty, Ui::fit(label, row.w - 24), fg, scale);
        }
    }
    SDL_RenderSetClipRect(ui.r, nullptr);

    // scrollbar for long pages
    const int visibleRows = kListH / rowH;
    if (n > visibleRows) {
        const int bh = std::max(16, kListH * visibleRows / n);
        const int by = kListY + static_cast<int>((kListH - bh) * (p.scroll / std::max(1, n - visibleRows)));
        ui.fill(Rect{kListX + rowW + 4, kListY, 3, kListH}, Col{255, 255, 255, 30});
        ui.fill(Rect{kListX + rowW + 4, by, 3, bh}, kAccent);
    }
}

void MenuScreen::drawDescription(Ui& ui, Page& p) {
    ui.fill(Rect{12, kDescY, kW - 24, kDescH}, Col{16, 16, 26, 230});
    ui.outline(Rect{12, kDescY, kW - 24, kDescH}, kLine);
    std::vector<std::pair<std::string, Col>> lines;
    for (const auto& item : descriptionLines(p)) {
        const Col c = item.second == 1 ? kWarn : item.second == 2 ? kDim : kText;
        for (const std::string& l : wrap(item.first, 72)) lines.emplace_back(l, c);
    }
    int y = kDescY + 5;
    for (size_t i = 0; i < lines.size() && i < 3; ++i) {
        ui.text(20, y, Ui::fit(lines[i].first, kW - 40), lines[i].second);
        y += 11;
    }
    const bool touch = SDL_GetNumTouchDevices() > 0;
    const std::string hint = own(touch ? "hint.touch" : "hint.keys");
    ui.text(16, 347, Ui::fit(hint, kW - 32), kDim);
}

void MenuScreen::drawModals(Ui& ui) {
    if (!confirmExit_ && !controls_) return;
    ui.fill(Rect{0, 0, kW, kH}, Col{4, 4, 10, 170});
    if (confirmExit_) {
        const Rect box{150, 110, 340, 150};
        ui.fill(box, kPanel);
        ui.outline(box, kAccent);
        const std::string title = L("Warningtitle");
        shadowText(ui, box.x + (box.w - Ui::textW(title, 2)) / 2, box.y + 12, title, kAccent, 2);
        int y = box.y + 40;
        for (const std::string& l : wrap(L("Confirmation"), 40)) {
            ui.text(box.x + (box.w - Ui::textW(l)) / 2, y, l, kText);
            y += 11;
        }
        const Rect yes{196, 214, 110, 28}, no{334, 214, 110, 28};
        for (int i = 0; i < 2; ++i) {
            const Rect b = i == 0 ? yes : no;
            ui.fill(b, modalSel_ == i ? kAccent : kPanel2);
            ui.outline(b, modalSel_ == i ? kText : kLine);
            const std::string lab = L(i == 0 ? "Yes" : "No");
            ui.text(b.x + (b.w - Ui::textW(lab)) / 2, b.y + 10, lab, kText);
        }
        return;
    }
    const Rect box{60, 70, 520, 200};
    ui.fill(box, kPanel);
    ui.outline(box, kAccent);
    shadowText(ui, box.x + 14, box.y + 12, own("controls"), kAccent, 2);
    int y = box.y + 44;
    for (const char* k : {"c1", "c2", "c3", "c4"}) {
        for (const std::string& l : wrap(own(k), 60)) {
            ui.text(box.x + 14, y, l, kText);
            y += 12;
        }
        y += 8;
    }
    const std::string ok = own("close");
    const Rect b{box.x + box.w - 110, box.y + box.h - 38, 96, 26};
    ui.fill(b, kAccent);
    ui.text(b.x + (b.w - Ui::textW(ok)) / 2, b.y + 9, ok, kText);
}

void MenuScreen::draw(Ui& ui, SDL_Renderer* r) {
    if (state_ == State::Disclaimer) {
        // The splash has its own gradient background.  While the data is still loading it is the only thing
        // drawn; the art (backdrop, showcase fighter) is decoded as soon as loading finishes, so the title
        // screen that follows Continue is ready.
        if (!loading_) ensureAssets(r);
        drawDisclaimer(ui);
        return;
    }
    ensureAssets(r);
    if (pendingCharSelect_ && state_ != State::Disclaimer && state_ != State::Splash) {
        pendingCharSelect_ = false;
        if (!showCharSelect(pendingStory_, pendingStep_)) std::fprintf(stderr, "charselect: Charselect.gbs is not in the mounted data\n");
    }
    if (pendingEdit_ && state_ == State::CharSelect && charSel_.ready()) {
        pendingEdit_ = false;
        if (pendingEditWho_.empty()) openColourEditor(charSel_.cursorName(), charSel_.paletteIndex());
        else openColourEditor(pendingEditWho_, 0);
    }
    if (state_ == State::Warning) {
        drawWarning(ui);
        return;
    }
    if (state_ == State::Splash) {
        // the game's own loading screen; without its files (or without the UI pack) the title follows at once
        if (sceneOk_ && !splashTried_) {
            splashTried_ = true;
            splash_.load(r, gd_->vfs(), langCode_);
        }
        if (sceneOk_ && splash_.ok()) {
            gfx_.setViewport(0, 0, 0.5f);
            splash_.draw(gfx_, timeMs_ - splashT0_);
            return;
        }
        beginAttract();
    }
    if (state_ == State::Attract) {
        if (attractPending_ && sceneOk_) {
            attractPending_ = false;
            beginAttract();   // loads the scene, starts it (and jumps when a smoke run asked for a shot)
        }
        if (state_ == State::Attract && sceneOk_ && attractOk_) {
            gfx_.setViewport(0, 0, 0.5f);
            gfx_.setJapanese(langCode_ == "jp");
            attract_.draw(gfx_);
            gfx_.loadScene("Main.gbs");
            return;
        }
        state_ = State::Title;
    }
    if (sceneOk_) {
        drawScene(ui, r);
        return;
    }
    drawBackdrop(ui, r);
    drawShowcase(ui, r);
    if (state_ == State::Title) {
        drawTitle(ui);
    } else {
        Page& p = stack_.back();
        drawList(ui, p);
        drawDescription(ui, p);
    }
    drawModals(ui);
}

Result MenuScreen::frame(Ui& ui, SDL_Renderer* r, int dtMs) {
    timeMs_ += dtMs;
    frameDt_ = dtMs;
    if (toastMs_ > 0) toastMs_ -= dtMs;
    if (stack_.empty()) stack_.push_back(makeRoot());
    const State st0 = state_;
    const size_t depth0 = stack_.size();
    const int sel0 = stack_.back().sel;
    if (const char* sc = std::getenv("ZDROID_MENU_SCRIPT"); sc && *sc) {
        // smoke captures only: "40:down,100:down,160:enter" presses a key on that frame number (down up left right enter back)
        static int frameNo = 0;
        ++frameNo;
        std::string all = sc;
        size_t from = 0;
        while (from < all.size()) {
            size_t c = all.find(',', from);
            if (c == std::string::npos) c = all.size();
            const std::string item = all.substr(from, c - from);
            from = c + 1;
            const size_t colon = item.find(':');
            if (colon == std::string::npos || std::atoi(item.c_str()) != frameNo) continue;
            const std::string key = item.substr(colon + 1);
            Input& kin = ui.in;
            if (key == "down") kin.keyDown = true;
            else if (key == "up") kin.keyUp = true;
            else if (key == "left") kin.left = true;
            else if (key == "right") kin.right = true;
            else if (key == "enter") kin.enter = true;
            else if (key == "back") onBack();
        }
    }
    const Result res = handleInput(ui);
    if (audio_ && !stack_.empty()) {
        if (st0 == State::CharSelect && state_ == State::Menu) sfx(sfxBack_);
        else if (st0 == State::Menu && state_ == State::Menu) {
            if (stack_.size() > depth0) { if (!(sceneOk_ && depth0 == 1)) sfx(sfxEnter_); }   // (SubMenuIntro carries its own cue)
            else if (stack_.size() < depth0) sfx(sfxBack_);
            else if (stack_.back().sel != sel0) sfx(sfxMove_);
        }
        // notice and loading screen are silent; the montage plays its own tune when the data has it (else silence); title and menu share the theme
        const bool attractTune = state_ == State::Attract && attractTrack_ >= 0;
        const int theme = attractTune ? attractTrack_ : state_ == State::CharSelect ? selectTrack_ : musicTrack_;
        // (no theme of its own in the data = silence, not the last fight's music still playing)
        audio_->playMusic((attractTune || state_ == State::Title || state_ == State::Menu || state_ == State::CharSelect) && theme >= 0, theme);
        applyAudio();
    }
    draw(ui, r);
    return res;
}

}  // namespace menu
}  // namespace zengine
