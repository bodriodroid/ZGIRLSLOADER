// lua_host.cpp - see lua_host.h
#include "lua_host.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <new>
#include <unordered_map>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include "foit_names.h"
#include "palfx.h"

namespace zengine {
namespace script {

namespace {

namespace rt = foit::rt;

std::string upperOf(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// slot name (upper case) -> id, built once from the vocabulary of the .foit scripts
const std::unordered_map<std::string, int>& slotIds() {
    static const std::unordered_map<std::string, int> m = [] {
        std::unordered_map<std::string, int> r;
        for (int i = 0; i < static_cast<int>(foit::kSlotCount); ++i)
            if (const char* n = foit::slotName(i)) r.emplace(n, i);
        // the names the lessons use for slots whose script spelling differs
        r.emplace("XPOS", 160);
        r.emplace("YPOS", 161);
        r.emplace("GRD", 5);
        return r;
    }();
    return m;
}

// ---------------------------------------------------------------------------------------------------- motions
int buttonBit(const std::string& w) {   // lower case word -> button mask (0 = unknown)
    static const std::map<std::string, int> m = {
        {"jab", 1}, {"lp", 1}, {"wp", 1}, {"strong", 2}, {"mp", 2}, {"fierce", 4}, {"hp", 4}, {"short", 8}, {"lk", 8}, {"wk", 8},
        {"fwd", 16}, {"forward", 16}, {"mk", 16}, {"rh", 32}, {"roundhouse", 32}, {"hk", 32},
        {"2p", 1 | 2}, {"pp", 1 | 2}, {"3p", 1 | 2 | 4}, {"2k", 8 | 16}, {"kk", 8 | 16}, {"3k", 8 | 16 | 32},
    };
    auto it = m.find(w);
    return it == m.end() ? 0 : it->second;
}

int dirOf(const std::string& w, bool* known) {   // lower case word -> 0..8 (LuaLib's `d`), -1 = "keep / any"
    static const std::map<std::string, int> m = {{"f", 0}, {"df", 1}, {"d", 2}, {"db", 3}, {"b", 4}, {"ub", 5}, {"u", 6}, {"uf", 7}, {"n", 8}, {"x", 8}};
    auto it = m.find(w);
    *known = it != m.end();
    return *known ? it->second : 8;
}

std::string lowerOf(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

std::vector<InputFrame> parseMotion(const std::string& text) {
    std::vector<InputFrame> out;
    // steps are separated by ','; each step is "dir+button+button"
    std::vector<std::string> steps;
    {
        std::string cur;
        for (char c : text) {
            if (c == ',') {
                steps.push_back(cur);
                cur.clear();
            } else {
                cur += c;
            }
        }
        steps.push_back(cur);
    }
    std::vector<InputFrame> held;
    for (const std::string& raw : steps) {
        std::string step = lowerOf(raw);
        step.erase(std::remove_if(step.begin(), step.end(), [](unsigned char c) { return std::isspace(c); }), step.end());
        if (step.empty() || step.rfind("valid=", 0) == 0) continue;   // the prompt window is display data
        InputFrame f;
        bool any = false;
        size_t pos = 0;
        bool first = true;
        while (pos <= step.size()) {
            size_t plus = step.find('+', pos);
            if (plus == std::string::npos) plus = step.size();
            std::string tok = step.substr(pos, plus - pos);
            pos = plus + 1;
            if (tok.empty()) {
                if (plus >= step.size()) break;
                continue;
            }
            // trailing modifiers such as "(l)" or ",valid=4" stay out of the name
            if (const size_t par = tok.find('('); par != std::string::npos) tok.erase(par);
            if (const size_t eq = tok.find('='); eq != std::string::npos) tok.erase(eq);
            bool release = false;
            bool negate = false;
            while (!tok.empty() && (tok[0] == '~' || tok[0] == '^' || tok[0] == '!')) {
                if (tok[0] == '!') negate = true;
                else release = true;
                tok.erase(0, 1);
            }
            bool isDir = false;
            const int d = dirOf(tok, &isDir);
            if (isDir && first) {
                f.dir = negate ? 8 : d;
                any = true;
            } else if (const int b = buttonBit(tok)) {
                if (!release) f.buttons = static_cast<uint8_t>(f.buttons | b);
                any = true;
            }
            first = false;
            if (plus >= step.size()) break;
        }
        if (!any) continue;
        held.push_back(f);
    }
    for (size_t i = 0; i < held.size(); ++i) {
        const int n = (i + 1 == held.size()) ? 3 : 2;
        for (int k = 0; k < n; ++k) out.push_back(held[i]);
    }
    return out;
}

bool parseSequence(const std::string& bytes, std::vector<InputFrame>& out) {
    out.clear();
    constexpr size_t kHeader = 5, kRecord = 18;
    if (bytes.size() < kHeader + kRecord || static_cast<uint8_t>(bytes[0]) != 1) return false;
    const size_t n = (bytes.size() - kHeader) / kRecord;
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* r = reinterpret_cast<const uint8_t*>(bytes.data()) + kHeader + i * kRecord;
        InputFrame f;
        f.dir = r[0] <= 8 ? r[0] : 8;
        for (int b = 0; b < 6; ++b) {
            const uint8_t st = r[6 + b];
            if (st == 0 || st == 3) f.buttons = static_cast<uint8_t>(f.buttons | (1u << b));
        }
        out.push_back(f);
    }
    return !out.empty();
}

// ---------------------------------------------------------------------------------------------------- the bindings
struct LuaHost::Impl {
    LuaHost* h = nullptr;

    static Impl* of(lua_State* L) { return static_cast<Impl*>(lua_touserdata(L, lua_upvalueindex(1))); }

    // ---- sprites
    struct Sprite {
        rt::Object* o;
    };

    bool alive(const rt::Object* o) const {
        if (!o || !h->fight_) return false;
        rt::World* w = h->fight_->world();
        if (!w) return false;
        return std::find(w->objects.begin(), w->objects.end(), o) != w->objects.end();
    }

    void pushSprite(lua_State* L, rt::Object* o) {
        if (!o) {
            lua_pushnil(L);
            return;
        }
        lua_getfield(L, LUA_REGISTRYINDEX, "zg_sprites");   // weak-valued cache so one object is one Lua value (== works)
        lua_pushlightuserdata(L, o);
        lua_rawget(L, -2);
        if (lua_isuserdata(L, -1)) {
            lua_remove(L, -2);
            return;
        }
        lua_pop(L, 1);
        Sprite* s = static_cast<Sprite*>(lua_newuserdata(L, sizeof(Sprite)));
        s->o = o;
        luaL_getmetatable(L, "zg_sprite");
        lua_setmetatable(L, -2);
        lua_pushlightuserdata(L, o);
        lua_pushvalue(L, -2);
        lua_rawset(L, -4);
        lua_remove(L, -2);
    }

    static Sprite* spriteArg(lua_State* L, int idx) { return static_cast<Sprite*>(luaL_checkudata(L, idx, "zg_sprite")); }

    static int spriteIndex(lua_State* L) {
        Impl* I = of(L);
        Sprite* s = spriteArg(L, 1);
        if (lua_type(L, 2) != LUA_TSTRING) {
            lua_pushnil(L);
            return 1;
        }
        const std::string key = lua_tostring(L, 2);
        // methods first
        lua_getfield(L, LUA_REGISTRYINDEX, "zg_sprite_methods");
        lua_pushvalue(L, 2);
        lua_rawget(L, -2);
        if (!lua_isnil(L, -1)) return 1;
        lua_pop(L, 2);
        if (!I->alive(s->o)) {
            lua_pushnumber(L, 0);
            return 1;
        }
        const std::string up = upperOf(key);
        const auto& ids = slotIds();
        if (auto it = ids.find(up); it != ids.end()) {
            lua_pushnumber(L, s->o->slots[it->second]);
            return 1;
        }
        if (up.rfind("SLOT", 0) == 0 && up.size() > 4 && std::isdigit(static_cast<unsigned char>(up[4]))) {
            const int id = std::atoi(up.c_str() + 4);
            lua_pushnumber(L, id >= 0 && id < static_cast<int>(foit::kSlotCount) ? s->o->slots[id] : 0);
            return 1;
        }
        if (up.rfind("FLAG", 0) == 0 && up.size() > 4 && std::isdigit(static_cast<unsigned char>(up[4]))) {
            const int bit = std::atoi(up.c_str() + 4);
            const bool on = bit >= 0 && bit < foit::vm::kFlagWords * 32 && ((s->o->flags[bit / 32] >> (bit % 32)) & 1u);
            lua_pushnumber(L, on ? 1 : 0);
            return 1;
        }
        if (up == "ID") {
            lua_pushnumber(L, s->o->id);
            return 1;
        }
        lua_pushnil(L);
        return 1;
    }

    static int spriteNewIndex(lua_State* L) {
        Impl* I = of(L);
        Sprite* s = spriteArg(L, 1);
        if (!I->alive(s->o) || lua_type(L, 2) != LUA_TSTRING) return 0;
        const std::string up = upperOf(lua_tostring(L, 2));
        const float v = static_cast<float>(luaL_optnumber(L, 3, 0));
        const auto& ids = slotIds();
        if (auto it = ids.find(up); it != ids.end()) {
            s->o->slots[it->second] = v;
        } else if (up.rfind("SLOT", 0) == 0 && up.size() > 4 && std::isdigit(static_cast<unsigned char>(up[4]))) {
            const int id = std::atoi(up.c_str() + 4);
            if (id >= 0 && id < static_cast<int>(foit::kSlotCount)) s->o->slots[id] = v;
        } else if (up.rfind("FLAG", 0) == 0 && up.size() > 4 && std::isdigit(static_cast<unsigned char>(up[4]))) {
            const int bit = std::atoi(up.c_str() + 4);
            if (bit >= 0 && bit < foit::vm::kFlagWords * 32) {
                if (v != 0) s->o->flags[bit / 32] |= 1u << (bit % 32);
                else s->o->flags[bit / 32] &= ~(1u << (bit % 32));
            }
        }
        return 0;
    }

    static int spriteEq(lua_State* L) {
        Sprite* a = static_cast<Sprite*>(lua_touserdata(L, 1));
        Sprite* b = static_cast<Sprite*>(lua_touserdata(L, 2));
        lua_pushboolean(L, a && b && a->o == b->o);
        return 1;
    }

    // sprite:hasTag("Normal", "Standing") - true when the sprite carries ALL of them
    static int mHasTag(lua_State* L) {
        Impl* I = of(L);
        Sprite* s = spriteArg(L, 1);
        const int n = lua_gettop(L);
        if (!I->alive(s->o)) {
            lua_pushboolean(L, 0);
            return 1;
        }
        for (int i = 2; i <= n; ++i) {
            if (lua_type(L, i) != LUA_TSTRING) continue;
            if (!rt::objectHasTag(*s->o, palfx::nameHash(lua_tostring(L, i)))) {
                lua_pushboolean(L, 0);
                return 1;
            }
        }
        lua_pushboolean(L, 1);
        return 1;
    }

    // sprite:getChildren() -> { sprite, ... }: the live objects that belong to this character (projectiles, effects, helpers)
    static int mGetChildren(lua_State* L) {
        Impl* I = of(L);
        Sprite* s = spriteArg(L, 1);
        lua_newtable(L);
        if (!I->alive(s->o)) return 1;
        int k = 1;
        for (rt::Object* o : I->h->fight_->world()->objects) {
            if (!o || !o->alive || o == s->o || o->leader != s->o) continue;
            I->pushSprite(L, o);
            lua_rawseti(L, -2, k++);
        }
        return 1;
    }

    static int mGetLastAttacker(lua_State* L) {
        Impl* I = of(L);
        Sprite* s = spriteArg(L, 1);
        if (!I->alive(s->o) || !I->alive(s->o->attacker)) {
            lua_pushnil(L);
            return 1;
        }
        I->pushSprite(L, s->o->attacker);
        return 1;
    }

    // A number that names one object for as long as it lives (scripts key tables with it): spawned children have their own id, the bodies
    // (id 0) are told apart by their place at the head of the world's list.
    double identity(const rt::Object* o) const {
        if (!o) return 0;
        if (o->id != 0) return o->id;
        rt::World* w = h->fight_ ? h->fight_->world() : nullptr;
        if (w)
            for (size_t i = 0; i < w->objects.size(); ++i)
                if (w->objects[i] == o) return -static_cast<double>(i + 1);
        return 0;
    }
    // sprite:ref() -> that number
    static int mRef(lua_State* L) {
        Impl* I = of(L);
        Sprite* s = spriteArg(L, 1);
        lua_pushnumber(L, I->alive(s->o) ? I->identity(s->o) : 0);
        return 1;
    }

    // sprite:getHitInfo() -> { ref = <identity of the hit being taken> } or nil while the sprite is not being hit
    static int mGetHitInfo(lua_State* L) {
        Impl* I = of(L);
        Sprite* s = spriteArg(L, 1);
        if (!I->alive(s->o) || !s->o->attacker || (s->o->slots[foit::slot::HITSTUN] <= 0 && s->o->slots[foit::slot::GOTHIT] == 0)) {
            lua_pushnil(L);
            return 1;
        }
        const rt::Object* a = s->o->attacker;
        lua_newtable(L);
        lua_pushnumber(L, I->identity(a) * 1000000.0 + a->slots[foit::slot::NUMHITS] * 1000.0 + a->hitNode * 10.0 + a->hitProp);
        lua_setfield(L, -2, "ref");
        lua_pushnumber(L, a->strength);
        lua_setfield(L, -2, "strength");
        return 1;
    }

    // the getChar index of a sprite (-1 = it is not one of the six characters: a child or an effect)
    int charIndex(const rt::Object* o) const {
        if (!o || !h->fight_) return -1;
        for (int i = 0; i < kChars; ++i)
            if (h->fight_->character(i) == o) return i;
        return -1;
    }
    static int mAddForbiddenTag(lua_State* L) {
        Impl* I = of(L);
        const int c = I->charIndex(spriteArg(L, 1)->o);
        if (c < 0) return 0;
        auto& list = I->h->forbidden_[c];
        for (int i = 2; i <= lua_gettop(L); ++i)
            if (lua_type(L, i) == LUA_TSTRING) {
                const uint32_t hsh = palfx::nameHash(lua_tostring(L, i));
                if (std::find(list.begin(), list.end(), hsh) == list.end()) list.push_back(hsh);
            }
        return 0;
    }
    static int mClearForbiddenTags(lua_State* L) {
        Impl* I = of(L);
        const int c = I->charIndex(spriteArg(L, 1)->o);
        if (c >= 0) I->h->forbidden_[c].clear();
        return 0;
    }
    static int mBlockingAllowed(lua_State* L) {
        Impl* I = of(L);
        const int c = I->charIndex(spriteArg(L, 1)->o);
        if (c >= 0) I->h->blocking_[c] = lua_toboolean(L, 2) != 0;
        return 0;
    }

    // ---- globals
    static int gGetChar(lua_State* L) {
        Impl* I = of(L);
        const int i = static_cast<int>(luaL_checknumber(L, 1));
        rt::Object* o = I->h->fight_ ? I->h->fight_->character(i) : nullptr;
        I->pushSprite(L, o);
        return 1;
    }
    static int gHash(lua_State* L) {
        lua_pushnumber(L, static_cast<double>(palfx::nameHash(luaL_checkstring(L, 1))));
        return 1;
    }
    static int gRandom(lua_State* L) {   // random(n) -> 1..n (a deterministic stream per host); random() -> 0..1
        Impl* I = of(L);
        uint32_t& r = I->h->rng_;
        r ^= r << 13;
        r ^= r >> 17;
        r ^= r << 5;
        if (lua_gettop(L) == 0) {
            lua_pushnumber(L, (r & 0xFFFFFF) / 16777216.0);
            return 1;
        }
        const int n = std::max(1, static_cast<int>(luaL_checknumber(L, 1)));
        lua_pushnumber(L, 1 + static_cast<int>(r % static_cast<uint32_t>(n)));
        return 1;
    }
    static int gPrint(lua_State* L) {
        std::string line;
        const int n = lua_gettop(L);
        for (int i = 1; i <= n; ++i) {
            size_t len = 0;
            const char* sp = luaL_tolstring_compat(L, i, &len);
            if (i > 1) line += "\t";
            line.append(sp, len);
            lua_pop(L, 1);
        }
        of(L)->h->logLine(line);
        return 0;
    }
    // tostring() without luaL_tolstring (5.1 has none): the global tostring does the work
    static const char* luaL_tolstring_compat(lua_State* L, int idx, size_t* len) {
        lua_getglobal(L, "tostring");
        lua_pushvalue(L, idx);
        lua_call(L, 1, 1);
        return lua_tolstring(L, -1, len);
    }
    static int gSetInit(lua_State* L) {
        luaL_checktype(L, 1, LUA_TFUNCTION);
        lua_getfield(L, LUA_REGISTRYINDEX, "zg_inits");
        lua_pushvalue(L, 1);
        lua_rawseti(L, -2, static_cast<int>(lua_objlen(L, -2)) + 1);
        return 0;
    }
    static int gSetUpdate(lua_State* L) {
        luaL_checktype(L, 1, LUA_TFUNCTION);
        lua_getfield(L, LUA_REGISTRYINDEX, "zg_updates");
        lua_pushvalue(L, 1);
        lua_rawseti(L, -2, static_cast<int>(lua_objlen(L, -2)) + 1);
        return 0;
    }
    static int gGetDifficulty(lua_State* L) {
        Impl* I = of(L);
        lua_pushnumber(L, I->h->fight_ ? I->h->fight_->difficulty() : 3);
        return 1;
    }
    static int gFloorHeight(lua_State* L) {
        Impl* I = of(L);
        lua_pushnumber(L, 0);
        (void)I;
        return 1;
    }
    static int gLevelDimensions(lua_State* L) {
        Impl* I = of(L);
        lua_pushnumber(L, I->h->fight_ ? I->h->fight_->levelWidth() : 3750);
        lua_pushnumber(L, I->h->fight_ ? I->h->fight_->levelHeight() : 1000);
        return 2;
    }
    static int gAddInput(lua_State* L) {   // addInput(dir [, button, ...])
        Impl* I = of(L);
        InputFrame f;
        const int n = lua_gettop(L);
        if (n >= 1 && lua_isnumber(L, 1)) f.dir = std::max(0, std::min(8, static_cast<int>(lua_tonumber(L, 1))));
        for (int i = 2; i <= n; ++i)
            if (lua_isnumber(L, i)) {
                const int b = static_cast<int>(lua_tonumber(L, i));
                if (b >= 0 && b < 8) f.buttons = static_cast<uint8_t>(f.buttons | (1u << b));
            }
        I->h->added_ = f;
        I->h->addedSet_ = true;
        return 0;
    }
    static int gDoMotion(lua_State* L) {
        Impl* I = of(L);
        const char* t = lua_tostring(L, 1);
        I->h->motion_.clear();
        I->h->seq_ = nullptr;
        if (t)
            for (const InputFrame& f : parseMotion(t)) I->h->motion_.push_back(f);
        return 0;
    }
    // sequences: the game's recorded dummy inputs (.seq).  loadSequence({names}) reads them ahead, playSequence(name) starts one for the dummy
    // (one record per tick, it wins over doMotion), sequenceComplete() is true once the last record went out (or nothing plays).
    const std::vector<InputFrame>* sequence(const std::string& name) {
        auto it = h->sequences_.find(name);
        if (it != h->sequences_.end()) return &it->second;
        std::string bytes;
        std::vector<InputFrame> frames;
        if (!h->read_ || !h->read_(name, bytes) || !parseSequence(bytes, frames)) {
            h->logLine("ERROR sequence not found: " + name);
            return nullptr;
        }
        return &h->sequences_.emplace(name, std::move(frames)).first->second;
    }
    static int gLoadSequence(lua_State* L) {
        Impl* I = of(L);
        if (lua_isstring(L, 1)) {
            I->sequence(lua_tostring(L, 1));
        } else if (lua_istable(L, 1)) {
            const int n = static_cast<int>(lua_objlen(L, 1));
            for (int i = 1; i <= n; ++i) {
                lua_rawgeti(L, 1, i);
                if (lua_isstring(L, -1)) I->sequence(lua_tostring(L, -1));
                lua_pop(L, 1);
            }
        }
        return 0;
    }
    static int gPlaySequence(lua_State* L) {
        Impl* I = of(L);
        const char* name = lua_tostring(L, 1);
        I->h->seq_ = name ? I->sequence(name) : nullptr;
        I->h->seqPos_ = 0;
        I->h->motion_.clear();
        return 0;
    }
    static int gSequenceComplete(lua_State* L) {
        Impl* I = of(L);
        lua_pushboolean(L, !I->h->seq_ || I->h->seqPos_ >= I->h->seq_->size());
        return 1;
    }
    static int gNoop(lua_State*) { return 0; }
    static int gSetCharHitpause(lua_State* L) {
        Impl* I = of(L);
        rt::Object* o = I->h->fight_ ? I->h->fight_->character(static_cast<int>(luaL_checknumber(L, 1))) : nullptr;
        if (o) o->slots[foit::slot::HITPAUSE] = static_cast<float>(luaL_optnumber(L, 2, 0));
        return 0;
    }

    // saveState() -> function: put the fight back as it was when saveState() ran
    struct Saved {
        std::vector<uint8_t> bytes;
    };
    static int savedGc(lua_State* L) {
        static_cast<Saved*>(lua_touserdata(L, 1))->~Saved();
        return 0;
    }
    static int savedRestore(lua_State* L) {
        Impl* I = of(L);
        Saved* sv = static_cast<Saved*>(lua_touserdata(L, lua_upvalueindex(2)));
        if (sv && I->h->fight_ && !sv->bytes.empty()) I->h->fight_->loadState(sv->bytes);
        return 0;
    }
    static int gSaveState(lua_State* L) {
        Impl* I = of(L);
        void* mem = lua_newuserdata(L, sizeof(Saved));
        Saved* sv = new (mem) Saved();
        luaL_getmetatable(L, "zg_saved");
        lua_setmetatable(L, -2);
        if (I->h->fight_) I->h->fight_->saveState(sv->bytes);
        // closure(host, saved)
        lua_pushlightuserdata(L, I);
        lua_pushvalue(L, -2);
        lua_pushcclosure(L, savedRestore, 2);
        lua_remove(L, -2);
        return 1;
    }

    // ---- ui.*
    UiSink& ui() const { return *h->ui_; }
    static int uLoadCatalog(lua_State* L) {
        of(L)->ui().loadCatalog(luaL_optstring(L, 1, ""));
        return 0;
    }
    static int uClearAll(lua_State* L) {
        of(L)->ui().clearAll();
        return 0;
    }
    static int uSetMax(lua_State* L) {
        of(L)->ui().setMax(static_cast<int>(luaL_optnumber(L, 1, 0)));
        return 0;
    }
    static std::string textArg(lua_State* L, int i) {   // an objective's text is a key, or { key, extra ... } (the first entry is the key)
        if (lua_type(L, i) == LUA_TTABLE) {
            lua_rawgeti(L, i, 1);
            std::string r = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
            lua_pop(L, 1);
            return r;
        }
        return lua_isstring(L, i) ? lua_tostring(L, i) : "";
    }
    static int uAdd(lua_State* L) {
        of(L)->ui().add(textArg(L, 1), static_cast<int>(luaL_optnumber(L, 2, 0)), static_cast<int>(luaL_optnumber(L, 3, 0)));
        return 0;
    }
    static int uUpdate(lua_State* L) {
        of(L)->ui().update(static_cast<int>(luaL_optnumber(L, 1, 0)), textArg(L, 2), static_cast<int>(luaL_optnumber(L, 3, 0)),
                           static_cast<int>(luaL_optnumber(L, 4, 0)));
        return 0;
    }
    static int uComplete(lua_State* L) {
        of(L)->ui().complete(static_cast<int>(luaL_optnumber(L, 1, 0)), lua_toboolean(L, 2) != 0);
        return 0;
    }
    static int uScrollTo(lua_State* L) {
        of(L)->ui().scrollTo(static_cast<int>(luaL_optnumber(L, 1, 0)));
        return 0;
    }
    static int uSetExplanation(lua_State* L) {
        of(L)->ui().setExplanation(textArg(L, 1));
        return 0;
    }
    static int uShowExplanation(lua_State* L) {
        of(L)->ui().showExplanation(lua_toboolean(L, 1) != 0);
        return 0;
    }
    static int uGetExplanationStatus(lua_State* L) {
        lua_pushboolean(L, of(L)->ui().explanationActive() ? 1 : 0);
        return 1;
    }
    static int uSetDetails(lua_State* L) {
        of(L)->ui().setDetails(textArg(L, 1));
        return 0;
    }
    static int uShowDetails(lua_State* L) {
        of(L)->ui().showDetails(lua_toboolean(L, 1) != 0);
        return 0;
    }
    static int uDisplayPlayerInputs(lua_State* L) {
        of(L)->ui().displayPlayerInputs(lua_toboolean(L, 1) != 0, lua_toboolean(L, 2) != 0);
        return 0;
    }
    static int uSetSlowMoPct(lua_State* L) {
        Impl* I = of(L);
        I->h->slowMo_ = static_cast<float>(luaL_optnumber(L, 1, 0));
        I->ui().setSlowMoPct(I->h->slowMo_);
        return 0;
    }
    static int uScreenFade(lua_State* L) {
        of(L)->ui().screenFade(static_cast<float>(luaL_optnumber(L, 1, 0)), static_cast<int>(luaL_optnumber(L, 2, 0)),
                               static_cast<float>(luaL_optnumber(L, 3, 1)));
        return 0;
    }
    static int uP1Disable(lua_State* L) {
        Impl* I = of(L);
        I->h->p1Disabled_ = lua_toboolean(L, 1) != 0;
        I->ui().p1Disable(I->h->p1Disabled_);
        return 0;
    }
    static int uEndMission(lua_State* L) {
        Impl* I = of(L);
        I->h->missionComplete_ = true;
        I->ui().endMission();
        return 0;
    }

    // setInputPrompts({ { frame = 40, motion = "UF,VALID=4", details = "UF", input = "" }, ... })
    static int gSetInputPrompts(lua_State* L) {
        Impl* I = of(L);
        std::vector<std::string> motions;
        std::vector<int> frames;
        if (lua_istable(L, 1)) {
            const int n = static_cast<int>(lua_objlen(L, 1));
            for (int i = 1; i <= n; ++i) {
                lua_rawgeti(L, 1, i);
                if (lua_istable(L, -1)) {
                    lua_getfield(L, -1, "motion");
                    motions.push_back(lua_isstring(L, -1) ? lua_tostring(L, -1) : "");
                    lua_pop(L, 1);
                    lua_getfield(L, -1, "frame");
                    frames.push_back(lua_isnumber(L, -1) ? static_cast<int>(lua_tonumber(L, -1)) : 0);
                    lua_pop(L, 1);
                }
                lua_pop(L, 1);
            }
        }
        I->ui().setInputPrompts(motions, frames);
        return 0;
    }

    // require("Name"): the script text comes from the game data, once per name
    static int gRequire(lua_State* L) {
        Impl* I = of(L);
        const std::string name = luaL_checkstring(L, 1);
        lua_getfield(L, LUA_REGISTRYINDEX, "zg_loaded");
        const int loaded = lua_gettop(L);
        lua_pushstring(L, name.c_str());
        lua_rawget(L, loaded);
        if (!lua_isnil(L, -1)) return 1;
        lua_pop(L, 1);
        std::string text;
        std::string file = name;
        if (file.size() < 4 || lowerOf(file.substr(file.size() - 4)) != ".lua") file += ".lua";
        if (!I->h->read_ || !I->h->read_(file, text)) return luaL_error(L, "module '%s' not found", name.c_str());
        lua_pushstring(L, name.c_str());
        lua_pushboolean(L, 1);   // set before running: a module that requires itself must not loop
        lua_rawset(L, loaded);
        if (luaL_loadbuffer(L, text.data(), text.size(), file.c_str()) != 0) return lua_error(L);
        lua_call(L, 0, 1);
        if (lua_isnil(L, -1)) {   // like Lua's own require: what the module returns is what require gives (true when it returns nothing)
            lua_pop(L, 1);
            lua_pushboolean(L, 1);
        } else {
            lua_pushstring(L, name.c_str());
            lua_pushvalue(L, -2);
            lua_rawset(L, loaded);
        }
        return 1;
    }

    static int gOsTime(lua_State* L) {
        lua_pushnumber(L, static_cast<double>(std::time(nullptr)));
        return 1;
    }
    static int gOsClock(lua_State* L) {
        lua_pushnumber(L, static_cast<double>(std::clock()) / CLOCKS_PER_SEC);
        return 1;
    }

    // ---- installation
    void reg(lua_State* L, const char* name, lua_CFunction fn) {
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, fn, 1);
        lua_setglobal(L, name);
    }
    void regField(lua_State* L, const char* name, lua_CFunction fn) {   // into the table on top of the stack
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, fn, 1);
        lua_setfield(L, -2, name);
    }

    void install(lua_State* L) {
        const struct {
            const char* name;
            lua_CFunction open;
        } libs[] = {{"", luaopen_base}, {LUA_TABLIBNAME, luaopen_table}, {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math}, {LUA_DBLIBNAME, luaopen_debug}};
        for (const auto& l : libs) {
            lua_pushcfunction(L, l.open);
            lua_pushstring(L, l.name);
            lua_call(L, 1, 0);
        }
        // registry tables
        lua_newtable(L);
        lua_setfield(L, LUA_REGISTRYINDEX, "zg_inits");
        lua_newtable(L);
        lua_setfield(L, LUA_REGISTRYINDEX, "zg_updates");
        lua_newtable(L);
        lua_setfield(L, LUA_REGISTRYINDEX, "zg_loaded");
        lua_newtable(L);   // sprite cache: weak values
        lua_newtable(L);
        lua_pushstring(L, "v");
        lua_setfield(L, -2, "__mode");
        lua_setmetatable(L, -2);
        lua_setfield(L, LUA_REGISTRYINDEX, "zg_sprites");
        // sprite metatable + methods
        luaL_newmetatable(L, "zg_sprite");
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, spriteIndex, 1);
        lua_setfield(L, -2, "__index");
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, spriteNewIndex, 1);
        lua_setfield(L, -2, "__newindex");
        lua_pushcfunction(L, spriteEq);
        lua_setfield(L, -2, "__eq");
        lua_pop(L, 1);
        lua_newtable(L);
        regField(L, "hasTag", mHasTag);
        regField(L, "ref", mRef);
        regField(L, "getChildren", mGetChildren);
        regField(L, "getLastAttacker", mGetLastAttacker);
        regField(L, "getHitInfo", mGetHitInfo);
        regField(L, "addForbiddenTag", mAddForbiddenTag);
        regField(L, "clearForbiddenTags", mClearForbiddenTags);
        regField(L, "blockingAllowed", mBlockingAllowed);
        lua_setfield(L, LUA_REGISTRYINDEX, "zg_sprite_methods");
        luaL_newmetatable(L, "zg_saved");
        lua_pushcfunction(L, savedGc);
        lua_setfield(L, -2, "__gc");
        lua_pop(L, 1);
        // globals
        reg(L, "getChar", gGetChar);
        reg(L, "hash", gHash);
        reg(L, "random", gRandom);
        reg(L, "print", gPrint);
        reg(L, "setInit", gSetInit);
        reg(L, "setUpdate", gSetUpdate);
        reg(L, "getDifficulty", gGetDifficulty);
        reg(L, "floorHeight", gFloorHeight);
        reg(L, "levelDimensions", gLevelDimensions);
        reg(L, "addInput", gAddInput);
        reg(L, "doMotion", gDoMotion);
        reg(L, "loadSequence", gLoadSequence);
        reg(L, "playSequence", gPlaySequence);
        reg(L, "sequenceComplete", gSequenceComplete);
        reg(L, "setCharHitpause", gSetCharHitpause);
        reg(L, "setCharInvincible", gNoop);
        reg(L, "loadReplay", gNoop);
        reg(L, "setInputPrompts", gSetInputPrompts);
        reg(L, "saveState", gSaveState);
        reg(L, "require", gRequire);
        // ui
        lua_newtable(L);
        regField(L, "loadcatalog", uLoadCatalog);
        regField(L, "clearall", uClearAll);
        regField(L, "setmax", uSetMax);
        regField(L, "add", uAdd);
        regField(L, "update", uUpdate);
        regField(L, "complete", uComplete);
        regField(L, "scrollto", uScrollTo);
        regField(L, "setexplanation", uSetExplanation);
        regField(L, "showexplanation", uShowExplanation);
        regField(L, "getexplanationstatus", uGetExplanationStatus);
        regField(L, "setdetails", uSetDetails);
        regField(L, "showdetails", uShowDetails);
        regField(L, "displayplayerinputs", uDisplayPlayerInputs);
        regField(L, "setslowmopct", uSetSlowMoPct);
        regField(L, "screenfade", uScreenFade);
        regField(L, "p1disable", uP1Disable);
        regField(L, "endmission", uEndMission);
        lua_setglobal(L, "ui");
        // a tiny os (no files, no processes)
        lua_newtable(L);
        regField(L, "time", gOsTime);
        regField(L, "clock", gOsClock);
        lua_setglobal(L, "os");
    }
};

// ---------------------------------------------------------------------------------------------------- host
LuaHost::LuaHost() = default;
LuaHost::~LuaHost() { closeState(); }

void LuaHost::closeState() {
    if (L_) {
        lua_close(L_);
        L_ = nullptr;
    }
    impl_.reset();
}

void LuaHost::logLine(const std::string& s) {
    if (log_.size() < 2000) log_.push_back(s);
    if (std::getenv("ZDROID_LUA_LOG")) std::fprintf(stderr, "[lua] %s\n", s.c_str());
}

void LuaHost::failure(const std::string& s) {
    if (error_.empty()) error_ = s;
    logLine("ERROR " + s);
}

namespace {
int traceback(lua_State* L) {
    const char* msg = lua_tostring(L, 1);
    lua_getfield(L, LUA_GLOBALSINDEX, "debug");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return 1;
    }
    lua_getfield(L, -1, "traceback");
    lua_remove(L, -2);
    lua_pushstring(L, msg ? msg : "error");
    lua_pushinteger(L, 2);
    lua_call(L, 2, 1);
    return 1;
}
}  // namespace

bool LuaHost::load(FightAccess* fight, UiSink* ui, ReadText read, const std::string& scriptName) {
    closeState();
    static UiSink none;
    fight_ = fight;
    ui_ = ui ? ui : &none;
    read_ = std::move(read);
    error_.clear();
    log_.clear();
    for (auto& f : forbidden_) f.clear();
    for (bool& b : blocking_) b = true;
    p1Disabled_ = false;
    missionComplete_ = false;
    started_ = false;
    slowMo_ = 0;
    ticks_ = 0;
    added_ = InputFrame();
    addedSet_ = false;
    motion_.clear();
    sequences_.clear();
    seq_ = nullptr;
    seqPos_ = 0;
    L_ = luaL_newstate();
    if (!L_) {
        failure("out of memory");
        return false;
    }
    impl_ = std::make_unique<Impl>();
    impl_->h = this;
    impl_->install(L_);
    std::string text;
    if (!read_ || !read_(scriptName, text)) {
        failure("script not found: " + scriptName);
        return false;
    }
    lua_pushcfunction(L_, traceback);
    const int tb = lua_gettop(L_);
    if (luaL_loadbuffer(L_, text.data(), text.size(), scriptName.c_str()) != 0 || lua_pcall(L_, 0, 0, tb) != 0) {
        failure(lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "script error");
        lua_settop(L_, 0);
        return false;
    }
    lua_settop(L_, 0);
    return true;
}

namespace {
// calls every function of the registry table `name`; an error is logged once per function and does not stop the others
void callAll(lua_State* L, const char* name, LuaHost* host, const std::function<void(const std::string&)>& fail) {
    (void)host;
    lua_pushcfunction(L, traceback);
    const int tb = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, name);
    const int n = static_cast<int>(lua_objlen(L, -1));
    for (int i = 1; i <= n; ++i) {
        lua_rawgeti(L, -1, i);
        if (lua_isfunction(L, -1)) {
            if (lua_pcall(L, 0, 0, tb) != 0) {
                fail(lua_tostring(L, -1) ? lua_tostring(L, -1) : "error");
                lua_pop(L, 1);
            }
        } else {
            lua_pop(L, 1);
        }
    }
    lua_settop(L, tb - 1);
}
}  // namespace

void LuaHost::start() {
    if (!L_ || started_) return;
    started_ = true;
    callAll(L_, "zg_inits", this, [this](const std::string& m) { failure(m); });
}

void LuaHost::tick() {
    if (!L_) return;
    if (!started_) start();
    ++ticks_;
    callAll(L_, "zg_updates", this, [this](const std::string& m) { logLine("ERROR " + m); });
}

InputFrame LuaHost::takeInput(bool* scripted) {
    InputFrame f;
    bool any = false;
    if (addedSet_) {
        f = added_;
        any = true;
        motion_.clear();
    } else if (seq_ && seqPos_ < seq_->size()) {
        f = (*seq_)[seqPos_++];
        any = true;
    } else if (!motion_.empty()) {
        f = motion_.front();
        motion_.pop_front();
        any = true;
    }
    addedSet_ = false;
    if (scripted) *scripted = any;
    return f;
}

}  // namespace script
}  // namespace zengine
