#include "ramprobe.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace zengine {
namespace ram {

Process process() {
    Process p;
#if defined(__linux__) || defined(__ANDROID__)
    std::FILE* f = std::fopen("/proc/self/status", "r");
    if (!f) return p;
    char line[256];
    int found = 0;
    while (std::fgets(line, sizeof line, f) && found < 3) {
        unsigned long long kb = 0;
        if (std::sscanf(line, "VmRSS: %llu kB", &kb) == 1) {
            p.rssBytes = kb * 1024ull;
            ++found;
        } else if (std::sscanf(line, "VmHWM: %llu kB", &kb) == 1) {
            p.peakBytes = kb * 1024ull;
            ++found;
        } else if (std::sscanf(line, "VmSize: %llu kB", &kb) == 1) {
            p.virtBytes = kb * 1024ull;
            ++found;
        }
    }
    std::fclose(f);
    p.ok = p.rssBytes > 0;
#endif
    return p;
}

void Tracker::update(uint64_t nowMs, uint64_t rssBytes) {
    if (rssBytes > peak_) peak_ = rssBytes;
    if (!have_) {
        have_ = true;
        t0_ = nowMs;
        rss0_ = rssBytes;
        return;
    }
    const uint64_t dt = nowMs - t0_;
    if (dt < 30000) return;
    const double mb = (static_cast<double>(rssBytes) - static_cast<double>(rss0_)) / (1024.0 * 1024.0);
    rate_ = mb * 60000.0 / static_cast<double>(dt);
    t0_ = nowMs;
    rss0_ = rssBytes;
}

std::string megabytes(uint64_t bytes, int width) {
    if (width < 1) width = 1;
    if (width > 12) width = 12;
    uint64_t limit = 1;
    for (int i = 0; i < width; ++i) limit *= 10;
    uint64_t mb = bytes / (1024ull * 1024ull);
    if (mb >= limit) mb = limit - 1;
    std::string s = std::to_string(mb);
    while (static_cast<int>(s.size()) < width) s.insert(s.begin(), '0');
    return s;
}

std::string hex(uint64_t v, int digits) {
    static const char* d = "0123456789ABCDEF";
    if (digits < 1) digits = 1;
    if (digits > 16) digits = 16;
    std::string s(static_cast<size_t>(digits), '0');
    for (int i = digits - 1; i >= 0; --i) {
        s[static_cast<size_t>(i)] = d[v & 0xF];
        v >>= 4;
    }
    return s;
}

}  // namespace ram
}  // namespace zengine
