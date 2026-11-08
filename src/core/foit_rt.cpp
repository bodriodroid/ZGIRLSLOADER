#include "foit_rt.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace zengine {
namespace foit {
namespace rt {

namespace {
void runScript(Object& o, World& w, const Chain& chain);
}  // namespace

// ======================================================================================================================
// input
// ======================================================================================================================
void InputRing::reset(size_t capacity) {
    buf_.assign(capacity, Sample());
    head_ = 0;
    pushes_ = 0;
}

void InputRing::push(const Sample& s) {
    if (buf_.empty()) return;
    head_ = (head_ + 1) % buf_.size();
    buf_[head_] = s;
    ++pushes_;
}

const Sample* InputRing::at(int age) const {
    if (buf_.empty() || age < 0) return nullptr;
    const size_t n = buf_.size();
    const size_t a = static_cast<size_t>(age) % n;
    return &buf_[(head_ + n - a) % n];
}

Sample composeSample(const Sample& prev, int dir, uint32_t rawDown) {
    Sample s;
    s.dir = static_cast<uint8_t>(dir);
    auto edgeState = [&](int b) {
        const bool down = (rawDown >> b) & 1u;
        const bool was = prev.btn[b] == BtnHeld || prev.btn[b] == BtnPressed;
        if (down != was) return down ? BtnPressed : BtnReleased;
        return down ? BtnHeld : BtnUp;
    };
    for (int b = 0; b < 6; ++b) s.btn[b] = edgeState(b);
    s.btn[SlotExtraA] = edgeState(SlotExtraA);
    s.btn[SlotExtraB] = edgeState(SlotExtraB);

    // per group (punches 0..2, kicks 3..5): buttons pressed / released / held; an edge of the previous tick still counts
    // when another button of the group has an edge now (chords do not have to land on the very same tick)
    struct Count { int pressed = 0, released = 0, held = 0; bool anyPress = false, anyRelease = false; };
    auto count = [&](int first) {
        Count c;
        for (int b = first; b < first + 3; ++b) {
            if (s.btn[b] == BtnPressed) c.anyPress = true;
            else if (s.btn[b] == BtnReleased) c.anyRelease = true;
        }
        for (int b = first; b < first + 3; ++b) {
            const uint8_t cur = s.btn[b], old = prev.btn[b];
            if (cur == BtnPressed || (c.anyPress && old == BtnPressed)) ++c.pressed;
            else if (cur == BtnReleased || (c.anyRelease && old == BtnReleased)) ++c.released;
            else if (cur == BtnHeld) ++c.held;
        }
        return c;
    };
    const Count P = count(0), K = count(3);
    auto agg = [](int pressed, int released, int held, int need) -> uint8_t {
        if (pressed >= need) return BtnPressed;
        if (released >= need) return BtnReleased;
        return held >= need ? BtnHeld : BtnUp;
    };
    s.btn[SlotAnyPunch] = agg(P.pressed, P.released, P.held, 1);
    s.btn[SlotAnyKick] = agg(K.pressed, K.released, K.held, 1);
    s.btn[SlotPP] = agg(P.pressed, P.released, P.held, 2);
    s.btn[SlotKK] = agg(K.pressed, K.released, K.held, 2);
    s.btn[SlotPPP] = agg(P.pressed, P.released, P.held, 3);
    s.btn[SlotKKK] = agg(K.pressed, K.released, K.held, 3);
    const bool lp = s.btn[SlotLP] == BtnPressed || prev.btn[SlotLP] == BtnPressed;
    const bool lk = s.btn[SlotLK] == BtnPressed || prev.btn[SlotLK] == BtnPressed;
    bool pr = false, re = false, he = false;
    if (lp && lk) {
        pr = P.pressed > 1 || K.pressed > 1;
        re = P.released > 1 || K.released > 1;
        he = P.held > 1 || K.held > 1;
    }
    s.btn[SlotChord] = pr ? BtnPressed : (re ? BtnReleased : (he ? BtnHeld : BtnUp));
    return s;
}

namespace {

// facing left: the physical direction of a sample is mirrored before it is compared with a step
constexpr uint8_t kMirror[11] = {4, 3, 2, 1, 0, 7, 6, 5, 8, 9, 10};

bool buttonOk(uint8_t req, uint8_t have) {
    if (req == have || req == BtnAny || have == BtnAny) return true;
    if (req == BtnEdge) return have == BtnPressed || have == BtnReleased;
    if (have == BtnEdge) return req == BtnPressed || req == BtnReleased;
    switch (req) {
        case BtnNotPressed: return have != BtnPressed;
        case BtnNotReleased: return have != BtnReleased;
        case BtnNotEdge: return have != BtnPressed && have != BtnReleased;
        case BtnNotHeld: return have != BtnHeld;
        case BtnNotUp: return have != BtnUp;
        default: return false;
    }
}

}  // namespace

bool stepMatches(const Step& st, const Sample& s, bool facingRight, vm::Context& cx) {
    uint8_t d = s.dir < 11 ? s.dir : static_cast<uint8_t>(DirNever);
    if (!facingRight) d = kMirror[d];
    if (st.b0 == DirNever || d == DirNever) return false;
    if (st.b0 != DirAny && d != DirAny) {
        const bool in = (d == st.b0 || d == st.b1 || d == st.b2);
        const bool negate = (st.flags & 1) != 0;
        if (negate ? in : !in) return false;
    }
    for (int i = 0; i < kButtons; ++i)
        if (!buttonOk(st.t[i], s.btn[i])) return false;
    if (!st.chain.empty() && !vm::testChain(st.chain, cx)) return false;
    return true;
}

bool matchAt(const Command& c, const InputRing& ring, int anchor, bool right, vm::Context& cx, int* ticks) {
    int scanned = 0;
    if (ticks) *ticks = 0;
    const std::vector<Step>& S = c.steps;
    if (S.empty()) return false;
    const Sample* s0 = ring.at(anchor);
    if (!s0) return false;
    if (!stepMatches(S[0], *s0, right, cx)) return false;
    if (c.a == 1) return true;
    if (c.hasDefault && stepMatches(c.def, *s0, right, cx)) return false;

    size_t idx = 1, prev = 0;
    bool justAdvanced = false;
    int pos = anchor;
    for (int k = 1; k < static_cast<int>(c.a); ++k) {
        ++pos;
        if (idx == S.size()) {
            if (ticks) *ticks = scanned;
            return true;
        }
        const Sample* p = ring.at(pos);
        if (!p) break;
        ++scanned;
        bool ok = stepMatches(S[idx], *p, right, cx);
        if (ok && S[idx].w != 0) {
            // the step must also hold for `w` older samples
            const int hold = S[idx].w;
            if (hold > static_cast<int>(c.a) - k) {
                ok = false;
            } else {
                for (int j = 1; j <= hold && ok; ++j) {
                    const Sample* q = ring.at(pos + j);
                    ok = q && stepMatches(S[idx], *q, right, cx);
                }
            }
        }
        if (c.hasDefault && stepMatches(c.def, *p, right, cx)) return false;  // the veto step may never appear
        if (ok) {
            prev = idx;
            ++idx;
            justAdvanced = true;
        } else if (justAdvanced) {
            // still holding what the previous step asked for is fine
            if (!stepMatches(S[prev], *p, right, cx)) justAdvanced = false;
        } else if (stepMatches(S[0], *p, right, cx)) {
            return false;  // the final step shows up again before the sequence is complete
        }
    }
    if (ticks) *ticks = scanned;
    return idx == S.size();
}

bool matchCommand(const Command& cmd, const InputRing& ring, int offset, bool facingRight, vm::Context& cx, int* age, int* ticks) {
    if (cmd.steps.empty() || ring.capacity() == 0 || offset < 0) return false;
    int t = 0;
    if (matchAt(cmd, ring, offset, facingRight, cx, &t)) {
        if (age) *age = 0;
        if (ticks) *ticks = t;
        return true;
    }
    for (int i = 0; i < static_cast<int>(cmd.b); ++i) {
        if (matchAt(cmd, ring, offset + i + 1, facingRight, cx, &t)) {
            if (age) *age = i + 1;
            if (ticks) *ticks = t + i;
            return true;
        }
    }
    return false;
}

// ======================================================================================================================
// host: what the scripts can ask of the engine
// ======================================================================================================================
namespace {

const Frame* frameOf(const Object& o);

// The characters of one player's team (the world lists every object; children point at their leader).
template <typename F>
void forTeam(const World& w, int player, F&& f) {
    for (Object* m : w.objects)
        if (m && m->alive && (m->leader == nullptr || m->leader == m) && static_cast<int>(m->slots[25]) == player) f(*m);
}

// decimal digit `k` of `v` (0 = ones); the shipped engine returns 0 when v < 10^k
int digitOf(int v, int k) {
    int p = 1;
    for (int i = 0; i < k; ++i) p *= 10;
    if (v < p) return 0;
    return (v / p) % 10;
}

struct Host : vm::Host {
    World* w = nullptr;
    static bool contains(const uint32_t* a, int n, uint32_t h) { return std::find(a, a + n, h) != a + n; }
    static bool hasTag(const Object* o, uint32_t hash) {
        if (!o || !o->def || o->state < 0 || static_cast<size_t>(o->state) >= o->def->states.size()) return false;
        if (contains(o->addedTags, o->numAddedTags, hash)) return true;
        if (contains(o->removedTags, o->numRemovedTags, hash)) return false;
        const std::vector<uint32_t>& h = o->st().hashes;
        return std::find(h.begin(), h.end(), hash) != h.end();
    }
    bool stateHasTag(const vm::Ref& who, uint32_t hash) override { return hasTag(static_cast<const Object*>(who.user), hash); }
    static uint32_t hashOf(float f) {  // the tag hash travels through the float operand as raw bits
        uint32_t h;
        std::memcpy(&h, &f, sizeof h);
        return h;
    }
    bool op(int opcode, float a, float b, const vm::Ref& who, float* out) override {
        Object* o = static_cast<Object*>(who.user);
        switch (opcode) {
            case 38:  // 1 when the addressed object carries tag B
            case 39:  // 1 when it does not
                if (!o) return false;
                *out = (hasTag(o, hashOf(b)) == (opcode == 38)) ? 1.0f : 0.0f;
                return true;
            case 118:  // give the object tag B
                if (!o) return false;
                if (o->numRemovedTags > 0) {
                    uint32_t* e = std::remove(o->removedTags, o->removedTags + o->numRemovedTags, hashOf(b));
                    o->numRemovedTags = static_cast<int>(e - o->removedTags);
                }
                if (!contains(o->addedTags, o->numAddedTags, hashOf(b)) && o->numAddedTags < Object::kMaxDynTags)
                    o->addedTags[o->numAddedTags++] = hashOf(b);
                *out = 1.0f;
                return true;
            case 119:  // take tag B away from the object
                if (!o) return false;
                if (o->numAddedTags > 0) {
                    uint32_t* e = std::remove(o->addedTags, o->addedTags + o->numAddedTags, hashOf(b));
                    o->numAddedTags = static_cast<int>(e - o->addedTags);
                }
                if (!contains(o->removedTags, o->numRemovedTags, hashOf(b)) && o->numRemovedTags < Object::kMaxDynTags)
                    o->removedTags[o->numRemovedTags++] = hashOf(b);
                *out = 1.0f;
                return true;
            case 42:  // 1 while a hit reaction (the attacker's chain) is waiting for this object
                if (!o) return false;
                *out = o->pendingChain ? 1.0f : 0.0f;
                return true;
            case 43:  // takes the waiting reaction away; 1 when there was one
                if (!o) return false;
                *out = o->pendingChain ? 1.0f : 0.0f;
                o->pendingChain = nullptr;
                return true;
            case 31:  // square root (negative input: the shipped engine hands back NaN, we hand back 0)
                *out = a >= 0.0f ? std::sqrt(a) : 0.0f;
                return true;
            case 34:  // A to the power B
                *out = static_cast<float>(std::pow(static_cast<double>(a), static_cast<double>(b)));
                return true;
            case 27:  // colour channel B of palette colour A of the character's palette (cosmetic: the palette files are not loaded here)
            case 25:  // x / y of a named point of the object's sprite image (needs the .art-pt files; cosmetic offsets)
            case 26:
            case 40:
            case 41:
            case 48:  // per-player match state read by the tag-team snap-back states (0 = nothing special going on)
            case 49:  // per-object frame counters of the move (only one script reads it)
            case 52:  // match mode flag
                if (w) ++w->stats.softOps[opcode & 255];
                *out = 0.0f;
                return true;
            case 44:  // x / y (op 45) of the point whose name hash is B in the object's current frame
            case 45: {
                if (!o) return false;
                const Frame* f = frameOf(*o);
                const uint32_t h = hashOf(b);
                *out = 0.0f;
                if (f)
                    for (const auto& e : f->entries)
                        if (e[0] == h) {
                            *out = static_cast<float>(static_cast<int32_t>(e[opcode == 44 ? 1 : 2]));
                            break;
                        }
                return true;
            }
            case 47: {  // 1 when any member of the addressed object's team is in a state tagged B
                if (!o || !w) return false;
                bool any = false;
                forTeam(*w, static_cast<int>(o->slots[25]), [&](Object& m) { any = any || hasTag(&m, hashOf(b)); });
                *out = any ? 1.0f : 0.0f;
                return true;
            }
            case 50:  // 1 when the leader of the addressed object is roster character B - 1
                if (!o) return false;
                *out = ((o->leader ? o->leader : o)->charId + 1 == static_cast<int>(b)) ? 1.0f : 0.0f;
                return true;
            case 53: {  // 1 when the addressed object's player is down to a single character standing
                if (!o || !w) return false;
                int alive = 0;
                forTeam(*w, static_cast<int>(o->slots[25]), [&](Object& m) { alive += m.life() > 0 ? 1 : 0; });
                *out = alive < 2 ? 1.0f : 0.0f;
                return true;
            }
            default:
                if (opcode >= 55 && opcode <= 64) {  // decimal digit (opcode - 55) of A
                    *out = static_cast<float>(digitOf(static_cast<int>(a), opcode - 55));
                    return true;
                }
                if (w) ++w->stats.hostOps[opcode & 255];
                return false;
        }
    }
    bool test(int opcode, float a, float c, const vm::Ref& who) override {
        const Object* o = static_cast<const Object*>(who.user);
        if (opcode == 42) return o && o->pendingChain;
        if (opcode == 50) return o && (o->leader ? o->leader : o)->charId + 1 == static_cast<int>(c);
        if (opcode == 54) return o && (o->leader ? o->leader : o)->palette == static_cast<int>(c);
        if (opcode == 53) {
            if (!o || !w) return false;
            int alive = 0;
            forTeam(*w, static_cast<int>(o->slots[25]), [&](Object& m) { alive += m.life() > 0 ? 1 : 0; });
            return alive < 2;
        }
        if (opcode >= 65 && opcode <= 114) {  // decimal digit (opcode - 65) % 10 of A against C: ==, <, >, <=, >= by blocks of ten
            const int d = digitOf(static_cast<int>(a), (opcode - 65) % 10), v = static_cast<int>(c);
            switch ((opcode - 65) / 10) {
                case 0: return d == v;
                case 1: return d < v;
                case 2: return d > v;
                case 3: return d <= v;
                default: return d >= v;
            }
        }
        if (w) ++w->stats.hostTests[opcode & 255];
        return false;
    }
};

vm::Ref refOf(Object* o) {
    vm::Ref r;
    if (!o) return r;
    r.slots = o->slots;
    r.flags = o->flags;
    r.defaults = o->def ? o->def->slotDefaults.data() : nullptr;
    r.user = o;
    return r;
}

// The character of side `side` (0 / 1 = PLAYERNUM 1 / 2) that is on point; null when the side has none.
Object* pointOfSide(const World& w, int side) {
    for (Object* o : w.objects)
        if (o->leader == o && o->alive && (o->slots[25] >= 2.0f ? 1 : 0) == side && o->slots[27] != 0) return o;
    return nullptr;
}

void makeContext(Object& o, World& w, Host& h, vm::Context& cx) {
    h.w = &w;
    if (w.teams) {
        const Object* l = o.leader ? o.leader : &o;
        cx.point = refOf(pointOfSide(w, l->slots[25] >= 2.0f ? 1 : 0));
    }
    cx.self = refOf(&o);
    cx.leader = refOf(o.leader ? o.leader : &o);
    cx.enemy = refOf(o.enemy);
    cx.target = refOf(o.target);
    cx.attacker = refOf(o.attacker);
    cx.parent = refOf(o.parent);
    cx.globals = w.globals;
    cx.numGlobals = 256;
    cx.rng = &o.rng;
    cx.host = &h;
}

int nodeCount(const Object& o) { return static_cast<int>(o.st().hdr.nodes.size()); }

const Node* curNode(const Object& o) {
    const auto& nodes = o.st().hdr.nodes;
    if (nodes.empty() || o.nodeIdx < 0) return nullptr;
    const size_t i = static_cast<size_t>(o.nodeIdx);
    return &nodes[i < nodes.size() ? i : nodes.size() - 1];
}

// The node cursor: index, ticks left, duration of the node, "hit data waiting to arm"
void setCursor(Object& o, int idx) {
    const auto& nodes = o.st().hdr.nodes;
    o.nodeIdx = idx;
    if (idx < 0 || nodes.empty()) {
        o.nodeTimer = o.nodeDur = 1;
        o.nodeHasHit = false;
        return;
    }
    const Node& n = nodes[static_cast<size_t>(idx) < nodes.size() ? static_cast<size_t>(idx) : nodes.size() - 1];
    const NodeProps* p = n.props.empty() ? nullptr : &n.props[0];
    o.nodeTimer = o.nodeDur = p ? p->dur : 1;
    o.nodeHasHit = p && p->hasHit;
    if (n.refs[1] != 0) o.frame = n.refs[1];  // script-only nodes keep showing the last frame
}

bool facingMirrored(const Object& o) { return !o.facingRight(); }

bool bit(const Object& o, int i) { return (o.trans[i >> 5] >> (i & 31)) & 1u; }
void setBit(Object& o, int i, bool v) {
    if (v) o.trans[i >> 5] |= 1u << (i & 31);
    else o.trans[i >> 5] &= ~(1u << (i & 31));
}

int clampWindow(float v) {
    const int i = static_cast<int>(v);
    return i < 0 ? 0 : (i > 10 ? 10 : i);
}

float toward(float v, float step) {  // v moves one step towards 0 (the engine's integer countdown)
    const int i = static_cast<int>(v);
    if (i < 0) return static_cast<float>(i + 1);
    if (i > 0) return static_cast<float>(i - 1);
    (void)step;
    return v;
}

}  // namespace

int findState(const foit::Object& def, const char* name) { return def.findState(name); }

// ======================================================================================================================
// object life
// ======================================================================================================================
void init(Object& o, const foit::Character& ch, const foit::Object& def, float x, bool facingRight, int inputRing) {
    o = Object();
    o.def = &def;
    o.ch = &ch;
    for (size_t i = 0; i < kSlotCount; ++i) {
        float f = 0.0f;
        if (def.slotDefaultsValid) std::memcpy(&f, &def.slotDefaults[i], sizeof f);
        o.slots[i] = f;
    }
    std::memset(o.flags, 0, sizeof o.flags);
    std::memset(o.trans, 0, sizeof o.trans);
    o.numAddedTags = o.numRemovedTags = 0;
    o.slots[56] = facingRight ? 1.0f : -1.0f;
    o.slots[25] = 1;  // PLAYERNUM
    o.slots[26] = 1;  // TEAMPOS
    o.slots[27] = 1;  // ISPOINT
    o.slots[30] = 0;  // ROUNDOVER: the round is on
    if (o.slots[64] <= 0) o.slots[64] = kStartHealth;  // LIFEBAR
    if (o.slots[65] <= 0) o.slots[65] = o.slots[64];   // REDLIFE (the red part of the bar starts level with the life)
    o.x = x;
    o.y = 0;
    const size_t cap = inputRing >= 0 ? static_cast<size_t>(inputRing) : std::max<size_t>(1, def.v374);
    o.ring.reset(cap);
    o.leader = &o;
    o.id = 0;
    o.rng = 0x1234567u;
}

Object* spawn(Object& parent, World& w, int childIdx, float ox, float oy, const Chain* initScript) {
    Object* lead = parent.leader ? parent.leader : &parent;
    if (!lead->ch || childIdx < 0 || static_cast<size_t>(childIdx) >= lead->ch->body.children.size()) return nullptr;
    const foit::Object& cdef = lead->ch->body.children[static_cast<size_t>(childIdx)];
    static const bool traceSpawn = std::getenv("ZRT_TRACE_SPAWN") != nullptr;
    if (traceSpawn)
        std::fprintf(stderr, "  [spawn] tick %llu parent obj %d state %s node %d -> child %d\n", static_cast<unsigned long long>(w.stats.ticks),
                     parent.id, parent.st().name.c_str(), parent.nodeIdx, childIdx);
    std::unique_ptr<Object> c(new Object());
    const bool right = parent.slots[56] == 1.0f;
    init(*c, *lead->ch, cdef, parent.x + (right ? ox : -ox), right);
    c->y = parent.y - oy;
    c->onGround = false;
    c->slots[25] = parent.slots[25];  // PLAYERNUM
    c->slots[56] = parent.slots[56];  // facing
    c->leader = lead;
    c->parent = &parent;
    c->enemy = parent.enemy;
    c->id = w.nextId++;
    // an object born "tethered to its parent" (TETHEREDTO default 1) starts following from where the parent stands right now
    if (static_cast<int>(c->slots[57]) == 1) {
        c->tethX = parent.x;
        c->tethY = parent.y;
        c->tethOk = true;
    }
    Object* raw = c.get();
    w.pool.push_back(std::move(c));
    w.objects.push_back(raw);
    enterState(*raw, w, cdef.refBe < cdef.states.size() ? cdef.refBe : 0);
    raw->fresh = true;
    if (initScript) runScript(*raw, w, *initScript);
    return raw;
}

void enterState(Object& o, World& w, int s) {
    if (s < 0 || static_cast<size_t>(s) >= o.def->states.size()) return;
    static const bool trace = std::getenv("ZRT_TRACE") != nullptr;
    if (trace)
        std::fprintf(stderr, "  [trace] tick %llu obj %d: %s -> %s\n", static_cast<unsigned long long>(w.stats.ticks), o.id,
                     o.def->states[static_cast<size_t>(o.state)].name.c_str(), o.def->states[static_cast<size_t>(s)].name.c_str());
    // leaving the old state
    o.slots[40] = o.slots[39];
    o.state = s;
    o.slots[39] = static_cast<float>(s);
    o.slots[1] = o.slots[2] = o.slots[3] = 0;       // hit / block / touch counters of the move
    o.slots[102] = o.slots[103] = 0;
    o.slots[111] = 0;
    // per state defaults of the physical constants and effects
    auto def = [&](size_t i) {
        float f;
        std::memcpy(&f, &o.def->slotDefaults[i], sizeof f);
        return f;
    };
    o.slots[54] = def(54);   // WEIGHT (gravity)
    o.slots[55] = def(55);   // FRICTION
    o.slots[164] = def(164); // XSCALE
    o.slots[165] = def(165); // YSCALE
    o.slots[80] = def(80);
    o.slots[81] = 0;
    o.slots[82] = def(82);
    o.slots[117] = 0.75f;
    o.slots[118] = 0.3f;
    o.slots[132] = 0;       // COUNTERHIT
    o.slots[133] = 1.0f;    // CANCELOK
    o.slots[142] = 0;
    setCursor(o, 0);
    std::memset(o.trans, 0, sizeof o.trans);
    // the armed hit data survives the state change (a projectile keeps the hit it was thrown with while it lands and walks);
    // it only ends with a connect, a "clear hit" record or the end of the object
    ++w.stats.stateChanges;
}

// ======================================================================================================================
// transitions: which of the state's transition commands are satisfied by the input history right now
// ======================================================================================================================
static void refreshSlots(Object& o, World& w);

static void evaluateTransitions(Object& o, World& w, const Sample* input) {
    if (input) o.ring.push(*input);
    refreshSlots(o, w);
    const State& S = o.st();
    for (int k = 0; k < kTransWords; ++k) o.trans[k] = 0;
    Host host;
    vm::Context cx;
    makeContext(o, w, host, cx);
    const int window = clampWindow(o.slots[141]);
    const bool right = !facingMirrored(o);
    int best = -1, bestPrio = -1, bestAge = 1000000;
    const int n = static_cast<int>(std::min<size_t>(S.transitions.size(), kTransWords * 32));
    for (int i = 0; i < n; ++i) {
        const Command& c = S.transitions[static_cast<size_t>(i)].cmd;
        int age = 0, ticks = 0;
        const bool m = matchCommand(c, o.ring, window, right, cx, &age, &ticks);
        setBit(o, i, m);
        if (!m) continue;
        o.slots[146] = static_cast<float>(ticks);
        const bool prio = (c.flags & 1) != 0;  // "closest in time wins" transitions
        if (!prio) {
            if (best < 0) best = i;
        } else if (bestAge < age) {
            setBit(o, i, false);
        } else {
            if (bestPrio >= 0) setBit(o, bestPrio, false);
            bestPrio = i;
            bestAge = age;
        }
        if (best >= 0 && !(bestPrio >= 0 && bestPrio <= best)) break;
    }
}

// ======================================================================================================================
// slots the engine refreshes before the scripts of a node run
// ======================================================================================================================
static void refreshSlots(Object& o, World& w) {
    o.slots[160] = o.x;
    o.slots[161] = o.y;
    o.slots[5] = o.onGround ? 1.0f : 0.0f;
    if (!(o.fb & fl::B_GotHit)) {
        o.slots[6] = 0;
        o.slots[7] = 0;
    } else if (!(o.fb & fl::B_Blocking)) {
        o.slots[6] = 1.0f;
        o.slots[7] = 0;
    } else {
        o.slots[6] = 0;
        o.slots[7] = 1.0f;
    }
    o.slots[9] = (o.target && o.target->attacker == &o) ? 1.0f : 0.0f;    // THROWING: this object holds a defender
    o.slots[10] = (o.attacker && o.attacker->target == &o) ? 1.0f : 0.0f; // THROWN: an attacker holds this object
    if (!o.enemy) {
        o.slots[23] = 1.0f;
        o.slots[20] = 0;
        o.slots[8] = 0;
    } else {
        const float ex = o.enemy->x;
        float dir = 1.0f;
        if (o.slots[56] == 1.0f) {
            if (ex < o.x) dir = -1.0f;
        } else if (ex <= o.x) {
            dir = -1.0f;
        }
        o.slots[23] = dir;
        o.slots[20] = (ex - o.x) * o.slots[56];
        o.slots[8] = (o.enemy->fb & fl::B_Attack) ? 1.0f : 0.0f;
    }
    o.slots[15] = w.leftWall;
    o.slots[16] = w.rightWall;
    o.slots[17] = 0;
    o.slots[18] = 0;
}

// ======================================================================================================================
// node execution
// ======================================================================================================================
namespace {

// node property byte layout (the .foit bits -> what the runtime tests)
struct PropFlags {
    bool hasDuration, setFlagA, clearHit, onConnectOnce, onConnectKeepA, onConnectKeepB, onConnectKeepC;
    bool commit, fallToState, runHeader, countUse, attack, hasStrength, b3bit1, b3bit3, b3bit5;
};

PropFlags decode(const NodeProps& p) {
    PropFlags f{};
    f.hasDuration = p.b1 & 0x01;
    f.setFlagA = p.b1 & 0x02;
    f.clearHit = p.b1 & 0x08;
    f.onConnectOnce = p.b1 & 0x10;
    f.onConnectKeepA = p.b1 & 0x20;
    f.onConnectKeepB = p.b1 & 0x40;
    f.onConnectKeepC = p.b1 & 0x80;
    f.commit = p.b2 & 0x02;
    f.fallToState = p.b2 & 0x04;
    f.runHeader = p.b2 & 0x08;
    f.b3bit1 = p.b3 & 0x02;
    f.countUse = p.b3 & 0x04;
    f.b3bit3 = p.b3 & 0x08;
    f.attack = p.b3 & 0x10;
    f.b3bit5 = p.b3 & 0x20;
    f.hasStrength = p.b3 & 0x40;
    return f;
}

// Runs `chain`; if the script flipped the facing the horizontal velocity flips with it (the engine keeps the motion physical).
void runScript(Object& o, World& w, const Chain& chain) {
    if (chain.empty()) return;
    Host host;
    vm::Context cx;
    makeContext(o, w, host, cx);
    const float before = o.slots[56];
    vm::runChain(chain, cx);
    if (o.slots[56] != before) o.slots[158] = -o.slots[158];
}

// Runs a chain, then puts back every slot the mode says must not survive it.
void runScriptRestoring(Object& o, World& w, const Chain& chain, int mode) {
    float saved[kSlotCount];
    std::memcpy(saved, o.slots, sizeof saved);
    runScript(o, w, chain);
    const bool keepSet[kSlotCount] = {};
    (void)keepSet;
    for (size_t i = 0; i < kSlotCount; ++i) {
        bool keep;
        switch (mode) {
            case 2: keep = (i == 0x5d || i == 0x54 || i == 0x56 || i == 0x5b); break;   // only the damage numbers stay
            case 4: keep = !(i == 0xa0 || i == 0x9e || i == 0xa1 || i == 0x9f); break;  // everything but the motion stays
            default: keep = !(i == 0xa0 || i == 0x9e || i == 0xa1 || i == 0x9f); break;
        }
        if (!keep) o.slots[i] = saved[i];
    }
}

}  // namespace

// The combo a character suffers ends the moment it runs a node of a free (actionable) state: counters back to 0 and the damage
// scale back to 1 (what the shipped engine does for every record that carries the "free" bit).
static void comboReset(Object& o) {
    if (o.comboHits != 0) {
        o.comboHits = 0;
        o.slots[22] = 0;
        o.slots[12] = 0;
        if (o.enemy) o.enemy->slots[11] = 0;
    }
    o.slots[83] = 1.0f;
}

static void execRecord(Object& o, World& w, const NodeProps& R, bool head, int nodeI, int propI, bool* commit) {
    const PropFlags f = decode(R);
    if (f.b3bit5) comboReset(o);
    if (head) {
        o.fa = static_cast<uint8_t>((o.fa & ~fl::A_Flag0) | (f.setFlagA ? fl::A_Flag0 : 0));
    } else if (!f.setFlagA) {
        o.fa &= static_cast<uint8_t>(~fl::A_Flag0);
    }
    if (f.attack && o.leader) o.leader->fb |= fl::B_Attack;
    if (f.hasStrength) o.strength = R.x1a;
    if (f.commit) *commit = true;
    if (!head && f.hasDuration) {
        o.nodeTimer += R.dur - o.nodeDur;
        o.nodeDur = R.dur;
    }
    if (f.clearHit) {
        o.fa &= static_cast<uint8_t>(~fl::A_HitActive);
        o.hitState = o.hitNode = o.hitProp = -1;
    }
    if (R.hasHit) {
        bool arm = true;
        if (head) {
            if (!o.nodeHasHit) arm = false;
            else o.nodeHasHit = false;
        }
        if (arm) {
            o.fa = static_cast<uint8_t>((o.fa | fl::A_HitActive) & ~fl::A_HasHit);
            o.hitState = o.state;
            o.hitNode = nodeI;
            o.hitProp = propI;
        }
    }
    if (f.countUse) ++o.moveUses;
    // the node's event list (sounds / effects by name hash) goes to the host; capped so a runaway script cannot grow it without bound
    if (!R.u32s.empty() && w.events.size() < 4096)
        for (uint32_t h : R.u32s) w.events.push_back(Event{w.stats.ticks, o.id, h, o.x, o.y});
    // children / helpers this record creates
    for (const SubItem& it : R.sub) {
        if (it.a == 0xFFFF) continue;  // no child: only a script slot
        spawn(o, w, static_cast<int>(it.a), static_cast<float>(static_cast<int32_t>(it.b)),
              static_cast<float>(static_cast<int32_t>(it.c)), it.chain.empty() ? nullptr : &it.chain);
    }
    // scripts that only run when the move connected
    if (o.pendingChain && (f.onConnectKeepA || f.onConnectKeepB || f.onConnectKeepC)) {
        const int mode = f.onConnectKeepA ? 2 : (f.onConnectKeepB ? 4 : 8);
        runScriptRestoring(o, w, *o.pendingChain, mode);
    }
    if (o.pendingChain && f.onConnectOnce) {
        runScript(o, w, *o.pendingChain);
        o.pendingChain = nullptr;
    }
    // the state's header script, then the node's own
    if (f.runHeader && !o.st().hdr.chain.empty()) runScript(o, w, o.st().hdr.chain);
    runScript(o, w, R.chain);
}

static void nodeExec(Object& o, World& w) {
    const State& S = o.st();
    const Node* node = curNode(o);
    const int nodeI = o.nodeIdx < 0 ? 0 : std::min<int>(o.nodeIdx, std::max(0, nodeCount(o) - 1));

    o.fb &= static_cast<uint8_t>(~fl::B_Attack);
    refreshSlots(o, w);
    o.fa &= static_cast<uint8_t>(~0x30);
    o.fb &= static_cast<uint8_t>(~(fl::B_GotHit | fl::B_Blocking));
    o.strength = o.def->x373;
    for (int s : {61, 63, 58, 62, 115, 116, 120, 107, 110, 147, 150, 151, 136, 140}) o.slots[s] = 0;

    bool commit = false;
    if (node && !node->props.empty()) {
        Host host;
        vm::Context cx;
        const std::vector<NodeProps>* list = &node->props;
        size_t ri = 0;
        const Command* gate = nullptr;
        bool extraDone = false;
        const bool head0FallToState = decode(node->props[0]).fallToState;
        for (;;) {
            const NodeProps& R = (*list)[ri];
            bool run = gate == nullptr || o.ring.capacity() == 0;
            if (!run) {
                makeContext(o, w, host, cx);
                int age = 0, ticks = 0;
                run = matchCommand(*gate, o.ring, clampWindow(o.slots[141]), !facingMirrored(o), cx, &age, &ticks);
                if (run) o.slots[146] = static_cast<float>(ticks + age);
            }
            if (static const char* tr = std::getenv("ZRT_TRACE_REC"); tr && (tr[0] == 'a' || o.def != &o.ch->body))
                std::fprintf(stderr, "  [rec] tick %llu obj %d %s node %d rec %zu run=%d hit=%d gate=%d cap=%d\n",
                             static_cast<unsigned long long>(w.stats.ticks), o.id, o.st().name.c_str(), nodeI, ri, run, R.hasHit,
                             gate != nullptr, static_cast<int>(o.ring.capacity()));
            if (run) execRecord(o, w, R, list == &node->props && ri == 0, nodeI, static_cast<int>(ri), &commit);
            const Command* next = R.hasCmd ? &R.cmd : nullptr;
            if (!next) {
                if (!extraDone && head0FallToState) {
                    extraDone = true;
                    if (!S.hdr.hasCmd || S.hdr.cmdNode.empty()) break;
                    list = &S.hdr.cmdNode;
                    ri = 0;
                    gate = &S.hdr.cmd;
                    continue;
                }
                break;
            }
            ++ri;
            if (ri >= list->size()) break;
            gate = next;
        }
    }
    if (commit) o.fb |= fl::B_Commit;

    // SPRITESTATE (1 = in the air, 2 = on the ground) is set by the engine when no script chose it this tick; the transition
    // conditions of the ground and air normals read it, including in the second look below
    if (static_cast<int>(o.slots[62]) == 0) o.slots[62] = (!o.onGround || o.accY > 0) ? 1.0f : 2.0f;

    // a script asked for another pass (SKIPPED): look at the transitions again, skip the rest of the tick
    if (o.slots[58] != 0) {
        evaluateTransitions(o, w, nullptr);
        return;
    }

    // timers that count down by themselves
    for (int s : {128, 129, 130, 137, 138, 43}) o.slots[s] = toward(o.slots[s], 1);
    if (o.slots[42] != 0 && o.slots[52] <= 0) o.slots[42] = toward(o.slots[42], 1);

    // physics
    const int type = static_cast<int>(o.slots[59]);
    if (type != 4) {
        const bool right = o.slots[56] == 1.0f;
        o.accX += right ? o.slots[158] : -o.slots[158];
        o.accY += o.slots[159];
        auto friction = [&]() {
            const float f = o.slots[55];
            const float v = o.slots[158];
            if (v > f) o.slots[158] = v - f;
            else if (-f <= v) o.slots[158] = 0;
            else o.slots[158] = v + f;
        };
        if (type == 1) {
            o.slots[159] -= o.slots[54];
            friction();
        } else if (type == 2) {
            o.slots[159] -= o.slots[54];
        } else if (type == 3) {
            friction();
        }
    }
    // displacement written by scripts into XPOS / YPOS (relative to the facing)
    if (o.slots[56] == 1.0f) o.accX += o.slots[160] - o.x;
    else o.accX += o.x - o.slots[160];
    o.accY += o.slots[161] - o.y;
}

// ======================================================================================================================
// state step
// ======================================================================================================================
static bool transitionReady(Object& o, World& w, const Transition& t) {
    const std::vector<Step>& st = t.cmd.steps;
    if (st.empty() || st[0].chain.empty()) return true;
    Host host;
    vm::Context cx;
    makeContext(o, w, host, cx);
    return vm::testChain(st[0].chain, cx);
}

// ======================================================================================================================
// throws: the link between the thrower (`target`) and the held defender (`attacker`)
//   * the defender sits in its throw state (the character's refBf, "Throw_Stun") and shows node THROW-1 of it: the thrower's
//     scripts pick the pose with the THROW slot (1-based), place the defender with THROWX / THROWY (forward / up from the thrower)
//   * THROWADDHITS / THROWADDDMG (this tick only) add hits and scaled damage to the defender; a throw never kills, it leaves 1 HP and
//     flags THROWFATAL; THROWFINALIZE then turns that flag into a KO
//   * THROW < 0 = one last placement, then the defender is let go; THROW == 0 (a state change zeroes it) lets go straight away
// ======================================================================================================================
static void stepOnce(Object& o, World& w);

static bool heldByThrow(const Object& o) {
    return o.attacker != nullptr && o.attacker->target == &o && o.def && o.state == static_cast<int>(o.def->refBf);
}

static void releaseThrow(Object& a) {
    Object* d = a.target;
    if (!d) return;
    if (d->attacker == &a) {
        d->attacker = nullptr;
        d->slots[10] = 0;    // THROWN
        d->slots[119] = 0;   // THROWFATAL
        d->slots[122] = 0;
    }
    a.target = nullptr;
    a.slots[9] = 0;
}

static void placeHeld(Object& a, Object& d) {
    if (heldByThrow(d)) {
        const int pose = std::max(0, static_cast<int>(a.slots[111]) - 1);
        setCursor(d, pose);
    }
    // forward offset mirrors with the thrower's facing, the height offset is added to the thrower's height
    const float fwd = a.slots[56] == 1.0f ? 1.0f : -1.0f;
    d.x = a.x + fwd * a.slots[112];
    d.y = a.y + a.slots[113];
    d.accX = d.accY = 0;
    static const bool trace = std::getenv("ZRT_TRACE_THROW") != nullptr;
    if (trace)
        std::fprintf(stderr, "  [throw] a x=%.1f face=%.0f THROW=%.0f X=%.0f Y=%.0f -> d x=%.1f y=%.1f pose %d\n", a.x, a.slots[56], a.slots[111],
                     a.slots[112], a.slots[113], d.x, d.y, d.nodeIdx);
}

// `final` = the pass that ends the tick (no SKIPPED request): only that pass decides whether the defender is let go; every pass
// places the defender and applies what its node asked for (a script-only node can carry the damage of a hit)
static void throwLinkUpdate(Object& a, World& w, bool final) {
    Object* d = a.target;
    if (!d || d->attacker != &a) {
        a.target = nullptr;
        return;
    }
    if (final && (a.fb & fl::B_Commit)) {
        releaseThrow(a);
        return;
    }
    float thr = a.slots[111];
    if (thr == 0 && final) {
        releaseThrow(a);
        return;
    }
    const bool last = thr < 0 && final;
    if (last) a.slots[111] = thr = -thr;
    placeHeld(a, *d);

    Object* victim = d->leader ? d->leader : d;
    const int hits = static_cast<int>(a.slots[115]);
    if (hits > 0) {
        Object* striker = a.leader ? a.leader : &a;
        striker->slots[11] += static_cast<float>(hits);
        victim->slots[12] = striker->slots[11];
        ++w.stats.hits;
        victim->comboHits += hits;  // (a throw prorates through its own scripts: T.DAMAGESCALE is halved at its end)
    }
    const float rawDamage = a.slots[116];
    if (rawDamage != 0) {
        const float floorScale = std::fabs(rawDamage) < 1000.0f ? 0.2f : 0.275f;
        const float scale = std::max(floorScale, victim->slots[83]);
        const int dmg = static_cast<int>(std::fabs(rawDamage) * scale) * (rawDamage < 0 ? -1 : 1);
        const float before = victim->life();
        victim->life() -= static_cast<float>(dmg);
        static const bool trT = std::getenv("ZRT_TRACE_THROW") != nullptr;
        if (trT) std::fprintf(stderr, "  [throw] damage raw %.1f scale %.3f -> %d (hp %.0f -> %.0f, comboHits %d)\n", rawDamage, scale, dmg, before, victim->life(), victim->comboHits);
        if (before > 0 && victim->life() <= 0) {
            victim->slots[119] = 1.0f;  // the throw would have been fatal
            victim->life() = 1.0f;
        }
        float maxLife = kStartHealth;
        if (victim->def && victim->def->slotDefaultsValid) std::memcpy(&maxLife, &victim->def->slotDefaults[64], sizeof maxLife);
        if (victim->life() > maxLife && maxLife > 0) victim->life() = maxLife;  // a negative THROWADDDMG heals, up to the full bar
    }
    if (static_cast<int>(a.slots[120]) >= 1 && victim->slots[119] != 0) victim->life() = 0;  // THROWFINALIZE on a fatal throw
    if (last) releaseThrow(a);
}

static void startThrow(Object& a, Object& d, World& w) {
    // whoever held the defender or was held by it lets go first
    if (d.attacker && d.attacker->target == &d) releaseThrow(*d.attacker);
    if (a.target) releaseThrow(a);
    a.target = &d;
    d.attacker = &a;
    d.fb &= static_cast<uint8_t>(~(fl::B_GotHit | fl::B_Blocking));
    d.slots[10] = 1.0f;
    a.slots[1] += 1.0f;  // NUMHITS: the move connected
    if (d.def && d.def->refBf < d.def->states.size() && d.state != static_cast<int>(d.def->refBf)) enterState(d, w, d.def->refBf);
    // the thrower looks at its transitions again right away (Throw_* -> ThrowSuccess_* tests THROWING) and takes that step
    evaluateTransitions(a, w, nullptr);
    stepOnce(a, w);
    int passes = 0;
    for (; a.slots[58] != 0 && !(a.fb & fl::B_Commit) && passes < 500; ++passes) stepOnce(a, w);
    if (a.target == &d && a.slots[111] != 0) placeHeld(a, d);
}

static void stepOnce(Object& o, World& w) {
    if (o.slots[58] == 0) o.fc |= 0x40;
    const float hp = o.slots[52];
    if (!(hp < 1.0f || (o.fb & fl::B_GotHit) || o.slots[58] != 0)) {
        // hitstop: the object is frozen
        o.slots[52] = hp - 1.0f;
        if (o.slots[52] < 0) o.slots[52] = 0;
        return;
    }
    const State& S = o.st();
    int chosen = -1;
    const int n = static_cast<int>(std::min<size_t>(S.transitions.size(), kTransWords * 32));
    for (int i = 0; i < n; ++i) {
        if (bit(o, i) && transitionReady(o, w, S.transitions[static_cast<size_t>(i)])) {
            chosen = i;
            break;
        }
    }
    if (chosen >= 0) {
        ++w.stats.transitions;
        o.fresh = false;
        enterState(o, w, S.transitions[static_cast<size_t>(chosen)].target);
        if (nodeCount(o) <= o.nodeIdx) enterState(o, w, o.st().next);
    } else if (o.fresh) {
        o.fresh = false;  // first step of a new object: node 0 has not run yet
    } else if (heldByThrow(o)) {
        // the thrower picks the pose (node) this object shows while it is held; the node timer does not advance by itself
    } else {
        if (--o.nodeTimer < 1) {
            setCursor(o, o.nodeIdx + 1);
            // an index past the last node (setCursor clamps the timer to the last node) leaves the state
        }
        if (o.nodeIdx >= nodeCount(o)) enterState(o, w, o.st().next);
    }
    nodeExec(o, w);
    if (o.target) throwLinkUpdate(o, w, o.slots[58] == 0);
}

// ======================================================================================================================
// tethers
// ======================================================================================================================
// TETHEREDTO (57) ties an object to another one; the shipped engine applies it once per tick, after the scripts ran and before the
// gathered movement is committed:
//   1   ride along: the object moves by what its parent moved since the last tick (59 helpers of the shipped data are born with it)
//   7 / 8 / 11   stay on a named point of the current frame of the leader / the parent / the attacker.  TETHERPT (45) holds the name hash as
//        raw bits; the point's offset from the frame's pivot is used (mirrored when the object faces left, y up), a frame without that
//        point puts the object on the pivot itself.  This is how Umbrella's Hungern follows her: the umbrella frames carry the point, the
//        frames where it is put away carry it far below the floor.
//   4 / 5 / 6   one-shot: jump to a random stored 16x16 cell of the current sprite of the leader (4, 6) / the attacker (5) - sparkles
//        and glows that appear on the body - then TETHEREDTO goes back to 0
//   3   anchored to the screen (needs the game camera), 2 / 9 / 10 other relatives: not reproduced yet (the object just stays where
//        its scripts put it)
void snapToCell(Object& o, const Object& t) {
    float dx = 0, dy = 0;
    if (const Frame* f = frameOf(t)) {
        const int slot = !f->img[1].placeholder() ? 1 : 0;
        const Image& im = f->img[slot];
        const int total = static_cast<int>(im.w) * static_cast<int>(im.h);
        auto stored = [&](int j) {
            return j >= 0 && j < total && static_cast<size_t>(j / 8) < im.mask.size() && ((im.mask[static_cast<size_t>(j / 8)] >> (j % 8)) & 1) != 0;
        };
        if (!im.placeholder() && total > 0) {
            const int idx = vm::scriptRandom(o.rng, static_cast<float>(total));
            int pick = -1;
            if (stored(idx)) pick = idx;
            for (int k = 1; k < total && pick < 0; ++k) {  // the nearest stored cell, looking forward first
                if (stored(idx + k)) pick = idx + k;
                else if (stored(idx - k)) pick = idx - k;
            }
            if (pick >= 0) {
                dx = static_cast<float>(f->drawX(slot)) + (static_cast<float>(pick % im.w) + 0.5f) * 16.0f;
                dy = -(static_cast<float>(f->drawY(slot)) + (static_cast<float>(pick / im.w) + 0.5f) * 16.0f);
                if (t.slots[56] != 1.0f) dx = -dx;
            }
        }
    }
    o.accX = t.x + dx - o.x;
    o.accY = t.y + dy - o.y;
}

void tetherStep(Object& o) {
    const int mode = static_cast<int>(o.slots[57]);
    if (mode != 1) o.tethOk = false;
    if (mode == 4 || mode == 5 || mode == 6) {
        o.slots[57] = 0;
        const Object* t = mode == 5 ? o.attacker : o.leader;
        if (t && t != &o) snapToCell(o, *t);
        return;
    }
    if (mode == 1) {
        const Object* p = o.parent;
        if (!p || p == &o) return;
        if (o.tethOk) {
            o.accX += p->x - o.tethX;
            o.accY += p->y - o.tethY;
        }
        o.tethX = p->x;
        o.tethY = p->y;
        o.tethOk = true;
        return;
    }
    if (mode != 7 && mode != 8 && mode != 11) return;
    const Object* t = mode == 7 ? o.leader : (mode == 8 ? o.parent : o.attacker);
    if (!t || t == &o) return;  // tethered to itself: nothing to follow
    float dx = 0, dy = 0;
    if (const Frame* f = frameOf(*t)) {
        uint32_t hash;
        std::memcpy(&hash, &o.slots[45], sizeof hash);
        for (const auto& e : f->entries)
            if (e[0] == hash) {
                dx = static_cast<float>(static_cast<int32_t>(e[1]) - f->pivotX);
                dy = -static_cast<float>(static_cast<int32_t>(e[2]) - f->pivotY);
                break;
            }
    }
    if (o.slots[56] != 1.0f) dx = -dx;
    o.accX = t->x + dx - o.x;
    o.accY = t->y + dy - o.y;
}

// ======================================================================================================================
// tick
// ======================================================================================================================
void tick(Object& o, World& w, const Sample* input) {
    if (!o.alive || !o.def) return;
    ++w.stats.ticks;
    const int startState = o.state, startNode = o.nodeIdx, startTimer = o.nodeTimer, startDur = o.nodeDur;
    const uint64_t startChanges = w.stats.stateChanges;
    evaluateTransitions(o, w, input);

    stepOnce(o, w);
    int passes = 0;
    for (; o.slots[58] != 0 && !(o.fb & fl::B_Commit) && passes < 500; ++passes) stepOnce(o, w);
    if (passes >= 500) ++w.stats.loopCaps;
    // A helper that follows its leader through router states (the Hungern sprite goes Idle -> Branch -> Idle every tick because its
    // table lists its own posture too) must keep playing its animation: when the tick ends in the state it started in after
    // a round trip, it carries on from where it was instead of restarting at node 0.
    if (o.leader != &o && !(o.fb & fl::B_Commit) && o.state == startState && w.stats.stateChanges - startChanges >= 2 && nodeCount(o) > 1 &&
        startNode >= 0 && startNode < nodeCount(o)) {
        o.nodeIdx = startNode;
        o.nodeTimer = startTimer;
        o.nodeDur = startDur;
        if (--o.nodeTimer < 1) {
            setCursor(o, o.nodeIdx + 1);
            if (o.nodeIdx >= nodeCount(o)) setCursor(o, 0);
        } else {
            setCursor(o, o.nodeIdx);
            o.nodeTimer = startTimer - 1;
        }
    }
    if (o.leader != &o && (o.fb & fl::B_Commit)) {  // a helper that ran a "commit" record has finished: it leaves the match
        o.alive = false;
        return;
    }
    o.fb &= static_cast<uint8_t>(~fl::B_Commit);

    if (o.slots[44] != 0) o.slots[44] = toward(o.slots[44], 1);
    if (static_cast<int>(o.slots[52]) < 1 && o.slots[53] > 0) o.slots[53] = static_cast<float>(static_cast<int>(o.slots[53]) - 1);

    tetherStep(o);

    // apply the gathered movement
    o.x += o.accX;
    o.y += o.accY;
    o.accX = o.accY = 0;
    if (o.slots[163] != 0) {  // SOLIDWALLS
        if (o.x < w.leftWall) o.x = w.leftWall;
        if (o.x > w.rightWall) o.x = w.rightWall;
    }
    // TOUCHSCREENLR (13), as the shipped engine computes it (0x49b500, end of the physics step): it is relative to the way the object
    // faces, positive = the screen edge in front of it, negative = the edge behind it.  1 = the object's push box touches the edge (within 2
    // pixels), 2 = it is entirely beyond it; a point character gets the extra half (1.5 / 2.5) when the edge is the end of the stage.  The
    // walls of this runtime are the pivot limits, so "touching" is the pivot within 2 pixels of a wall.
    {
        float lr = 0;
        const bool cornerChar = o.leader == &o && o.slots[27] != 0;
        if (o.x < w.rightWall - 2) {
            if (o.x > w.leftWall + 2) lr = 0;
            else lr = -(o.x < w.leftWall ? 2.0f : 1.0f) - (cornerChar ? 0.5f : 0.0f);
        } else {
            lr = (o.x > w.rightWall ? 2.0f : 1.0f) + (cornerChar ? 0.5f : 0.0f);
        }
        if (o.slots[56] != 1.0f) lr = -lr;
        o.slots[13] = lr;
    }
    // PUSHBACK (95 time, 96 speed, 97 change per tick, 98 always): while the time lasts, outside hit-stop, the object that hit us is
    // shoved away from us by the whole-pixel speed - always with PUSHBACKALWAYS, otherwise only when our back is against a solid wall
    // (that is the corner pushback; the defender's own slide is done by the XVEL its hit scripts set).  Speed and time decay by themselves.
    if (o.slots[95] > 0 && static_cast<int>(o.slots[52]) == 0) {
        Object* pusher = o.attacker ? (o.attacker->leader ? o.attacker->leader : o.attacker) : nullptr;
        const bool cornered = o.slots[163] != 0 && o.slots[13] < 0;
        if (pusher && pusher != &o && (o.slots[98] != 0 || cornered)) {
            int mv = static_cast<int>(o.slots[96]);
            if (o.slots[56] != 1.0f) mv = -mv;
            pusher->accX += static_cast<float>(mv);
        }
        o.slots[96] += o.slots[97];
        o.slots[95] -= 1.0f;
    }
    if (o.slots[162] != 0 && o.y <= w.floorY) {  // SOLIDFLOOR
        o.y = w.floorY;
        o.onGround = true;
    } else {
        o.onGround = false;
    }
}

// ======================================================================================================================
// boxes
// ======================================================================================================================
namespace {

const Frame* frameOf(const Object& o) {
    if (!o.ch || o.frame <= 0 || static_cast<size_t>(o.frame) > o.ch->frames.size()) return nullptr;
    return &o.ch->frames[static_cast<size_t>(o.frame) - 1];
}

struct Rect {
    float x0, x1;  // left / right
    float y0, y1;  // bottom / top (y grows upwards)
};

// A box is stored relative to the pivot with y growing downwards and x as if the object faced right.
Rect worldRect(const Object& o, const Box& b) {
    Rect r;
    if (o.slots[56] == 1.0f) {
        r.x0 = o.x + static_cast<float>(b.left);
        r.x1 = o.x + static_cast<float>(b.right);
    } else {
        r.x0 = o.x - static_cast<float>(b.right);
        r.x1 = o.x - static_cast<float>(b.left);
    }
    r.y0 = o.y - static_cast<float>(b.bottom);
    r.y1 = o.y - static_cast<float>(b.top);
    return r;
}

bool overlap(const Rect& a, const Rect& b) { return a.x0 <= b.x1 && b.x0 <= a.x1 && a.y0 <= b.y1 && b.y0 <= a.y1; }

const HitData* armedHit(const Object& o) {
    if (!(o.fa & fl::A_HitActive) || o.hitState < 0 || o.hitNode < 0 || o.hitProp < 0) return nullptr;
    if (static_cast<size_t>(o.hitState) >= o.def->states.size()) return nullptr;
    const auto& nodes = o.def->states[static_cast<size_t>(o.hitState)].hdr.nodes;
    if (static_cast<size_t>(o.hitNode) >= nodes.size()) return nullptr;
    const auto& props = nodes[static_cast<size_t>(o.hitNode)].props;
    if (static_cast<size_t>(o.hitProp) >= props.size() || !props[static_cast<size_t>(o.hitProp)].hasHit) return nullptr;
    return &props[static_cast<size_t>(o.hitProp)].hit;
}

// Does any hit box of `a` touch any hurt box of `d`?
bool hitTouches(const Object& a, const Object& d) {
    const Frame* fa = frameOf(a);
    const Frame* fd = frameOf(d);
    if (!fa || !fd) return false;
    for (const Box& hb : fa->hit) {
        const Rect hr = worldRect(a, hb);
        for (const Box& ub : fd->hurt)
            if (overlap(hr, worldRect(d, ub))) return true;
    }
    return false;
}

Object* characterOf(Object* o) { return o ? (o->leader ? o->leader : o) : nullptr; }

}  // namespace

int parseStrengthTable(const std::string& text, float* table) {
    int set = 0;
    size_t i = 0;
    std::vector<std::string> tok;
    while (i < text.size()) {
        size_t e = text.find('\n', i);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(i, e - i);
        i = e + 1;
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        size_t p = 0;
        while (p < line.size()) {
            while (p < line.size() && std::isspace(static_cast<unsigned char>(line[p]))) ++p;
            size_t q = p;
            while (q < line.size() && !std::isspace(static_cast<unsigned char>(line[q]))) ++q;
            if (q > p) tok.push_back(line.substr(p, q - p));
            p = q;
        }
    }
    // the engine walks the tokens two at a time: a name whose first letter is L / M / H, then its number
    for (size_t k = 0; k + 1 < tok.size(); k += 2) {
        const int c = std::toupper(static_cast<unsigned char>(tok[k][0]));
        const int idx = c == 'L' ? 1 : c == 'M' ? 2 : c == 'H' ? 3 : -1;
        if (idx < 0) break;
        table[idx] = static_cast<float>(std::atoi(tok[k + 1].c_str()));
        ++set;
    }
    return set;
}

// ======================================================================================================================
// hit resolution (what the shipped engine does when an armed hit data touches the enemy)
//   * the touch needs hit box x hurt box and the defender must not be invincible to the hit's level mask (INVINCIBLE & f13)
//   * blocked = the defender's BLOCKTYPE mask shares a bit with the level mask (a blocking state sets BLOCKTYPE)
//   * a grounded defender (SPRITESTATE 2, on the floor, not rising) takes the second chain + the second pair of result codes, any
//     other defender the first chain + pair; result code 0 = no effect, 1 = hit, 2 = block, 3 = throw (handled with the throws), 4 = touch only
//   * effects: hitstop on both, GOTHIT / BLOCKED on the defender, the chain is left for the defender's own hit states
//     (`pendingChain`), hit info slots (hitstun, damage, stun, meter, pushback ...) are handed to the defender
// ======================================================================================================================
void resolveHits(World& w) {
    for (Object* a : w.objects) {
        if (!a->alive || !a->def) continue;
        const HitData* h = armedHit(*a);
        if (!h || (a->fa & fl::A_HasHit)) continue;
        Object* d = a->enemy ? a->enemy : (a->leader ? a->leader->enemy : nullptr);
        if (!d || d == a || !d->alive || !d->def) continue;
        if (!hitTouches(*a, *d)) continue;
        static const bool traceHits = std::getenv("ZRT_TRACE") != nullptr;
        if (traceHits) {
            const Frame* fa = frameOf(*a);
            const Frame* fd = frameOf(*d);
            std::fprintf(stderr, "  [trace] touch A x=%.1f frame %d (%zu hit boxes) -> D x=%.1f frame %d (%zu hurt)\n", a->x, a->frame,
                         fa ? fa->hit.size() : 0, d->x, d->frame, fd ? fd->hurt.size() : 0);
            if (fa && !fa->hit.empty()) {
                const Rect r = worldRect(*a, fa->hit[0]);
                std::fprintf(stderr, "          A hit0 x[%.1f,%.1f] y[%.1f,%.1f]\n", r.x0, r.x1, r.y0, r.y1);
            }
            if (fd)
                for (const Box& ub : fd->hurt) {
                    const Rect r = worldRect(*d, ub);
                    std::fprintf(stderr, "          D hurt x[%.1f,%.1f] y[%.1f,%.1f]\n", r.x0, r.x1, r.y0, r.y1);
                }
            if (fd && !fd->hurt.empty()) {
                const Rect r = worldRect(*d, fd->hurt[0]);
                std::fprintf(stderr, "          D hurt0 x[%.1f,%.1f] y[%.1f,%.1f]\n", r.x0, r.x1, r.y0, r.y1);
            }
        }
        const int level = h->f13;
        if ((static_cast<int>(d->slots[61]) & level) != 0) continue;  // invincible to this kind of hit
        // the hit's own condition (seen from the defender: e.g. "not in a state tagged X and alive", "is the point character")
        if (h->hasChain[2]) {
            Host host;
            vm::Context cx;
            makeContext(*d, w, host, cx);
            if (!vm::testChain(h->chains[2], cx)) continue;
        }
        const bool blocked = (static_cast<int>(d->slots[60]) & level) != 0;
        const bool ground = static_cast<int>(d->slots[62]) == 2 && d->slots[5] != 0 && d->slots[159] <= 0;
        // the first pair of result codes and the first chain are for a defender that is NOT standing on the floor, the second pair and
        // chain for a grounded one (a ground throw is (0,0 | 3,3): it grabs grounded opponents only)
        const int code = ground ? (blocked ? h->f17 : h->f16) : (blocked ? h->f15 : h->f14);
        const int ci = ground ? 1 : 0;
        a->fa |= fl::A_HasHit;
        a->slots[3] += 1.0f;  // NUMTOUCHES
        ++w.stats.touches;
        // the effect (spark) the hit asks for; its chain 3 says when it is shown
        auto spark = [&](uint8_t kind) {
            if (h->f0a == 0xFFFE || w.events.size() >= 4096) return;
            if (h->hasChain[3]) {
                Host host;
                vm::Context cx;
                makeContext(*d, w, host, cx);
                if (!vm::testChain(h->chains[3], cx)) return;
            }
            Event e;
            e.tick = w.stats.ticks;
            e.object = a->id;
            e.hash = h->f0a;
            e.x = d->x;
            e.y = d->y;
            e.kind = kind;
            w.events.push_back(e);
        };
        if (code == 3) {  // throw: the defender is held by the attacker (the grab does not trade hitstop)
            if (!(d->attacker && d->attacker->target == d)) {
                startThrow(*a, *d, w);
                spark(Event::HitSpark);
            }
            continue;
        }
        if (code != 1 && code != 2) continue;
        const bool isBlock = code == 2;
        spark(isBlock ? Event::BlockSpark : Event::HitSpark);

        a->slots[isBlock ? 2 : 1] += 1.0f;  // NUMBLOCKS / NUMHITS
        d->fb |= fl::B_GotHit;
        if (isBlock) d->fb |= fl::B_Blocking;
        else d->fb &= static_cast<uint8_t>(~fl::B_Blocking);
        d->attacker = a;
        d->pendingChain = h->hasChain[ci] ? &h->chains[ci] : nullptr;
        d->pendDef = a->def;
        d->pendState = a->hitState;
        d->pendNode = a->hitNode;
        d->pendProp = a->hitProp;
        d->pendCi = ci;

        // Hit info the engine writes into the defender (shipped engine: the hit-apply routine at 0x4a05a0).  The defender's own hit /
        // block-stun states then take the numbers off LIFEBAR, REDLIFE, TOTALSTUN and the super bar with their scripts.
        //   scale  = the attacking character's DAMAGEMULT, times the defender's ASSISTDMGMOD while the defender is not the point character
        //   DAMAGE = whole(fc x combo proration) x scale        CHIPDAMAGE = fe x scale (not prorated)      85 = fc as it is in the data
        //   NOCOUNTERS / LastHitCannotKill come from the hit's flag byte; BASESTUN = stun of the hit strength, STUNDAMAGE only against the
        //   point character; METERGAIN / BASEMETERGAIN = the hit's meter value, nothing once the defender's life is gone
        const uint8_t hf = h->flags19();
        Object* attackerChar = characterOf(a);
        Object* victim = characterOf(d);
        float scale = attackerChar->slots[68];
        if (d->slots[27] == 0) scale *= d->slots[69];
        // the engine clears the hints the hit's own chain (run next) fills in: AIRHINT 91, ANIMHINT 93, PUSHBACK* 95..98; WASHITBYASSIST 94 says
        // whether the attacking character is not the point character (an assist)
        d->slots[70] = d->slots[132];  // WASCH: the defender was in a counter-hit window (its own COUNTERHIT) when it was hit
        d->slots[91] = d->slots[93] = 0.0f;
        d->slots[94] = (attackerChar->slots[27] == 0) ? 1.0f : 0.0f;
        d->slots[95] = d->slots[96] = d->slots[97] = d->slots[98] = 0.0f;
        d->slots[53] = static_cast<float>(h->f8);  // HITSTUN
        const float floorScale = static_cast<float>(h->fc) < 1000.0f ? 0.2f : 0.275f;
        const float prorate = std::max(floorScale, victim->slots[83]);
        const float damage = std::floor(static_cast<float>(h->fc) * prorate) * scale;
        d->slots[84] = damage;                                              // DAMAGE
        d->slots[85] = static_cast<float>(h->fc);
        d->slots[86] = static_cast<float>(h->fe) * scale;                   // CHIPDAMAGE
        d->slots[90] = (hf & 8) ? 1.0f : 0.0f;                              // NOCOUNTERS
        d->slots[21] = ((hf & 1) && d->leader == d && d->slots[27] != 0) ? 1.0f : 0.0f;  // LastHitCannotKill
        const float baseStun = h->f18 < 8 ? w.strengthTable[h->f18] : 0.0f;
        d->slots[88] = baseStun;                                            // BASESTUN
        d->slots[87] = d->slots[27] != 0 ? baseStun : 0.0f;                 // STUNDAMAGE
        float gain;
        std::memcpy(&gain, &h->f2c, sizeof gain);
        if (d->life() <= 0) gain = std::min(gain, 0.0f);
        d->slots[89] = gain;                                                // METERGAIN
        d->slots[92] = gain;                                                // BASEMETERGAIN
        // the engine only hands DAMAGE / CHIPDAMAGE over: the defender's hit / block-stun states take them off LIFEBAR with their own scripts
        if (!isBlock) {
            ++w.stats.hits;
            Object* attacker = attackerChar;
            attacker->slots[11] += 1.0f;
            victim->slots[12] = attacker->slots[11];
            // proration: from the 3rd hit of a combo on every further hit scales the next ones down (x0.875, floor 0.2)
            if (++victim->comboHits > 2) victim->slots[83] = std::max(0.2f, victim->slots[83] * 0.875f);
        }
        if (isBlock) ++w.stats.blocks;

        // hitstop: both fighters freeze for the hit data's hitstop ticks
        if (h->f10 > 0) {
            a->slots[52] = static_cast<float>(h->f10);
            d->slots[52] = static_cast<float>(h->f10);
            if (a->leader && a->leader != a) a->leader->slots[52] = static_cast<float>(h->f10);
        }
    }
}

// ======================================================================================================================
// push boxes: fighters never stand inside each other; the stage walls win over the push
// ======================================================================================================================
void resolvePush(World& w) {
    Object* c[2] = {nullptr, nullptr};
    int n = 0;
    for (Object* o : w.objects)
        if (o->alive && o->def && o->leader == o && (!w.teams || o->slots[27] != 0) && n < 2) c[n++] = o;
    if (n < 2) return;
    // once the round is over the fighters pass through each other: WinPose_RunAway (Fukua) runs through the opponent and off the screen, and
    // the pose only ends when TOUCHSCREENLR reaches 2 (assumed rule: the data has no other way to let it pass a standing loser)
    if (c[0]->slots[30] > 0 || c[1]->slots[30] > 0) return;
    // a held defender is placed by its thrower: no pushing while the link lasts
    for (int i = 0; i < 2; ++i)
        if (c[i]->target == c[1 - i] && c[1 - i]->attacker == c[i]) return;
    auto pushRect = [](const Object& o) {
        const Frame* f = frameOf(o);
        Box b;
        if (f && !f->push.empty()) b = f->push[0];
        else b = Box{o.def->box[0], o.def->box[1], o.def->box[2], o.def->box[3]};
        return worldRect(o, b);
    };
    const Rect r0 = pushRect(*c[0]), r1 = pushRect(*c[1]);
    if (!overlap(r0, r1)) return;
    const float depth = std::min(r0.x1, r1.x1) - std::max(r0.x0, r1.x0);
    if (depth <= 0) return;
    // the one on the left goes left; exact overlap: the first fighter yields against its facing
    float dir0 = c[0]->x < c[1]->x ? -1.0f : (c[0]->x > c[1]->x ? 1.0f : (c[0]->slots[56] == 1.0f ? -1.0f : 1.0f));
    float move0 = dir0 * depth * 0.5f, move1 = -dir0 * depth * 0.5f;
    auto clampMove = [&](Object& o, float mv) {
        const float nx = std::min(std::max(o.x + mv, w.leftWall), w.rightWall);
        const float used = nx - o.x;
        o.x = nx;
        return mv - used;  // what the wall refused
    };
    const float left0 = clampMove(*c[0], move0);
    const float left1 = clampMove(*c[1], move1);
    if (left0 != 0) clampMove(*c[1], -left0);
    if (left1 != 0) clampMove(*c[0], -left1);
}



// ======================================================================================================================
// teams: what the engine tells the members of a side and what it does with their tag requests
// ======================================================================================================================
namespace {

constexpr int kSlotTeamPos = 26, kSlotIsPointT = 27, kSlotAssistOk = 28, kSlotPointX = 31, kSlotPointY = 32, kSlotPointFacing = 33,
              kSlotPointAssistLock = 34, kSlotTagTo = 147, kSlotTagInfo = 148, kSlotAssistLockNow = 149;

int sideIndex(const Object& o) { return o.slots[25] >= 2.0f ? 1 : 0; }

// Before the objects run: every member learns where the point character stands and whether it may be called, and every object learns
// who its enemy is (the opposing point character).
//   POINTX / POINTY / POINTFACING (31..33): where the last visible point character of the side stood (an offscreen character that is
//     tagged in reads them to appear exactly there, so they are not overwritten while the new point is still offscreen).
//   POINTASSISTLOCK (34): +1 while the point lets assists in, -1 while the state of the point carries ASSISTLOCKNOW (a super, a throw ...).
//   ASSISTOK (28): a member that still has life may be called / tagged in; the point may tag out only while a teammate is left.
void teamRefresh(World& w) {
    Object* pt[2] = {pointOfSide(w, 0), pointOfSide(w, 1)};
    int alive[2] = {0, 0};
    for (Object* o : w.objects)
        if (o->leader == o && o->alive && o->life() > 0) ++alive[sideIndex(*o)];
    for (int sd = 0; sd < 2; ++sd)
        if (pt[sd] && pt[sd]->y > -1000.0f) {  // on screen (offscreen members live ~10000 px below the floor)
            w.anchor[sd][0] = pt[sd]->x;
            w.anchor[sd][1] = pt[sd]->y;
            w.anchor[sd][2] = pt[sd]->slots[56];
        }
    for (Object* o : w.objects) {
        if (o->leader != o) {
            o->enemy = o->leader ? o->leader->enemy : nullptr;
            continue;
        }
        const int sd = sideIndex(*o);
        o->enemy = pt[1 - sd];
        o->slots[kSlotPointX] = w.anchor[sd][0];
        o->slots[kSlotPointY] = w.anchor[sd][1];
        o->slots[kSlotPointFacing] = w.anchor[sd][2] == 0 ? 1.0f : w.anchor[sd][2];
        o->slots[kSlotPointAssistLock] = (pt[sd] && pt[sd]->slots[kSlotAssistLockNow] != 0) ? -1.0f : 1.0f;
        const bool isPoint = o->slots[kSlotIsPointT] != 0;
        o->slots[kSlotAssistOk] = o->life() > 0 && (!isPoint || alive[sd] > 1) ? 1.0f : 0.0f;
    }
}

// After the objects ran: a point character that wrote TAGTO = n (1 / 2) swaps places with the member whose TEAMPOS is n (a member that is
// out of life is skipped for the other one).  The old point becomes a plain member (ISPOINT 0: its states walk it offscreen), the new
// one goes on point (ISPOINT 1: its offscreen states see it and run the tag-in); TEAMPOS swap, TAGINFO travels with the request.
void teamTags(World& w) {
    for (int sd = 0; sd < 2; ++sd) {
        Object* p = pointOfSide(w, sd);
        if (!p) continue;
        const int req = static_cast<int>(p->slots[kSlotTagTo]);
        if (req < 1) continue;
        Object* t = nullptr;
        Object* other = nullptr;
        for (Object* o : w.objects) {
            if (o->leader != o || o == p || !o->alive || sideIndex(*o) != sd || o->life() <= 0) continue;
            if (static_cast<int>(o->slots[kSlotTeamPos]) == req) t = o;
            else if (!other) other = o;
        }
        if (!t) t = other;
        if (!t) {
            p->slots[kSlotTagTo] = 0;  // nobody left to come in
            continue;
        }
        const float info = p->slots[kSlotTagInfo];
        std::swap(p->slots[kSlotTeamPos], t->slots[kSlotTeamPos]);
        p->slots[kSlotIsPointT] = 0;
        t->slots[kSlotIsPointT] = 1;
        t->slots[kSlotTagInfo] = info;
        p->slots[kSlotTagTo] = 0;
        Event e;
        e.tick = w.stats.ticks;
        e.object = t->id;
        e.hash = static_cast<uint32_t>(sd);
        e.x = info;
        e.kind = Event::TagIn;
        w.events.push_back(e);
    }
}

}  // namespace

// ======================================================================================================================
// match layer: round flow above the characters
// ======================================================================================================================
namespace {

constexpr int kSlotRoundOver = 30, kSlotDoPose = 135, kSlotPlayerNum = 25, kSlotIsPoint = 27, kSlotSuperBar = 66, kSlotSuperFlash = 71;
constexpr int kIntroCap = 1500;   // longest intro pose is ~430 ticks; a pose that never signals "done" must not stall the match
constexpr int kEndCap = 1500;

void leadersOf(World& w, std::vector<Object*>& out) {
    out.clear();
    for (Object* o : w.objects)
        if (o->leader == o) out.push_back(o);
}

int sideOf(const Object& o) { return o.slots[kSlotPlayerNum] >= 2.0f ? 1 : 0; }

void matchEvent(World& w, int phase, int winnerSide) {
    Event e;
    e.tick = w.stats.ticks;
    e.object = w.match.round;
    e.hash = static_cast<uint32_t>(phase);
    e.x = static_cast<float>(winnerSide);
    e.kind = Event::MatchPhase;
    w.events.push_back(e);
}

// Fresh fighters for a new round: same character, side, colour, meter; full life; back at the start position, in Stand.
void resetFighters(World& w) {
    std::vector<Object*> L;
    leadersOf(w, L);
    // children of the last round leave with it
    for (Object* o : L) o->parent = o->target = o->attacker = nullptr;
    std::vector<Object*> keep = L;
    w.objects = keep;
    w.pool.clear();
    for (size_t i = 0; i < L.size(); ++i) {
        Object& o = *L[i];
        const foit::Character& ch = *o.ch;
        const foit::Object& def = *o.def;
        const int charId = o.charId, palette = o.palette;
        Object* enemy = o.enemy;
        const float side = o.slots[kSlotPlayerNum], teamPos = o.slots[26], isPoint = o.slots[kSlotIsPoint], meter = o.slots[kSlotSuperBar];
        const size_t ringCap = o.ring.capacity();
        const float sx = i < static_cast<size_t>(Match::kMaxFighters) ? w.match.startX[i] : o.x;
        const bool right = i < static_cast<size_t>(Match::kMaxFighters) ? w.match.startRight[i] != 0 : o.slots[56] == 1.0f;
        Object fresh;
        init(fresh, ch, def, sx, right, static_cast<int>(ringCap));
        o = fresh;
        o.leader = &o;
        o.enemy = enemy;
        o.charId = charId;
        o.palette = palette;
        o.slots[kSlotPlayerNum] = side;
        o.slots[26] = teamPos;
        o.slots[kSlotIsPoint] = isPoint;
        o.slots[kSlotSuperBar] = meter;
        const int stand = findState(def, "Stand");
        enterState(o, w, stand < 0 ? 0 : stand);
    }
}

// Fighters are free to act: ROUNDOVER 0, the clock runs.
void enterFight(World& w, const std::vector<Object*>& L) {
    Match& m = w.match;
    for (Object* o : L) o->slots[kSlotRoundOver] = 0;
    m.phase = Match::Fight;
    m.phaseTicks = 0;
    matchEvent(w, Match::Fight, -1);
}

// After the poses (or without them): the round card, then the fight.  ROUNDOVER stays -1 while the card is up.
void enterReady(World& w, const std::vector<Object*>& L) {
    Match& m = w.match;
    if (m.readyTicks <= 0) {
        enterFight(w, L);
        return;
    }
    m.phase = Match::Ready;
    m.phaseTicks = 0;
    matchEvent(w, Match::Ready, -1);
}

void beginRound(World& w, bool first) {
    Match& m = w.match;
    if (!first) resetFighters(w);
    std::vector<Object*> L;
    leadersOf(w, L);
    const bool poses = m.round <= m.introRounds;
    for (Object* o : L) {
        o->slots[kSlotRoundOver] = -1;
        o->slots[kSlotDoPose] = poses ? 1.0f : 0.0f;
    }
    m.phaseTicks = 0;
    m.clock = m.roundTicks;
    m.winner = -1;
    m.reason = 0;
    if (poses) {
        m.phase = Match::Intro;
        matchEvent(w, Match::Intro, -1);
    } else {
        enterReady(w, L);
    }
}

bool poseDone(const std::vector<Object*>& L) {
    for (Object* o : L)
        if (o->slots[kSlotIsPoint] != 0 && o->slots[kSlotDoPose] != 0) return false;
    return true;
}

void endRound(World& w, const std::vector<Object*>& L, int reason, int winnerSide) {
    Match& m = w.match;
    m.phase = Match::Ending;
    m.phaseTicks = 0;
    m.reason = reason;
    m.winner = winnerSide;
    for (Object* o : L) {
        o->slots[kSlotRoundOver] = reason == 2 ? 2.0f : 1.0f;
        // a fighter that is out of life lies where it fell and is not asked for a pose; a living loser (time over) gets the lose pose
        const bool wins = winnerSide < 0 ? true : sideOf(*o) == winnerSide;  // a draw gives everybody the win pose
        o->slots[kSlotDoPose] = o->life() <= 0 ? 0.0f : (wins ? 1.0f : -1.0f);
    }
    matchEvent(w, Match::Ending, winnerSide);
}

void matchStep(World& w) {
    Match& m = w.match;
    if (m.phase == Match::Off || m.phase == Match::Over) return;
    std::vector<Object*> L;
    leadersOf(w, L);
    if (L.empty()) return;
    ++m.phaseTicks;
    switch (m.phase) {
    case Match::Intro: {
        // stage 2 of the intro: the poses hold at their first frame (`DOPOSE > 1` is the transition out of Intro_Kirby / Intro_WaitForSword ...)
        // until the match layer says go, which it does by raising DOPOSE from 1 to 2
        if (m.phaseTicks == m.introGoTicks)
            for (Object* o : L)
                if (o->slots[kSlotDoPose] == 1.0f) o->slots[kSlotDoPose] = 2.0f;
        const bool timeout = m.phaseTicks > kIntroCap;
        if (poseDone(L) || timeout) {
            if (timeout) {
                ++m.timeouts;
                for (Object* o : L) o->slots[kSlotDoPose] = 0;
            }
            enterReady(w, L);
        }
        break;
    }
    case Match::Ready:
        if (m.phaseTicks >= m.readyTicks) enterFight(w, L);
        break;
    case Match::Fight: {
        bool flash = false;
        float bestLife[2] = {-1, -1};
        bool dead[2] = {false, false};
        bool any[2] = {false, false};
        for (Object* o : L) {
            if (o->slots[kSlotSuperFlash] != 0) flash = true;
            if (o->slots[kSlotIsPoint] == 0) continue;
            const int sd = sideOf(*o);
            any[sd] = true;
            if (o->life() <= 0) dead[sd] = true;
            bestLife[sd] = std::max(bestLife[sd], o->life());
        }
        const bool timed = m.roundTicks > 0 && !m.endless;
        if (timed && !flash && m.clock > 0) --m.clock;
        if (dead[0] || dead[1]) {
            m.phase = Match::Settle;
            m.phaseTicks = 0;
            m.still = 0;
            matchEvent(w, Match::Settle, -1);
        } else if (timed && m.clock <= 0 && any[0] && any[1]) {
            const float a = bestLife[0], b = bestLife[1];
            endRound(w, L, 2, a > b ? 0 : (b > a ? 1 : -1));
        }
        break;
    }
    case Match::Settle: {
        // wait for every fallen point character to be on the ground and at rest (or give up after 5 s)
        bool dead[2] = {false, false};
        bool resting = true;
        for (Object* o : L) {
            if (o->slots[kSlotIsPoint] == 0 || o->life() > 0) continue;
            dead[sideOf(*o)] = true;
            if (!o->onGround || std::fabs(o->slots[158]) > 0.01f || std::fabs(o->slots[159]) > 0.01f) resting = false;
        }
        m.still = resting ? m.still + 1 : 0;
        if (m.still >= m.settleTicks || m.phaseTicks > 300)
            endRound(w, L, dead[0] && dead[1] ? 3 : 1, dead[0] && dead[1] ? -1 : (dead[0] ? 1 : 0));
        break;
    }
    case Match::Ending: {
        const bool timeout = m.phaseTicks > kEndCap;
        if ((poseDone(L) && m.phaseTicks >= m.endHold) || timeout) {
            if (timeout) ++m.timeouts;
            if (m.winner < 0) {
                ++m.wins[0];
                ++m.wins[1];
            } else {
                ++m.wins[m.winner];
            }
            bool a = m.wins[0] >= m.roundsToWin, b = m.wins[1] >= m.roundsToWin;
            if (a && b && m.tieBreak) {   // level at the target: one more round decides
                ++m.roundsToWin;
                a = b = false;
            }
            if (a || b) {
                m.phase = Match::Over;
                m.matchWinner = (a && b) ? -1 : (a ? 0 : 1);
                m.phaseTicks = 0;
                matchEvent(w, Match::Over, m.matchWinner);
            } else {
                ++m.round;
                beginRound(w, false);
            }
        }
        break;
    }
    default:
        break;
    }
}

}  // namespace

void startMatch(World& w, const MatchOptions& opt) {
    Match& m = w.match;
    m = Match();
    m.roundTicks = opt.roundSeconds <= 0 ? 0 : opt.roundSeconds * 60;
    m.roundsToWin = std::max(1, opt.roundsToWin);
    m.introRounds = opt.introRounds;
    m.readyTicks = std::max(0, opt.readyTicks);
    m.endHold = std::max(0, opt.endHold);
    m.endless = opt.endless ? 1 : 0;
    m.tieBreak = opt.tieBreak ? 1 : 0;
    std::vector<Object*> L;
    leadersOf(w, L);
    m.numFighters = static_cast<int>(std::min<size_t>(L.size(), Match::kMaxFighters));
    for (int i = 0; i < m.numFighters; ++i) {
        m.startX[i] = L[static_cast<size_t>(i)]->x;
        m.startRight[i] = L[static_cast<size_t>(i)]->slots[56] == 1.0f ? 1 : 0;
        m.maxLife[i] = L[static_cast<size_t>(i)]->life();
    }
    beginRound(w, true);
}

void startMatch(World& w, int roundSeconds, int roundsToWin) {
    MatchOptions o;
    o.roundSeconds = std::max(1, roundSeconds);
    o.roundsToWin = roundsToWin;
    startMatch(w, o);
}

void matchSkipIntro(World& w) {
    Match& m = w.match;
    if (m.phase != Match::Intro) return;
    std::vector<Object*> L;
    leadersOf(w, L);
    for (Object* o : L) {
        if (o->slots[kSlotDoPose] != 0) {
            o->slots[kSlotDoPose] = 0;
            const int stand = findState(*o->def, "Stand");
            enterState(*o, w, stand < 0 ? 0 : stand);
        }
    }
    enterReady(w, L);
}

void tickAll(World& w, const Sample* inputs, int numInputs) {
    int ci = 0;
    if (w.match.endless && w.match.phase != Match::Off) {   // training: top the life up first, so no single hit of this tick can kill
        int idx = 0;
        for (Object* o : w.objects) {
            if (o->leader != o) continue;
            if (idx < Match::kMaxFighters && o->slots[kSlotIsPoint] != 0 && w.match.maxLife[idx] > 0)
                o->life() = o->slots[65] = w.match.maxLife[idx];
            ++idx;
        }
    }
    if (w.teams) teamRefresh(w);
    const size_t n = w.objects.size();  // helpers spawned this tick start acting on the next one
    for (size_t i = 0; i < n; ++i) {
        Object* o = w.objects[i];
        // a duel feeds the characters in the order they are listed; with teams every member of a side reads that side's input
        const int slot = w.teams ? sideIndex(*(o->leader ? o->leader : o)) : ci;
        const Sample* in = (o->leader == o && slot < numInputs && inputs) ? &inputs[slot] : nullptr;
        if (o->leader == o) ++ci;
        tick(*o, w, in);
    }
    resolveHits(w);
    resolvePush(w);
    if (w.teams) teamTags(w);
    // helpers that finished leave the world (nothing may keep pointing at them)
    for (size_t i = 0; i < w.objects.size();) {
        Object* d = w.objects[i];
        if (d->alive || d->leader == d) {
            ++i;
            continue;
        }
        for (Object* o : w.objects) {
            if (o->parent == d) o->parent = nullptr;
            if (o->target == d) o->target = nullptr;
            if (o->attacker == d) o->attacker = nullptr;
            if (o->enemy == d) o->enemy = nullptr;
        }
        w.objects.erase(w.objects.begin() + static_cast<std::ptrdiff_t>(i));
        for (size_t k = 0; k < w.pool.size(); ++k)
            if (w.pool[k].get() == d) {
                w.pool.erase(w.pool.begin() + static_cast<std::ptrdiff_t>(k));
                break;
            }
    }
    matchStep(w);
}

// ======================================================================================================================
// rollback support: save / load / hash
// ======================================================================================================================
namespace {

constexpr uint32_t kSaveMagic = 0x5a52544fu;  // "ORTZ"
constexpr uint32_t kSaveVersion = 4;   // 4: objects carry their tether bookkeeping; 3: Match gained introRounds / readyTicks / endHold / endless; teams + anchor are part of the stream

struct Out {
    std::vector<uint8_t>& b;
    template <class T>
    void put(const T& v) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
        b.insert(b.end(), p, p + sizeof(T));
    }
    void bytes(const void* p, size_t n) {
        const uint8_t* q = static_cast<const uint8_t*>(p);
        b.insert(b.end(), q, q + n);
    }
};

struct In {
    const uint8_t* p;
    size_t n;
    size_t i = 0;
    bool ok = true;
    template <class T>
    T get() {
        T v{};
        if (i + sizeof(T) > n) {
            ok = false;
            return v;
        }
        std::memcpy(&v, p + i, sizeof(T));
        i += sizeof(T);
        return v;
    }
    void bytes(void* dst, size_t len) {
        if (i + len > n) {
            ok = false;
            return;
        }
        std::memcpy(dst, p + i, len);
        i += len;
    }
};

int indexOf(const std::vector<Object*>& v, const Object* o) {
    if (!o) return -1;
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i] == o) return static_cast<int>(i);
    return -1;
}

}  // namespace

void saveWorld(const World& w, std::vector<uint8_t>& out) {
    out.clear();
    Out o{out};
    o.put(kSaveMagic);
    o.put(kSaveVersion);
    o.bytes(w.globals, sizeof w.globals);
    o.put(w.rng);
    o.put(w.floorY);
    o.put(w.leftWall);
    o.put(w.rightWall);
    o.bytes(&w.match, sizeof w.match);
    o.put(static_cast<uint8_t>(w.teams ? 1 : 0));
    o.bytes(w.anchor, sizeof w.anchor);
    o.put(w.nextId);
    o.put(w.stats.ticks);
    // the characters: objects that lead themselves, in world order (they come first)
    int numChars = 0;
    std::vector<const Object*> chars;
    for (const Object* x : w.objects)
        if (x->leader == x) {
            chars.push_back(x);
            ++numChars;
        }
    o.put(static_cast<int32_t>(w.objects.size()));
    o.put(static_cast<int32_t>(numChars));
    for (const Object* x : w.objects) {
        const int leaderIdx = indexOf(w.objects, x->leader);
        int childIdx = -1;
        if (x->leader && x->leader != x && x->leader->ch)
            for (size_t k = 0; k < x->leader->ch->body.children.size(); ++k)
                if (&x->leader->ch->body.children[k] == x->def) childIdx = static_cast<int>(k);
        o.put(static_cast<int32_t>(leaderIdx));
        o.put(static_cast<int32_t>(childIdx));
        o.put(static_cast<int32_t>(x->id));
        o.bytes(x->slots, sizeof x->slots);
        o.bytes(x->flags, sizeof x->flags);
        o.put(x->rng);
        o.put(static_cast<int32_t>(x->state));
        o.put(static_cast<int32_t>(x->nodeIdx));
        o.put(static_cast<int32_t>(x->nodeTimer));
        o.put(static_cast<int32_t>(x->nodeDur));
        o.put(static_cast<uint8_t>(x->nodeHasHit));
        o.put(static_cast<uint8_t>(x->fresh));
        o.bytes(x->trans, sizeof x->trans);
        o.put(x->fa);
        o.put(x->fb);
        o.put(x->fc);
        o.put(static_cast<int32_t>(x->hitState));
        o.put(static_cast<int32_t>(x->hitNode));
        o.put(static_cast<int32_t>(x->hitProp));
        o.put(static_cast<int32_t>(x->moveUses));
        o.put(static_cast<int32_t>(x->frame));
        o.bytes(x->addedTags, sizeof x->addedTags);
        o.bytes(x->removedTags, sizeof x->removedTags);
        o.put(static_cast<int32_t>(x->numAddedTags));
        o.put(static_cast<int32_t>(x->numRemovedTags));
        o.put(static_cast<int32_t>(x->charId));
        o.put(static_cast<int32_t>(x->palette));
        o.put(static_cast<int32_t>(x->comboHits));
        o.put(static_cast<int32_t>(x->strength));
        o.put(x->x);
        o.put(x->y);
        o.put(x->accX);
        o.put(x->accY);
        o.put(x->tethX);
        o.put(x->tethY);
        o.put(static_cast<uint8_t>(x->tethOk));
        o.put(static_cast<uint8_t>(x->onGround));
        o.put(static_cast<uint8_t>(x->alive));
        o.put(static_cast<int32_t>(indexOf(w.objects, x->enemy)));
        o.put(static_cast<int32_t>(indexOf(w.objects, x->target)));
        o.put(static_cast<int32_t>(indexOf(w.objects, x->attacker)));
        o.put(static_cast<int32_t>(indexOf(w.objects, x->parent)));
        // the reaction script this object is carrying: where it lives (character index, body or child, state, node, record, chain)
        int32_t pendChar = -1, pendChild = -1;
        if (x->pendingChain && x->pendDef)
            for (size_t c = 0; c < chars.size() && pendChar < 0; ++c) {
                if (!chars[c]->ch) continue;
                if (x->pendDef == &chars[c]->ch->body) {
                    pendChar = static_cast<int32_t>(c);
                } else {
                    for (size_t k = 0; k < chars[c]->ch->body.children.size(); ++k)
                        if (x->pendDef == &chars[c]->ch->body.children[k]) {
                            pendChar = static_cast<int32_t>(c);
                            pendChild = static_cast<int32_t>(k);
                        }
                }
            }
        o.put(pendChar);
        o.put(pendChild);
        o.put(static_cast<int32_t>(x->pendState));
        o.put(static_cast<int32_t>(x->pendNode));
        o.put(static_cast<int32_t>(x->pendProp));
        o.put(static_cast<int32_t>(x->pendCi));
        // input history
        const std::vector<Sample>& ring = x->ring.raw();
        o.put(static_cast<int32_t>(ring.size()));
        o.put(static_cast<int32_t>(x->ring.head()));
        o.put(x->ring.pushes());
        for (const Sample& s : ring) {
            o.put(s.dir);
            o.bytes(s.btn, sizeof s.btn);
        }
    }
}

bool loadWorld(World& w, const std::vector<Object*>& characters, const uint8_t* data, size_t size) {
    In in{data, size};
    if (in.get<uint32_t>() != kSaveMagic || in.get<uint32_t>() != kSaveVersion || !in.ok) return false;
    World nw;  // built on the side so a bad stream leaves `w` alone
    in.bytes(nw.globals, sizeof nw.globals);
    nw.rng = in.get<uint32_t>();
    nw.floorY = in.get<float>();
    nw.leftWall = in.get<float>();
    nw.rightWall = in.get<float>();
    in.bytes(&nw.match, sizeof nw.match);
    nw.teams = in.get<uint8_t>() != 0;
    in.bytes(nw.anchor, sizeof nw.anchor);
    nw.nextId = in.get<int32_t>();
    nw.stats.ticks = in.get<uint64_t>();
    const int32_t count = in.get<int32_t>();
    const int32_t numChars = in.get<int32_t>();
    if (!in.ok || count < 0 || count > 4096 || numChars != static_cast<int32_t>(characters.size()) || numChars > count) return false;

    struct Links {
        int enemy, target, attacker, parent;
    };
    std::vector<Links> links(static_cast<size_t>(count));
    std::vector<std::unique_ptr<Object>> pool;
    std::vector<Object*> objs(static_cast<size_t>(count), nullptr);
    std::vector<int32_t> leaders(static_cast<size_t>(count), -1);
    for (int32_t i = 0; i < count; ++i) {
        const int32_t leaderIdx = in.get<int32_t>();
        const int32_t childIdx = in.get<int32_t>();
        const int32_t id = in.get<int32_t>();
        if (!in.ok) return false;
        Object* x = nullptr;
        if (i < numChars) {
            x = characters[static_cast<size_t>(i)];
            if (!x || !x->def || !x->ch || leaderIdx != i) return false;
        } else {
            if (leaderIdx < 0 || leaderIdx >= numChars) return false;
            const Object* lead = characters[static_cast<size_t>(leaderIdx)];
            if (!lead->ch || childIdx < 0 || static_cast<size_t>(childIdx) >= lead->ch->body.children.size()) return false;
            pool.emplace_back(new Object());
            x = pool.back().get();
            x->ch = lead->ch;
            x->def = &lead->ch->body.children[static_cast<size_t>(childIdx)];
        }
        objs[static_cast<size_t>(i)] = x;
        leaders[static_cast<size_t>(i)] = leaderIdx;
        x->id = id;
        in.bytes(x->slots, sizeof x->slots);
        in.bytes(x->flags, sizeof x->flags);
        x->rng = in.get<uint32_t>();
        x->state = in.get<int32_t>();
        x->nodeIdx = in.get<int32_t>();
        x->nodeTimer = in.get<int32_t>();
        x->nodeDur = in.get<int32_t>();
        x->nodeHasHit = in.get<uint8_t>() != 0;
        x->fresh = in.get<uint8_t>() != 0;
        in.bytes(x->trans, sizeof x->trans);
        x->fa = in.get<uint8_t>();
        x->fb = in.get<uint8_t>();
        x->fc = in.get<uint8_t>();
        x->hitState = in.get<int32_t>();
        x->hitNode = in.get<int32_t>();
        x->hitProp = in.get<int32_t>();
        x->moveUses = in.get<int32_t>();
        x->frame = in.get<int32_t>();
        in.bytes(x->addedTags, sizeof x->addedTags);
        in.bytes(x->removedTags, sizeof x->removedTags);
        x->numAddedTags = in.get<int32_t>();
        x->numRemovedTags = in.get<int32_t>();
        x->charId = in.get<int32_t>();
        x->palette = in.get<int32_t>();
        x->comboHits = in.get<int32_t>();
        x->strength = in.get<int32_t>();
        x->x = in.get<float>();
        x->y = in.get<float>();
        x->accX = in.get<float>();
        x->accY = in.get<float>();
        x->tethX = in.get<float>();
        x->tethY = in.get<float>();
        x->tethOk = in.get<uint8_t>() != 0;
        x->onGround = in.get<uint8_t>() != 0;
        x->alive = in.get<uint8_t>() != 0;
        links[static_cast<size_t>(i)] = Links{in.get<int32_t>(), in.get<int32_t>(), in.get<int32_t>(), in.get<int32_t>()};
        const int32_t pendChar = in.get<int32_t>();
        const int32_t pendChild = in.get<int32_t>();
        x->pendState = in.get<int32_t>();
        x->pendNode = in.get<int32_t>();
        x->pendProp = in.get<int32_t>();
        x->pendCi = in.get<int32_t>();
        const int32_t ringSize = in.get<int32_t>();
        const int32_t ringHead = in.get<int32_t>();
        const uint64_t ringPushes = in.get<uint64_t>();
        if (!in.ok || ringSize < 0 || ringSize > 4096 || x->state < 0 || static_cast<size_t>(x->state) >= x->def->states.size() ||
            x->numAddedTags < 0 || x->numAddedTags > Object::kMaxDynTags || x->numRemovedTags < 0 || x->numRemovedTags > Object::kMaxDynTags)
            return false;
        std::vector<Sample> ring(static_cast<size_t>(ringSize));
        for (Sample& s : ring) {
            s.dir = in.get<uint8_t>();
            in.bytes(s.btn, sizeof s.btn);
        }
        if (!in.ok) return false;
        x->ring.restore(std::move(ring), static_cast<size_t>(ringHead), ringPushes);
        // the pending reaction script
        x->pendingChain = nullptr;
        x->pendDef = nullptr;
        if (pendChar >= 0) {
            if (pendChar >= numChars || !characters[static_cast<size_t>(pendChar)]->ch) return false;
            const foit::Character& pc = *characters[static_cast<size_t>(pendChar)]->ch;
            if (pendChild >= static_cast<int32_t>(pc.body.children.size())) return false;
            const foit::Object& pd = pendChild < 0 ? pc.body : pc.body.children[static_cast<size_t>(pendChild)];
            if (x->pendState < 0 || static_cast<size_t>(x->pendState) >= pd.states.size()) return false;
            const auto& nodes = pd.states[static_cast<size_t>(x->pendState)].hdr.nodes;
            if (x->pendNode < 0 || static_cast<size_t>(x->pendNode) >= nodes.size()) return false;
            const auto& props = nodes[static_cast<size_t>(x->pendNode)].props;
            if (x->pendProp < 0 || static_cast<size_t>(x->pendProp) >= props.size() || x->pendCi < 0 || x->pendCi > 3) return false;
            x->pendDef = &pd;
            x->pendingChain = &props[static_cast<size_t>(x->pendProp)].hit.chains[x->pendCi];
        }
    }
    // links by index
    for (int32_t i = 0; i < count; ++i) {
        auto at = [&](int idx) -> Object* { return idx >= 0 && idx < count ? objs[static_cast<size_t>(idx)] : nullptr; };
        Object* x = objs[static_cast<size_t>(i)];
        x->leader = at(leaders[static_cast<size_t>(i)]);
        x->enemy = at(links[static_cast<size_t>(i)].enemy);
        x->target = at(links[static_cast<size_t>(i)].target);
        x->attacker = at(links[static_cast<size_t>(i)].attacker);
        x->parent = at(links[static_cast<size_t>(i)].parent);
    }
    nw.objects = objs;
    nw.pool = std::move(pool);
    // commit
    const Stats keepStats = w.stats;
    w.objects = std::move(nw.objects);
    w.pool = std::move(nw.pool);
    std::memcpy(w.globals, nw.globals, sizeof w.globals);
    w.rng = nw.rng;
    w.floorY = nw.floorY;
    w.leftWall = nw.leftWall;
    w.rightWall = nw.rightWall;
    w.match = nw.match;
    w.teams = nw.teams;
    std::memcpy(w.anchor, nw.anchor, sizeof w.anchor);
    w.nextId = nw.nextId;
    w.events.clear();
    w.stats = keepStats;
    w.stats.ticks = nw.stats.ticks;
    return true;
}

uint64_t hashWorld(const World& w) {
    std::vector<uint8_t> b;
    saveWorld(w, b);
    uint64_t h = 1469598103934665603ull;
    for (uint8_t c : b) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

}  // namespace rt
}  // namespace foit
}  // namespace zengine
