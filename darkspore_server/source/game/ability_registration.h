#ifndef RECAP_GAME_ABILITY_REGISTRATION_H
#define RECAP_GAME_ABILITY_REGISTRATION_H

#include <sol/sol.hpp>

#include <optional>
#include <string>

// Darkspore's ability scripts register themselves with
//   nAbility.RegisterAbility("SplinteringCleave", nAbility_SplinteringCleave)
// (name first, then the ability table). The upstream server form is
//   Ability.Register(abilityTable)  -- name read from abilityTable.name
// Both must work: binding only the second form makes sol2 treat the name
// string as the table, so every game ability failed to register with
// "attempt to index a string value".
namespace Game::AbilityRegistration {
	struct Request {
		std::string name;
		sol::table table;
	};

	inline std::optional<Request> Resolve(const sol::object& first, const sol::object& second) {
		if (first.get_type() == sol::type::string && second.get_type() == sol::type::table) {
			return Request { first.as<std::string>(), second.as<sol::table>() };
		}
		if (first.get_type() == sol::type::table) {
			sol::table table = first.as<sol::table>();
			auto name = table["name"].get<sol::optional<std::string>>();
			if (!name) {
				name = table["mName"].get<sol::optional<std::string>>();
			}
			return Request { name.value_or("unnamed_ability"), table };
		}
		return std::nullopt;
	}

	// Binding entry point: sol2 rejects calls with fewer arguments than a
	// lambda declares, so take the arguments as variadic_args.
	inline std::optional<Request> Resolve(const sol::variadic_args& args) {
		const sol::object nil = sol::make_object(args.lua_state(), sol::lua_nil);
		const sol::object first = args.size() > 0 ? sol::object(args[0]) : nil;
		const sol::object second = args.size() > 1 ? sol::object(args[1]) : nil;
		return Resolve(first, second);
	}
}

#endif
