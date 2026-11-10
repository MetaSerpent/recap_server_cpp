// Game::CombatCall in a real sol2 3.3.1 + LuaJIT state, using the argument
// orders of Darkspore's TakeDamage / HealDamage calls.
// Build: see the "Register game abilities" step in gameplay-guardrails.yml.
#include "../darkspore_server/source/game/combat_call.h"

#include <iostream>
#include <memory>
#include <string>

namespace {
	int failures = 0;
	void Check(bool condition, const std::string& what) {
		if (!condition) {
			std::cerr << "FAIL: " << what << '\n';
			++failures;
		}
	}

	struct Attributes { float attackSpeed = 0.f; };
	using AttributesPtr = std::shared_ptr<Attributes>;
}

int main() {
	sol::state lua;
	lua.open_libraries(sol::lib::base);
	lua.new_usertype<Attributes>("Attributes", sol::no_constructor);
	lua["GetAgentAttributeSnapshot"] = [] { return std::make_shared<Attributes>(); };
	lua["kInvalidGUID"] = 0;

	std::optional<Game::CombatCall::Damage> lastDamage;
	std::optional<Game::CombatCall::Heal> lastHeal;
	std::string lastError;
	lua["TakeDamage"] = [&](sol::variadic_args args) {
		lastError.clear();
		lastDamage = Game::CombatCall::ParseTakeDamage(args, lastError);
	};
	lua["HealDamage"] = [&](sol::variadic_args args) {
		lastError.clear();
		lastHeal = Game::CombatCall::ParseHealDamage(args, lastError);
	};

	// template_ability_cone form (7 arguments).
	lua.safe_script("TakeDamage(GetAgentAttributeSnapshot(), 32, {20, 30}, 2, 1, 0.5, 64)");
	Check(lastDamage.has_value(), "7-argument game call parses");
	if (lastDamage) {
		Check(lastDamage->snapshot.is<AttributesPtr>(), "first argument is kept as the attribute snapshot");
		Check(lastDamage->target.as<uint32_t>() == 32, "second argument is the target");
		Check(lastDamage->range.min == 20.f && lastDamage->range.max == 30.f, "damage {min, max}");
		Check(lastDamage->damageType == 2 && lastDamage->damageSource == 1, "damage type and source");
		Check(lastDamage->coefficient == 0.5f && lastDamage->descriptors == 64, "coefficient and descriptors");
		Check(!lastDamage->multiplier && !lastDamage->direction, "no multiplier or direction");
	}

	// template_ability_melee form (11 arguments: multiplier and direction).
	lua.safe_script("TakeDamage(GetAgentAttributeSnapshot(), 32, {6, 12}, 3, 0, 1, 8, 1.5, 0, 1, 0)");
	Check(lastDamage && lastDamage->multiplier == 1.5f, "11-argument call reads the multiplier");
	Check(lastDamage && lastDamage->direction && (*lastDamage->direction)[1] == 1.f, "11-argument call reads the direction");

	// Sourceless damage and a plain number amount.
	lua.safe_script("TakeDamage(kInvalidGUID, 7, 15, 1, 2, 0, 0)");
	Check(lastDamage && !lastDamage->snapshot.is<AttributesPtr>() && lastDamage->range.min == 15.f, "kInvalidGUID snapshot and number damage");

	// The previous binding order put the snapshot first and a number where a
	// table was read: such calls must be rejected, never misread.
	// A userdata target is passed through untouched; resolving it to an object
	// (LuaGetObject: is<ObjectPtr>()) rejects non-object userdata such as a snapshot.
	lua.safe_script("TakeDamage(GetAgentAttributeSnapshot(), GetAgentAttributeSnapshot(), {1, 2}, 1, 1, 0, 0)");
	Check(lastDamage && lastDamage->target.get_type() == sol::type::userdata && !lastDamage->target.is<uint32_t>(),
	      "a userdata target is not coerced into an object id");
	lua.safe_script("TakeDamage(GetAgentAttributeSnapshot(), nil, {1, 2}, 1, 1, 0, 0)");
	Check(!lastDamage && !lastError.empty(), "a missing target is rejected");
	lua.safe_script("TakeDamage(GetAgentAttributeSnapshot(), 32, 'lots', 1, 1, 0, 0)");
	Check(!lastDamage, "non-numeric damage is rejected");
	lua.safe_script("TakeDamage(GetAgentAttributeSnapshot(), 32, {5, 6}, {}, 1, 0, 0)");
	Check(!lastDamage && lastError.find("damageType") != std::string::npos, "a table damage type is rejected");

	// HealDamage forms (4 and 7 arguments).
	lua.safe_script("HealDamage(GetAgentAttributeSnapshot(), 9, {10, 20}, 0.25)");
	Check(lastHeal && lastHeal->target.as<uint32_t>() == 9 && lastHeal->range.max == 20.f && lastHeal->coefficient == 0.25f, "4-argument heal");
	lua.safe_script("HealDamage(GetAgentAttributeSnapshot(), 9, 5, 0, 16, true, false)");
	Check(lastHeal && lastHeal->descriptors == 16u && lastHeal->critical == true && lastHeal->canResurrect == false, "7-argument heal");
	lua.safe_script("HealDamage(GetAgentAttributeSnapshot(), 9, 5, 0, 16, 'yes')");
	Check(!lastHeal, "non-boolean critical flag is rejected");

	if (failures != 0) {
		return 1;
	}
	std::cout << "combat_call_test: all checks passed\n";
	return 0;
}
