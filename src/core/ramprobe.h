// ramprobe.h - what the debug display shows about memory (C++17).  Process figures come from /proc on Linux / Android; on other
// platforms `ok` stays false and the display just shows dashes.  Nothing here allocates per call besides the returned strings.
#pragma once
#include <cstdint>
#include <string>

namespace zengine {
namespace ram {

struct Process {
    bool ok = false;
    uint64_t rssBytes = 0;    // resident set now
    uint64_t peakBytes = 0;   // resident set high-water mark of the process
    uint64_t virtBytes = 0;   // address space
};
Process process();

// Follows the resident size over time so a slow leak shows up in the debug display: `mbPerMin` is the growth over the last
// window (about 30 s), negative when memory went down.
class Tracker {
public:
    void update(uint64_t nowMs, uint64_t rssBytes);
    uint64_t peakBytes() const { return peak_; }
    double mbPerMin() const { return rate_; }
    bool growing() const { return rate_ > 4.0; }   // more than 4 MB/min for a whole window: worth a look

private:
    uint64_t peak_ = 0;
    uint64_t t0_ = 0, rss0_ = 0;
    bool have_ = false;
    double rate_ = 0.0;
};

// Whole megabytes, zero padded on the left to `width` digits ("0412"); values that do not fit show 9s.  Used for the
// seven-segment readout.
std::string megabytes(uint64_t bytes, int width = 4);
// 32-bit value as `digits` hex characters, upper case ("A3F09C12"): the rollback hash of the match.
std::string hex(uint64_t v, int digits = 8);

}  // namespace ram
}  // namespace zengine
