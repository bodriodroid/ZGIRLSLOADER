#include "steam_proc.h"

#if defined(_WIN32)

namespace zengine {
RunResult runDepotDownloader(const std::string&, const std::vector<std::string>&,
                             const std::vector<std::pair<std::string, std::string>>&, const std::vector<std::string>&,
                             const RunCallbacks&, const std::atomic<bool>*) {
    RunResult r;
    r.error = "process runner is not implemented on Windows";
    return r;
}
}  // namespace zengine

#else

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <thread>

extern char** environ;

namespace zengine {

namespace {

struct Pipe {
    int rd = -1, wr = -1;
    bool open() {
        int fds[2];
        if (::pipe(fds) != 0) return false;
        rd = fds[0];
        wr = fds[1];
        return true;
    }
    static void closeFd(int& fd) {
        if (fd >= 0) ::close(fd);
        fd = -1;
    }
    void closeBoth() {
        closeFd(rd);
        closeFd(wr);
    }
};

std::string redact(std::string s, const std::vector<std::string>& secrets) {
    for (const std::string& sec : secrets) {
        if (sec.size() < 3) continue;  // too short to redact meaningfully without mangling normal text
        size_t p = 0;
        while ((p = s.find(sec, p)) != std::string::npos) {
            s.replace(p, sec.size(), "***");
            p += 3;
        }
    }
    return s;
}

bool writeAll(int fd, const std::string& data) {
    size_t off = 0;
    while (off < data.size()) {
        ssize_t n = ::write(fd, data.data() + off, data.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        off += static_cast<size_t>(n);
    }
    return true;
}

std::string resolveExe(const std::string& exe) {
    if (exe.find('/') != std::string::npos) return exe;
    const char* path = std::getenv("PATH");
    if (!path) return exe;
    std::string p = path;
    size_t i = 0;
    while (i <= p.size()) {
        size_t j = p.find(':', i);
        if (j == std::string::npos) j = p.size();
        std::string cand = p.substr(i, j - i) + "/" + exe;
        if (::access(cand.c_str(), X_OK) == 0) return cand;
        i = j + 1;
    }
    return exe;
}

struct Stream {
    int fd = -1;
    std::string pending;  // unterminated tail of the output
};

}  // namespace

RunResult runDepotDownloader(const std::string& exeIn, const std::vector<std::string>& args,
                             const std::vector<std::pair<std::string, std::string>>& env,
                             const std::vector<std::string>& secrets, const RunCallbacks& cb,
                             const std::atomic<bool>* cancel) {
    RunResult res;
    ::signal(SIGPIPE, SIG_IGN);

    const std::string exe = resolveExe(exeIn);
    if (::access(exe.c_str(), X_OK) != 0) {
        res.error = "cannot execute '" + exe + "': " + std::strerror(errno);
        return res;
    }

    // Everything the child needs is prepared before fork so the child only calls async-signal-safe functions.
    std::vector<std::string> argvStr;
    argvStr.push_back(exe);
    argvStr.insert(argvStr.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (std::string& s : argvStr) argv.push_back(s.data());
    argv.push_back(nullptr);

    std::vector<std::string> envStr;
    for (char** e = environ; e && *e; ++e) {
        bool overridden = false;
        for (const auto& kv : env) {
            const std::string key = kv.first + "=";
            if (std::strncmp(*e, key.c_str(), key.size()) == 0) { overridden = true; break; }
        }
        if (!overridden) envStr.emplace_back(*e);
    }
    for (const auto& kv : env) envStr.push_back(kv.first + "=" + kv.second);
    std::vector<char*> envp;
    for (std::string& s : envStr) envp.push_back(s.data());
    envp.push_back(nullptr);

    Pipe in, out, err;
    if (!in.open() || !out.open() || !err.open()) {
        res.error = std::string("pipe() failed: ") + std::strerror(errno);
        in.closeBoth(); out.closeBoth(); err.closeBoth();
        return res;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        res.error = std::string("fork() failed: ") + std::strerror(errno);
        in.closeBoth(); out.closeBoth(); err.closeBoth();
        return res;
    }
    if (pid == 0) {
        ::setpgid(0, 0);  // own process group so cancel can stop the whole tree
        ::dup2(in.rd, 0);
        ::dup2(out.wr, 1);
        ::dup2(err.wr, 2);
        in.closeBoth(); out.closeBoth(); err.closeBoth();
        ::execve(exe.c_str(), argv.data(), envp.data());
        _exit(127);
    }
    ::setpgid(pid, pid);  // also from the parent to avoid a race
    Pipe::closeFd(in.rd);
    Pipe::closeFd(out.wr);
    Pipe::closeFd(err.wr);

    Stream streams[2];
    streams[0].fd = out.rd;
    streams[1].fd = err.rd;
    int stdinFd = in.wr;
    bool killed = false;

    auto emitLine = [&](std::string line) {
        line = redact(std::move(line), secrets);
        if (isMobileConfirmLine(line) || isWrongCodeLine(line)) {
            if (cb.onNotice) cb.onNotice(line);
            return;
        }
        if (cb.onEvent) cb.onEvent(parseOutputLine(line));
    };

    auto killGroup = [&](int sig) { ::kill(-pid, sig); };

    auto handleData = [&](Stream& s, const char* data, size_t n) {
        s.pending.append(data, n);
        size_t nl;
        while ((nl = s.pending.find('\n')) != std::string::npos) {
            std::string line = s.pending.substr(0, nl);
            s.pending.erase(0, nl + 1);
            while (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) emitLine(std::move(line));
        }
        const PromptKind kind = classifyPrompt(s.pending);
        if (kind != PromptKind::None) {
            s.pending.clear();
            std::string answer;
            if (!cb.onPrompt || !cb.onPrompt(kind, answer)) {
                res.aborted = true;
                killGroup(SIGTERM);
                killed = true;
                return;
            }
            // The answer may be a secret; it is only ever written to the child's stdin.
            if (stdinFd < 0 || !writeAll(stdinFd, answer + "\n")) {
                res.error = "could not write the answer to the child's stdin";
                killGroup(SIGTERM);
                killed = true;
            }
        }
    };

    char buf[4096];
    while ((streams[0].fd >= 0 || streams[1].fd >= 0) && !killed) {
        if (cancel && cancel->load()) {
            res.cancelled = true;
            killGroup(SIGTERM);
            killed = true;
            break;
        }
        pollfd pfds[2];
        int map[2], n = 0;
        for (int i = 0; i < 2; ++i)
            if (streams[i].fd >= 0) {
                pfds[n] = {streams[i].fd, POLLIN, 0};
                map[n++] = i;
            }
        const int pr = ::poll(pfds, static_cast<nfds_t>(n), 100);
        if (pr < 0) {
            if (errno == EINTR) continue;
            res.error = std::string("poll() failed: ") + std::strerror(errno);
            killGroup(SIGTERM);
            killed = true;
            break;
        }
        for (int k = 0; k < n && !killed; ++k) {
            if (!(pfds[k].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            Stream& s = streams[map[k]];
            const ssize_t got = ::read(s.fd, buf, sizeof buf);
            if (got > 0) {
                handleData(s, buf, static_cast<size_t>(got));
            } else if (got == 0 || (got < 0 && errno != EINTR && errno != EAGAIN)) {
                if (!s.pending.empty()) emitLine(s.pending);  // flush an unterminated last line
                s.pending.clear();
                Pipe::closeFd(s.fd);
            }
        }
    }

    Pipe::closeFd(stdinFd);
    for (Stream& s : streams) Pipe::closeFd(s.fd);

    int status = 0;
    bool reaped = false;
    if (killed) {
        for (int i = 0; i < 30 && !reaped; ++i) {  // up to ~3 s for a graceful stop, then SIGKILL
            reaped = (::waitpid(pid, &status, WNOHANG) == pid);
            if (!reaped) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!reaped) killGroup(SIGKILL);
    }
    while (!reaped) {
        const pid_t w = ::waitpid(pid, &status, 0);
        if (w == pid) reaped = true;
        else if (w < 0 && errno != EINTR) break;
    }
    if (WIFEXITED(status)) res.exitCode = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) res.exitCode = 128 + WTERMSIG(status);
    return res;
}

}  // namespace zengine

#endif
