// audio.h - the menu music loop and the UI sound effects, mixed by one SDL audio callback.
//
// Nothing here ships any game content: the music and the effects are read through the Vfs from the folders the user
// mounted (menu_loop.wav, UISound/*.wav).  SDL 2.30 decodes PCM and MS-ADPCM .wav natively; the loop points come from
// the file's own RIFF "smpl" chunk (SDL ignores it), so a track that loops sample-exact keeps looping sample-exact.
//
// Everything degrades to silence: no audio device, a missing file or an odd format only means nothing is played.
#pragma once
#include <SDL.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "vfs.h"

namespace zengine {

// A decoded clip: interleaved signed 16-bit stereo at the device rate.
struct AudioClip {
    std::vector<int16_t> pcm;
    uint32_t frames = 0;
    uint32_t loopStart = 0;  // first frame of the repeated part
    uint32_t loopEnd = 0;    // one past the last frame of the repeated part (== frames when the whole clip repeats)
    bool looped = false;
};

// RIFF helper (exposed for tests): {loop start, loop end inclusive} of the first loop of a "smpl" chunk.
bool wavLoopPoints(const std::vector<uint8_t>& wav, uint32_t& start, uint32_t& endInclusive);

class Audio {
public:
    Audio() = default;
    ~Audio() { close(); }
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    // Opens the default output (44.1 kHz stereo S16).  false = no sound device; every other call then does nothing.
    bool open();
    void close();
    bool isOpen() const { return dev_ != 0; }
    int sampleRate() const { return rate_; }

    // The menu music: an optional intro that plays once, then the loop that repeats for ever (the game's menu theme
    // ships as menu_intro.wav + menu_loop.wav).  Decodes .wav files found through the Vfs.  Returns false (and keeps the
    // previous music) when the loop is missing or unreadable; a missing intro only means the loop starts at once.
    bool loadMusic(const Vfs& vfs, std::string_view loopName, std::string_view introName = {});
    // Same, but keeps several themes (menu, character select, ...): returns the track id, -1 when the loop could not be
    // loaded (nothing changes then).  Loading a loop name that is already there replaces that track.
    // keep = the theme stays decoded for good (menu, character select); the others are cached (the last few) and the oldest
    // one that is not playing is dropped when more than kMaxCached are loaded, so a long run of fights does not fill the memory.
    int loadTrack(const Vfs& vfs, std::string_view loopName, std::string_view introName = {}, bool keep = false);
    // The id of a theme that is loaded now (by its loop file name), -1 when it is not (or was dropped).
    // The cache key of a theme: its loop file, plus its intro file when it has one (two themes may share a loop and differ in the intro).
    static std::string trackKey(std::string_view loopName, std::string_view introName = {});
    int findTrack(std::string_view loopName, std::string_view introName = {}) const;
    // Music_InterruptIntro: the intro of the playing theme stops now and its loop starts (no effect when no intro is playing).
    void cutIntro();
    // Frees a theme's samples (not the one that plays / is wanted).  The slot and its name stay.
    void releaseTrack(int id);
    static constexpr int kMaxCached = 3;
    size_t trackCount() const { return tracks_.size(); }
    int currentTrack() const { return cur_; }
    bool hasMusic() const { return cur_ >= 0 && tracks_[static_cast<size_t>(cur_)].loop.frames > 0; }
    const AudioClip& music() const;
    const AudioClip& intro() const;
    // Starts / stops the music with a short fade.  Starting while it is silent plays it from the intro again; starting
    // while it is still fading out keeps the position.  `track` picks the theme (-1 = the one that is current); asking
    // for another track while one plays fades the old one out first, then the new one starts from its intro.
    void playMusic(bool on, int track = -1);
    bool musicWanted() const { return wantMusic_; }
    bool musicInIntro() const { return inIntro_.load(); }

    // Sound effects: load once, trigger many times.  -1 = could not load.
    int loadSfx(const Vfs& vfs, std::string_view name);
    void playSfx(int id, float gain = 1.0f);
    float sfxSeconds(int id) const { return (id >= 0 && static_cast<size_t>(id) < sfx_.size() && rate_ > 0) ? static_cast<float>(sfx_[static_cast<size_t>(id)].frames) / static_cast<float>(rate_) : 0.0f; }

    // 0..1 each; master scales both.
    void setVolumes(float master, float music, float effects);

    // Introspection (tests / smoke output)
    uint64_t framesMixed() const { return mixed_.load(); }
    uint32_t musicPosition() const { return pos_.load(); }
    float musicGain() const { return musicGainNow_.load(); }
    size_t sfxCount() const { return sfx_.size(); }

    // Decodes wav bytes into a clip at (rate, S16, 2 ch).  Used by loadMusic/loadSfx; public for tests.
    static bool decodeWav(const std::vector<uint8_t>& bytes, int rate, AudioClip& out);

    // One block of the mixer (what the device callback runs); public so tests can drive it without a device.
    void mix(int16_t* out, int frames);

private:
    struct Voice {
        int clip = -1;
        uint32_t pos = 0;
        float gain = 0;
    };
    static void SDLCALL callback(void* user, Uint8* stream, int len);

    SDL_AudioDeviceID dev_ = 0;
    int rate_ = 44100;
    struct Track {
        std::string key;   // loop file name
        AudioClip loop;    // repeated for ever
        AudioClip intro;   // plays once before the loop (may be empty)
        bool keep = false;       // never dropped
        uint64_t stamp = 0;      // last use, for dropping the oldest
    };
    uint64_t stampCounter_ = 0;
    void evictLocked();
    std::vector<Track> tracks_;
    int cur_ = -1;         // the track the mixer reads
    int wantTrack_ = -1;   // the track playMusic() asked for (differs from cur_ while the old one fades out)
    bool introPhase_ = false;
    uint32_t introPos_ = 0;
    std::atomic<bool> inIntro_{false};
    std::vector<AudioClip> sfx_;
    std::vector<std::string> sfxNames_;
    Voice voices_[8];
    bool wantMusic_ = false;
    float masterVol_ = 1, musicVol_ = 1, sfxVol_ = 1;
    float musicGain_ = 0;  // current fade gain
    uint32_t musicPos_ = 0;
    std::atomic<uint64_t> mixed_{0};
    std::atomic<uint32_t> pos_{0};
    std::atomic<float> musicGainNow_{0};
};

}  // namespace zengine
