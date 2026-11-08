// zg_progress.cpp - see zg_progress.h
#include "zg_progress.h"

#include <set>

#include <SDL.h>

#include "app_paths.h"
#include "zd_features.h"

#include <fstream>
#include <vector>

namespace zengine {

std::string zgKey(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c >= 'A' && c <= 'Z') out += static_cast<char>(c + 32);
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') out += c;
    }
    return out;
}

bool zgOption(const char* word) {
    const std::string w0 = word ? word : "";
    if (kUnlockAllForTesting && (w0 == "story_all" || w0 == "gallery_all")) return true;
    std::vector<std::string> paths;
#ifdef __ANDROID__
    if (const char* ext = SDL_AndroidGetExternalStoragePath()) paths.push_back(std::string(ext) + "/zgirls_flags.txt");
#endif
    if (const std::string f = apppaths::prefFile("zgirls_flags.txt"); !f.empty()) paths.push_back(f);
    const std::string want = word ? word : "";
    for (const std::string& path : paths) {
        std::ifstream f(path);
        std::string line;
        while (f && std::getline(f, line)) {
            if (const size_t h = line.find('#'); h != std::string::npos) line.erase(h);
            std::string w;
            for (char c : line)
                if (c != ' ' && c != '\t' && c != '\r') w += static_cast<char>((c >= 'A' && c <= 'Z') ? c + 32 : c);
            if (!w.empty() && w == want) return true;
        }
    }
    return false;
}

void ZgProgress::load(const std::string& prefsDir) { ini().open(prefsDir); }

void ZgProgress::save() const { ini().save(); }

void ZgProgress::setInitText(const std::string& text) {
    if (!text.empty()) ini().applyDefaults(text);
}

int ZgProgress::storyPos(const std::string& character) const {
    const std::string k = zgKey(character);
    return k.empty() ? 0 : ini().getInt("story", "pos." + k, 0);
}

void ZgProgress::setStoryPos(const std::string& character, int pos) {
    const std::string k = zgKey(character);
    if (k.empty()) return;
    if (pos > storyPos(k)) ini().setInt("story", "pos." + k, pos);   // keeps the highest value
}

bool ZgProgress::storyDone(const std::string& character) const {
    const std::string k = zgKey(character);
    return !k.empty() && ini().getInt("story", "done." + k, 0) != 0;
}

void ZgProgress::setStoryDone(const std::string& character) {
    const std::string k = zgKey(character);
    if (k.empty()) return;
    SavIni& s = ini();
    s.setAutosave(false);
    s.set("story", "done." + k, "1");
    s.set("progression", "Story" + k, "C");   // the same flag ProgressionInit.txt calls StoryFilia = I -> C
    s.setAutosave(true);
    s.save();
}

int ZgProgress::storiesDone() const {
    int n = 0;
    for (const SavIni::Entry& e : ini().entries("story"))
        if (e.key.compare(0, 5, "done.") == 0 && e.value != "0") ++n;
    return n;
}

bool ZgProgress::arcadeDone(const std::string& character) const {
    const std::string k = zgKey(character);
    return !k.empty() && (flag("Arcade" + k) || flag("Gallery" + k + "_Arcade"));
}

void ZgProgress::setArcadeDone(const std::string& character) {
    const std::string k = zgKey(character);
    if (k.empty()) return;
    setFlag("Arcade" + k);
    setFlag("Gallery" + k + "_Arcade");
}

int ZgProgress::arcadesDone() const {
    std::set<std::string> who;   // one per character, whichever of the two flags says it
    for (const SavIni::Entry& e : ini().entries("progression")) {
        const std::string k = zgKey(e.key);
        std::string name;
        if (k.compare(0, 6, "arcade") == 0) name = k.substr(6);
        else if (k.compare(0, 7, "gallery") == 0 && k.size() > 13 && k.compare(k.size() - 6, 6, "arcade") == 0) name = k.substr(7, k.size() - 13);
        if (!name.empty() && ini().isDone(e.key)) who.insert(name);
    }
    return static_cast<int>(who.size());
}

int ZgProgress::addWin(const std::string& character) {
    const std::string k = zgKey(character);
    return k.empty() ? 0 : ini().addCounter(k + "Wins", 1);
}

bool ZgProgress::flag(const std::string& key) const {
    const std::string k = zgKey(key);
    return !k.empty() && ini().isDone(k);
}

void ZgProgress::setFlag(const std::string& key) {
    const std::string k = zgKey(key);
    if (!k.empty()) ini().unlock(k);
}

}  // namespace zengine
