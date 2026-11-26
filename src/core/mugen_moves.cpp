// mugen_moves.cpp - see mugen_moves.h.
#include "mugen_moves.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>

#include "foit_names.h"

namespace zengine {
namespace mugen {

namespace {

using foit::Chain;
using foit::ChainNode;
using foit::Command;
using foit::Node;
using foit::NodeProps;
using foit::Operand;
using foit::State;
using foit::Step;
using foit::Transition;
namespace S = foit::slot;

// ---- tiny script builders: the nodes the shipped files use (see foit_vm.h for the op numbers) ----------------------------------------------------
Operand lit(float v) {
    Operand o;
    o.desc = 0;
    std::memcpy(&o.raw, &v, 4);
    return o;
}
Operand slot(int id, uint8_t who = 0) {   // who: 0 = this object, 0x80 = the attacker (A.)
    Operand o;
    o.desc = static_cast<uint8_t>(1 | who);
    o.raw = static_cast<uint32_t>(id);
    return o;
}
ChainNode arith(uint8_t op, Operand a, Operand b, int dst) {
    ChainNode n;
    n.op = op;
    n.a = a;
    n.b = b;
    n.c = slot(dst);
    return n;
}
ChainNode set(int dst, float v) { return arith(1, lit(v), lit(0), dst); }
ChainNode test(int s, uint8_t op, float v) {   // boolean node: slot <op> literal
    ChainNode n;
    n.flags = 1;
    n.op = op;
    n.a = slot(s);
    n.b = n.c = lit(v);
    return n;
}
constexpr uint8_t kLt = 0x10, kGt = 0x11, kEq = 0x12, kGe = 0x13, kLe = 0x14, kNe = 0x15;

// ---- inputs -------------------------------------------------------------------------------------------------------------------------------------------------
enum { F = 0, DF = 1, D = 2, DB = 3, B = 4, UB = 5, U = 6, UF = 7, NEUTRAL = 8, ANY = 9, NEVER = 10 };

Command command(const Step& st) {
    Command c;
    c.a = 1;
    c.b = 0;
    c.flags = 4;
    c.steps.push_back(st);
    return c;
}
Command when(const Chain& cond, int d0 = ANY, int d1 = NEVER, int d2 = NEVER, bool negate = false) {
    Step s;
    s.b0 = static_cast<uint8_t>(d0);
    s.b1 = static_cast<uint8_t>(d1);
    s.b2 = static_cast<uint8_t>(d2);
    s.flags = static_cast<uint8_t>((cond.empty() ? 0 : 2) | (negate ? 1 : 0));
    s.chain = cond;
    return command(s);
}
Command pressed(int button, const Chain& cond = {}, int d0 = ANY, int d1 = NEVER, int d2 = NEVER, bool negate = false) {
    Command c = when(cond, d0, d1, d2, negate);
    c.steps[0].t[button] = 0;   // BtnPressed
    return c;
}

// ---- states -------------------------------------------------------------------------------------------------------------------------------------------
// a frame node of `ticks` ticks (b1 = has duration; b3 0x20 = "a free state": the combo it suffered is over)
Node frameNode(uint16_t ref, uint16_t ticks, bool freeState) {
    Node n;
    n.refs[1] = ref;
    NodeProps p;
    p.b1 = 1;
    p.dur = ticks;
    if (freeState) p.b3 = 0x20;
    n.props.push_back(p);
    return n;
}
// a node with no picture that runs a script and asks the engine to look at the transitions again (SKIPPED = 1)
Node scriptNode(Chain script, uint8_t b1 = 0x02) {
    Node n;
    NodeProps p;
    p.b1 = b1;
    p.b2 = 0;
    script.push_back(set(S::SKIPPED, 1));
    p.chain = std::move(script);
    n.props.push_back(p);
    return n;
}

struct Builder {
    foit::Character& c;
    const MoveSet& m;
    std::vector<std::string>* notes;
    std::map<std::string, int> ids;   // state name -> index (all states exist before the transitions are filled in)

    int find(const std::string& n) const {
        const auto it = ids.find(n);
        return it == ids.end() ? -1 : it->second;
    }
    static std::string animName(int id) { return id == 0 ? "Stand" : "Action_" + std::to_string(id); }
    const State* anim(int id) const {
        const int i = c.body.findState(animName(id));
        return i < 0 ? nullptr : &c.body.states[static_cast<size_t>(i)];
    }
    // the picture nodes of an action, as they are in its animation state (durations and frame references)
    std::vector<Node> nodesOf(int id, bool freeState) const {
        std::vector<Node> v;
        if (const State* s = anim(id))
            for (const Node& n : s->hdr.nodes) {
                Node x = n;
                if (freeState)
                    for (NodeProps& p : x.props) p.b3 = static_cast<uint8_t>(p.b3 | 0x20);
                v.push_back(std::move(x));
            }
        return v;
    }
    uint16_t add(State s) {
        const uint16_t i = static_cast<uint16_t>(c.body.states.size());
        ids[s.name] = i;
        c.body.states.push_back(std::move(s));
        return i;
    }
    // reserves a state (so transitions can point at it before it is filled in)
    uint16_t reserve(const std::string& name) {
        State s;
        s.name = name;
        s.hdr.nodes.push_back(scriptNode({}));
        return add(std::move(s));
    }
    State& at(const std::string& name) { return c.body.states[static_cast<size_t>(ids.at(name))]; }
    void note(const std::string& t) {
        if (notes) notes->push_back(t);
    }
};

Chain header(int physics, bool zeroVel = false) {
    Chain h = {set(S::BLOCKTYPE, 0), set(S::PHYSICSTYPE, static_cast<float>(physics))};
    if (zeroVel) {
        h.push_back(set(S::XVEL, 0));
        h.push_back(set(S::YVEL, 0));
    }
    return h;
}

// The engine runs a state's header script only for the nodes that carry the "run header" bit (b2 & 8): all of them (walking: the speed is set
// again every node) or just the first (set-up that must not be repeated).
void runHeader(State& s, bool allNodes) {
    for (size_t i = 0; i < s.hdr.nodes.size(); ++i)
        if ((allNodes || i == 0) && !s.hdr.nodes[i].props.empty()) s.hdr.nodes[i].props[0].b2 = static_cast<uint8_t>(s.hdr.nodes[i].props[0].b2 | 0x08);
}

void addTransition(State& s, uint16_t target, Command cmd) {
    Transition t;
    t.target = target;
    t.cmd = std::move(cmd);
    s.transitions.push_back(std::move(t));
}

const char* kButtonNames[6] = {"lp", "mp", "hp", "lk", "mk", "hk"};

}  // namespace

// ============================================================================================================================================================
bool parseMoves(const std::string& text, MoveSet& out, std::vector<std::string>* warnings) {
    out = MoveSet();
    std::istringstream in(text);
    std::string line, section;
    Attack* cur = nullptr;
    auto lower = [](std::string s) {
        for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return s;
    };
    auto trim = [](std::string s) {
        const auto a = s.find_first_not_of(" \t\r\n");
        const auto b = s.find_last_not_of(" \t\r\n");
        return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
    };
    auto ints = [&](const std::string& v) {
        std::vector<int> r;
        std::string t = v;
        for (char& ch : t)
            if (ch == ',') ch = ' ';
        std::istringstream is(t);
        std::string w;
        while (is >> w) r.push_back(std::atoi(w.c_str()));
        return r;
    };
    auto warn = [&](const std::string& w) {
        if (warnings) warnings->push_back(w);
    };
    while (std::getline(in, line)) {
        const auto sc = line.find(';');
        if (sc != std::string::npos) line.erase(sc);
        line = trim(line);
        if (line.empty()) continue;
        if (line[0] == '[') {
            const auto e = line.find(']');
            section = lower(trim(line.substr(1, e == std::string::npos ? std::string::npos : e - 1)));
            cur = nullptr;
            if (section.compare(0, 6, "attack") == 0) {
                out.attacks.emplace_back();
                cur = &out.attacks.back();
                cur->name = trim(line.substr(1, e - 1).substr(6));
                if (cur->name.empty()) cur->name = "Attack" + std::to_string(out.attacks.size());
            } else if (section != "moves") {
                warn("unknown section [" + section + "]");
            }
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            warn("not understood: " + line);
            continue;
        }
        const std::string k = lower(trim(line.substr(0, eq))), v = trim(line.substr(eq + 1));
        if (cur) {
            if (k == "posture") cur->posture = lower(v);
            else if (k == "button") {
                std::istringstream is(lower(v));
                std::string w;
                while (is >> w) {
                    int b = -1;
                    for (int i = 0; i < 6; ++i)
                        if (w == kButtonNames[i]) b = i;
                    if (b < 0) warn("unknown button " + w);
                    else cur->buttons.push_back(b);
                }
            } else if (k == "anim") cur->anims = ints(v);
            else if (k == "damage") cur->damage = std::atoi(v.c_str());
            else if (k == "hitstop") cur->hitstop = std::atoi(v.c_str());
            else if (k == "hitstun") cur->hitstun = std::atoi(v.c_str());
            else if (k == "level") cur->level = std::atoi(v.c_str());
            else if (k == "pushback") cur->pushback = std::atoi(v.c_str());
            else warn("unknown key " + k + " in [Attack " + cur->name + "]");
        } else if (section == "moves") {
            const float f = static_cast<float>(std::atof(v.c_str()));
            if (k == "stand") out.stand = std::atoi(v.c_str());
            else if (k == "walkfwd") out.walkFwd = std::atoi(v.c_str());
            else if (k == "walkback") out.walkBack = std::atoi(v.c_str());
            else if (k == "crouch") out.crouch = std::atoi(v.c_str());
            else if (k == "jumpstart") out.jumpStart = std::atoi(v.c_str());
            else if (k == "land") out.land = std::atoi(v.c_str());
            else if (k == "hit") out.hit = std::atoi(v.c_str());
            else if (k == "crouchhit") out.crouchHit = std::atoi(v.c_str());
            else if (k == "airhit") out.airHit = std::atoi(v.c_str());
            else if (k == "jump") {
                const std::vector<int> j = ints(v);
                for (size_t i = 0; i < 3 && i < j.size(); ++i) out.jump[i] = j[i];
            } else if (k == "walkspeed") out.walkSpeed = f;
            else if (k == "walkbackspeed") out.walkBackSpeed = f;
            else if (k == "jumpspeed") out.jumpSpeed = f;
            else if (k == "jumpforward") out.jumpForward = f;
            else if (k == "jumpback") out.jumpBack = f;
            else if (k == "life") out.life = f;
            else if (k == "push") {
                const std::vector<int> p = ints(v);
                if (p.size() >= 2 && p[0] > 0 && p[1] > 0) {
                    out.pushBack = p[0];
                    out.pushFront = p[1];
                } else {
                    warn("push needs two positive numbers (back, front)");
                }
            }
            else warn("unknown key " + k);
        }
    }
    return true;
}

// ============================================================================================================================================================
bool addMoves(foit::Character& c, const MoveSet& m, std::vector<std::string>* notes, std::string* err) {
    const size_t before = c.body.states.size();
    Builder b{c, m, notes, {}};
    for (size_t i = 0; i < c.body.states.size(); ++i) b.ids[c.body.states[i].name] = static_cast<int>(i);
    if (b.find("Stand") < 0 || !b.anim(m.stand)) {
        if (err) *err = "the stand animation (action " + std::to_string(m.stand) + ") is not in the character";
        return false;
    }
    auto have = [&](int id) { return id >= 0 && b.anim(id) != nullptr; };
    auto skip = [&](const std::string& what, int id) { b.note("skipped " + what + ": action " + std::to_string(id) + " is not in the .air"); };

    c.body.slotDefaults[S::LIFEBAR] = c.body.slotDefaults[S::REDLIFE] = 0;
    auto setDefault = [&](int slotId, float v) { std::memcpy(&c.body.slotDefaults[static_cast<size_t>(slotId)], &v, 4); };
    if (m.pushBack > 0 && m.pushFront > 0) {
        c.body.box[2] = -m.pushBack;
        c.body.box[3] = m.pushFront;
    }
    setDefault(S::LIFEBAR, m.life);
    setDefault(S::REDLIFE, m.life);
    setDefault(S::WEIGHT, 1.5f);
    setDefault(S::FRICTION, 1.5f);
    setDefault(S::FACINGDIR, 1.0f);
    setDefault(S::SPRITESTATE, 2.0f);
    setDefault(S::DAMAGEMULT, 1.0f);
    setDefault(S::ASSISTDMGMOD, 1.35f);
    setDefault(S::DAMAGESCALE, 1.0f);
    setDefault(S::TOTALSTUN, 240.0f);
    setDefault(S::SOLIDFLOOR, 1.0f);
    setDefault(S::SOLIDWALLS, 1.0f);
    setDefault(S::XSCALE, 1.0f);
    setDefault(S::YSCALE, 1.0f);
    setDefault(S::ALPHA, 1.0f);
    setDefault(S::THROWTECHTIME, 15.0f);

    // ---- reserve every state first so the transitions can point at them -----------------------------------------------------------------------------
    const bool canWalkF = have(m.walkFwd), canWalkB = have(m.walkBack), canCrouch = have(m.crouch), canJump = have(m.jumpStart) && have(m.jump[0]);
    if (m.walkFwd >= 0 && !canWalkF) skip("WalkFwd", m.walkFwd);
    if (m.walkBack >= 0 && !canWalkB) skip("WalkBack", m.walkBack);
    if (m.crouch >= 0 && !canCrouch) skip("Crouch", m.crouch);
    if (m.jumpStart >= 0 && !canJump) skip("jumping", m.jumpStart);
    if (canWalkF) b.reserve("WalkFwd");
    if (canWalkB) b.reserve("WalkBack");
    if (canCrouch) b.reserve("Crouch");
    const char* dirTag[3] = {"N", "F", "B"};
    if (canJump)
        for (int i = 0; i < 3; ++i) {
            b.reserve(std::string("JumpStart_") + dirTag[i]);
            b.reserve(std::string("Jump_") + dirTag[i]);
            b.reserve(std::string("JumpAir_") + dirTag[i]);
        }
    const bool canLand = canJump && have(m.land);
    if (canJump) {
        b.reserve("JumpLand");
    }
    const bool canHit = have(m.hit);
    if (canHit) {
        b.reserve("s.Hit");
        b.reserve("s.HitStun");
        b.reserve("c.Hit");
        b.reserve("c.HitStun");
        b.reserve("j.Hit");
        b.reserve("j.HitStun");
    } else {
        b.note("no 'hit' animation: the character cannot be hit (it stays in Stand)");
    }
    std::vector<std::string> attackNames;
    for (const Attack& a : m.attacks) {
        bool ok = !a.buttons.empty() && !a.anims.empty();
        for (int id : a.anims)
            if (!have(id)) {
                ok = false;
                skip("attack " + a.name, id);
                break;
            }
        if (a.posture != "stand" && a.posture != "crouch" && a.posture != "air") ok = false;
        if (a.posture == "crouch" && !canCrouch) ok = false;
        if (a.posture == "air" && !canJump) ok = false;
        attackNames.push_back(ok ? a.name : std::string());
        if (ok) b.reserve(a.name);
        else if (!a.name.empty()) b.note("skipped attack " + a.name + " (incomplete)");
    }

    const Chain gotHit = {test(S::GOTHIT, kNe, 0)};
    const Chain airborne = {test(S::ONGROUND, kEq, 0)};
    const Chain grounded = {test(S::ONGROUND, kNe, 0)};
    const Chain landed = {test(S::ONGROUND, kNe, 0), test(S::YVEL, kLe, 0)};

    // hit reactions first in every free state's transition list (a hit beats anything the player is pressing)
    auto addHitReactions = [&](State& s, bool crouching) {
        if (!canHit) return;
        addTransition(s, static_cast<uint16_t>(b.find("j.Hit")), when({test(S::GOTHIT, kNe, 0), test(S::ONGROUND, kEq, 0)}));
        addTransition(s, static_cast<uint16_t>(b.find(crouching ? "c.Hit" : "s.Hit")), when(gotHit));
    };
    // attacks are listed before movement: a button press wins over a held direction.  From a standing state the held direction picks the posture:
    // down + button starts a crouching attack (so pressing both on the same tick works), a button without down a standing one.
    enum { kAnyDir = 0, kNotDown = 1, kDown = 2 };
    auto addAttackStarts = [&](State& s, const std::string& posture, int dirMode) {
        for (size_t i = 0; i < m.attacks.size(); ++i) {
            if (attackNames[i].empty() || m.attacks[i].posture != posture) continue;
            for (int bt : m.attacks[i].buttons) {
                const auto target = static_cast<uint16_t>(b.find(m.attacks[i].name));
                if (dirMode == kNotDown) addTransition(s, target, pressed(bt, {}, D, DF, DB, true));
                else if (dirMode == kDown) addTransition(s, target, pressed(bt, {}, D, DF, DB, false));
                else addTransition(s, target, pressed(bt));
            }
        }
    };
    auto addGroundAttackStarts = [&](State& s) {
        addAttackStarts(s, "stand", kNotDown);
        if (canCrouch) addAttackStarts(s, "crouch", kDown);
    };

    // ---- Stand ----------------------------------------------------------------------------------------------------------------------------------------
    {
        State& s = b.at("Stand");
        s.hdr.hasChain = true;
        s.hdr.chain = header(3, true);
        for (Node& n : s.hdr.nodes)
            for (NodeProps& p : n.props) p.b3 = static_cast<uint8_t>(p.b3 | 0x20);
        runHeader(s, true);
        s.transitions.clear();
        addHitReactions(s, false);
        if (canJump) addTransition(s, static_cast<uint16_t>(b.find("JumpAir_N")), when(airborne));   // walked off a ledge / was launched: fall
        addGroundAttackStarts(s);
        if (canJump) {
            addTransition(s, static_cast<uint16_t>(b.find("JumpStart_F")), when({}, UF));
            addTransition(s, static_cast<uint16_t>(b.find("JumpStart_B")), when({}, UB));
            addTransition(s, static_cast<uint16_t>(b.find("JumpStart_N")), when({}, U));
        }
        if (canCrouch) addTransition(s, static_cast<uint16_t>(b.find("Crouch")), when({}, D, DF, DB));
        if (canWalkF) addTransition(s, static_cast<uint16_t>(b.find("WalkFwd")), when({}, F));
        if (canWalkB) addTransition(s, static_cast<uint16_t>(b.find("WalkBack")), when({}, B));
    }

    // ---- walking and crouching ----------------------------------------------------------------------------------------------------------------------
    auto freeLoop = [&](const std::string& name, int animId, int physics, float xvel, bool crouching) -> State& {
        State& s = b.at(name);
        s.hdr.nodes = b.nodesOf(animId, true);
        s.hdr.hasChain = true;
        s.hdr.chain = header(physics, true);
        if (xvel != 0) s.hdr.chain.push_back(set(S::XVEL, xvel));
        s.next = static_cast<uint16_t>(b.find(name));
        runHeader(s, true);
        s.transitions.clear();
        addHitReactions(s, crouching);
        return s;
    };
    if (canWalkF) {
        State& s = freeLoop("WalkFwd", m.walkFwd, 3, m.walkSpeed, false);
        addGroundAttackStarts(s);
        addTransition(s, static_cast<uint16_t>(b.find("Stand")), when({}, F, DF, UF, true));   // not holding forward any more
    }
    if (canWalkB) {
        State& s = freeLoop("WalkBack", m.walkBack, 3, -m.walkBackSpeed, false);
        addGroundAttackStarts(s);
        addTransition(s, static_cast<uint16_t>(b.find("Stand")), when({}, B, DB, UB, true));
    }
    if (canCrouch) {
        State& s = freeLoop("Crouch", m.crouch, 3, 0, true);
        addAttackStarts(s, "crouch", kAnyDir);
        addTransition(s, static_cast<uint16_t>(b.find("Stand")), when({}, D, DF, DB, true));   // released down
    }

    // ---- jumping ------------------------------------------------------------------------------------------------------------------------------------
    if (canJump) {
        const float vx[3] = {0.0f, m.jumpForward, -m.jumpBack};
        for (int i = 0; i < 3; ++i) {
            const int jumpAnim = have(m.jump[i]) ? m.jump[i] : m.jump[0];
            State& st = b.at(std::string("JumpStart_") + dirTag[i]);
            st.hdr.nodes = b.nodesOf(m.jumpStart, false);
            st.hdr.hasChain = true;
            st.hdr.chain = header(3, true);
            st.next = static_cast<uint16_t>(b.find(std::string("Jump_") + dirTag[i]));
            runHeader(st, false);
            st.transitions.clear();
            addHitReactions(st, false);

            State& j = b.at(std::string("Jump_") + dirTag[i]);
            j.hdr.nodes.clear();
            // the launch happens in the first node (a script node), then the animation loops until the feet touch the floor
            j.hdr.nodes.push_back(scriptNode({set(S::PHYSICSTYPE, 2), set(S::SPRITESTATE, 1), set(S::XVEL, vx[i]), set(S::YVEL, m.jumpSpeed)}));
            for (Node& n : b.nodesOf(jumpAnim, false)) j.hdr.nodes.push_back(std::move(n));
            j.hdr.hasChain = true;
            j.hdr.chain = {set(S::BLOCKTYPE, 0)};
            j.next = static_cast<uint16_t>(b.find(std::string("JumpAir_") + dirTag[i]));
            j.transitions.clear();
            addHitReactions(j, false);
            addAttackStarts(j, "air", kAnyDir);
            addTransition(j, static_cast<uint16_t>(b.find(canLand ? "JumpLand" : "Stand")), when(landed));

            State& a = b.at(std::string("JumpAir_") + dirTag[i]);   // the same picture looping, with no new launch
            a.hdr.nodes = b.nodesOf(jumpAnim, false);
            a.hdr.hasChain = true;
            a.hdr.chain = {set(S::BLOCKTYPE, 0), set(S::PHYSICSTYPE, 2)};
            a.next = static_cast<uint16_t>(b.find(std::string("JumpAir_") + dirTag[i]));
            runHeader(a, false);
            a.transitions.clear();
            addHitReactions(a, false);
            addAttackStarts(a, "air", kAnyDir);
            addTransition(a, static_cast<uint16_t>(b.find(canLand ? "JumpLand" : "Stand")), when(landed));
        }
        State& l = b.at("JumpLand");
        l.hdr.nodes = canLand ? b.nodesOf(m.land, true) : std::vector<Node>{frameNode(b.anim(m.stand)->hdr.nodes[0].refs[1], 1, true)};
        l.hdr.hasChain = true;
        l.hdr.chain = header(3, true);
        l.next = static_cast<uint16_t>(b.find("Stand"));
        runHeader(l, false);
        l.transitions.clear();
        addHitReactions(l, false);

    }

    // ---- attacks ------------------------------------------------------------------------------------------------------------------------------------
    for (size_t i = 0; i < m.attacks.size(); ++i) {
        if (attackNames[i].empty()) continue;
        const Attack& a = m.attacks[i];
        const bool air = a.posture == "air", crouch = a.posture == "crouch";
        State& s = b.at(a.name);
        s.hdr.nodes.clear();
        s.hdr.nodes.push_back(scriptNode({arith(1, slot(S::FACINGTOENEMY), lit(0), S::FACINGDIR)}));   // a ground attack faces the enemy
        for (int id : a.anims)
            for (Node& n : b.nodesOf(id, false)) s.hdr.nodes.push_back(std::move(n));
        // arm the hit on the first node whose frame has hit boxes; clear it with a node at the end
        bool armed = false;
        for (Node& n : s.hdr.nodes) {
            if (n.refs[1] == 0 || n.props.empty()) continue;
            const foit::Frame& fr = c.frames[n.refs[1] - 1u];
            if (armed || fr.hit.empty()) continue;
            armed = true;
            NodeProps& p = n.props[0];
            p.b3 = static_cast<uint8_t>(p.b3 | 0x10);   // attack
            p.f2 = 1;
            p.hasHit = true;
            foit::HitData& h = p.hit;
            h.f8 = static_cast<uint16_t>(a.hitstun);
            h.fc = static_cast<uint16_t>(a.damage);
            h.f10 = static_cast<uint16_t>(a.hitstop);
            h.f13 = static_cast<uint8_t>(a.level);
            h.f18 = 1;
            h.f14 = 1;
            h.f15 = 2;
            h.f16 = 1;
            h.f17 = 2;
            h.fa = 3;
            h.hasChain[0] = h.hasChain[1] = true;
            // chain 0 = a defender in the air, chain 1 = one on the ground: pushed back, the air one popped up a little
            h.chains[0] = {set(S::PUSHBACK, static_cast<float>(a.pushback)), set(S::PUSHBACKTIME, 4), set(S::PUSHBACKDELTA, -2), set(S::YVEL, 5), set(S::XVEL, -4)};
            h.chains[1] = {set(S::PUSHBACK, static_cast<float>(a.pushback)), set(S::PUSHBACKTIME, 5), set(S::PUSHBACKDELTA, -1), set(S::YVEL, 0), set(S::XVEL, -8)};
        }
        if (!armed) b.note("attack " + a.name + ": none of its frames has hit boxes (clsn1), so it cannot hit anything");
        s.hdr.nodes.push_back(scriptNode({}, 0x0A));   // the hit is cleared when the move ends
        s.hdr.hasChain = true;
        s.hdr.chain = air ? Chain{set(S::BLOCKTYPE, 0)} : header(3, true);
        // an air attack whose animation ends before the ground does goes on falling (landing is the transition below), it never lands by itself
        s.next = static_cast<uint16_t>(b.find(air ? "JumpAir_N" : (crouch ? "Crouch" : "Stand")));
        runHeader(s, false);
        s.transitions.clear();
        addHitReactions(s, crouch);
        if (air && canLand) addTransition(s, static_cast<uint16_t>(b.find("JumpLand")), when(landed));
        if (air && !canLand) addTransition(s, static_cast<uint16_t>(b.find("Stand")), when(landed));
    }

    // ---- being hit ----------------------------------------------------------------------------------------------------------------------------------
    if (canHit) {
        struct Reaction { const char* hitName; const char* loopName; int anim; bool air; bool crouch; };
        const Reaction rs[3] = {{"s.Hit", "s.HitStun", m.hit, false, false},
                                {"c.Hit", "c.HitStun", have(m.crouchHit) ? m.crouchHit : m.hit, false, true},
                                {"j.Hit", "j.HitStun", have(m.airHit) ? m.airHit : m.hit, true, false}};
        for (const Reaction& r : rs) {
            State& h = b.at(r.hitName);
            h.hdr.nodes.clear();
            // first node: take the life off, turn to the attacker, run the attacker's reaction chain (pushback / launch) once
            Chain first = {set(S::SOLIDFLOOR, 1), set(S::SOLIDWALLS, 1), arith(4, slot(S::FACINGDIR, 0x80), lit(-1), S::FACINGDIR),
                           arith(3, slot(S::LIFEBAR), slot(S::DAMAGE), S::LIFEBAR), set(S::XVEL, 0), set(S::YVEL, 0), set(S::INVINCIBLE, 0)};
            h.hdr.nodes.push_back(scriptNode(first, 0x1A));   // setFlagA | clearHit | run the pending chain once
            h.hdr.hasChain = true;
            h.hdr.chain = {set(S::BLOCKTYPE, 0), set(S::PHYSICSTYPE, r.air ? 2.0f : 3.0f)};
            h.next = static_cast<uint16_t>(b.find(r.loopName));
            runHeader(h, false);
            h.transitions.clear();

            State& l = b.at(r.loopName);
            l.hdr.nodes = b.nodesOf(r.anim, false);
            // the picture keeps looping while the stun lasts
            l.hdr.hasChain = true;
            l.hdr.chain = {set(S::BLOCKTYPE, 0), set(S::PHYSICSTYPE, r.air ? 2.0f : 3.0f)};
            l.next = static_cast<uint16_t>(b.find(r.loopName));
            runHeader(l, false);
            l.transitions.clear();
            addTransition(l, static_cast<uint16_t>(b.find("j.Hit")), when({test(S::GOTHIT, kNe, 0), test(S::ONGROUND, kEq, 0)}));
            addTransition(l, static_cast<uint16_t>(b.find(r.crouch ? "c.Hit" : "s.Hit")), when(gotHit));
            if (r.air) {
                addTransition(l, static_cast<uint16_t>(b.find("Stand")), when({test(S::ONGROUND, kNe, 0), test(S::YVEL, kLe, 0)}));
            } else {
                addTransition(l, static_cast<uint16_t>(b.find("j.Hit")), when(airborne));
                addTransition(l, static_cast<uint16_t>(b.find(r.crouch && canCrouch ? "Crouch" : "Stand")), when({test(S::HITSTUN, kLe, 0)}));
            }
        }
    }

    // the character's default state for the engine (state 0 = the one the match starts it in)
    c.body.refBe = static_cast<uint16_t>(b.find("Stand"));
    b.note("states: " + std::to_string(c.body.states.size()) + " in all, " + std::to_string(c.body.states.size() - before) + " of them gameplay states");
    return true;
}

}  // namespace mugen
}  // namespace zengine
