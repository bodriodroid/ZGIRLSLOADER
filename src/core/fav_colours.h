// fav_colours.h - the player's favourite colours of a fighter, kept as files in the app's own settings folder.
//
//   <dir>/<character>/<name>.zgcol   the keys (colour_edit.h, a small text file)
//   <dir>/<character>/<name>.dds     the baked palette texture the fight and the selector read (written together with it)
//
// Not a mod: nothing here goes through the mod loader, the mods folders or the game's files; deleting the folder deletes the favourites and nothing else.
#pragma once
#include <string>
#include <vector>

#include "colour_edit.h"

namespace zengine {
namespace coledit {

struct Fav {
    std::string character, name;
    std::string file;   // the .zgcol
    std::string dds;    // the baked palette texture
};

class Favourites {
public:
    explicit Favourites(std::string dir = std::string()) : dir_(std::move(dir)) {}
    bool enabled() const { return !dir_.empty(); }

    // The favourites of a character, sorted by file name.  One whose palette texture is missing is baked again from its keys, with the lit flags of
    // `litBase` (the character's own palette); without it (and without a texture) it is skipped.
    std::vector<Fav> list(const std::string& character, const Palette* litBase) const;
    // Saves `s` under its name (a new file; a name already taken gets _2, _3 ...) or over `existingFile`.  `baked` is written as the texture.
    bool save(const Scheme& s, const Palette& baked, const std::string& existingFile, Fav& out, std::string* err = nullptr) const;
    // Deletes both files of a favourite.
    bool remove(const Fav& f) const;
    // Reads one favourite file.
    static bool load(const std::string& file, Scheme& out, std::string* err = nullptr);

private:
    std::string dir_;
};

}  // namespace coledit
}  // namespace zengine
