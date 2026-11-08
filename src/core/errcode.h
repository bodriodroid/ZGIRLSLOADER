// errcode.h - the numbered error codes of ZGirls Loader and the crash marker behind code 1 (C++17, no platform dependencies
// except <signal.h> on unix).
//
//   1  CRASH            the runtime died: a caught exception, std::terminate, or a fatal signal in the previous run
//   2  NO AUDIO         the runtime runs but is NOT using the voices / music (no .wav found, no tracks, no output device)
//   3  NO DATA          the runtime runs but is NOT using the game's other files (characters, UI scenes, stages, textures)
//                       -> the "Lite" experience: not fatal, but it changes how the game plays
//   4  MOD DAMAGED      a mod / pack / stage folder holds a file that is empty, truncated or does not parse; the folder is not mounted
//   5  RUNTIME ALTERED  something is changing the runtime's own behaviour: native code inside a mod, a link leaving the mod folder,
//                       or a state-integrity check of the runtime failed; the folder is not mounted
//
// The display (the red corner at the bottom of the screen, seven-segment digits) lives in the app layer (err_overlay.h); this
// file only keeps the bookkeeping, so the same codes work in the command-line tools and the tests.
#pragma once
#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace zengine {
namespace err {

enum Code : int { kNone = 0, kCrash = 1, kNoAudio = 2, kNoData = 3, kModDamaged = 4, kRuntimeAltered = 5 };
constexpr int kCodeCount = 5;

const char* name(Code c);                      // "crash", "no-audio", ...
const char* meaning(Code c, bool spanish);     // one short sentence
std::string displayText(Code c);               // what the seven-segment display shows: "Err 3"
inline bool isFatal(Code c) { return c == kCrash; }

// Codes that are currently raised, each with the detail lines that explain it (de-duplicated, capped).
class Registry {
public:
    void raise(Code c, const std::string& detail = std::string());
    void clear(Code c);
    void clearAll();
    bool any() const;
    bool has(Code c) const;
    std::vector<Code> active() const;                  // ascending: the most severe (1) first
    Code primary() const;                              // kNone when nothing is raised
    std::vector<std::string> details(Code c) const;
    uint32_t generation() const;                       // changes whenever the set or a detail changes

private:
    static constexpr size_t kMaxDetails = 24;
    mutable std::mutex m_;
    std::array<std::vector<std::string>, kCodeCount + 1> detail_{};
    std::array<bool, kCodeCount + 1> on_{};
    uint32_t gen_ = 0;
};

// The process-wide registry the app and the loaders report into.
Registry& global();

// ---- crash marker (code 1)
// beginSession() looks at the marker the previous run left at `markerPath`, then writes a fresh "running" marker:
//   previous run wrote "crash ..."  -> kCrash is raised in `reg` with that text (the previous run really died in front of the player)
//   previous run left "running"     -> no code (the OS may simply have killed a backgrounded app); the text is returned in
//                                      `uncleanNote` so the debug display can mention it
// endSession() removes the marker (clean exit).  markBackground() rewrites it as "background" so a kill while the app sits in the
// background does not look like a crash.
struct SessionInfo {
    bool previousCrash = false;
    std::string crashText;      // what the handler wrote
    std::string uncleanNote;    // "running" marker found, no crash text
};
SessionInfo beginSession(const std::string& markerPath, Registry& reg);
void endSession();
void markBackground();
void markForeground();

// Fatal signal / std::terminate handlers: they write "crash <what>" into the marker (async-signal-safe) and then let the default
// action go ahead.  Only meaningful after beginSession().
void installCrashHandlers();

// A caught exception in the main loop: the marker says "crash exception: <what>" and kCrash is raised in the global registry.
void reportFatal(const std::string& what);

}  // namespace err
}  // namespace zengine
