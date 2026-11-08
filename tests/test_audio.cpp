// Audio tests: the .wav loop chunk, decoding, and the mixer (intro -> loop, fades, effects).  No sound device needed:
// the mixer is driven by hand.
//   ZDROID_TEST_MUSIC  folder with the user's real menu_intro.wav + menu_loop.wav (optional; SKIP without it)
#include <algorithm>
#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "audio.h"
#include "vfs.h"

namespace fs = std::filesystem;
using namespace zengine;

static int g_checks = 0, g_fail = 0, g_skipped = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(c)) {                                                           \
            ++g_fail;                                                         \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
        }                                                                     \
    } while (0)

// ---------------------------------------------------------------------------------------- synthetic wav files

static void put16(std::vector<uint8_t>& v, uint32_t x) { v.push_back(x & 255); v.push_back((x >> 8) & 255); }
static void put32(std::vector<uint8_t>& v, uint32_t x) { put16(v, x & 0xffff); put16(v, x >> 16); }
static void tag(std::vector<uint8_t>& v, const char* t) { v.insert(v.end(), t, t + 4); }

// PCM16 stereo wav; frame i holds {L = base + i, R = -(base + i)}.  loop = {start, endInclusive} adds a "smpl" chunk.
static std::vector<uint8_t> makeWav(uint32_t frames, int rate, int base, const uint32_t* loop = nullptr) {
    std::vector<uint8_t> d;
    tag(d, "RIFF");
    put32(d, 0);  // patched below
    tag(d, "WAVE");
    tag(d, "fmt ");
    put32(d, 16);
    put16(d, 1);
    put16(d, 2);
    put32(d, static_cast<uint32_t>(rate));
    put32(d, static_cast<uint32_t>(rate) * 4);
    put16(d, 4);
    put16(d, 16);
    tag(d, "data");
    put32(d, frames * 4);
    for (uint32_t i = 0; i < frames; ++i) {
        put16(d, static_cast<uint16_t>(static_cast<int16_t>(base + static_cast<int>(i))));
        put16(d, static_cast<uint16_t>(static_cast<int16_t>(-(base + static_cast<int>(i)))));
    }
    if (loop) {
        tag(d, "smpl");
        put32(d, 60);
        for (int i = 0; i < 7; ++i) put32(d, 0);  // manufacturer, product, period, note, fraction, smpte fmt, offset
        put32(d, 1);                              // loop count
        put32(d, 0);                              // sampler data
        put32(d, 0);                              // id
        put32(d, 0);                              // type
        put32(d, loop[0]);
        put32(d, loop[1]);
        put32(d, 0);
        put32(d, 0);
    }
    const uint32_t riff = static_cast<uint32_t>(d.size()) - 8;
    std::memcpy(d.data() + 4, &riff, 4);
    return d;
}

static void writeFile(const fs::path& p, const std::vector<uint8_t>& d) {
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
}

// ---------------------------------------------------------------------------------------- tests

static void testLoopChunk() {
    std::printf("[wav loop chunk]\n");
    const uint32_t loop[2] = {10, 49};
    uint32_t s = 0, e = 0;
    CHECK(wavLoopPoints(makeWav(100, 44100, 0, loop), s, e));
    CHECK(s == 10 && e == 49);
    CHECK(!wavLoopPoints(makeWav(100, 44100, 0), s, e));  // no smpl chunk
    std::vector<uint8_t> junk(64, 7);
    CHECK(!wavLoopPoints(junk, s, e));
    auto cut = makeWav(100, 44100, 0, loop);
    cut.resize(cut.size() - 30);  // truncated chunk must not read past the end
    CHECK(!wavLoopPoints(cut, s, e));
}

static void testDecode() {
    std::printf("[wav decode]\n");
    AudioClip c;
    CHECK(Audio::decodeWav(makeWav(1000, 44100, 100), 44100, c));
    CHECK(c.frames == 1000 && c.pcm.size() == 2000);
    CHECK(c.pcm[0] == 100 && c.pcm[1] == -100 && c.pcm[2 * 999] == 1099);
    CHECK(!c.looped && c.loopStart == 0 && c.loopEnd == 1000);
    const uint32_t loop[2] = {200, 599};
    CHECK(Audio::decodeWav(makeWav(1000, 44100, 0, loop), 44100, c));
    CHECK(c.looped && c.loopStart == 200 && c.loopEnd == 600);
    // a file saved at 44101 Hz plays as it is (no resampling, same length)
    CHECK(Audio::decodeWav(makeWav(1000, 44101, 5), 44100, c));
    CHECK(c.frames == 1000 && c.pcm[0] == 5);
    // a real rate change keeps the loop points proportional
    CHECK(Audio::decodeWav(makeWav(1000, 22050, 0, loop), 44100, c));
    CHECK(c.frames > 1900 && c.frames < 2100);
    CHECK(c.looped && c.loopStart > 380 && c.loopStart < 420);
    std::vector<uint8_t> junk(200, 1);
    CHECK(!Audio::decodeWav(junk, 44100, c));
}

// A 32-bit float WAV the way a DAW writes it: 18-byte fmt chunk, an "acid" chunk before the data, 48 kHz stereo.
static std::vector<uint8_t> makeFloatWav(int frames, int rate, int channels, double freqHz, float amp, bool extensible = false) {
    std::vector<uint8_t> b;
    auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i))); };
    auto put16 = [&](uint32_t v) { b.push_back(static_cast<uint8_t>(v)); b.push_back(static_cast<uint8_t>(v >> 8)); };
    auto tag = [&](const char* t4) { b.insert(b.end(), t4, t4 + 4); };
    tag("RIFF");
    put32(0);   // patched below
    tag("WAVE");
    tag("fmt ");
    put32(extensible ? 40 : 18);
    put16(extensible ? 0xFFFE : 3);
    put16(static_cast<uint32_t>(channels));
    put32(static_cast<uint32_t>(rate));
    put32(static_cast<uint32_t>(rate * channels * 4));
    put16(static_cast<uint32_t>(channels * 4));
    put16(32);
    if (extensible) {
        put16(22), put16(32), put32(3);   // cbSize, valid bits, channel mask
        put16(3);                         // sub-format GUID: IEEE float
        const uint8_t guidTail[14] = {0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71};
        b.insert(b.end(), guidTail, guidTail + 14);
    } else {
        put16(0);   // cbSize
    }
    tag("acid");
    put32(24);
    for (int i = 0; i < 24; ++i) b.push_back(0);
    tag("data");
    put32(static_cast<uint32_t>(frames * channels * 4));
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c) {
            const float v = amp * static_cast<float>(std::sin(2.0 * 3.14159265358979 * freqHz * i / rate));
            uint32_t u;
            std::memcpy(&u, &v, 4);
            put32(u);
        }
    const uint32_t riff = static_cast<uint32_t>(b.size() - 8);
    for (int i = 0; i < 4; ++i) b[4 + i] = static_cast<uint8_t>(riff >> (8 * i));
    return b;
}

static void testFloatWav() {
    std::printf("[wav decode: 32-bit float]\n");
    AudioClip c;
    // 48 kHz -> 44.1 kHz: one second stays one second, the 440 Hz tone keeps its pitch and level
    CHECK(Audio::decodeWav(makeFloatWav(48000, 48000, 2, 440.0, 0.5f), 44100, c));
    CHECK(c.frames >= 44099 && c.frames <= 44101 && c.pcm.size() == static_cast<size_t>(c.frames) * 2);
    int peak = 0, crossings = 0;
    for (uint32_t i = 0; i < c.frames; ++i) {
        peak = std::max(peak, std::abs(static_cast<int>(c.pcm[i * 2])));
        if (i > 0 && (c.pcm[i * 2] < 0) != (c.pcm[(i - 1) * 2] < 0)) ++crossings;
        CHECK(c.pcm[i * 2] == c.pcm[i * 2 + 1]);   // both channels carry the same signal here
        if (g_fail > 20) break;
    }
    CHECK(peak > 16200 && peak < 16500);          // 0.5 * 32767
    CHECK(crossings >= 878 && crossings <= 882);  // 440 Hz = 880 zero crossings per second
    CHECK(!c.looped && c.loopStart == 0 && c.loopEnd == c.frames);
    // mono is played on both sides; a rate one Hz off is left alone; values past full scale are clipped, not wrapped
    CHECK(Audio::decodeWav(makeFloatWav(4410, 44101, 1, 100.0, 1.5f), 44100, c));
    CHECK(c.frames == 4410);
    int hi = 0;
    for (uint32_t i = 0; i < c.frames; ++i) hi = std::max(hi, static_cast<int>(c.pcm[i * 2]));
    CHECK(hi == 32767);
    // WAVE_FORMAT_EXTENSIBLE float
    CHECK(Audio::decodeWav(makeFloatWav(4800, 48000, 2, 1000.0, 0.25f, true), 44100, c));
    CHECK(c.frames >= 4408 && c.frames <= 4412);
    // a data chunk that claims more than the file holds (streamed export): what is there plays, nothing is read past the end
    auto cut = makeFloatWav(4800, 48000, 2, 1000.0, 0.25f);
    cut.resize(cut.size() - 1000);
    CHECK(Audio::decodeWav(cut, 44100, c));
    CHECK(c.frames > 4000 && c.frames < 4408);
    // two frames: too short for the float path, SDL's answers (either way, no crash)
    (void)Audio::decodeWav(makeFloatWav(2, 48000, 2, 1000.0, 0.25f), 44100, c);
}

static void testMixer(const fs::path& dir) {
    std::printf("[mixer: intro, loop, fades, effects]\n");
    const uint32_t loop[2] = {100, 299};  // loop frames 100..299 of a 400-frame clip
    writeFile(dir / "loop.wav", makeWav(400, 44100, 1000, loop));
    writeFile(dir / "intro.wav", makeWav(50, 44100, 5000, loop));  // its own smpl chunk must be ignored: played once
    writeFile(dir / "sfx.wav", makeWav(30, 44100, 2000));
    Vfs vfs;
    vfs.mount(dir.string());

    Audio a;  // no device: the mixer is called by hand
    CHECK(a.loadMusic(vfs, "loop.wav", "intro.wav"));
    CHECK(a.hasMusic() && a.intro().frames == 50 && !a.intro().looped);
    CHECK(a.music().looped && a.music().loopStart == 100 && a.music().loopEnd == 300);
    CHECK(a.musicInIntro());
    CHECK(!a.loadMusic(vfs, "missing.wav", "intro.wav"));  // keeps the previous music
    CHECK(a.hasMusic() && a.intro().frames == 50);

    std::vector<int16_t> out(2 * 44100);
    a.mix(out.data(), 100);  // not started: silence
    bool silent = true;
    for (int i = 0; i < 200; ++i) silent = silent && out[static_cast<size_t>(i)] == 0;
    CHECK(silent);

    a.playMusic(true);
    // let the fade finish (0.6 s), then look at the samples; the clip positions advance meanwhile, so restart the track
    a.mix(out.data(), 44100);
    CHECK(a.musicGain() > 0.999f);
    CHECK(!a.musicInIntro());  // 1 s is long past the 50-frame intro
    // the loop part: positions stay inside 100..299
    CHECK(a.musicPosition() >= 100 && a.musicPosition() < 300);

    // restart from silence: stop, let it fade out, start again -> the intro plays from its first frame, fading in
    a.playMusic(false);
    a.mix(out.data(), 44100);
    CHECK(a.musicGain() < 0.001f);
    a.playMusic(true);
    CHECK(a.musicInIntro());
    a.mix(out.data(), 120);
    // frame k of the intro is 5000+k, scaled by the fade gain k/(0.6*44100)
    {
        const float step = 1.0f / (0.6f * 44100.0f);
        bool ok = true;
        for (int k = 1; k < 50; ++k) {
            const float expect = (5000.0f + static_cast<float>(k)) / 32768.0f * (step * static_cast<float>(k + 1)) * 32767.0f;
            ok = ok && std::fabs(static_cast<float>(out[static_cast<size_t>(k) * 2]) - expect) < 1.5f;
        }
        CHECK(ok);
        // the loop begins on the frame right after the intro: loop frame 0 = 1000 (not 5000-ish, not silence)
        const float g50 = step * 51.0f;
        const float expect50 = 1000.0f / 32768.0f * g50 * 32767.0f;
        CHECK(std::fabs(static_cast<float>(out[50 * 2]) - expect50) < 1.5f);
    }

    // seamless wrap: run past the end of the loop and check the frame after loopEnd is loopStart's
    a.playMusic(true);
    a.mix(out.data(), 44100);  // gain is 1 now
    a.mix(out.data(), 2 * 300);
    bool wraps = true;
    for (int i = 1; i < 600; ++i) {
        const int cur = out[static_cast<size_t>(i) * 2], prev = out[static_cast<size_t>(i - 1) * 2];
        // inside the loop the samples go 1100, 1101, ... 1299 then jump back to 1100
        if (cur != prev + 1 && cur != 1100) wraps = false;
        if (cur < 1100 || cur > 1299) wraps = false;
    }
    CHECK(wraps);

    // effects add on top of the music, volumes scale them, and many at once do not wrap around
    const int id = a.loadSfx(vfs, "sfx.wav");
    CHECK(id >= 0 && a.loadSfx(vfs, "sfx.wav") == id);  // loaded once
    CHECK(a.loadSfx(vfs, "nothing.wav") == -1);
    a.playMusic(false);
    a.mix(out.data(), 44100);  // music faded out
    a.setVolumes(1.0f, 1.0f, 0.5f);
    a.playSfx(id);
    a.mix(out.data(), 40);
    CHECK(std::abs(out[0] - 1000) <= 1);   // 2000 * 0.5
    CHECK(std::abs(out[2 * 10] - 1005) <= 1);  // (2000+10) * 0.5
    CHECK(out[2 * 31] == 0);               // finished after 30 frames
    a.setVolumes(0.0f, 1.0f, 1.0f);        // master 0: nothing at all
    a.playSfx(id);
    a.mix(out.data(), 40);
    bool mute = true;
    for (int i = 0; i < 80; ++i) mute = mute && out[static_cast<size_t>(i)] == 0;
    CHECK(mute);
    a.setVolumes(1.0f, 1.0f, 1.0f);
    for (int i = 0; i < 8; ++i) a.playSfx(id, 4.0f);  // far too loud: must clip, not wrap
    a.mix(out.data(), 20);
    bool clipped = true;
    for (int i = 0; i < 40; i += 2) clipped = clipped && out[static_cast<size_t>(i)] > 0 && out[static_cast<size_t>(i + 1)] < 0;
    CHECK(clipped);
}

static void testRealMusic(const fs::path& dir) {
    std::printf("[real menu music]\n");
    Vfs vfs;
    vfs.mount(dir.string());
    Audio a;
    CHECK(a.loadMusic(vfs, "menu_loop.wav", "menu_intro.wav"));
    CHECK(a.intro().frames == 319488);   // 7.24 s, saved at 44101 Hz: played as 44100
    CHECK(a.music().frames == 4233600);  // exactly 96 s
    CHECK(a.music().looped && a.music().loopStart == 0 && a.music().loopEnd == 4233600);
    CHECK(!a.intro().looped);

    // play through the junction and the loop point: no sample-to-sample jump bigger than the music itself produces
    a.playMusic(true);
    std::vector<int16_t> warm(44100 * 2);
    a.mix(warm.data(), 44100);  // fade in (intro plays meanwhile)
    // restart the track to measure from its beginning without a fade
    Audio b;
    CHECK(b.loadMusic(vfs, "menu_loop.wav", "menu_intro.wav"));
    b.playMusic(true);
    std::vector<int16_t> all(static_cast<size_t>(319488 + 4233600 + 4000) * 2);
    // the fade is 0.6 s; skip it by measuring the junctions only (they are far away from it)
    size_t done = 0;
    while (done < all.size() / 2) {
        const size_t n = std::min<size_t>(4096, all.size() / 2 - done);
        b.mix(all.data() + done * 2, static_cast<int>(n));
        done += n;
    }
    auto maxJump = [&](size_t center) {
        int worst = 0;
        for (size_t i = center - 8; i < center + 8; ++i)
            for (int ch = 0; ch < 2; ++ch) worst = std::max(worst, std::abs(all[i * 2 + ch] - all[(i - 1) * 2 + ch]));
        return worst;
    };
    auto typicalJump = [&](size_t center) {  // mean |delta| around the junction, away from it
        double sum = 0;
        int n = 0;
        for (size_t i = center + 200; i < center + 2200; ++i)
            for (int ch = 0; ch < 2; ++ch, ++n) sum += std::abs(all[i * 2 + ch] - all[(i - 1) * 2 + ch]);
        return sum / n;
    };
    const size_t junction = 319488;                 // intro -> loop
    const size_t wrap = 319488 + 4233600;           // loop end -> loop start
    std::printf("  intro->loop: max step %d (typical %.0f);  loop wrap: max step %d (typical %.0f)\n", maxJump(junction), typicalJump(junction),
                maxJump(wrap), typicalJump(wrap));
    CHECK(maxJump(junction) < 4 * static_cast<int>(typicalJump(junction)) + 2000);
    CHECK(maxJump(wrap) < 4 * static_cast<int>(typicalJump(wrap)) + 2000);
    CHECK(a.framesMixed() == 44100);
}


static void testTracks(const fs::path& dir) {
    std::printf("[several themes: menu + character select]\n");
    writeFile(dir / "a_loop.wav", makeWav(400, 44100, 1000));
    writeFile(dir / "a_intro.wav", makeWav(50, 44100, 5000));
    writeFile(dir / "b_loop.wav", makeWav(400, 44100, -3000));
    writeFile(dir / "b_intro.wav", makeWav(60, 44100, -7000));
    Vfs vfs;
    vfs.mount(dir.string());
    Audio a;
    const int ta = a.loadTrack(vfs, "a_loop.wav", "a_intro.wav");
    const int tb = a.loadTrack(vfs, "b_loop.wav", "b_intro.wav");
    CHECK(ta == 0 && tb == 1 && a.trackCount() == 2);
    CHECK(a.loadTrack(vfs, "a_loop.wav", "a_intro.wav") == 0 && a.trackCount() == 2);  // same loop name: replaced, not added
    CHECK(a.loadTrack(vfs, "nope.wav") == -1 && a.trackCount() == 2);
    CHECK(a.currentTrack() == 0 && a.intro().frames == 50);

    std::vector<int16_t> out(2 * 44100);
    a.playMusic(true, ta);
    a.mix(out.data(), 44100);  // well past the 0.6 s fade-in and the 50-frame intro
    CHECK(!a.musicInIntro() && a.musicGain() > 0.99f && a.currentTrack() == 0);
    // ask for the other theme while the first plays: it fades out, then the second starts from its own intro
    a.playMusic(true, tb);
    a.mix(out.data(), 4096);  // 0.093 s: still fading out
    CHECK(a.currentTrack() == 0 && a.musicGain() < 1.0f);
    a.mix(out.data(), 44100);  // fade out (0.6 s) done, swapped, fading back in
    CHECK(a.currentTrack() == 1 && a.intro().frames == 60);
    a.mix(out.data(), 44100);
    CHECK(a.musicGain() > 0.99f && !a.musicInIntro());
    // the loop of theme b is negative: proves the mixer reads b, not a
    long long sum = 0;
    for (int i = 0; i < 200; ++i) sum += out[static_cast<size_t>(i) * 2];
    CHECK(a.music().looped && a.music().loopEnd == 400);   // no smpl chunk: the whole clip repeats
    CHECK(sum < 0);
    // stopped, then started on theme a again: it begins at once (silent -> no fade-out to wait for) from its intro
    a.playMusic(false);
    a.mix(out.data(), 44100);
    CHECK(a.musicGain() < 0.001f);
    a.playMusic(true, ta);
    CHECK(a.currentTrack() == 0 && a.musicInIntro());
}

static void testTrackCache(const fs::path& dir) {
    std::printf("[theme cache: kept themes, the last few others, the playing one is never dropped]\n");
    writeFile(dir / "pin_loop.wav", makeWav(300, 44100, 100));
    for (int i = 0; i < 7; ++i) writeFile(dir / ("t" + std::to_string(i) + "_loop.wav"), makeWav(300, 44100, 200 + i * 10));
    Vfs vfs;
    vfs.mount(dir.string());
    Audio a;
    const int pin = a.loadTrack(vfs, "pin_loop.wav", {}, true);
    CHECK(pin >= 0 && a.findTrack("pin_loop.wav") == pin);
    CHECK(a.findTrack("t0_loop.wav") == -1);
    int id[7];
    for (int i = 0; i < Audio::kMaxCached; ++i) id[i] = a.loadTrack(vfs, ("t" + std::to_string(i) + "_loop.wav").c_str());
    for (int i = 0; i < Audio::kMaxCached; ++i) CHECK(id[i] >= 0 && a.findTrack(("t" + std::to_string(i) + "_loop.wav").c_str()) == id[i]);
    // one more than the cache holds: the oldest goes (t0), the kept one and the newest stay
    id[Audio::kMaxCached] = a.loadTrack(vfs, ("t" + std::to_string(Audio::kMaxCached) + "_loop.wav").c_str());
    CHECK(a.findTrack("t0_loop.wav") == -1);
    CHECK(a.findTrack("t1_loop.wav") == id[1]);
    CHECK(a.findTrack("pin_loop.wav") == pin);
    CHECK(a.findTrack(("t" + std::to_string(Audio::kMaxCached) + "_loop.wav").c_str()) == id[Audio::kMaxCached]);
    // the theme that is wanted survives however old it is
    a.playMusic(true, id[1]);
    id[5] = a.loadTrack(vfs, "t5_loop.wav");
    id[6] = a.loadTrack(vfs, "t6_loop.wav");
    CHECK(a.findTrack("t1_loop.wav") == id[1]);   // wanted: kept
    CHECK(a.findTrack("t2_loop.wav") == -1 || a.findTrack("t3_loop.wav") == -1);   // others made room
    CHECK(a.findTrack("t6_loop.wav") == id[6]);
    // a dropped theme loads again under its old slot name
    const int again = a.loadTrack(vfs, "t0_loop.wav");
    CHECK(again >= 0 && a.findTrack("t0_loop.wav") == again);
    // releaseTrack: not the kept one, not the wanted one
    a.releaseTrack(pin);
    CHECK(a.findTrack("pin_loop.wav") == pin);
    a.releaseTrack(id[1]);
    CHECK(a.findTrack("t1_loop.wav") == id[1]);
    a.releaseTrack(id[6]);
    CHECK(a.findTrack("t6_loop.wav") == -1);
    // playing something that was dropped is harmless (silence, no crash)
    std::vector<int16_t> out(2 * 4096);
    a.playMusic(true, id[6]);
    a.mix(out.data(), 4096);
    CHECK(true);
}

static void testRealSelectMusic(const fs::path& dir) {
    std::printf("[real character select music]\n");
    Vfs vfs;
    vfs.mount(dir.string());
    Audio a;
    const int m = a.loadTrack(vfs, "menu_loop.wav", "menu_intro.wav");
    const int s = a.loadTrack(vfs, "select_loop.wav", "select_intro.wav");
    CHECK(m == 0);
    if (s < 0) {
        std::printf("  (select_loop.wav / select_intro.wav not in the music folder: skipped)\n");
        return;
    }
    CHECK(s == 1);
    a.playMusic(true, s);
    CHECK(a.currentTrack() == 1 && a.musicInIntro() && a.intro().frames > 1000 && a.music().frames > 44100 * 10);
    std::printf("  select: intro %u frames (%.1f s), loop %u frames (%.1f s) %s\n", a.intro().frames, a.intro().frames / 44100.0, a.music().frames,
                a.music().frames / 44100.0, a.music().looped ? "(smpl loop)" : "(whole clip)");
    std::vector<int16_t> out(2 * 4096);
    uint64_t guard = 0;
    while (a.musicInIntro() && guard < 44100ull * 60) {  // plays out the intro and lands in the loop
        a.mix(out.data(), 4096);
        guard += 4096;
    }
    CHECK(!a.musicInIntro() && a.musicGain() > 0.99f);
}

int main() {
    testLoopChunk();
    testDecode();
    testFloatWav();
    const fs::path dir = fs::temp_directory_path() / "zdroid_audio_test";
    fs::create_directories(dir);
    testMixer(dir);
    testTracks(dir);
    testTrackCache(dir);
    fs::remove_all(dir);
    if (const char* m = std::getenv("ZDROID_TEST_MUSIC"); m && *m) {
        testRealMusic(m);
        testRealSelectMusic(m);
    } else {
        std::printf("[real menu music]\n  SKIP set ZDROID_TEST_MUSIC=<folder with menu_intro.wav + menu_loop.wav>\n");
        ++g_skipped;
    }
    std::printf("\n%d checks, %d failed, %d skipped\n", g_checks, g_fail, g_skipped);
    return g_fail ? 1 : 0;
}
