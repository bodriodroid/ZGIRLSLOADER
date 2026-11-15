// music_player.h - ZGirls Loader: puts a theme of the MusicLibrary (music_lib.h) on the Audio mixer.
//
// One instance for the whole app; the screens ask it for a theme by index (fight, story cue, credits, gallery ...).
// Decoding goes through Audio::loadTrack, which keeps the menu / select themes for good and only the last few of the
// others, so the whole soundtrack can be played without holding it all in memory.  Calling play() every frame is fine.
#pragma once
#include <cstdio>
#include <string>

#include "audio.h"
#include "music_lib.h"
#include "vfs.h"

namespace zengine {

class MusicPlayer {
public:
    void setup(Audio* a, const MusicLibrary* l, const Vfs* v) {
        audio_ = a;
        lib_ = l;
        vfs_ = v;
    }
    bool ready() const { return audio_ && lib_ && vfs_; }
    Audio* audio() const { return audio_; }
    const MusicLibrary* lib() const { return lib_; }
    int count() const { return lib_ ? static_cast<int>(lib_->entries().size()) : 0; }
    const MusicEntry* entry(int idx) const { return lib_ && idx >= 0 && idx < count() ? &lib_->entries()[static_cast<size_t>(idx)] : nullptr; }

    // Decodes the theme into the audio cache when it is not there yet; the Audio track id, -1 = no file / no device.
    // Safe to call from a worker thread (the fight loader does).
    int load(int idx) const {
        const MusicEntry* e = entry(idx);
        if (!ready() || !e || !e->present()) return -1;
        int id = audio_->findTrack(e->loopFile, e->introFile);
        if (id < 0) id = audio_->loadTrack(*vfs_, e->loopFile, e->introFile, e->kind == MusicEntry::Kind::Menu || e->kind == MusicEntry::Kind::Select);
        return id;
    }
    // Plays (or keeps playing) the theme: the old one fades out, the new one starts from its intro.  idx < 0 = nothing changes.
    bool play(int idx) const {
        if (idx < 0) return false;
        const int id = load(idx);
        if (id < 0) return false;
        audio_->playMusic(true, id);
        if (idx != lastLogged_) {   // one line per change: which theme went on, for finding wrong file names
            lastLogged_ = idx;
            const MusicEntry* e = entry(idx);
            std::printf("[music] now playing: %s (%s)\n", e ? e->title.c_str() : "?", e ? e->loopFile.c_str() : "?");
        }
        return true;
    }
    // Music_InterruptIntro: the intro is over now, the loop starts.
    void cutIntro() const {
        if (audio_) audio_->cutIntro();
    }
    void stop() const {
        if (audio_) audio_->playMusic(false);
    }

private:
    mutable int lastLogged_ = -1;
    Audio* audio_ = nullptr;
    const MusicLibrary* lib_ = nullptr;
    const Vfs* vfs_ = nullptr;
};

}  // namespace zengine
