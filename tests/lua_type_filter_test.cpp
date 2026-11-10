// Game::LuaTypeFilter and the sol2 Release-mode safety configuration.
// Built with the server's Release defines (-O2 -DNDEBUG and the SOL_* macros
// from darkspore_server/CMakeLists.txt): sol2 only checks argument types in
// Release when SOL_SAFE_FUNCTION_CALLS is set. See gameplay-guardrails.yml.
#include "../darkspore_server/source/game/lua_type_filter.h"

#include <iostream>
#include <string>
#include <vector>

namespace {
	int failures = 0;
	void Check(bool condition, const std::string& what) {
		if (!condition) {
			std::cerr << "FAIL: " << what << '\n';
			++failures;
		}
	}

	struct Vec3 { float x = 0, y = 0, z = 0; };
}

int main() {
	sol::state lua;
	lua.open_libraries(sol::lib::base);
	lua.new_usertype<Vec3>("Vec3", sol::constructors<Vec3()>());

	std::vector<uint32_t> filter;
	std::string form;
	// Same overload shape as nObjectManager.GetObjectsInRadius in the server:
	// upstream (position, radius, {types}) and the game's coordinate form.
	lua["GetObjectsInRadius"] = sol::overload(
		[&](Vec3, float, sol::table types) {
			form = "vector";
			filter = Game::LuaTypeFilter::Read(sol::object(types));
		},
		[&](float, float, float, float, sol::variadic_args rest) {
			form = "coords";
			filter = Game::LuaTypeFilter::Read(rest, 0);
		});
	lua["kType_Creature"] = 3;

	auto run = [&](const char* code) {
		form.clear();
		filter = { 0xDEADu };
		auto result = lua.safe_script(code, sol::script_pass_on_error);
		return result.valid();
	};

	// The call that failed in the client session ("no matching function call").
	Check(run("GetObjectsInRadius(1, 2, 3, 10, kType_Creature)"), "single type number is accepted");
	Check(form == "coords" && filter == std::vector<uint32_t> { 3 }, "single type becomes a one-element filter");
	Check(run("GetObjectsInRadius(1, 2, 3, 10, {3, 5, 9})") && filter == std::vector<uint32_t>({ 3, 5, 9 }), "table of types");
	Check(run("GetObjectsInRadius(1, 2, 3, 10)") && form == "coords" && filter.empty(), "omitted filter means no filter");
	Check(run("GetObjectsInRadius(1, 2, 3, 10, nil)") && filter.empty(), "nil filter means no filter");
	Check(run("GetObjectsInRadius(1, 2, 3, 10, {3, 'x', {}, 7})") && filter == std::vector<uint32_t>({ 3, 7 }), "non-numeric entries are ignored");
	Check(run("GetObjectsInRadius(1, 2, 3, 10, 'creature')") && filter.empty(), "a string filter is ignored, not misread");
	Check(run("GetObjectsInRadius(Vec3.new(), 10, {4})") && form == "vector" && filter == std::vector<uint32_t> { 4 }, "upstream vector form still resolves");

	// Release-mode safety: a bound function declared with sol::table must reject
	// a number instead of reading memory through it (the session-5 server crash).
	bool bodyRan = false;
	lua["TakesTable"] = [&](sol::table) { bodyRan = true; };
	auto bad = lua.safe_script("TakesTable(5)", sol::script_pass_on_error);
	Check(!bad.valid() && !bodyRan, "a number passed as sol::table is a Lua error, the body does not run");
	auto good = lua.safe_script("TakesTable({})", sol::script_pass_on_error);
	Check(good.valid() && bodyRan, "a real table is accepted");

	if (failures != 0) {
		return 1;
	}
	std::cout << "lua_type_filter_test: all checks passed\n";
	return 0;
}
