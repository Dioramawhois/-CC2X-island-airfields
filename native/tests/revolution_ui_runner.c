// SPDX-License-Identifier: GPL-3.0-or-later
// Execute only our explicitly named Lua files; never a Revolution/game script.
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <stdio.h>
int main(int argc, char **argv) {
    if (argc != 4) return 2;
    lua_State *state = luaL_newstate();
    if (!state) return 3;
    luaL_openlibs(state);
    for (int i = 1; i < argc; ++i) {
        if (luaL_dofile(state, argv[i]) != LUA_OK) {
            fprintf(stderr, "%s\n", lua_tostring(state, -1));
            lua_close(state);
            return 1;
        }
    }
    lua_close(state);
    return 0;
}
