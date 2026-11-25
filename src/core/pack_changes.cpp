#include "pack_changes.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

namespace zengine {
namespace packchanges {

namespace fs = std::filesystem;

namespace {

std::string lowerOf(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string slashes(std::string s) {
    for (char& c : s)
        if (c == '\\') c = '/';
    return s;
}
bool hasDotDot(const std::string& s) {
    size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find('/', i);
        if (j == std::string::npos) j = s.size();
        if (s.compare(i, j - i, "..") == 0 && j - i == 2) return true;
        i = j + 1;
    }
    return false;
}
std::string trimSlashes(std::string s) {
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}
bool startsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }
bool startsWithNoCase(const std::string& s, const std::string& p) { return startsWith(lowerOf(s), lowerOf(p)); }

std::string jsonEscape(const std::string& v) {
    std::string o;
    for (unsigned char c : v) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", c);
                    o += b;
                } else {
                    o += static_cast<char>(c);
                }
        }
    }
    return o;
}

struct Impl {
    const Context* ctx = nullptr;
    Result* res = nullptr;
    std::string work;                                         // work_dir with '/'
    std::vector<std::pair<std::string, std::string>> locs;    // addLocalization
    int hookTicks = 0;

    // The pack-relative part of a path a script gave: "<work>/mods/<name>/rest" -> rest, "<pack>/rest" -> rest, "<temp>/rest" -> rest,
    // a relative path as it is.  false = not a path the script may use (outside the pack, "..").
    bool restOf(const std::string& raw, std::string& rest) const {
        std::string s = slashes(raw);
        if (s.empty() || hasDotDot(s)) return false;
        const std::string pack = trimSlashes(slashes(ctx->packDir)), tmp = trimSlashes(slashes(ctx->tempDir));
        const std::string modsPrefix = work + "/mods/";
        if (startsWithNoCase(s, tmp + "/")) rest = s.substr(tmp.size() + 1);
        else if (startsWithNoCase(s, pack + "/")) rest = s.substr(pack.size() + 1);
        else if (!work.empty() && startsWithNoCase(s, modsPrefix)) {
            const size_t slash = s.find('/', modsPrefix.size());
            rest = slash == std::string::npos ? std::string() : s.substr(slash + 1);
        } else if (s[0] == '/' || (s.size() > 1 && s[1] == ':')) {
            return false;
        } else {
            rest = s;
        }
        while (startsWith(rest, "./")) rest = rest.substr(2);
        return true;
    }
    // The real path: writes always go to the scratch folder; a read prefers a scratch file of that name, then the pack's own.
    bool mapPath(const std::string& raw, bool write, std::string& real) const {
        std::string rest;
        if (!restOf(raw, rest)) return false;
        const fs::path t = fs::path(ctx->tempDir) / rest;
        if (write) {
            real = t.string();
            return true;
        }
        std::error_code ec;
        real = fs::exists(t, ec) ? t.string() : (fs::path(ctx->packDir) / rest).string();
        return true;
    }
    void warn(const std::string& m) { res->warnings.push_back(m); }
    void addRule(const std::string& gameRaw, const std::string& real) {
        std::string game = slashes(gameRaw);
        while (startsWith(game, "./")) game = game.substr(2);
        while (!game.empty() && game[0] == '/') game.erase(0, 1);
        if (game.empty() || hasDotDot(game)) {
            warn("refused game path: " + gameRaw);
            return;
        }
        const std::string key = lowerOf(game);
        for (auto it = res->rules.begin(); it != res->rules.end(); ++it)
            if (lowerOf(it->game) == key) {
                res->rules.erase(it);
                break;
            }
        res->rules.push_back({game, real});
    }
    // a file -> one rule; a folder -> one rule per file under `game`
    bool addFrom(const std::string& game, const std::string& realPath) {
        std::error_code ec;
        if (fs::is_directory(realPath, ec)) {
            std::vector<std::pair<std::string, std::string>> files;
            for (fs::recursive_directory_iterator it(realPath, ec), end; !ec && it != end; it.increment(ec)) {
                std::error_code e2;
                if (!it->is_regular_file(e2)) continue;
                files.emplace_back(fs::relative(it->path(), realPath, e2).generic_string(), it->path().string());
            }
            std::sort(files.begin(), files.end());
            std::string g = trimSlashes(slashes(game));
            for (const auto& f : files) addRule(g.empty() ? f.first : g + "/" + f.first, f.second);
            return !files.empty();
        }
        if (!fs::is_regular_file(realPath, ec)) return false;
        std::string g = slashes(game);
        if (g.empty() || g.back() == '/') g += fs::path(realPath).filename().string();
        addRule(g, realPath);
        return true;
    }
};

Impl* impl(lua_State* L) { return static_cast<Impl*>(lua_touserdata(L, lua_upvalueindex(1))); }

int cAddFile(lua_State* L) {
    Impl* I = impl(L);
    const std::string game = luaL_checkstring(L, 2), file = luaL_checkstring(L, 3);
    std::string real;
    bool ok = I->mapPath(file, false, real) && I->addFrom(game, real);
    if (!ok) I->warn("gfsAddFile: nothing at " + file);
    lua_pushboolean(L, ok);
    return 1;
}
int cExtract(lua_State* L) {
    Impl* I = impl(L);
    const std::string game = slashes(luaL_checkstring(L, 2)), out = luaL_checkstring(L, 3);
    std::string dst;
    if (!I->mapPath(out, true, dst)) {
        I->warn("gfsExtractFile: path not allowed: " + out);
        lua_pushboolean(L, 0);
        return 1;
    }
    std::vector<uint8_t> bytes;
    bool found = false;
    for (auto it = I->res->rules.rbegin(); it != I->res->rules.rend() && !found; ++it)   // what an earlier rule made of it
        if (lowerOf(it->game) == lowerOf(game)) found = readFile(it->real, bytes);
    if (!found && I->ctx->base) found = I->ctx->base->read(game, bytes);
    if (!found) {
        I->warn("gfsExtractFile: the game has no " + game);
        lua_pushboolean(L, 0);
        return 1;
    }
    std::error_code ec;
    fs::create_directories(fs::path(dst).parent_path(), ec);
    std::ofstream f(dst, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    lua_pushboolean(L, f.good());
    return 1;
}
int cTrue(lua_State* L) {
    lua_pushboolean(L, 1);
    return 1;
}
int cLoc(lua_State* L) {
    impl(L)->locs.emplace_back(luaL_checkstring(L, 1), luaL_checkstring(L, 2));
    lua_pushboolean(L, 1);
    return 1;
}
int cPermission(lua_State* L) {
    impl(L)->res->log.push_back(std::string("addNewPermission(") + luaL_checkstring(L, 1) + "): nothing to do here");
    lua_pushboolean(L, 1);
    return 1;
}
int cWorkDir(lua_State* L) {
    lua_pushstring(L, impl(L)->work.c_str());
    return 1;
}
int cVersion(lua_State* L) {
    Impl* I = impl(L);
    lua_pushnumber(L, I->ctx->modVersion ? I->ctx->modVersion(luaL_checkstring(L, 1)) : 0.0);
    return 1;
}
int cHasMod(lua_State* L) {
    Impl* I = impl(L);
    lua_pushboolean(L, I->ctx->modVersion && I->ctx->modVersion(luaL_checkstring(L, 1)) > 0);
    return 1;
}
int cRefusedFalse(lua_State* L) {
    impl(L)->warn("a call that would change the runtime was refused (memory access / gbsMerge are not available)");
    lua_pushboolean(L, 0);
    return 1;
}
int cRefusedNil(lua_State* L) {
    impl(L)->warn("a call that would read the runtime was refused");
    lua_pushnil(L);
    return 1;
}
int cPrint(lua_State* L) {
    std::string line;
    const int n = lua_gettop(L);
    for (int i = 1; i <= n; ++i) {
        size_t len = 0;
        const char* s = lua_tolstring(L, i, &len);
        line += (i > 1 ? "\t" : "") + std::string(s ? s : "", len);
        lua_pop(L, 1);
    }
    impl(L)->res->log.push_back("[lua] " + line);
    return 0;
}
// A small file object of our own (the standard io library is not part of the sandbox): read / write / lines-free, scratch folder only.
struct LFile {
    std::FILE* f;
};
LFile* toFile(lua_State* L, int i) { return static_cast<LFile*>(luaL_checkudata(L, i, "zg.file")); }
std::FILE* openFile(lua_State* L, int i) {
    LFile* lf = toFile(L, i);
    if (!lf->f) luaL_error(L, "attempt to use a closed file");
    return lf->f;
}
int fWrite(lua_State* L) {
    std::FILE* f = openFile(L, 1);
    for (int i = 2; i <= lua_gettop(L); ++i) {
        size_t len = 0;
        const char* d = luaL_checklstring(L, i, &len);
        if (std::fwrite(d, 1, len, f) != len) {
            lua_pushnil(L);
            lua_pushstring(L, "write error");
            return 2;
        }
    }
    lua_pushvalue(L, 1);
    return 1;
}
int fRead(lua_State* L) {
    std::FILE* f = openFile(L, 1);
    const int top = lua_gettop(L);
    if (top < 2) {   // default: a line
        std::string line;
        int c, any = 0;
        while ((c = std::fgetc(f)) != EOF) {
            any = 1;
            if (c == '\n') break;
            line += static_cast<char>(c);
        }
        if (!any) lua_pushnil(L);
        else lua_pushlstring(L, line.data(), line.size());
        return 1;
    }
    for (int i = 2; i <= top; ++i) {
        if (lua_type(L, i) == LUA_TNUMBER) {
            const size_t n = static_cast<size_t>(lua_tonumber(L, i));
            std::string buf(n, '\0');
            const size_t got = n ? std::fread(&buf[0], 1, n, f) : 0;
            if (n && !got) lua_pushnil(L);
            else lua_pushlstring(L, buf.data(), got);
        } else {
            const std::string fmt = luaL_checkstring(L, i);
            if (fmt == "*a" || fmt == "a") {
                std::string all;
                char tmp[4096];
                size_t n;
                while ((n = std::fread(tmp, 1, sizeof tmp, f)) > 0) all.append(tmp, n);
                lua_pushlstring(L, all.data(), all.size());
            } else {   // "*l" / "l"
                std::string line;
                int c, any = 0;
                while ((c = std::fgetc(f)) != EOF) {
                    any = 1;
                    if (c == '\n') break;
                    line += static_cast<char>(c);
                }
                if (!any) lua_pushnil(L);
                else lua_pushlstring(L, line.data(), line.size());
            }
        }
    }
    return top - 1;
}
// f:lines(...) -> an iterator that reads with the same formats as f:read(...) (default: a line)
int linesIter(lua_State* L) {
    const int nf = static_cast<int>(lua_tointeger(L, lua_upvalueindex(1)));
    lua_settop(L, 0);
    lua_pushvalue(L, lua_upvalueindex(2));   // the file
    for (int i = 0; i < nf; ++i) lua_pushvalue(L, lua_upvalueindex(3 + i));
    return fRead(L);
}
int fLines(lua_State* L) {
    toFile(L, 1);
    const int nf = lua_gettop(L) - 1;
    if (nf > 200) luaL_error(L, "too many formats");
    lua_pushinteger(L, nf);
    lua_pushvalue(L, 1);
    for (int i = 0; i < nf; ++i) lua_pushvalue(L, 2 + i);
    lua_pushcclosure(L, linesIter, 2 + nf);
    return 1;
}
int fClose(lua_State* L) {
    LFile* lf = toFile(L, 1);
    if (lf->f) std::fclose(lf->f);
    lf->f = nullptr;
    lua_pushboolean(L, 1);
    return 1;
}
int fFlush(lua_State* L) {
    std::fflush(openFile(L, 1));
    lua_pushvalue(L, 1);
    return 1;
}
int fSeek(lua_State* L) {
    std::FILE* f = openFile(L, 1);
    static const int whence[] = {SEEK_SET, SEEK_CUR, SEEK_END};
    static const char* const names[] = {"set", "cur", "end", nullptr};
    const int w = luaL_checkoption(L, 2, "cur", names);
    const long off = static_cast<long>(luaL_optnumber(L, 3, 0));
    if (std::fseek(f, off, whence[w]) != 0) {
        lua_pushnil(L);
        lua_pushstring(L, "seek error");
        return 2;
    }
    lua_pushnumber(L, static_cast<lua_Number>(std::ftell(f)));
    return 1;
}
int cOpen(lua_State* L) {   // io.open limited to the scratch folder / the pack's own files
    Impl* I = impl(L);
    const std::string path = luaL_checkstring(L, 1);
    const std::string mode = luaL_optstring(L, 2, "r");
    if (mode.empty() || mode.find_first_not_of("rwab+") != std::string::npos) luaL_argerror(L, 2, "invalid mode");
    const bool write = mode.find_first_of("wa+") != std::string::npos;
    std::string real;
    if (!I->mapPath(path, write, real)) {
        lua_pushnil(L);
        lua_pushstring(L, "path not allowed");
        return 2;
    }
    if (write) {
        std::error_code ec;
        fs::create_directories(fs::path(real).parent_path(), ec);
    }
    std::FILE* f = std::fopen(real.c_str(), mode.c_str());
    if (!f) {
        lua_pushnil(L);
        lua_pushstring(L, (path + ": cannot open").c_str());
        return 2;
    }
    LFile* lf = static_cast<LFile*>(lua_newuserdata(L, sizeof(LFile)));
    lf->f = f;
    luaL_getmetatable(L, "zg.file");
    lua_setmetatable(L, -2);
    return 1;
}
int cTime(lua_State* L) {
    lua_pushnumber(L, static_cast<lua_Number>(std::time(nullptr)));
    return 1;
}
int cClock(lua_State* L) {
    lua_pushnumber(L, static_cast<lua_Number>(std::clock()) / CLOCKS_PER_SEC);
    return 1;
}
int cDate(lua_State* L) {
    const std::string fmt = luaL_optstring(L, 1, "%c");
    std::time_t t = lua_isnumber(L, 2) ? static_cast<std::time_t>(lua_tonumber(L, 2)) : std::time(nullptr);
    std::tm* tm = std::localtime(&t);
    char buf[256];
    const size_t n = tm ? std::strftime(buf, sizeof buf, fmt.c_str() + (fmt[0] == '!' ? 1 : 0), tm) : 0;
    lua_pushlstring(L, buf, n);
    return 1;
}
int cRemove(lua_State* L) {
    Impl* I = impl(L);
    std::string real;
    if (!I->mapPath(luaL_checkstring(L, 1), true, real)) {
        lua_pushnil(L);
        lua_pushstring(L, "path not allowed");
        return 2;
    }
    std::error_code ec;
    if (fs::remove(real, ec)) {
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushnil(L);
    lua_pushstring(L, "no such file");
    return 2;
}
void hook(lua_State* L, lua_Debug*) {
    lua_getfield(L, LUA_REGISTRYINDEX, "zg_pack_impl");
    Impl* I = static_cast<Impl*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (I && ++I->hookTicks > 60) luaL_error(L, "the script ran too long and was stopped");
}

void reg(lua_State* L, Impl* I, const char* name, lua_CFunction fn) {   // into the table on top
    lua_pushlightuserdata(L, I);
    lua_pushcclosure(L, fn, 1);
    lua_setfield(L, -2, name);
}

// the rule table forms: { ["game"] = "file", { game=, source= }, { "game", "file" }, "file" }
void readTable(lua_State* L, int idx, Impl* I) {
    if (idx < 0) idx = lua_gettop(L) + idx + 1;
    auto str = [&](int at, const char* k) -> std::string {
        lua_getfield(L, at, k);
        std::string v = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
        lua_pop(L, 1);
        return v;
    };
    lua_pushnil(L);
    while (lua_next(L, idx) != 0) {
        const int vi = lua_gettop(L), ki = vi - 1;
        std::string game, source;
        if (lua_type(L, ki) == LUA_TSTRING && lua_type(L, vi) == LUA_TSTRING) {
            game = lua_tostring(L, ki);
            source = lua_tostring(L, vi);
        } else if (lua_type(L, vi) == LUA_TTABLE) {
            for (const char* k : {"game", "target", "file", "path", "name"})
                if (game.empty()) game = str(vi, k);
            for (const char* k : {"source", "with", "from", "replace", "replacement", "src"})
                if (source.empty()) source = str(vi, k);
            if (game.empty() && source.empty()) {   // { "game path", "pack file" }
                lua_rawgeti(L, vi, 1);
                if (lua_isstring(L, -1)) game = lua_tostring(L, -1);
                lua_pop(L, 1);
                lua_rawgeti(L, vi, 2);
                if (lua_isstring(L, -1)) source = lua_tostring(L, -1);
                lua_pop(L, 1);
            }
        } else if (lua_type(L, vi) == LUA_TSTRING) {   // a bare file: it replaces the game's file of that name
            source = lua_tostring(L, vi);
            game = fs::path(slashes(source)).filename().string();
        }
        if (!game.empty() && !source.empty()) {
            std::string real;
            if (I->mapPath(source, false, real) && I->addFrom(game, real)) {}
            else I->warn("changes: no file " + source + " for " + game);
        }
        lua_pop(L, 1);
    }
}

}  // namespace

bool isModFile(const std::string& path) {
    const std::string l = lowerOf(path);
    for (const std::string& e : {std::string(".foit"), std::string(".art"), std::string(".art-pt")})
        if (l.size() >= e.size() && l.compare(l.size() - e.size(), e.size(), e) == 0) return true;
    return false;
}

Result run(const std::string& changesFile, const Context& ctx) {
    Result res;
    std::string text;
    {
        std::ifstream f(changesFile, std::ios::binary);
        if (!f) {
            res.error = "cannot read " + changesFile;
            return res;
        }
        text.assign(std::istreambuf_iterator<char>(f), {});
    }
    std::error_code ec;
    fs::remove_all(ctx.tempDir, ec);
    fs::create_directories(ctx.tempDir, ec);
    fs::create_directories(ctx.outDir, ec);

    Impl I;
    I.ctx = &ctx;
    I.res = &res;
    I.work = trimSlashes(slashes(ctx.gameDir));

    lua_State* L = luaL_newstate();
    if (!L) {
        res.error = "out of memory";
        return res;
    }
    // libraries: base + table + string + math; io / os are rebuilt below with only what the sandbox lets through
    struct Lib {
        const char* name;
        lua_CFunction open;
    };
    const Lib libs[] = {{"", luaopen_base}, {LUA_TABLIBNAME, luaopen_table}, {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math}};
    for (const Lib& l : libs) {
        lua_pushcfunction(L, l.open);
        lua_pushstring(L, l.name);
        lua_call(L, 1, 0);
    }
    for (const char* g : {"dofile", "loadfile", "newproxy"}) {
        lua_pushnil(L);
        lua_setglobal(L, g);
    }
    lua_pushlightuserdata(L, &I);
    lua_setfield(L, LUA_REGISTRYINDEX, "zg_pack_impl");
    lua_pushlightuserdata(L, &I);
    lua_pushcclosure(L, cPrint, 1);
    lua_setglobal(L, "print");
    {   // io: only open (our own file object); os: only time / date / clock / remove (scratch folder)
        luaL_newmetatable(L, "zg.file");
        lua_newtable(L);
        const luaL_Reg methods[] = {{"write", fWrite}, {"read", fRead}, {"close", fClose}, {"flush", fFlush}, {"seek", fSeek}, {"lines", fLines}, {nullptr, nullptr}};
        for (const luaL_Reg* m = methods; m->name; ++m) {
            lua_pushcfunction(L, m->func);
            lua_setfield(L, -2, m->name);
        }
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, fClose);
        lua_setfield(L, -2, "__gc");
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushlightuserdata(L, &I);
        lua_pushcclosure(L, cOpen, 1);
        lua_setfield(L, -2, "open");
        lua_setglobal(L, "io");
        lua_newtable(L);
        lua_pushcfunction(L, cTime);
        lua_setfield(L, -2, "time");
        lua_pushcfunction(L, cClock);
        lua_setfield(L, -2, "clock");
        lua_pushcfunction(L, cDate);
        lua_setfield(L, -2, "date");
        lua_pushlightuserdata(L, &I);
        lua_pushcclosure(L, cRemove, 1);
        lua_setfield(L, -2, "remove");
        lua_setglobal(L, "os");
    }
    lua_newtable(L);   // CCLib
    reg(L, &I, "gfsAddFile", cAddFile);
    reg(L, &I, "gfsAddFiles", cAddFile);
    reg(L, &I, "gfsExtractFile", cExtract);
    reg(L, &I, "gfsExtractFiles", cExtract);
    reg(L, &I, "gfsCommitChanges", cTrue);
    reg(L, &I, "addLocalization", cLoc);
    reg(L, &I, "addNewPermission", cPermission);
    reg(L, &I, "hasMod", cHasMod);
    reg(L, &I, "getModVersion", cVersion);
    reg(L, &I, "getWorkingDirectory", cWorkDir);
    reg(L, &I, "gbsMerge", cRefusedFalse);
    reg(L, &I, "writeAddressString", cRefusedFalse);
    reg(L, &I, "writeAddressNumber", cRefusedFalse);
    reg(L, &I, "readAddressString", cRefusedNil);
    reg(L, &I, "readAddressNumber", cRefusedNil);
    reg(L, &I, "getGameBaseAddress", cRefusedNil);
    lua_pushstring(L, I.work.c_str());
    lua_setfield(L, -2, "work_dir");
    lua_pushstring(L, (I.work + "/data01").c_str());
    lua_setfield(L, -2, "data01_dir");
    lua_pushstring(L, (I.work + "/data02").c_str());
    lua_setfield(L, -2, "data02_dir");
    lua_pushnumber(L, 1);
    lua_setfield(L, -2, "CC_version");
    lua_setglobal(L, "CCLib");
    lua_sethook(L, hook, LUA_MASKCOUNT, 1000000);

    auto fail = [&](const std::string& m) {
        res.error = m;
        res.rules.clear();
        lua_close(L);
        return res;
    };
    if (luaL_loadbuffer(L, text.data(), text.size(), "changes.lua") != 0) return fail(lua_tostring(L, -1));
    if (lua_pcall(L, 0, 1, 0) != 0) return fail(lua_tostring(L, -1));
    const int retIdx = lua_gettop(L);
    auto callIf = [&](const char* table, const char* fn) -> bool {   // Mod.install() / install() / init()
        if (table) {
            lua_getglobal(L, table);
            if (!lua_istable(L, -1)) {
                lua_pop(L, 1);
                return true;
            }
            lua_getfield(L, -1, fn);
            lua_remove(L, -2);
        } else {
            lua_getglobal(L, fn);
        }
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 1);
            return true;
        }
        if (lua_pcall(L, 0, 0, 0) != 0) {
            res.error = lua_tostring(L, -1);
            return false;
        }
        return true;
    };
    if (!callIf("Mod", "init") || !callIf(nullptr, "init") || !callIf("Mod", "install") || !callIf(nullptr, "install")) return fail(res.error);
    // table styles
    if (lua_istable(L, retIdx)) readTable(L, retIdx, &I);
    for (const char* g : {"Changes", "changes", "Rules"}) {
        lua_getglobal(L, g);
        if (lua_istable(L, -1)) readTable(L, lua_gettop(L), &I);
        lua_pop(L, 1);
    }
    // ModInfo
    lua_getglobal(L, "ModInfo");
    if (lua_istable(L, -1)) {
        auto get = [&](const char* k) {
            lua_getfield(L, -1, k);
            std::string v = lua_isstring(L, -1) ? lua_tostring(L, -1) : (lua_isnumber(L, -1) ? std::to_string(lua_tonumber(L, -1)) : "");
            lua_pop(L, 1);
            return v;
        };
        res.name = get("ModName");
        res.version = get("Version");
        res.author = get("Author");
        res.description = get("Desc");
        if (res.version.find('.') != std::string::npos) {   // 2.000000 -> 2
            while (res.version.size() > 1 && res.version.back() == '0') res.version.pop_back();
            if (!res.version.empty() && res.version.back() == '.') res.version.pop_back();
        }
    }
    lua_pop(L, 1);
    lua_close(L);

    // addLocalization: a copy of every <lang>_loc.json with the keys added (the originals stay as they are)
    if (!I.locs.empty() && ctx.base) {
        for (const std::string& real : ctx.base->listByExt("_loc.json")) {
            std::vector<uint8_t> bytes;
            if (!readFile(real, bytes)) continue;
            std::string t(bytes.begin(), bytes.end());
            const size_t close = t.find_last_of('}');
            if (close == std::string::npos) continue;
            size_t last = close;
            while (last > 0 && std::isspace(static_cast<unsigned char>(t[last - 1]))) --last;
            const bool empty = last > 0 && t[last - 1] == '{';
            std::string add;
            for (const auto& kv : I.locs) add += std::string(empty && add.empty() ? "" : ",") + "\n  \"" + jsonEscape(kv.first) + "\": \"" + jsonEscape(kv.second) + "\"";
            t = t.substr(0, last) + add + "\n" + t.substr(close);
            const std::string name = fs::path(real).filename().string();
            const fs::path out = fs::path(ctx.outDir) / name;
            std::ofstream f(out, std::ios::binary | std::ios::trunc);
            f.write(t.data(), static_cast<std::streamsize>(t.size()));
            if (f.good()) res.rules.push_back({name, out.string()});
        }
    }
    if (res.rules.empty()) res.warnings.push_back("changes.lua made no changes");
    res.ok = true;
    return res;
}

}  // namespace packchanges
}  // namespace zengine
