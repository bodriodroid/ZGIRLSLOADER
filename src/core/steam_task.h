// sg_loader core - one Steam download running on a worker thread, with a state snapshot the UI can poll.
//
// Flow: start() spawns DepotDownloader (steam_proc.h), answers its password prompt from the password given
// to start(), and parks on a Steam Guard prompt until the UI calls submitCode() (or cancel()).
// The password is wiped from this object as soon as it has been sent (best effort: std::string may have been
// copied elsewhere by the caller).
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "steam_dl.h"
#include "steam_proc.h"

namespace zengine {

struct SteamTaskConfig {
    std::string exe;         // DepotDownloader executable
    std::string installDir;  // where the game files go
    std::string username;
    bool rememberPassword = true;
    std::string homeDir;     // writable HOME for the child (Android has none): login token + .NET caches live here
    uint32_t appId = kSkullgirlsAppId;
    std::string os = "windows";
    int maxDownloads = 8;
};

enum class SteamPhase { Idle, Connecting, NeedGuardCode, Downloading, Done, Failed, Cancelled };

struct SteamTaskState {
    SteamPhase phase = SteamPhase::Idle;
    PromptKind pendingPrompt = PromptKind::None;  // set while phase == NeedGuardCode
    bool wrongCode = false;       // the last code was rejected
    bool mobileConfirm = false;   // Steam asked to approve the sign-in in the Steam mobile app
    double percent = 0;
    std::string currentFile;
    uint32_t licenses = 0;
    uint32_t depot = 0;
    uint64_t bytes = 0;           // from the final "Total downloaded" line
    std::string message;          // human-readable result / reason for failure
    int exitCode = -1;
    std::vector<std::string> log; // most recent lines (secrets redacted)
};

class SteamTask {
public:
    ~SteamTask();
    // Returns false (and *err) if a task is already running or the config is unusable.
    bool start(const SteamTaskConfig& cfg, std::string password, std::string* err = nullptr);
    SteamTaskState snapshot() const;
    void submitCode(const std::string& code);
    void cancel();
    bool running() const { return running_.load(); }
    void join();  // waits for the worker (call after it finished or after cancel())

private:
    void work(SteamTaskConfig cfg, std::string password);
    void pushLog(const std::string& line);

    mutable std::mutex mu_;
    std::condition_variable cv_;
    SteamTaskState st_;
    std::string pendingCode_;
    bool codeReady_ = false;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> running_{false};
    std::thread th_;
};

}  // namespace zengine
