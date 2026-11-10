// LuaJIT + sol2 behaviour behind "cannot resume non-suspended coroutine":
// a pooled coroutine thread whose script raised an error cannot run anything
// again, so Lua::ReturnThreadToPool must replace it (Game::LuaThreadNeedsReset).
// Build: see the "Register game abilities" step in gameplay-guardrails.yml.
#include "../darkspore_server/source/game/lua_thread_state.h"

#include <sol/sol.hpp>

#include <iostream>
#include <string>

namespace {
	int failures = 0;
	void Check(bool condition, const std::string& what) {
		if (!condition) {
			std::cerr << "FAIL: " << what << '\n';
			++failures;
		}
	}

	sol::protected_function_result RunOn(const sol::thread& thread, const sol::function& fn) {
		sol::coroutine co(thread.thread_state(), sol::ref_index(fn.registry_index()));
		return co();
	}
}

int main() {
	sol::state lua;
	lua.open_libraries(sol::lib::base, sol::lib::coroutine);
	lua.safe_script(R"(
		function failing_ability() local hook = nil; hook() end
		function working_ability() return 42 end
	)");
	const sol::function failing = lua["failing_ability"];
	const sol::function working = lua["working_ability"];

	// A thread whose coroutine finished normally can be reused.
	sol::thread healthy = sol::thread::create(lua);
	Check(RunOn(healthy, working).valid(), "working coroutine runs");
	Check(!Game::LuaThreadNeedsReset(lua_status(healthy.thread_state())), "finished thread is reusable");
	Check(RunOn(healthy, working).valid(), "finished thread runs the next coroutine");

	// A thread whose coroutine raised an error is left in an error state.
	sol::thread broken = sol::thread::create(lua);
	Check(!RunOn(broken, failing).valid(), "failing coroutine reports its error");
	const int status = lua_status(broken.thread_state());
	Check(status != 0 && status != LUA_YIELD, "errored thread is not in OK/YIELD state");
	Check(Game::LuaThreadNeedsReset(status), "errored thread is flagged for reset");

	// Reusing it fails exactly like the server log ("cannot resume ...").
	auto reused = RunOn(broken, working);
	Check(!reused.valid(), "errored thread cannot run another coroutine");
	if (!reused.valid()) {
		sol::error err = reused;
		Check(std::string(err.what()).find("cannot resume") != std::string::npos, "the failure is 'cannot resume ...'");
	}

	// A fresh thread (what ReturnThreadToPool now installs) runs fine.
	sol::thread fresh = sol::thread::create(lua);
	auto ok = RunOn(fresh, working);
	Check(ok.valid() && ok.get<int>() == 42, "replacement thread runs the next cast");

	Check(!Game::LuaThreadNeedsReset(0) && !Game::LuaThreadNeedsReset(LUA_YIELD), "OK and YIELD are reusable");
	Check(Game::LuaThreadNeedsReset(LUA_ERRRUN) && Game::LuaThreadNeedsReset(LUA_ERRMEM), "error codes need reset");

	if (failures != 0) {
		return 1;
	}
	std::cout << "lua_thread_recovery_test: all checks passed\n";
	return 0;
}
