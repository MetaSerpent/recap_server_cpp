#ifndef RECAP_GAME_LUA_THREAD_STATE_H
#define RECAP_GAME_LUA_THREAD_STATE_H

namespace Game {
	// lua_status() of a pooled coroutine thread after its coroutine finished:
	// 0 (LUA_OK) and 1 (LUA_YIELD) can run again; error codes (LUA_ERRRUN, ...)
	// leave the thread unusable and it must be replaced.
	constexpr bool LuaThreadNeedsReset(int luaStatus) noexcept {
		return luaStatus != 0 && luaStatus != 1;
	}
}

#endif
