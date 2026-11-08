#include "gbs.h"

#include <cstring>

namespace zengine {

namespace {

uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

bool fail(std::string* err, const char* msg) {
    if (err) *err = msg;
    return false;
}

bool lowerEndsWith(const std::string& s, const char* ext) {
    size_t n = std::strlen(ext);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        char c = s[s.size() - n + i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != ext[i]) return false;
    }
    return true;
}

bool isImageName(const std::string& s) {
    return lowerEndsWith(s, ".png") || lowerEndsWith(s, ".dds") || lowerEndsWith(s, ".tga") ||
           lowerEndsWith(s, ".jpg") || lowerEndsWith(s, ".pcx");
}

// Finds NUL-terminated printable-ASCII strings of length >= 6 that end in an image extension.
void scanTextureNames(const uint8_t* d, size_t begin, size_t end, std::vector<std::string>& out) {
    size_t i = begin;
    while (i < end) {
        size_t j = i;
        while (j < end && d[j] >= 0x20 && d[j] < 0x7F) ++j;
        if (j < end && d[j] == 0 && j - i >= 6) {
            std::string s(reinterpret_cast<const char*>(d + i), j - i);
            if (isImageName(s)) {
                bool dup = false;
                for (const std::string& e : out)
                    if (e == s) { dup = true; break; }
                if (!dup) out.push_back(std::move(s));
            }
        }
        i = (j > i) ? j + 1 : i + 1;
    }
}

}  // namespace

bool parseGbs(const uint8_t* d, size_t size, Gbs& g, std::string* err) {
    g = Gbs{};
    if (size < 0x3C) return fail(err, "file shorter than the 0x3C-byte header+footer");
    if (rd32(d) != kGbsMagic) return fail(err, "bad magic (expected CSGG)");
    g.totalSize = rd32(d + 4);
    if (g.totalSize != size) return fail(err, "size field does not match file size");
    if (rd32(d + size - 4) != kGbsFooter) return fail(err, "missing 0x12344321 footer");
    uint32_t vbits = rd32(d + 8);
    std::memcpy(&g.formatVersion, &vbits, 4);
    g.sceneId = rd32(d + 0x0C);
    g.count0 = rd32(d + 0x10);
    g.textureCount = rd32(d + 0x14);
    g.count2 = rd32(d + 0x18);
    g.viewCount = rd32(d + 0x1C);
    g.count4 = rd32(d + 0x20);
    g.off0 = rd32(d + 0x28);
    g.off1 = rd32(d + 0x2C);
    g.off2 = rd32(d + 0x30);
    g.payloadSize = rd32(d + 0x34);
    // The payload size field does not count the optional trailing section: count4 entries of 68 bytes
    // (verified on all 6 files that have one: 24x68, 2x68, 2x68, 2x68, 2x68, 1x68).
    const uint64_t tailBytes = static_cast<uint64_t>(g.count4) * kGbsTailEntrySize;
    if (tailBytes > size - 0x3C) return fail(err, "trailing section larger than file");
    if (g.payloadSize != size - 0x3C - tailBytes) return fail(err, "payload size field inconsistent with file size");

    const size_t payloadBegin = 0x38;
    const size_t payloadEnd = payloadBegin + g.payloadSize;
    size_t p = payloadBegin + g.off2;
    if (p > payloadEnd) return fail(err, "views section offset beyond payload");

    for (uint32_t v = 0; v < g.viewCount; ++v) {
        if (p + 12 + 64 > payloadEnd) return fail(err, "view chunk header truncated");
        if (rd32(d + p) != kGbsViewTag) return fail(err, "expected WVGG view chunk");
        uint32_t csz = rd32(d + p + 4);
        if (csz < 12 + 64 || p + csz > payloadEnd) return fail(err, "view chunk size out of range");
        GbsView view;
        view.offset = static_cast<uint32_t>(p);
        view.size = csz;
        const char* nm = reinterpret_cast<const char*>(d + p + 12);
        view.name.assign(nm, strnlen(nm, 64));
        g.views.push_back(std::move(view));
        p += csz;
    }
    if (p != payloadEnd) return fail(err, "views do not tile the payload exactly");
    g.tailOffset = static_cast<uint32_t>(payloadEnd);
    g.tailSize = static_cast<uint32_t>(size - 4 - payloadEnd);
    if (g.tailSize != tailBytes) return fail(err, "trailing section size does not match count4 * 68");

    scanTextureNames(d, payloadBegin, payloadBegin + g.off2, g.textureNames);
    return true;
}

}  // namespace zengine
