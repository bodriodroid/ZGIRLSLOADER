#include "foit_vm.h"

#include <cmath>
#include <cstring>

namespace zengine {
namespace foit {
namespace vm {

namespace {

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;

float bitsToFloat(uint32_t b) {
    float f;
    std::memcpy(&f, &b, sizeof f);
    return f;
}

// Which object an operand addresses (the prefix bits of its descriptor).
//   0x18 PT.   the team's point char          0x90 PRNT.  the parent
//   0x10 E.    the enemy                  0x40 T.     the target        0x80 A.  the attacker
//   0x08 L.    the leader (0x48: the leader's target)        none: the object itself
const Ref* resolve(uint8_t desc, const Context& cx, Ref& scratch) {
    (void)scratch;
    if ((desc & 0x18) == 0x18) {
        // PT.: the character of the object's own team that is on point (the enemy of the enemy); a match without teams
        // leaves it empty and the object's own leader answers
        if (cx.point.valid()) return &cx.point;
        return cx.leader.valid() ? &cx.leader : nullptr;
    }
    if ((desc & 0x90) == 0x90) return cx.parent.valid() ? &cx.parent : nullptr;
    if ((desc & 0x08) == 0) {
        if (desc & 0x10) return cx.enemy.valid() ? &cx.enemy : nullptr;
        if (desc & 0x40) return cx.target.valid() ? &cx.target : nullptr;
        if (desc & 0x80) return cx.attacker.valid() ? &cx.attacker : nullptr;
        return cx.self.valid() ? &cx.self : nullptr;
    }
    if (desc & 0x40) return cx.target.valid() ? &cx.target : nullptr;  // L.T.: the leader's target
    return cx.leader.valid() ? &cx.leader : (cx.self.valid() ? &cx.self : nullptr);
}

bool slotInRange(uint32_t id) { return id < kSlotCount; }

// Reads a node's A or "B" operand into a float; false = skip the node.
bool get(const Operand& o, Context& cx, float* v) { return readOperand(o, cx, v); }

}  // namespace

int scriptRandom(uint32_t& seed, float range) {
    seed = seed * 0x41c64e6du + 0x3039u;
    const float r = static_cast<float>((seed >> 16) & 0x7fff) * (1.0f / 32768.0f);
    return static_cast<int>(r * range);
}

bool readOperand(const Operand& o, Context& cx, float* out) {
    const uint8_t d = o.desc;
    if (d & 2) {  // global slot
        const uint32_t id = o.raw & 0xFF;
        *out = (cx.globals && static_cast<int>(id) < cx.numGlobals) ? cx.globals[id] : 0.0f;
        return true;
    }
    if ((d & 0x24) == 0x24) {  // literal constant (flag + random bits together mean "float literal")
        *out = bitsToFloat(o.raw);
        return true;
    }
    Ref scratch;
    const Ref* who = resolve(d, cx, scratch);
    if (!who) return false;
    if (!(d & 1)) {
        if (d & 4) {  // random number in [0, range)
            float range = bitsToFloat(o.raw);
            uint32_t dummy = 1;
            *out = static_cast<float>(scriptRandom(cx.rng ? *cx.rng : dummy, range));
            return true;
        }
        float v = bitsToFloat(o.raw);
        if (d & 0x20) {  // a flag bit
            const int idx = static_cast<int>(v);
            const int w = idx / 32, b = idx % 32;
            v = (who->flags && idx >= 0 && w < kFlagWords && ((who->flags[w] >> b) & 1u)) ? 1.0f : 0.0f;
        }
        *out = v;
        return true;
    }
    const uint32_t id = o.raw & 0xFF;
    switch (id) {
        case 0x30:  // slot 47 of the enemy's enemy
        case 0x32:  // slot 47 of the enemy
        case 0x33: {  // slot 47 of the attacker
            const Ref* r = id == 0x32 ? &cx.enemy : (id == 0x33 ? &cx.attacker : &cx.leader);
            *out = (r->valid()) ? r->slots[0x2f] : 0.0f;
            return true;
        }
        default: break;
    }
    *out = (slotInRange(id) && who->slots) ? who->slots[id] : 0.0f;
    return true;
}

bool writeOperand(const Operand& o, float value, Context& cx) {
    const uint8_t d = o.desc;
    if ((d & 0x22) == 2) {  // global slot
        const uint32_t id = o.raw & 0xFF;
        if (cx.globals && static_cast<int>(id) < cx.numGlobals) {
            cx.globals[id] = value;
            return true;
        }
        return false;
    }
    Ref scratch;
    const Ref* who = resolve(d, cx, scratch);
    if (!who) return false;
    if (!(d & 1)) {
        if (!(d & 0x20) || !who->flags) return false;
        const int idx = static_cast<int>(bitsToFloat(o.raw));
        if (idx < 0) return false;
        if (d & 2) {  // whole flag word
            if ((idx >> 5) < kFlagWords) who->flags[idx >> 5] = static_cast<int>(value) != 0 ? ~0u : 0u;
            return true;
        }
        const int w = idx / 32;
        if (w >= kFlagWords) return false;
        const uint32_t bit = 1u << (idx % 32);
        if (static_cast<int>(value) == 0) who->flags[w] &= ~bit;
        else who->flags[w] |= bit;
        return true;
    }
    const uint32_t id = o.raw & 0xFF;
    if (id < static_cast<uint32_t>(kFirstWritableSlot) || !slotInRange(id) || !who->slots) return false;
    who->slots[id] = value;
    return true;
}

// One arithmetic node.  Returns true when it executed (even if the op decided not to write).
static bool runNode(const ChainNode& n, Context& cx) {
    if (n.boolean()) return false;
    float a = 0, b = 0;
    if (!get(n.a, cx, &a) || !get(n.b, cx, &b)) return false;
    float r = 0;
    bool write = true;
    switch (n.op) {
        case op::Set: r = a; break;
        case op::Add: r = a + b; break;
        case op::Sub: r = a - b; break;
        case op::Mul: r = a * b; break;
        case op::Div:
            if (b == 0.0f) write = false;
            else r = a / b;
            break;
        case op::Abs: r = std::fabs(a); break;
        case op::Trunc: r = static_cast<float>(static_cast<int>(a)); break;
        case op::Reset: {  // C := the default value of the slot operand A names (on the object A's prefix selects: WEIGHT, A.LIFEBAR ...)
            Ref scratch;
            const Ref* who = (n.a.desc & 2) ? nullptr : resolve(n.a.desc, cx, scratch);
            if (!who || !(n.a.desc & 1) || !who->defaults || !slotInRange(n.a.raw & 0xFF)) return false;
            r = bitsToFloat(who->defaults[n.a.raw & 0xFF]);
            break;
        }
        case op::Sin: r = static_cast<float>(std::sin(static_cast<double>(a) * kDegToRad)); break;
        case op::Cos: r = static_cast<float>(std::cos(static_cast<double>(a) * kDegToRad)); break;
        case op::Max: r = (a <= b) ? b : a; break;
        case op::Min: r = (b <= a) ? b : a; break;
        case op::Atan2:
            r = (a != 0.0f || b != 0.0f) ? static_cast<float>(std::atan2(static_cast<double>(a), static_cast<double>(b)) * kRadToDeg) : 0.0f;
            break;
        case op::SetIfNonZero:
            if (b == 0.0f) write = false;
            else r = a;
            break;
        case op::SetIfZero:
            if (b != 0.0f) write = false;
            else r = a;
            break;
        case op::Nop: write = false; break;
        case op::Or: r = static_cast<float>(static_cast<int>(b) | static_cast<int>(a)); break;
        case op::And: r = static_cast<float>(static_cast<int>(b) & static_cast<int>(a)); break;
        case op::Mod: {
            const int bi = static_cast<int>(b);
            if (bi == 0) write = false;
            else r = static_cast<float>(static_cast<int>(a) % bi);
            break;
        }
        case op::Eq: r = (a == b) ? 1.0f : 0.0f; break;
        case op::Ne: r = (a != b) ? 1.0f : 0.0f; break;
        case op::Lt: r = (a < b) ? 1.0f : 0.0f; break;
        case op::Lerp: {  // C := (1 - C) * A + C * B      (C is read first)
            float t = 0;
            if (!get(n.c, cx, &t)) return false;
            r = (1.0f - t) * a + t * b;
            break;
        }
        case op::InvLerp: {  // C := (C - A) / (B - A)
            float t = 0;
            if (!get(n.c, cx, &t)) return false;
            r = (b - a) != 0.0f ? (t - a) / (b - a) : 0.0f;
            break;
        }
        default: {  // engine ops: the host decides
            Ref scratch;
            const Ref* who = resolve(n.a.desc, cx, scratch);
            float out = 0;
            if (!cx.host || !who || !cx.host->op(n.op, a, b, *who, &out)) return false;
            r = out;
            break;
        }
    }
    if (write) writeOperand(n.c, r, cx);
    return true;
}

int runChain(const Chain& c, Context& cx) {
    int n = 0;
    for (const ChainNode& nd : c) n += runNode(nd, cx) ? 1 : 0;
    return n;
}

static bool testNode(const ChainNode& n, Context& cx) {
    if (!n.boolean()) return false;
    float a = 0, c = 0;
    if (!get(n.a, cx, &a) || !get(n.c, cx, &c)) return false;
    switch (n.op) {
        case op::TLt: return a < c;
        case op::TGt: return c < a;
        case op::TEq: case op::TEq2: return a == c;
        case op::TGe: return c <= a;
        case op::TLe: return a <= c;
        case op::TNe: case op::TNe2: return a != c;
        case op::TTrue: return true;
        case op::TTag:
        case op::TNoTag: {  // does the current state of the addressed object carry the tag whose hash is the literal C?
            Ref scratch;
            const Ref* who = resolve(n.a.desc, cx, scratch);
            if (!who || !cx.host) return false;
            uint32_t h;
            std::memcpy(&h, &c, sizeof h);
            const bool has = cx.host->stateHasTag(*who, h);
            return n.op == op::TTag ? has : !has;
        }
        default: {
            Ref scratch;
            const Ref* who = resolve(n.a.desc, cx, scratch);
            return who && cx.host && cx.host->test(n.op, a, c, *who);
        }
    }
}

bool testChain(const Chain& c, Context& cx) {
    for (const ChainNode& nd : c)
        if (!testNode(nd, cx)) return false;
    return true;
}

}  // namespace vm
}  // namespace foit
}  // namespace zengine
