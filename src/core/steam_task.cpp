#include "steam_task.h"

#include <algorithm>
#include <filesystem>

namespace zengine {

namespace {
constexpr size_t kMaxLog = 200;

void wipe(std::string& s) {
    std::fill(s.begin(), s.end(), '\0');
    s.clear();
}
}  // namespace

SteamTask::~SteamTask() {
    cancel();
    join();
}

void SteamTask::join() {
    if (th_.joinable()) th_.join();
}

void SteamTask::pushLog(const std::string& line) {
    std::lock_guard<std::mutex> lk(mu_);
    st_.log.push_back(line);
    if (st_.log.size() > kMaxLog) st_.log.erase(st_.log.begin(), st_.log.begin() + (st_.log.size() - kMaxLog));
}

SteamTaskState SteamTask::snapshot() const {
    std::lock_guard<std::mutex> lk(mu_);
    return st_;
}

void SteamTask::submitCode(const std::string& code) {
    std::lock_guard<std::mutex> lk(mu_);
    if (st_.phase != SteamPhase::NeedGuardCode) return;
    pendingCode_ = code;
    codeReady_ = true;
    cv_.notify_all();
}

void SteamTask::cancel() {
    cancel_ = true;
    std::lock_guard<std::mutex> lk(mu_);
    cv_.notify_all();
}

bool SteamTask::start(const SteamTaskConfig& cfg, std::string password, std::string* err) {
    auto fail = [&](const char* m) {
        if (err) *err = m;
        wipe(password);
        return false;
    };
    if (running_.load()) return fail("a download is already running");
    join();  // reap a finished previous worker
    if (cfg.exe.empty()) return fail("no DepotDownloader executable configured");
    if (cfg.username.empty()) return fail("enter your Steam username");
    if (cfg.installDir.empty()) return fail("no install folder configured");
    {
        std::lock_guard<std::mutex> lk(mu_);
        st_ = SteamTaskState{};
        st_.phase = SteamPhase::Connecting;
        codeReady_ = false;
        pendingCode_.clear();
    }
    cancel_ = false;
    running_ = true;
    th_ = std::thread(&SteamTask::work, this, cfg, std::move(password));
    return true;
}

void SteamTask::work(SteamTaskConfig cfg, std::string password) {
    std::error_code ec;
    std::filesystem::create_directories(cfg.installDir, ec);
    if (!cfg.homeDir.empty()) std::filesystem::create_directories(cfg.homeDir, ec);

    DownloadRequest req;
    req.appId = cfg.appId;
    req.installDir = cfg.installDir;
    req.os = cfg.os;
    req.maxDownloads = cfg.maxDownloads;
    SteamLogin login;
    login.username = cfg.username;
    login.rememberPassword = cfg.rememberPassword;

    std::vector<std::pair<std::string, std::string>> env;
    if (!cfg.homeDir.empty()) {
        env.push_back({"HOME", cfg.homeDir});
        env.push_back({"XDG_DATA_HOME", cfg.homeDir + "/.local/share"});
        env.push_back({"XDG_CONFIG_HOME", cfg.homeDir + "/.config"});
        env.push_back({"DOTNET_BUNDLE_EXTRACT_BASE_DIR", cfg.homeDir + "/.net"});
    }
    env.push_back({"DOTNET_SYSTEM_GLOBALIZATION_INVARIANT", "1"});  // no ICU on Android
    env.push_back({"DOTNET_CLI_TELEMETRY_OPTOUT", "1"});

    std::string firstError;
    bool sentFinished = false;

    RunCallbacks cb;
    cb.onEvent = [&](const Event& e) {
        pushLog(e.text);
        std::lock_guard<std::mutex> lk(mu_);
        switch (e.type) {
            case EventType::Licenses: st_.licenses = e.number; break;
            case EventType::DepotStart:
                st_.depot = e.number;
                st_.phase = SteamPhase::Downloading;
                break;
            case EventType::Progress:
                st_.phase = SteamPhase::Downloading;
                st_.percent = e.percent;
                st_.currentFile = e.file;
                break;
            case EventType::Finished:
                st_.bytes = e.bytes;
                sentFinished = true;
                break;
            case EventType::Error:
                if (firstError.empty()) firstError = e.text;
                break;
            case EventType::Log: break;
        }
    };
    cb.onNotice = [&](const std::string& line) {
        pushLog(line);
        std::lock_guard<std::mutex> lk(mu_);
        if (isMobileConfirmLine(line)) st_.mobileConfirm = true;
        if (isWrongCodeLine(line)) st_.wrongCode = true;
    };
    cb.onPrompt = [&](PromptKind kind, std::string& answer) {
        if (kind == PromptKind::Password) {
            if (password.empty()) {  // e.g. a remembered login whose token expired
                std::lock_guard<std::mutex> lk(mu_);
                firstError = "Steam asked for the password again; enter it and start again";
                return false;
            }
            answer = password;
            wipe(password);  // sent once; never keep it around
            return true;
        }
        // Steam Guard: wait for the UI.
        std::unique_lock<std::mutex> lk(mu_);
        st_.phase = SteamPhase::NeedGuardCode;
        st_.pendingPrompt = kind;
        codeReady_ = false;
        cv_.wait(lk, [&] { return codeReady_ || cancel_.load(); });
        if (cancel_.load()) return false;
        answer = pendingCode_;
        pendingCode_.clear();
        codeReady_ = false;
        st_.pendingPrompt = PromptKind::None;
        st_.wrongCode = false;
        st_.phase = SteamPhase::Connecting;
        return !answer.empty();
    };

    // Only the password is registered as a secret (see steam_proc.h).
    const RunResult rr = runDepotDownloader(cfg.exe, buildDepotDownloaderArgs(req, login), env,
                                            password.empty() ? std::vector<std::string>{} : std::vector<std::string>{password}, cb, &cancel_);
    wipe(password);

    std::lock_guard<std::mutex> lk(mu_);
    st_.exitCode = rr.exitCode;
    st_.pendingPrompt = PromptKind::None;
    if (!rr.error.empty()) {
        st_.phase = SteamPhase::Failed;
        st_.message = rr.error;
    } else if (rr.cancelled || (cancel_.load() && rr.aborted)) {
        st_.phase = SteamPhase::Cancelled;
        st_.message = "Cancelled";
    } else if (rr.aborted) {
        st_.phase = SteamPhase::Failed;
        st_.message = firstError.empty() ? "Cancelled at the prompt" : firstError;
    } else if (rr.exitCode == 0) {
        st_.phase = SteamPhase::Done;
        st_.percent = 100;
        st_.message = sentFinished ? "Download finished" : "Finished (nothing new to download)";
    } else {
        st_.phase = SteamPhase::Failed;
        if (firstError.find("is not available from this account") != std::string::npos)
            st_.message = "This Steam account cannot download the game (it does not own Skullgirls 2nd Encore): " + firstError;
        else if (!firstError.empty()) st_.message = firstError;
        else if (st_.wrongCode) st_.message = "Steam rejected the Steam Guard code";
        else st_.message = "DepotDownloader exited with code " + std::to_string(rr.exitCode);
    }
    running_ = false;
}

}  // namespace zengine
