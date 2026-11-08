// foit_vm.h - the "slot" virtual machine that runs the script chains of a .foit (see foit.h: Chain / ChainNode / Operand).
//
// An object owns kSlotCount float slots and a few flag words.  A chain is a list of nodes:
//   arithmetic chain   every node does  C := op(A, B)            (nodes flagged boolean are skipped)
//   boolean chain      every node is a test of A against C; the chain is true when ALL tests hold (first failure stops;
//                      an arithmetic node inside a boolean chain counts as a failed test)
// The behaviour below was worked out from the shipped executable's evaluator (static analysis) and from the operand statistics of
// the 20 character files; this is a clean implementation, not a transcription.  Everything about the *world* a few ops look at
// (teams, assists, input, tags of the current state ...) is answered by a `Host` supplied by the runtime.
//
// Deterministic: plain float arithmetic, no allocation, the random numbers come from a caller owned 32 bit seed.
#pragma once
#include <cstdint>

#include "foit.h"

namespace zengine {
namespace foit {
namespace vm {

constexpr int kFlagWords = 3;        // 96 flag bits per object
constexpr int kFirstWritableSlot = 0x25;  // slots 0..0x24 are owned by the engine: scripts can read but not write them

// An object a script can reach.  All pointers are owned by the runtime.
struct Ref {
    float* slots = nullptr;              // kSlotCount floats
    uint32_t* flags = nullptr;           // kFlagWords words
    const uint32_t* defaults = nullptr;  // float bit patterns the slots get on a reset (op 8)
    void* user = nullptr;                // the runtime's own object, handed back to the Host untouched
    bool valid() const { return slots != nullptr; }
};

class Host;

// Everything one chain run can see.  `self` is the object the chain belongs to; the others are what the operand prefixes
// (L. / E. / T. / A. / PT. / PRNT.) select.  Missing objects are left empty: a node that needs one is skipped.
struct Context {
    Ref self, leader, enemy, target, attacker, parent;
    Ref point;  // PT.: the point character of the team of `self` (left empty in a match without teams: PT. is then the leader)
    float* globals = nullptr;  // global slot table (operand bit 1)
    int numGlobals = 0;
    uint32_t* rng = nullptr;   // seed of the script random generator
    Host* host = nullptr;
};

// The engine side of the few ops that look beyond the object's own slots.  The default answers "unknown" (the node is skipped /
// the test fails), which is what a chain test run outside a match wants.
class Host {
public:
    virtual ~Host() = default;
    // arithmetic ops without a pure-math meaning (position queries, team lookups ...); return true and set *out to write C
    virtual bool op(int opcode, float a, float b, const Ref& who, float* out) {
        (void)opcode; (void)a; (void)b; (void)who; (void)out;
        return false;
    }
    // boolean ops beyond the six comparisons
    virtual bool test(int opcode, float a, float c, const Ref& who) {
        (void)opcode; (void)a; (void)c; (void)who;
        return false;
    }
    // "the current state of `who` carries the tag with this hash" (boolean ops 0x20 / 0x21; the hashes are State::hashes)
    virtual bool stateHasTag(const Ref& who, uint32_t hash) {
        (void)who; (void)hash;
        return false;
    }
};

// Script random generator: seed = seed * 1103515245 + 12345; value = ((seed >> 16) & 0x7fff) / 32768 * range, truncated.
int scriptRandom(uint32_t& seed, float range);

// Reads one operand.  Returns false when the object it addresses does not exist.
bool readOperand(const Operand& o, Context& cx, float* out);
// Writes one operand (slot, global slot or flag bit).  Returns false when the write is not allowed / not possible.
bool writeOperand(const Operand& o, float value, Context& cx);

// Runs every arithmetic node of `c`.  Returns the number of nodes that were executed.
int runChain(const Chain& c, Context& cx);
// True when every test of the boolean chain holds.  An empty chain is true.
bool testChain(const Chain& c, Context& cx);

// Opcode numbers (the `op` byte of a node).
namespace op {
constexpr int Set = 1, Add = 2, Sub = 3, Mul = 4, Div = 5, Abs = 6, Trunc = 7, Reset = 8, Sin = 9, Cos = 10, Max = 11, Min = 12,
              Atan2 = 13, SetIfNonZero = 14, SetIfZero = 15, Nop = 24, Or = 28, And = 29, Mod = 30, Eq = 35, Ne = 36, Lt = 37,
              Lerp = 115, InvLerp = 116;
// boolean
constexpr int TLt = 16, TGt = 17, TEq = 18, TGe = 19, TLe = 20, TNe = 21, TEq2 = 22, TNe2 = 23, TTrue = 24, TTag = 32, TNoTag = 33;
}  // namespace op

}  // namespace vm
}  // namespace foit
}  // namespace zengine
