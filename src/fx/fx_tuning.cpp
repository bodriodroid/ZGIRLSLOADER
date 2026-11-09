#include "fx_tuning.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace zengine {
namespace fx {
namespace {

std::string stripComments(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            while (i < s.size() && s[i] != '\n') ++i;
        } else if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '*') {
            i += 2;
            while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) ++i;
            i = i + 2 < s.size() ? i + 2 : s.size();
        } else {
            o += s[i++];
        }
    }
    return o;
}

// [-]number[f][/number[f]]  ->  value.  `p` is advanced past it.
bool parseExpr(const std::string& s, size_t& p, float& v) {
    auto num = [&](double& d) {
        while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) ++p;
        size_t q = p;
        if (q < s.size() && (s[q] == '-' || s[q] == '+')) ++q;
        const size_t digits = q;
        while (q < s.size() && (std::isdigit(static_cast<unsigned char>(s[q])) || s[q] == '.')) ++q;
        if (q == digits) return false;
        d = std::strtod(s.substr(p, q - p).c_str(), nullptr);
        p = q;
        if (p < s.size() && (s[p] == 'f' || s[p] == 'F' || s[p] == 'h')) ++p;
        return true;
    };
    double a = 0, b = 1;
    if (!num(a)) return false;
    size_t save = p;
    while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) ++p;
    if (p < s.size() && s[p] == '/') {
        ++p;
        if (!num(b) || b == 0) return false;
        a /= b;
    } else {
        p = save;
    }
    v = static_cast<float>(a);
    return true;
}

// "a, b, c" (three expressions) starting at `p`.
bool parseTriple(const std::string& s, size_t& p, float out[3]) {
    for (int i = 0; i < 3; ++i) {
        if (!parseExpr(s, p, out[i])) return false;
        while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) ++p;
        if (i < 2) {
            if (p >= s.size() || s[p] != ',') return false;
            ++p;
        }
    }
    return true;
}

bool identAt(const std::string& s, size_t pos, const char* name) {
    const size_t n = std::strlen(name);
    if (s.compare(pos, n, name) != 0) return false;
    if (pos > 0 && (std::isalnum(static_cast<unsigned char>(s[pos - 1])) || s[pos - 1] == '_')) return false;
    const size_t e = pos + n;
    return e >= s.size() || !(std::isalnum(static_cast<unsigned char>(s[e])) || s[e] == '_');
}

size_t findIdent(const std::string& s, const char* name, size_t from = 0) {
    for (size_t p = s.find(name, from); p != std::string::npos; p = s.find(name, p + 1))
        if (identAt(s, p, name)) return p;
    return std::string::npos;
}

// The text between the braces of the function called `name` (definition, not a call: followed by "(...)" and "{").
std::string functionBody(const std::string& s, const char* name) {
    for (size_t p = findIdent(s, name); p != std::string::npos; p = findIdent(s, name, p + 1)) {
        size_t q = s.find('(', p);
        if (q == std::string::npos) return {};
        int depth = 0;
        for (; q < s.size(); ++q) {
            if (s[q] == '(') ++depth;
            if (s[q] == ')' && --depth == 0) break;
        }
        ++q;
        while (q < s.size() && std::isspace(static_cast<unsigned char>(s[q]))) ++q;
        if (q >= s.size() || s[q] != '{') continue;  // a call or a declaration
        int d = 0;
        size_t e = q;
        for (; e < s.size(); ++e) {
            if (s[e] == '{') ++d;
            if (s[e] == '}' && --d == 0) break;
        }
        return s.substr(q + 1, e - q - 1);
    }
    return {};
}

bool namedFloat(const std::string& s, const char* name, float& v) {
    const size_t p = findIdent(s, name);
    if (p == std::string::npos) return false;
    size_t q = s.find('=', p);
    if (q == std::string::npos) return false;
    ++q;
    return parseExpr(s, q, v);
}

// `name = {a, b, c}` or `name = float3(a, b, c)` or `name {a,b,c}` ...
bool namedTriple(const std::string& s, const char* name, float out[3]) {
    const size_t p = findIdent(s, name);
    if (p == std::string::npos) return false;
    size_t q = s.find('=', p);
    if (q == std::string::npos) return false;
    ++q;
    while (q < s.size() && std::isspace(static_cast<unsigned char>(s[q]))) ++q;
    if (s.compare(q, 6, "float3") == 0) q += 6;
    while (q < s.size() && (std::isspace(static_cast<unsigned char>(s[q])) || s[q] == '(' || s[q] == '{')) ++q;
    return parseTriple(s, q, out);
}

// The first `count` float3(...) constructors after `name`.
bool tripleArray(const std::string& s, const char* name, int count, float (*out)[3]) {
    size_t p = findIdent(s, name);
    if (p == std::string::npos) return false;
    p = s.find('{', p);
    if (p == std::string::npos) return false;
    for (int i = 0; i < count; ++i) {
        p = s.find("float3", p);
        if (p == std::string::npos) return false;
        p = s.find('(', p);
        if (p == std::string::npos) return false;
        ++p;
        if (!parseTriple(s, p, out[i])) return false;
    }
    return true;
}

bool flashDomain(const std::string& colourSrc, const std::string& curveSrc, FlashDomain& d) {
    float lo[4][3], hi[4][3];
    if (!tripleArray(colourSrc, "gLowColors", 4, lo) || !tripleArray(colourSrc, "gHighColors", 4, hi)) return false;
    const std::string body = functionBody(curveSrc, "Apply_SuperFlash");
    FlashDomain t = d;
    for (int i = 0; i < 4; ++i)
        for (int c = 0; c < 3; ++c) {
            t.look[i].low[c] = lo[i][c];
            t.look[i].high[c] = hi[i][c];
        }
    if (!namedTriple(body, "coeff", t.lumaCoeff) || !namedFloat(body, "CONTRAST", t.contrast) ||
        !namedFloat(body, "BRIGHTNESS", t.brightness) || !namedFloat(body, "SATURATION", t.saturation))
        return false;
    d = t;
    return true;
}

}  // namespace

FxTuning neutralTuning() { return FxTuning(); }

int extractTuning(const std::string& tiledSpriteFp, const std::string& otterVp, const std::string& otterFp, FxTuning& out,
                  std::vector<std::string>* notes) {
    int found = 0;
    auto note = [&](const char* m) {
        if (notes) notes->push_back(m);
    };
    const std::string tiled = stripComments(tiledSpriteFp), ov = stripComments(otterVp), of = stripComments(otterFp);

    if (!tiled.empty() && flashDomain(tiled, tiled, out.sprite)) ++found;
    else note("sprite flash looks not found");

    if (!ov.empty() && !of.empty() && flashDomain(ov, of, out.ui)) ++found;
    else note("UI flash looks not found");

    {
        const std::string body = functionBody(tiled, "Apply_SuperShadowBG");
        float tint[3], luma[3];
        if (!body.empty() && namedTriple(body, "gInSuperColor", tint) && namedTriple(body, "gRGB2LUM", luma)) {
            std::memcpy(out.shadowTint, tint, sizeof(tint));
            std::memcpy(out.shadowLuma, luma, sizeof(luma));
            ++found;
        } else {
            note("super shadow tint not found");
        }
    }

    {
        const std::string pb = functionBody(of, "JojosDeathPortrait"), bb = functionBody(of, "JojosDeathBackground");
        FxTuning t = out;
        bool ok = !pb.empty() && !bb.empty() && namedTriple(pb, "strokeColor", t.deathStroke) && namedTriple(pb, "fillColor", t.deathFill);
        if (ok) {
            // texOffset = {-1.f/160.f, 0.f}
            const size_t p = findIdent(pb, "texOffset");
            size_t q = p == std::string::npos ? std::string::npos : pb.find('{', p);
            float v = 0;
            if (q != std::string::npos) {
                ++q;
                ok = parseExpr(pb, q, v);
                if (ok) t.deathStrokeShift = v;
            } else {
                ok = false;
            }
        }
        static const char* const col[5] = {"colorA", "colorB", "colorC", "colorD", "colorE"};
        static const char* const pct[5] = {"percentA", "percentB", "percentC", "percentD", "percentE"};
        for (int i = 0; ok && i < 5; ++i) {
            ok = namedTriple(bb, col[i], t.deathBg[i]) && namedFloat(bb, pct[i], t.deathBgStop[i]);
        }
        if (ok) {
            // "centerDists.x *= 1280.f/720.f"
            const size_t p = bb.find("centerDists.x *=");
            if (p != std::string::npos) {
                size_t q = p + 16;
                float a = 0;
                if (parseExpr(bb, q, a) && a > 0) t.deathBgAspect = a;
            }
            out = t;
            ++found;
        } else {
            note("death-portrait look not found");
        }
    }
    out.fromCore = found > 0;
    return found;
}

// ---------------------------------------------------------------- ini
namespace {
void putv(std::ostringstream& o, const char* key, const float* v, int n) {
    o << key << '=';
    char b[32];
    for (int i = 0; i < n; ++i) {
        std::snprintf(b, sizeof(b), "%.9g", static_cast<double>(v[i]));
        o << (i ? "," : "") << b;
    }
    o << '\n';
}
void putDomain(std::ostringstream& o, const char* pre, const FlashDomain& d) {
    for (int i = 0; i < 4; ++i) {
        const std::string k = std::string(pre) + ".low" + std::to_string(i), h = std::string(pre) + ".high" + std::to_string(i);
        putv(o, k.c_str(), d.look[i].low, 3);
        putv(o, h.c_str(), d.look[i].high, 3);
    }
    putv(o, (std::string(pre) + ".luma").c_str(), d.lumaCoeff, 3);
    putv(o, (std::string(pre) + ".contrast").c_str(), &d.contrast, 1);
    putv(o, (std::string(pre) + ".brightness").c_str(), &d.brightness, 1);
    putv(o, (std::string(pre) + ".saturation").c_str(), &d.saturation, 1);
}
bool getv(const std::string& ini, const std::string& key, float* v, int n) {
    size_t p = 0;
    while (p < ini.size()) {
        size_t e = ini.find('\n', p);
        if (e == std::string::npos) e = ini.size();
        if (ini.compare(p, key.size(), key) == 0 && p + key.size() < e && ini[p + key.size()] == '=') {
            const char* s = ini.c_str() + p + key.size() + 1;
            for (int i = 0; i < n; ++i) {
                char* end = nullptr;
                v[i] = std::strtof(s, &end);
                if (end == s) return false;
                s = end;
                if (i < n - 1) {
                    if (*s != ',') return false;
                    ++s;
                }
            }
            return true;
        }
        p = e + 1;
    }
    return false;
}
bool getDomain(const std::string& ini, const char* pre, FlashDomain& d) {
    for (int i = 0; i < 4; ++i) {
        if (!getv(ini, std::string(pre) + ".low" + std::to_string(i), d.look[i].low, 3)) return false;
        if (!getv(ini, std::string(pre) + ".high" + std::to_string(i), d.look[i].high, 3)) return false;
    }
    return getv(ini, std::string(pre) + ".luma", d.lumaCoeff, 3) && getv(ini, std::string(pre) + ".contrast", &d.contrast, 1) &&
           getv(ini, std::string(pre) + ".brightness", &d.brightness, 1) && getv(ini, std::string(pre) + ".saturation", &d.saturation, 1);
}
}  // namespace

std::string tuningToIni(const FxTuning& t) {
    std::ostringstream o;
    o << "# PAFX tuning (generated; numbers read from the player's Core at pack time)\n";
    const float core = t.fromCore ? 1.0f : 0.0f;
    putv(o, "fromCore", &core, 1);
    putDomain(o, "sprite", t.sprite);
    putDomain(o, "ui", t.ui);
    putv(o, "shadow.tint", t.shadowTint, 3);
    putv(o, "shadow.luma", t.shadowLuma, 3);
    putv(o, "death.stroke", t.deathStroke, 3);
    putv(o, "death.fill", t.deathFill, 3);
    putv(o, "death.strokeShift", &t.deathStrokeShift, 1);
    for (int i = 0; i < 5; ++i) putv(o, (std::string("death.bg") + std::to_string(i)).c_str(), t.deathBg[i], 3);
    putv(o, "death.bgStop", t.deathBgStop, 5);
    putv(o, "death.bgAspect", &t.deathBgAspect, 1);
    return o.str();
}

bool tuningFromIni(const std::string& ini, FxTuning& out) {
    FxTuning t;
    float core = 0;
    if (!getv(ini, "fromCore", &core, 1)) return false;
    t.fromCore = core > 0.5f;
    bool ok = getDomain(ini, "sprite", t.sprite) && getDomain(ini, "ui", t.ui) && getv(ini, "shadow.tint", t.shadowTint, 3) &&
              getv(ini, "shadow.luma", t.shadowLuma, 3) && getv(ini, "death.stroke", t.deathStroke, 3) &&
              getv(ini, "death.fill", t.deathFill, 3) && getv(ini, "death.strokeShift", &t.deathStrokeShift, 1) &&
              getv(ini, "death.bgStop", t.deathBgStop, 5) && getv(ini, "death.bgAspect", &t.deathBgAspect, 1);
    for (int i = 0; ok && i < 5; ++i) ok = getv(ini, std::string("death.bg") + std::to_string(i), t.deathBg[i], 3);
    if (!ok) return false;
    out = t;
    return true;
}

}  // namespace fx
}  // namespace zengine
