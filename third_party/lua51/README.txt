Lua 5.1.5 (https://www.lua.org/), MIT license (see COPYRIGHT), unmodified.
Only the language core and the base / table / string / math / debug libraries are kept: no io, os, package
or the stand-alone interpreter - the scripts of the game get their own `require` and a tiny `os` from the host
(src/core/lua_host.cpp).
The game's training / tutorial / trial scripts are Lua 5.1 (unpack, the string metatable, coroutines), which is why this version.
