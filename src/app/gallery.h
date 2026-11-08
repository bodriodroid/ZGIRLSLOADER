// gallery.h - ZGirls Loader: Extras -> Art Gallery (Galería de Extras), from the user's own game data.
//
// Where the pictures come from (first source that has any):
//   1. ui_artgallery/gallery.ini   blocks of "Name: / Description: / ArtistName: / Tags: / UnlockCriteria: / Path: / CSPath:"
//                                  (the keys the game's executable looks for; Path / CSPath are tried as written, under
//                                  ui_artgallery/Assets/ and under ui_artgallery/, with .dds)
//   2. every .dds under a ui_artgallery/ folder (grouped by sub-folder)
//   3. the story pictures, story_art/Cinematics/<Char>/*.dds (grouped by character)
// Locks follow Story Mode (zg_progress): "OneStory" / "OneArcade" = any story finished, "Gallery<Char>_Story" or a
// story-picture group = that character's story finished.  "gallery_all" in zgirls_flags.txt unlocks everything.
//
// Screens: thumbnail grid (pages of 5 x 3, group tabs) and a full-screen viewer (arrows, swipe, Back).  The backdrop is
// the game's own ArtGallery.gbs "Posterpile_BG" view when it is mounted.  Nothing is bundled.
#pragma once
#include <SDL.h>

#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "game_ui.h"
#include "gamedata.h"
#include "gbs_anim.h"
#include "menu_data.h"
#include "music_player.h"
#include "ui.h"
#include "zg_progress.h"

namespace zengine {
namespace gallery {

struct Item {
    std::string name, desc, artist;
    std::string path;      // real path of the picture (.dds)
    std::string thumb;     // real path of a smaller picture (CSPath), "" = use `path`
    std::string group;     // tab
    std::string criteria;  // UnlockCriteria as written: OneStory / OneArcade / AllStory / AllArcade, or a progression flag name
    bool cutscene = false; // "Type: Cutscene" (shown with the movie frame); "Type: Texture" = a picture
    std::string csPath;    // "CSPath:" - the cutscene script, relative to arcade/story/ (not played here yet)
};

class GalleryScreen {
public:
    // Call after every (re)scan of the data folder.
    void setGameData(const GameData* gd);
    void setPrefsDir(const std::string& dir) { prefsDir_ = dir; }
    // Music: the gallery plays the data's gallery theme (else the menu's), and its MUSIC button opens a jukebox over every
    // theme the data has (fights, cutscenes, menu ...).
    void setMusic(MusicPlayer* m) { music_ = m; }
    void begin(const std::string& lang);
    // false = leave the gallery (back to the menu).
    bool frame(Ui& ui, SDL_Renderer* r, int dtMs);
    // Android back / Escape: closes the viewer (true) or says the gallery can be left (false).
    bool onBack();

private:
    struct Tex {
        SDL_Texture* t = nullptr;
        int w = 0, h = 0;
        bool tried = false;
    };
    void scan();
    void scanIni(const std::string& text);
    void scanFolder(const std::string& marker, bool byChar);
    std::string resolvePic(const std::string& rel) const;
    bool unlocked(const Item& it) const;
    void freeAll();
    Tex load(SDL_Renderer* r, const std::string& realPath, int maxSize);
    void buildGroups();
    void drawGrid(Ui& ui, SDL_Renderer* r, bool& leave);        // the game's ArtGallery view
    void drawViewer(Ui& ui, SDL_Renderer* r);                   // the game's ArtGallery_Viewer view
    void drawGridPlain(Ui& ui, SDL_Renderer* r, bool& leave);   // fallbacks when ArtGallery.gbs is not mounted
    void drawViewerPlain(Ui& ui, SDL_Renderer* r);
    void drawJukebox(Ui& ui, SDL_Renderer* r, bool& leave);   // the music selector
    void openJukebox();
    void playJuke(int entry);
    void bindViews(SDL_Renderer* r);
    void stepAnims(int dtMs);
    void drawBackdrop();
    void startPage(int group, int page, int sel);   // fade the thumbnails out, switch, fade in
    bool cellOrigin(int cell, float& x, float& y) const;   // canvas top-left of a cell's frame group
    void loadFull(SDL_Renderer* r);
    std::vector<size_t> shown() const;   // items of the current tab

    const GameData* gd_ = nullptr;
    MusicPlayer* music_ = nullptr;
    bool juke_ = false;              // the jukebox is open
    int jukeSel_ = 0, jukeTop_ = 0;  // highlighted row / first visible row
    int jukeIdx_ = -1;               // playing: MusicLibrary index, -1 = the gallery's own theme, -2 = stopped by the user
    std::vector<int> jukeList_;      // library indices, the ones with files first
    std::string jukeMsg_;
    int jukeMsgT_ = 0;
    int jukeT_ = 0;                  // frames, drives the little equaliser
    int jukeDragY_ = 0;
    bool jukeDragging_ = false;
    int jukePlayOnOpen_ = 0;         // smoke tests
    std::string prefsDir_, lang_ = "en";
    Localization loc_;
    ZgProgress prog_;
    std::set<std::string> storyChars_;   // zgKey of every character that has a .stm
    bool all_ = false;
    bool scanned_ = false;
    std::vector<Item> items_;
    std::vector<std::string> groups_;
    int group_ = 0;
    int page_ = 0;
    int view_ = -1;          // index into items_ shown full screen, -1 = grid
    std::vector<Tex> thumbs_;
    Tex full_;
    int fullIdx_ = -1;
    SDL_Renderer* ren_ = nullptr;
    int swipeX_ = 0;
    bool swiping_ = false;

    // the game's own scene (ArtGallery.gbs): Posterpile_BG behind, ArtGallery (grid) and ArtGallery_Viewer in front
    GameUi gfx_;
    GbsAnimPlayer bgAnim_, gAnim_, vAnim_;
    const GbsSceneView* bgView_ = nullptr;
    const GbsSceneView* gView_ = nullptr;
    const GbsSceneView* vView_ = nullptr;
    bool bgTried_ = false;
    bool gfxAttached_ = false;
    double bgAcc_ = 0;
    // grid layout taken from the view: 5 columns, each with a frame group per row (A1..A20, row 4 is the scroll spare)
    struct CellNodes {
        const GbsNode* col = nullptr;
        const GbsNode* grp = nullptr;
    };
    CellNodes cells_[20];
    const GbsNode* framesNode_ = nullptr;
    std::unordered_map<uint32_t, std::pair<int, char>> role_;   // node id -> (cell, 'G' group / 'T' thumbnail / 'I' info)
    int sel_ = 0;                  // highlighted cell on the page
    int pendGroup_ = -1, pendPage_ = 0, pendSel_ = 0;   // page change waiting for the fade-out
    bool viewerOn_ = false;
    float selX_ = 0, selY_ = 0;    // the highlight's canvas position (glides)
    bool selInit_ = false;
    int lmx_ = -1, lmy_ = -1;      // last pointer position: hover only follows a moving mouse
};

}  // namespace gallery
}  // namespace zengine
