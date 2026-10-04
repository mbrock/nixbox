#include <Luau/Compiler.h>
#include <lua.h>
#include <lualib.h>
#include <cstdio>

static int apply_damage(lua_State* state)
{
    lua_pushnumber(state, luaL_checknumber(state, 1) * 2);
    return 1;
}

int main()
{
    lua_State* state = luaL_newstate();
    if (!state)
        return 1;
    luaL_openlibs(state);
    lua_pushcfunction(state, apply_damage, "apply_damage");
    lua_setglobal(state, "apply_damage");
    luaL_sandbox(state);
    luaL_sandboxthread(state);
    const auto bytecode = Luau::compile(R"(
        local health = 100
        for _, hit in {7, 11, 9} do
            health -= apply_damage(hit)
        end
        return health
    )");
    if (luau_load(state, "gameplay-smoke", bytecode.data(), bytecode.size(), 0) != 0 ||
        lua_pcall(state, 0, 1, 0) != 0)
    {
        std::fprintf(stderr, "Luau failed: %s\n", lua_tostring(state, -1));
        lua_close(state);
        return 1;
    }
    const double health = lua_tonumber(state, -1);
    std::printf("Luau scripted health: %.0f\n", health);
    lua_close(state);
    return health == 46 ? 0 : 1;
}
