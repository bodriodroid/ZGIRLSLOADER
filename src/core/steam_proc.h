// sg_loader core - runs DepotDownloader as a child process and talks to it over pipes (POSIX: Linux, Android, macOS).
//
// Why pipes and not a library: DepotDownloader is a .NET program built on SteamKit2. Running it as a
// child process keeps the loader free of any Steam protocol code and lets the user update it independently.
#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "steam_dl.h"

namespace zengine {

struct RunCallbacks {
    // Every understood output line (secrets already redacted). Called from the runner's thread.
    std::function<void(const Event&)> onEvent;
    // DepotDownloader is waiting for input (password / Steam Guard code). Fill `answer` (no newline)
    // and return true, or return false to abort the run. May block while the UI asks the user.
    std::function<bool(PromptKind, std::string& answer)> onPrompt;
    // Steam Guard notices that need no typed answer (mobile-app confirmation, wrong code).
    std::function<void(const std::string& line)> onNotice;
};

struct RunResult {
    int exitCode = -1;       // child's exit status (128+signal if killed)
    bool cancelled = false;  // stopped because *cancel became true
    bool aborted = false;    // stopped because onPrompt returned false
    std::string error;       // spawn/setup failure, empty otherwise
};

// exe: path to the DepotDownloader executable (or a name found on PATH).
// env: extra variables (e.g. HOME / XDG_DATA_HOME so the remembered login token has a writable home on Android).
// secrets: strings that must never appear in events; every occurrence is replaced with "***".
//   Pass the PASSWORD here. Steam Guard codes are single-use and DepotDownloader never echoes them; being
//   short, registering them would also mask unrelated text (e.g. a byte count that contains the code).
// Note: ignores SIGPIPE process-wide so a dying child cannot kill the host app.
RunResult runDepotDownloader(const std::string& exe, const std::vector<std::string>& args,
                             const std::vector<std::pair<std::string, std::string>>& env,
                             const std::vector<std::string>& secrets, const RunCallbacks& cb,
                             const std::atomic<bool>* cancel = nullptr);

}  // namespace zengine
