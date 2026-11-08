#include "errcode.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>

#if defined(__unix__) || defined(__APPLE__) || defined(__ANDROID__)
#define ZERR_POSIX 1
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace zengine {
namespace err {

namespace fs = std::filesystem;

const char* name(Code c) {
    switch (c) {
        case kCrash: return "crash";
        case kNoAudio: return "no-audio";
        case kNoData: return "no-data";
        case kModDamaged: return "mod-damaged";
        case kRuntimeAltered: return "runtime-altered";
        default: return "none";
    }
}

const char* meaning(Code c, bool es) {
    switch (c) {
        case kCrash: return es ? "El runtime se cerro de golpe" : "The runtime crashed";
        case kNoAudio: return es ? "Corre, pero sin voces ni musica del juego" : "Running without the game's voices / music";
        case kNoData: return es ? "Corre, pero sin los demas archivos del juego (modo Lite)" : "Running without the game's other files (Lite)";
        case kModDamaged: return es ? "Un mod esta danado y no se cargo" : "A mod is damaged and was not loaded";
        case kRuntimeAltered: return es ? "Algo altera el codigo del runtime" : "Something alters the runtime's code";
        default: return "";
    }
}

std::string displayText(Code c) { return c == kNone ? std::string() : "Err " + std::to_string(static_cast<int>(c)); }

// ------------------------------------------------------------------------------------------------------------------ Registry
void Registry::raise(Code c, const std::string& detail) {
    if (c < 1 || c > kCodeCount) return;
    std::lock_guard<std::mutex> lk(m_);
    if (!on_[c]) {
        on_[c] = true;
        ++gen_;
    }
    if (detail.empty()) return;
    auto& v = detail_[c];
    if (std::find(v.begin(), v.end(), detail) != v.end() || v.size() >= kMaxDetails) return;
    v.push_back(detail);
    ++gen_;
}

void Registry::clear(Code c) {
    if (c < 1 || c > kCodeCount) return;
    std::lock_guard<std::mutex> lk(m_);
    if (on_[c] || !detail_[c].empty()) ++gen_;
    on_[c] = false;
    detail_[c].clear();
}

void Registry::clearAll() {
    std::lock_guard<std::mutex> lk(m_);
    for (int c = 1; c <= kCodeCount; ++c) {
        on_[c] = false;
        detail_[c].clear();
    }
    ++gen_;
}

bool Registry::any() const {
    std::lock_guard<std::mutex> lk(m_);
    for (int c = 1; c <= kCodeCount; ++c)
        if (on_[c]) return true;
    return false;
}

bool Registry::has(Code c) const {
    if (c < 1 || c > kCodeCount) return false;
    std::lock_guard<std::mutex> lk(m_);
    return on_[c];
}

std::vector<Code> Registry::active() const {
    std::lock_guard<std::mutex> lk(m_);
    std::vector<Code> out;
    for (int c = 1; c <= kCodeCount; ++c)
        if (on_[c]) out.push_back(static_cast<Code>(c));
    return out;
}

Code Registry::primary() const {
    std::lock_guard<std::mutex> lk(m_);
    for (int c = 1; c <= kCodeCount; ++c)
        if (on_[c]) return static_cast<Code>(c);
    return kNone;
}

std::vector<std::string> Registry::details(Code c) const {
    if (c < 1 || c > kCodeCount) return {};
    std::lock_guard<std::mutex> lk(m_);
    return detail_[c];
}

uint32_t Registry::generation() const {
    std::lock_guard<std::mutex> lk(m_);
    return gen_;
}

Registry& global() {
    static Registry r;
    return r;
}

// ------------------------------------------------------------------------------------------------------------ crash marker
namespace {

char g_path[1024] = {0};
volatile int g_armed = 0;
int g_state = 0;  // 0 none, 1 running, 2 background

// Plain open/write/close so it can run inside a signal handler.
void writeMarker(const char* text, size_t n) {
#ifdef ZERR_POSIX
    if (!g_path[0]) return;
    const int fd = ::open(g_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    size_t off = 0;
    while (off < n) {
        const ssize_t w = ::write(fd, text + off, n - off);
        if (w <= 0) break;
        off += static_cast<size_t>(w);
    }
    ::close(fd);
#else
    if (!g_path[0]) return;
    std::FILE* f = std::fopen(g_path, "wb");
    if (!f) return;
    std::fwrite(text, 1, n, f);
    std::fclose(f);
#endif
}

void writeState(const char* s) { writeMarker(s, std::strlen(s)); }

#ifdef ZERR_POSIX
// "crash signal <n>\n" without printf / malloc
void onSignal(int sig) {
    if (g_armed) {
        char buf[48] = "crash signal ";
        size_t n = std::strlen(buf);
        char digits[12];
        int d = 0;
        int v = sig < 0 ? 0 : sig;
        do {
            digits[d++] = static_cast<char>('0' + v % 10);
            v /= 10;
        } while (v && d < 11);
        while (d) buf[n++] = digits[--d];
        buf[n++] = '\n';
        writeMarker(buf, n);
    }
    ::signal(sig, SIG_DFL);  // then the default action (core dump / the platform's own crash handling) goes ahead
    ::raise(sig);
}
#endif

void onTerminate() {
    if (g_armed) writeState("crash terminate (uncaught exception)\n");
    std::abort();
}

std::string readAll(const std::string& path, size_t cap) {
    std::ifstream f(path, std::ios::binary);
    std::string s;
    if (!f) return s;
    s.resize(cap);
    f.read(&s[0], static_cast<std::streamsize>(cap));
    s.resize(static_cast<size_t>(std::max<std::streamsize>(0, f.gcount())));
    return s;
}

std::string firstLine(const std::string& s) {
    const size_t e = s.find_first_of("\r\n");
    return e == std::string::npos ? s : s.substr(0, e);
}

}  // namespace

SessionInfo beginSession(const std::string& markerPath, Registry& reg) {
    SessionInfo info;
    if (markerPath.empty() || markerPath.size() >= sizeof(g_path)) return info;
    std::error_code ec;
    const fs::path p(markerPath);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    const std::string old = firstLine(readAll(markerPath, 400));
    if (old.rfind("crash", 0) == 0) {
        info.previousCrash = true;
        info.crashText = old.size() > 6 ? old.substr(6) : std::string("unknown");
        reg.raise(kCrash, "previous run: " + info.crashText);
    } else if (old.rfind("running", 0) == 0) {
        info.uncleanNote = "previous run did not exit cleanly (no crash recorded)";
    }
    std::memcpy(g_path, markerPath.c_str(), markerPath.size() + 1);
    g_armed = 1;
    g_state = 1;
    writeState("running\n");
    return info;
}

void endSession() {
    if (!g_path[0]) return;
    g_armed = 0;
    g_state = 0;
    std::error_code ec;
    fs::remove(fs::path(g_path), ec);
}

void markBackground() {
    if (g_armed && g_state == 1) {
        g_state = 2;
        writeState("background\n");
    }
}

void markForeground() {
    if (g_armed && g_state == 2) {
        g_state = 1;
        writeState("running\n");
    }
}

void installCrashHandlers() {
    std::set_terminate(onTerminate);
#ifdef ZERR_POSIX
    struct sigaction sa;
    std::memset(&sa, 0, sizeof sa);
    sa.sa_handler = onSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND | SA_NODEFER;
    for (int s : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS}) ::sigaction(s, &sa, nullptr);
#endif
}

void reportFatal(const std::string& what) {
    std::string t = "crash exception: " + what;
    for (char& c : t)
        if (c == '\n' || c == '\r') c = ' ';
    if (t.size() > 300) t.resize(300);
    t += "\n";
    if (g_armed) writeMarker(t.data(), t.size());
    global().raise(kCrash, what);
}

}  // namespace err
}  // namespace zengine
