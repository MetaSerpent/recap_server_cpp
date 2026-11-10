// Runs Game::AbilityRegistration::Resolve inside a real LuaJIT + sol2 state,
// with the call forms used by Darkspore scripts and by the upstream server.
// Build: see the "Register game abilities" step in gameplay-guardrails.yml.
#include "../darkspore_server/source/game/ability_registration.h"

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
}

int main() {
	sol::state lua;
	lua.open_libraries(sol::lib::base);

	std::vector<std::string> registered;
	int rejected = 0;
	auto bind = [&](sol::variadic_args args) {
		auto request = Game::AbilityRegistration::Resolve(args);
		if (!request) {
			++rejected;
			return;
		}
		request->table["registeredAs"] = request->name;
		registered.push_back(request->name);
	};
	lua.create_named_table("nAbility")["RegisterAbility"] = bind;
	lua.create_named_table("Ability")["Register"] = bind;

	// The form used by every Darkspore ability script.
	auto game = lua.safe_script(R"(
		nAbility_Test = { cooldown = { 30, 30, 30 } }
		nAbility.RegisterAbility("SplinteringCleave", nAbility_Test)
		return nAbility_Test.registeredAs
	)", sol::script_pass_on_error);
	Check(game.valid(), "RegisterAbility(name, table) does not raise a Lua error");
	Check(game.valid() && game.get<std::string>() == "SplinteringCleave", "the name argument names the ability");

	// The upstream server form.
	auto upstream = lua.safe_script(R"(Ability.Register({ name = "UpstreamAbility" }))", sol::script_pass_on_error);
	Check(upstream.valid(), "Ability.Register(table) still works");
	auto unnamed = lua.safe_script(R"(Ability.Register({}))", sol::script_pass_on_error);
	Check(unnamed.valid(), "a table without a name is accepted");

	// Invalid calls are rejected without a Lua error.
	auto invalid = lua.safe_script(R"(nAbility.RegisterAbility(42) nAbility.RegisterAbility("OnlyName"))", sol::script_pass_on_error);
	Check(invalid.valid() && rejected == 2, "calls without an ability table are rejected");

	Check(registered == std::vector<std::string> { "SplinteringCleave", "UpstreamAbility", "unnamed_ability" },
	      "registered names in call order");

	// The previous binding took the table first: the game form failed with
	// "attempt to index a string value". Keep that regression visible.
	sol::state old;
	old.open_libraries(sol::lib::base);
	old.create_named_table("nAbility")["RegisterAbility"] = [](sol::table table) { (void)table["name"]; };
	auto oldResult = old.safe_script(R"(nAbility.RegisterAbility("SplinteringCleave", {}))", sol::script_pass_on_error);
	Check(!oldResult.valid(), "the old table-first binding rejects the game form (documents the bug)");

	if (failures != 0) {
		return 1;
	}
	std::cout << "ability_registration_test: all checks passed\n";
	return 0;
}
