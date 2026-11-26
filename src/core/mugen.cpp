// mugen.cpp - see mugen.h.
#include "mugen.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace zengine {
namespace mugen {
namespace {

uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
int16_t le16s(const uint8_t* p) { return static_cast<int16_t>(le16(p)); }

// a PCX (8 bit, one plane, RLE) -> indices
bool decodePcx(const uint8_t* b, size_t n, Sprite& s) {
    if (n < 128 || b[0] != 10 || b[2] != 1 || b[3] != 8 || b[65] != 1) return false;
    const int w = le16(b + 8) - le16(b + 4) + 1, h = le16(b + 10) - le16(b + 6) + 1;
    const int bpl = le16(b + 66);
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096 || bpl < w) return false;
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(bpl) * h);
    const size_t need = static_cast<size_t>(bpl) * h;
    size_t i = 128;
    while (raw.size() < need && i < n) {
        const uint8_t c = b[i++];
        if (c >= 0xC0) {
            if (i >= n) return false;
            const size_t run = c & 0x3F;
            raw.insert(raw.end(), run, b[i++]);
        } else {
            raw.push_back(c);
        }
    }
    if (raw.size() < need) return false;
    s.w = w;
    s.h = h;
    s.px.resize(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; ++y) std::memcpy(&s.px[static_cast<size_t>(y) * w], &raw[static_cast<size_t>(y) * bpl], static_cast<size_t>(w));
    if (n >= 128 + 769 && b[n - 769] == 12) {
        std::memcpy(s.ownPal.data(), b + n - 768, 768);
        s.hasOwnPal = true;
    }
    return true;
}

std::string trim(const std::string& s) {
    size_t a = 0, e = s.size();
    while (a < e && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (e > a && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(a, e - a);
}
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string stripComment(const std::string& s) {
    bool q = false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '"') q = !q;
        if (s[i] == ';' && !q) return s.substr(0, i);
    }
    return s;
}
std::vector<std::string> splitCommas(const std::string& s) {
    std::vector<std::string> r;
    std::string cur;
    for (char c : s + ",") {
        if (c == ',') {
            r.push_back(trim(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    return r;
}
int toInt(const std::string& s, int def = 0) {
    if (s.empty()) return def;
    char* e = nullptr;
    const long v = std::strtol(s.c_str(), &e, 10);
    return e == s.c_str() ? def : static_cast<int>(v);
}

}  // namespace

bool readSff1(const uint8_t* d, size_t size, std::vector<Sprite>& out, std::string* err) {
    auto fail = [&](const char* m) {
        if (err) *err = m;
        return false;
    };
    out.clear();
    if (size < 512 || std::memcmp(d, "ElecbyteSpr", 11) != 0) return fail("not an SFF file");
    if (d[15] != 1) return fail("only SFF version 1 is supported");
    const uint32_t images = le32(d + 20), first = le32(d + 24);
    if (images > 100000) return fail("bad image count");
    size_t off = first;
    for (uint32_t k = 0; k < images; ++k) {
        if (off + 32 > size) return fail("subfile header out of range");
        const uint32_t next = le32(d + off), len = le32(d + off + 4);
        Sprite s;
        s.axisX = le16s(d + off + 8);
        s.axisY = le16s(d + off + 10);
        s.group = le16(d + off + 12);
        s.image = le16(d + off + 14);
        const int link = le16(d + off + 16);
        if (len == 0) {
            if (link >= static_cast<int>(k)) return fail("a linked sprite points forward");
            s.linkedTo = link;
            const Sprite& t = out[static_cast<size_t>(link)];
            s.w = t.w;
            s.h = t.h;
            s.px = t.px;
            s.ownPal = t.ownPal;
            s.hasOwnPal = t.hasOwnPal;
        } else {
            if (off + 32 + len > size) return fail("subfile data out of range");
            if (!decodePcx(d + off + 32, len, s)) return fail("a sprite is not an 8-bit PCX");
        }
        out.push_back(std::move(s));
        if (!next) break;
        if (next <= off) return fail("subfile chain loops");
        off = next;
    }
    return true;
}

bool readAct(const uint8_t* d, size_t size, std::array<uint8_t, 768>& rgb) {
    if (size < 768) return false;
    for (int i = 0; i < 256; ++i)
        for (int c = 0; c < 3; ++c) rgb[static_cast<size_t>(i) * 3 + c] = d[static_cast<size_t>(255 - i) * 3 + c];
    return true;
}

std::vector<AirAction> parseAir(const std::string& text) {
    std::vector<AirAction> acts;
    std::vector<Box> cur1, cur2, def1, def2;
    bool have1 = false, have2 = false;   // a one-frame list was announced for the next frame
    int expect1 = 0, expect2 = 0;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = trim(stripComment(text.substr(pos, nl - pos)));
        pos = nl + 1;
        if (line.empty()) continue;
        const std::string low = lower(line);
        if (low[0] == '[') {
            if (low.compare(0, 14, "[begin action ") == 0) {
                AirAction a;
                a.id = toInt(trim(line.substr(14, line.find(']') - 14)));
                acts.push_back(std::move(a));
                cur1.clear();
                cur2.clear();
                def1.clear();
                def2.clear();
                have1 = have2 = false;
            }
            continue;
        }
        if (acts.empty()) continue;
        AirAction& a = acts.back();
        if (low == "loopstart") {
            a.loopStart = static_cast<int>(a.frames.size());
            continue;
        }
        if (low.compare(0, 5, "clsn1") == 0 || low.compare(0, 5, "clsn2") == 0) {
            const bool one = low[4] == '1';
            const size_t br = low.find('[');
            const size_t eq = line.find('=');
            const size_t col = low.find(':');
            if (br != std::string::npos && eq != std::string::npos) {   // Clsn1[0] = x1,y1,x2,y2
                const std::vector<std::string> v = splitCommas(line.substr(eq + 1));
                if (v.size() >= 4) {
                    Box b{toInt(v[0]), toInt(v[1]), toInt(v[2]), toInt(v[3])};
                    (one ? (have1 ? cur1 : def1) : (have2 ? cur2 : def2)).push_back(b);
                }
            } else if (col != std::string::npos) {   // Clsn1: 2   /   Clsn1Default: 2
                const bool isDef = low.find("default") != std::string::npos;
                const int n = toInt(trim(line.substr(col + 1)));
                if (one) {
                    if (isDef) {
                        def1.clear();
                        have1 = false;
                    } else {
                        cur1.clear();
                        have1 = true;
                    }
                    expect1 = n;
                } else {
                    if (isDef) {
                        def2.clear();
                        have2 = false;
                    } else {
                        cur2.clear();
                        have2 = true;
                    }
                    expect2 = n;
                }
            }
            continue;
        }
        const std::vector<std::string> v = splitCommas(line);
        if (v.size() >= 5 && (std::isdigit(static_cast<unsigned char>(v[0][0])) || v[0][0] == '-')) {
            AirFrame f;
            f.group = toInt(v[0]);
            f.image = toInt(v[1]);
            f.x = toInt(v[2]);
            f.y = toInt(v[3]);
            f.time = toInt(v[4], 1);
            if (v.size() >= 6) {
                const std::string fl = lower(v[5]);
                f.flipH = fl.find('h') != std::string::npos;
                f.flipV = fl.find('v') != std::string::npos;
            }
            f.clsn1 = have1 ? cur1 : def1;
            f.clsn2 = have2 ? cur2 : def2;
            have1 = have2 = false;   // a list announced with "Clsn1: n" belongs to this one frame
            (void)expect1;
            (void)expect2;
            a.frames.push_back(std::move(f));
        }
    }
    return acts;
}

Def parseDef(const std::string& text) {
    Def d;
    std::string section;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = trim(stripComment(text.substr(pos, nl - pos)));
        pos = nl + 1;
        if (line.empty()) continue;
        if (line[0] == '[') {
            section = lower(line.substr(1, line.find(']') - 1));
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = lower(trim(line.substr(0, eq)));
        std::string v = trim(line.substr(eq + 1));
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
        if (section == "info") {
            if (k == "name") d.name = v;
            else if (k == "displayname") d.displayName = v;
        } else if (section == "files") {
            d.files[k] = v;
        }
    }
    return d;
}

void scaleSprite(Sprite& s, int k) {
    if (k < 2 || s.w <= 0 || s.h <= 0) return;
    std::vector<uint8_t> o(static_cast<size_t>(s.w) * k * s.h * k);
    const size_t W = static_cast<size_t>(s.w) * k;
    for (int y = 0; y < s.h * k; ++y)
        for (int x = 0; x < s.w * k; ++x) o[static_cast<size_t>(y) * W + x] = s.px[static_cast<size_t>(y / k) * s.w + x / k];
    s.px = std::move(o);
    s.w *= k;
    s.h *= k;
    s.axisX *= k;
    s.axisY *= k;
}

void scaleActions(std::vector<AirAction>& acts, int k) {
    if (k < 2) return;
    for (AirAction& a : acts)
        for (AirFrame& f : a.frames) {
            f.x *= k;
            f.y *= k;
            for (std::vector<Box>* v : {&f.clsn1, &f.clsn2})
                for (Box& b : *v) {
                    b.x1 *= k;
                    b.y1 *= k;
                    b.x2 *= k;
                    b.y2 *= k;
                }
        }
}

}  // namespace mugen
}  // namespace zengine
