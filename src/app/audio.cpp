// audio.cpp - see audio.h
#include "audio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace zengine {

namespace {

uint32_t rd32(const uint8_t* p) { return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24; }

constexpr float kFadeSeconds = 0.6f;

}  // namespace

bool wavLoopPoints(const std::vector<uint8_t>& wav, uint32_t& start, uint32_t& endInclusive) {
    if (wav.size() < 12 || std::memcmp(wav.data(), "RIFF", 4) != 0 || std::memcmp(wav.data() + 8, "WAVE", 4) != 0) return false;
    size_t p = 12;
    while (p + 8 <= wav.size()) {
        const uint32_t sz = rd32(wav.data() + p + 4);
        const size_t body = p + 8;
        if (std::memcmp(wav.data() + p, "smpl", 4) == 0) {
            // 36 bytes of header (loop count at +28), then 24 bytes per loop {id, type, start, end, fraction, playCount}
            if (sz < 36 || body + 36 > wav.size()) return false;
            const uint32_t loops = rd32(wav.data() + body + 28);
            if (loops == 0 || body + 36 + 24 > wav.size()) return false;
            start = rd32(wav.data() + body + 36 + 8);
            endInclusive = rd32(wav.data() + body + 36 + 12);
            return endInclusive >= start;
        }
        const size_t next = body + sz + (sz & 1u);
        if (next <= p) break;  // overflow guard
        p = next;
    }
    return false;
}

namespace {
uint32_t rd16(const uint8_t* p) { return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8; }

// 32-bit float WAV (what a DAW exports: "Final Remake Loop.wav", 48 kHz stereo, 135 s = 52 MB): converted straight to 16-bit stereo at the
// device rate.  SDL's converter would keep the file, a working copy several times its size and the result at once (~225 MB for that
// file); this keeps the file and the result (~75 MB).  Cubic (Catmull-Rom) resampling.  false = not such a file: the caller uses SDL.
bool decodeFloatWav(const std::vector<uint8_t>& wav, int rate, AudioClip& out, int& srcFreq) {
    if (wav.size() < 44 || std::memcmp(wav.data(), "RIFF", 4) != 0 || std::memcmp(wav.data() + 8, "WAVE", 4) != 0) return false;
    uint32_t tag = 0, ch = 0, freq = 0, bits = 0;
    const uint8_t* data = nullptr;
    size_t dataLen = 0;
    size_t p = 12;
    while (p + 8 <= wav.size()) {
        const uint32_t sz = rd32(wav.data() + p + 4);
        const size_t body = p + 8;
        if (std::memcmp(wav.data() + p, "fmt ", 4) == 0 && sz >= 16 && body + 16 <= wav.size()) {
            tag = rd16(wav.data() + body);
            ch = rd16(wav.data() + body + 2);
            freq = rd32(wav.data() + body + 4);
            bits = rd16(wav.data() + body + 14);
            if (tag == 0xFFFE && sz >= 26 && body + 26 <= wav.size()) tag = rd16(wav.data() + body + 24);   // WAVE_FORMAT_EXTENSIBLE: the sub-format's first word
        } else if (std::memcmp(wav.data() + p, "data", 4) == 0) {
            data = wav.data() + body;
            dataLen = std::min<size_t>(sz, wav.size() - body);   // a writer that streamed the file leaves 0 / 0xFFFFFFFF here
            break;
        }
        const size_t next = body + sz + (sz & 1u);
        if (next <= p) break;
        p = next;
    }
    if (tag != 3 || bits != 32 || (ch != 1 && ch != 2) || freq == 0 || !data) return false;
    const size_t inFrames = dataLen / (4u * ch);
    if (inFrames < 4) return false;
    if (std::abs(static_cast<int>(freq) - rate) <= 2) freq = static_cast<uint32_t>(rate);   // one Hz off: play as it is
    const double step = static_cast<double>(freq) / static_cast<double>(rate);   // input frames per output frame
    const size_t outFrames = static_cast<size_t>(std::llround(static_cast<double>(inFrames) / step));
    if (outFrames == 0) return false;
    AudioClip clip;
    clip.frames = static_cast<uint32_t>(outFrames);
    clip.pcm.resize(outFrames * 2);
    auto sample = [&](long i, uint32_t c) {
        i = std::clamp<long>(i, 0, static_cast<long>(inFrames) - 1);
        float v;
        std::memcpy(&v, data + (static_cast<size_t>(i) * ch + (ch == 2 ? c : 0)) * 4, 4);
        return std::isfinite(v) ? v : 0.0f;
    };
    auto toPcm = [](float v) { return static_cast<int16_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f)); };
    const bool same = freq == static_cast<uint32_t>(rate);
    for (size_t o = 0; o < outFrames; ++o) {
        const double pos = static_cast<double>(o) * step;
        const long i0 = static_cast<long>(pos);
        const float t = static_cast<float>(pos - static_cast<double>(i0));
        for (uint32_t c = 0; c < 2; ++c) {
            float v;
            if (same) {
                v = sample(i0, c);
            } else {
                const float a = sample(i0 - 1, c), b = sample(i0, c), d = sample(i0 + 1, c), e = sample(i0 + 2, c);
                v = b + 0.5f * t * (d - a + t * (2.0f * a - 5.0f * b + 4.0f * d - e + t * (3.0f * (b - d) + e - a)));
            }
            clip.pcm[o * 2 + c] = toPcm(v);
        }
    }
    srcFreq = static_cast<int>(freq);
    out = std::move(clip);
    return true;
}
}  // namespace

bool Audio::decodeWav(const std::vector<uint8_t>& bytes, int rate, AudioClip& out) {
    if (bytes.size() < 44) return false;
    AudioClip clip;
    bool ok = false;
    int srcFreq = 0;
    if (decodeFloatWav(bytes, rate, clip, srcFreq)) {
        ok = true;
    } else {
        SDL_AudioSpec spec;
        Uint8* buf = nullptr;
        Uint32 len = 0;
        SDL_RWops* rw = SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size()));
        if (!rw) return false;
        if (!SDL_LoadWAV_RW(rw, 1, &spec, &buf, &len)) return false;  // freesrc = 1: rw is closed either way
        // Files exported with a rate one Hz off (44101) would go through the resampler for nothing: play them as they are.
        if (std::abs(spec.freq - rate) <= 2) spec.freq = rate;
        SDL_AudioCVT cvt;
        const int built = SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS, 2, rate);
        if (built >= 0 && len > 0) {
            size_t cap = static_cast<size_t>(len) * static_cast<size_t>(std::max(1, cvt.len_mult));
            std::vector<uint8_t> work(cap);
            std::memcpy(work.data(), buf, len);
            cvt.buf = work.data();
            cvt.len = static_cast<int>(len);
            if (built == 0 || SDL_ConvertAudio(&cvt) == 0) {
                const size_t outBytes = built == 0 ? len : static_cast<size_t>(cvt.len_cvt);
                clip.frames = static_cast<uint32_t>(outBytes / 4);
                clip.pcm.resize(static_cast<size_t>(clip.frames) * 2);
                std::memcpy(clip.pcm.data(), work.data(), static_cast<size_t>(clip.frames) * 4);
                ok = clip.frames > 0;
            }
        }
        SDL_FreeWAV(buf);
        srcFreq = spec.freq;
    }
    if (!ok) return false;

    clip.loopStart = 0;
    clip.loopEnd = clip.frames;
    uint32_t ls = 0, le = 0;
    if (wavLoopPoints(bytes, ls, le)) {
        const double k = srcFreq > 0 ? static_cast<double>(rate) / static_cast<double>(srcFreq) : 1.0;
        const uint32_t s = static_cast<uint32_t>(std::llround(static_cast<double>(ls) * k));
        const uint32_t e = static_cast<uint32_t>(std::llround(static_cast<double>(le + 1) * k));
        if (e > s && s < clip.frames) {
            clip.looped = true;
            clip.loopStart = s;
            clip.loopEnd = std::min(e, clip.frames);
        }
    }
    out = std::move(clip);
    return true;
}

bool Audio::open() {
    if (dev_) return true;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::printf("[audio] no audio subsystem: %s\n", SDL_GetError());
        return false;
    }
    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = &Audio::callback;
    want.userdata = this;
    SDL_AudioSpec have;
    SDL_zero(have);
    // allowed_changes = 0: SDL converts to what the device really wants, the mixer always sees 44.1 kHz stereo S16
    dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!dev_) {
        std::printf("[audio] cannot open a device: %s\n", SDL_GetError());
        return false;
    }
    rate_ = want.freq;
    SDL_PauseAudioDevice(dev_, 0);
    std::printf("[audio] device open (%s), %d Hz stereo, buffer %d frames\n", SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "?",
                rate_, static_cast<int>(have.samples));
    return true;
}

void Audio::close() {
    if (!dev_) return;
    SDL_CloseAudioDevice(dev_);
    dev_ = 0;
}

int Audio::loadTrack(const Vfs& vfs, std::string_view loopName, std::string_view introName, bool keep) {
    auto load = [&](std::string_view name, AudioClip& clip) {
        std::vector<uint8_t> bytes;
        if (name.empty() || !vfs.read(name, bytes)) {
            std::printf("[audio] %.*s not found in the mounted folders\n", static_cast<int>(name.size()), name.data());
            return false;
        }
        if (!decodeWav(bytes, rate_, clip)) {
            std::printf("[audio] %.*s: cannot decode (SDL: %s)\n", static_cast<int>(name.size()), name.data(), SDL_GetError());
            return false;
        }
        return true;
    };
    AudioClip loop, intro;
    if (!load(loopName, loop)) {
        std::printf("[audio] %.*s: that theme stays silent\n", static_cast<int>(loopName.size()), loopName.data());
        return -1;
    }
    if (!loop.looped) {  // a theme's loop repeats even when the file carries no loop points: the whole clip
        loop.looped = true;
        loop.loopStart = 0;
        loop.loopEnd = loop.frames;
    }
    if (!introName.empty() && load(introName, intro)) {
        intro.looped = false;  // the exporter writes a loop over the whole intro: it is played once
        intro.loopStart = 0;
        intro.loopEnd = intro.frames;
    } else {
        intro = AudioClip{};
    }
    if (dev_) SDL_LockAudioDevice(dev_);
    int id = -1;
    const std::string key = trackKey(loopName, introName);
    for (size_t k = 0; k < tracks_.size(); ++k)
        if (tracks_[k].key == key) id = static_cast<int>(k);
    if (id < 0) {
        tracks_.push_back(Track{});
        id = static_cast<int>(tracks_.size()) - 1;
    }
    Track& t = tracks_[static_cast<size_t>(id)];
    t.key = key;
    t.loop = std::move(loop);
    t.intro = std::move(intro);
    t.keep = t.keep || keep;
    t.stamp = ++stampCounter_;
    if (cur_ < 0) {
        cur_ = wantTrack_ = id;
    }
    if (cur_ == id) {  // the current theme was replaced (or is the first one): start it from the top
        musicPos_ = 0;
        introPos_ = 0;
        introPhase_ = t.intro.frames > 0;
        inIntro_.store(introPhase_);
        pos_.store(0);
    }
    evictLocked();
    if (dev_) SDL_UnlockAudioDevice(dev_);
    std::printf("[audio] track %d (%.*s): intro %u frames (%.1f s) once, then loop %u frames (%.1f s) from %u to %u%s\n", id,
                static_cast<int>(loopName.size()), loopName.data(), t.intro.frames, static_cast<double>(t.intro.frames) / rate_,
                t.loop.frames, static_cast<double>(t.loop.frames) / rate_, t.loop.loopStart, t.loop.loopEnd,
                t.loop.looped ? " (file's smpl chunk)" : " (whole clip)");
    return id;
}

// Called with the device locked: drops the oldest decoded themes (not kept, not playing / wanted) beyond kMaxCached.
void Audio::evictLocked() {
    for (;;) {
        int loaded = 0, victim = -1;
        for (size_t k = 0; k < tracks_.size(); ++k) {
            const Track& t = tracks_[k];
            if (t.keep || t.loop.frames == 0) continue;
            ++loaded;
            if (static_cast<int>(k) == cur_ || static_cast<int>(k) == wantTrack_) continue;
            if (victim < 0 || t.stamp < tracks_[static_cast<size_t>(victim)].stamp) victim = static_cast<int>(k);
        }
        if (loaded <= kMaxCached || victim < 0) return;
        Track& v = tracks_[static_cast<size_t>(victim)];
        std::printf("[audio] dropping the samples of %s (cache of %d themes)\n", v.key.c_str(), kMaxCached);
        v.loop = AudioClip{};
        v.intro = AudioClip{};
    }
}

std::string Audio::trackKey(std::string_view loopName, std::string_view introName) {
    std::string k(loopName);
    if (!introName.empty()) k += "|" + std::string(introName);
    return k;
}

void Audio::cutIntro() {
    if (dev_) SDL_LockAudioDevice(dev_);
    if (introPhase_ && cur_ >= 0) {   // the loop begins on the next frame, like the end of an intro that ran out
        introPhase_ = false;
        musicPos_ = 0;
        inIntro_.store(false);
    }
    if (dev_) SDL_UnlockAudioDevice(dev_);
}

int Audio::findTrack(std::string_view loopName, std::string_view introName) const {
    if (dev_) SDL_LockAudioDevice(dev_);
    int id = -1;
    const std::string key = trackKey(loopName, introName);
    for (size_t k = 0; k < tracks_.size(); ++k)
        if (tracks_[k].key == key && tracks_[k].loop.frames > 0) id = static_cast<int>(k);
    if (dev_) SDL_UnlockAudioDevice(dev_);
    return id;
}

void Audio::releaseTrack(int id) {
    if (id < 0 || static_cast<size_t>(id) >= tracks_.size()) return;
    if (dev_) SDL_LockAudioDevice(dev_);
    if (id != cur_ && id != wantTrack_ && !tracks_[static_cast<size_t>(id)].keep) {
        tracks_[static_cast<size_t>(id)].loop = AudioClip{};
        tracks_[static_cast<size_t>(id)].intro = AudioClip{};
    }
    if (dev_) SDL_UnlockAudioDevice(dev_);
}

bool Audio::loadMusic(const Vfs& vfs, std::string_view loopName, std::string_view introName) {
    return loadTrack(vfs, loopName, introName) >= 0;
}

const AudioClip& Audio::music() const {
    static const AudioClip none;
    return cur_ >= 0 ? tracks_[static_cast<size_t>(cur_)].loop : none;
}

const AudioClip& Audio::intro() const {
    static const AudioClip none;
    return cur_ >= 0 ? tracks_[static_cast<size_t>(cur_)].intro : none;
}

void Audio::playMusic(bool on, int track) {
    if (dev_) SDL_LockAudioDevice(dev_);
    if (track >= 0 && static_cast<size_t>(track) < tracks_.size()) {
        wantTrack_ = track;
        tracks_[static_cast<size_t>(track)].stamp = ++stampCounter_;
    }
    else if (wantTrack_ < 0) wantTrack_ = cur_;
    const bool silent = !wantMusic_ && musicGain_ <= 0.001f;
    if (on && silent && cur_ >= 0) {  // silent: start the theme from its beginning (the wanted one, at once)
        cur_ = wantTrack_;
        const Track& t = tracks_[static_cast<size_t>(cur_)];
        musicPos_ = 0;
        introPos_ = 0;
        introPhase_ = t.intro.frames > 0;
        inIntro_.store(introPhase_);
    }
    wantMusic_ = on;
    if (dev_) SDL_UnlockAudioDevice(dev_);
}

int Audio::loadSfx(const Vfs& vfs, std::string_view name) {
    for (size_t i = 0; i < sfxNames_.size(); ++i)
        if (sfxNames_[i] == name) return sfx_[i].frames ? static_cast<int>(i) : -1;
    std::vector<uint8_t> bytes;
    AudioClip clip;
    if (!vfs.read(name, bytes) || !decodeWav(bytes, rate_, clip)) clip = AudioClip{};
    clip.looped = false;
    if (dev_) SDL_LockAudioDevice(dev_);
    sfx_.push_back(std::move(clip));
    sfxNames_.emplace_back(name);
    if (dev_) SDL_UnlockAudioDevice(dev_);
    return sfx_.back().frames ? static_cast<int>(sfx_.size()) - 1 : -1;
}

void Audio::playSfx(int id, float gain) {
    if (id < 0 || static_cast<size_t>(id) >= sfx_.size() || !sfx_[static_cast<size_t>(id)].frames) return;
    if (dev_) SDL_LockAudioDevice(dev_);
    Voice* slot = nullptr;
    for (Voice& v : voices_)
        if (v.clip < 0) {
            slot = &v;
            break;
        }
    if (!slot) {  // all busy: steal the one that has played the longest
        slot = &voices_[0];
        for (Voice& v : voices_)
            if (v.pos > slot->pos) slot = &v;
    }
    slot->clip = id;
    slot->pos = 0;
    slot->gain = gain;
    if (dev_) SDL_UnlockAudioDevice(dev_);
}

void Audio::setVolumes(float master, float music, float effects) {
    if (dev_) SDL_LockAudioDevice(dev_);
    masterVol_ = std::clamp(master, 0.0f, 1.0f);
    musicVol_ = std::clamp(music, 0.0f, 1.0f);
    sfxVol_ = std::clamp(effects, 0.0f, 1.0f);
    if (dev_) SDL_UnlockAudioDevice(dev_);
}

void SDLCALL Audio::callback(void* user, Uint8* stream, int len) {
    Audio* a = static_cast<Audio*>(user);
    a->mix(reinterpret_cast<int16_t*>(stream), len / 4);
}

void Audio::mix(int16_t* out, int frames) {
    const bool musicLive = cur_ >= 0 && (wantMusic_ || musicGain_ > 0.0f);
    const float fadeStep = 1.0f / (kFadeSeconds * static_cast<float>(rate_));
    const float mg = masterVol_ * musicVol_, sg = masterVol_ * sfxVol_;
    for (int i = 0; i < frames; ++i) {
        float l = 0, r = 0;
        if (musicLive) {
            const bool switching = wantTrack_ >= 0 && wantTrack_ != cur_;  // another theme was asked for: fade out, swap, fade in
            if (switching && musicGain_ <= 0.0f) {
                cur_ = wantTrack_;
                musicPos_ = 0;
                introPos_ = 0;
                introPhase_ = tracks_[static_cast<size_t>(cur_)].intro.frames > 0;
                inIntro_.store(introPhase_);
            }
            const AudioClip& music_ = tracks_[static_cast<size_t>(cur_)].loop;
            const AudioClip& intro_ = tracks_[static_cast<size_t>(cur_)].intro;
            const bool nowSwitching = wantTrack_ >= 0 && wantTrack_ != cur_;
            const float target = (wantMusic_ && !nowSwitching) ? 1.0f : 0.0f;
            if (musicGain_ < target) musicGain_ = std::min(target, musicGain_ + fadeStep);
            else if (musicGain_ > target) musicGain_ = std::max(target, musicGain_ - fadeStep);
            const float g = musicGain_ * mg * (1.0f / 32768.0f);
            if (introPhase_) {
                if (introPos_ < intro_.frames) {
                    l += static_cast<float>(intro_.pcm[static_cast<size_t>(introPos_) * 2]) * g;
                    r += static_cast<float>(intro_.pcm[static_cast<size_t>(introPos_) * 2 + 1]) * g;
                    ++introPos_;
                }
                if (introPos_ >= intro_.frames) {  // the loop starts on the very next frame: no gap
                    introPhase_ = false;
                    musicPos_ = 0;
                    inIntro_.store(false);
                }
            } else if (music_.frames > 0) {
                if (musicPos_ >= music_.frames) musicPos_ = music_.looped ? music_.loopStart : music_.frames;
                if (musicPos_ < music_.frames) {
                    l += static_cast<float>(music_.pcm[static_cast<size_t>(musicPos_) * 2]) * g;
                    r += static_cast<float>(music_.pcm[static_cast<size_t>(musicPos_) * 2 + 1]) * g;
                    ++musicPos_;
                    const uint32_t end = music_.looped ? music_.loopEnd : music_.frames;
                    if (musicPos_ >= end) musicPos_ = music_.looped ? music_.loopStart : music_.frames;
                }
            }
        }
        for (Voice& v : voices_) {
            if (v.clip < 0) continue;
            const AudioClip& c = sfx_[static_cast<size_t>(v.clip)];
            if (v.pos >= c.frames) {
                v.clip = -1;
                continue;
            }
            const float g = v.gain * sg * (1.0f / 32768.0f);
            l += static_cast<float>(c.pcm[static_cast<size_t>(v.pos) * 2]) * g;
            r += static_cast<float>(c.pcm[static_cast<size_t>(v.pos) * 2 + 1]) * g;
            ++v.pos;
        }
        // clip: two effects on top of the music must not wrap around
        auto shape = [](float x) { return x > 1.0f ? 1.0f : x < -1.0f ? -1.0f : x; };
        out[i * 2] = static_cast<int16_t>(std::lround(shape(l) * 32767.0f));
        out[i * 2 + 1] = static_cast<int16_t>(std::lround(shape(r) * 32767.0f));
    }
    mixed_.fetch_add(static_cast<uint64_t>(frames));
    pos_.store(musicPos_);
    musicGainNow_.store(musicGain_);
}

}  // namespace zengine
