#ifndef RECAP_GAME_COMBAT_CALL_H
#define RECAP_GAME_COMBAT_CALL_H

#include <sol/sol.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>

// Argument parsing for nGameObject.TakeDamage / HealDamage in the order used by
// Darkspore's scripts (measured over all 1,029 build-103 scripts):
//   TakeDamage(snapshot, target, damage, damageType, damageSource,
//              coefficient, descriptors[, multiplier[, dirX, dirY, dirZ]])
//   HealDamage(snapshot, target, heal, coefficient[, descriptors[, critical[, canResurrect]]])
// snapshot is an attribute snapshot (the attacker), or kInvalidGUID/kObjIDNone
// for sourceless damage; damage/heal is a {min, max} table or a number.
// The previous bindings expected (object, attacker, snapshot, ...): the snapshot
// was taken as the target and a number was used as a table, which is undefined
// behaviour in LuaJIT and crashed the server on the first melee hit.
// Every argument is type-checked here; nothing is read through a non-table.
namespace Game::CombatCall {
	struct Range {
		float min = 0.f;
		float max = 0.f;
	};

	struct Damage {
		sol::object snapshot;
		sol::object target;
		Range range;
		uint32_t damageType = 0;
		uint32_t damageSource = 0;
		float coefficient = 0.f;
		uint32_t descriptors = 0;
		std::optional<float> multiplier;
		std::optional<std::array<float, 3>> direction;
	};

	struct Heal {
		sol::object snapshot;
		sol::object target;
		Range range;
		float coefficient = 0.f;
		std::optional<uint32_t> descriptors;
		std::optional<bool> critical;
		std::optional<bool> canResurrect;
	};

	namespace Detail {
		inline sol::object Arg(const sol::variadic_args& args, size_t index) {
			return index < args.size() ? sol::object(args[index]) : sol::make_object(args.lua_state(), sol::lua_nil);
		}

		inline bool IsAbsent(const sol::object& value) {
			return value.get_type() == sol::type::lua_nil || value.get_type() == sol::type::none;
		}

		inline std::optional<Range> ReadRange(const sol::object& value) {
			if (value.get_type() == sol::type::number) {
				const float amount = value.as<float>();
				return Range { amount, amount };
			}
			if (value.get_type() == sol::type::table) {
				const sol::table table = value.as<sol::table>();
				const sol::object low = table.raw_get<sol::object>(1);
				const sol::object high = table.raw_get<sol::object>(2);
				if (low.get_type() != sol::type::number) {
					return std::nullopt;
				}
				const float min = low.as<float>();
				const float max = high.get_type() == sol::type::number ? high.as<float>() : min;
				return Range { min, max };
			}
			return std::nullopt;
		}

		// Optional numeric argument: absent -> fallback, wrong type -> error.
		template<typename T>
		inline bool ReadNumber(const sol::variadic_args& args, size_t index, T& out, const char* name, std::string& error) {
			const sol::object value = Arg(args, index);
			if (IsAbsent(value)) {
				return true;
			}
			if (value.get_type() != sol::type::number) {
				error = std::string(name) + " must be a number";
				return false;
			}
			out = value.as<T>();
			return true;
		}

		inline bool ReadTarget(const sol::object& target, std::string& error) {
			const auto type = target.get_type();
			if (type != sol::type::number && type != sol::type::userdata) {
				error = "target must be an object id or object";
				return false;
			}
			return true;
		}
	}

	inline std::optional<Damage> ParseTakeDamage(const sol::variadic_args& args, std::string& error) {
		using namespace Detail;
		Damage call;
		call.snapshot = Arg(args, 0);
		call.target = Arg(args, 1);
		if (!ReadTarget(call.target, error)) {
			return std::nullopt;
		}
		const auto range = ReadRange(Arg(args, 2));
		if (!range) {
			error = "damage must be a number or {min, max} table";
			return std::nullopt;
		}
		call.range = *range;
		if (!ReadNumber(args, 3, call.damageType, "damageType", error) ||
		    !ReadNumber(args, 4, call.damageSource, "damageSource", error) ||
		    !ReadNumber(args, 5, call.coefficient, "coefficient", error) ||
		    !ReadNumber(args, 6, call.descriptors, "descriptors", error)) {
			return std::nullopt;
		}
		float multiplier = 1.f;
		if (!IsAbsent(Arg(args, 7))) {
			if (!ReadNumber(args, 7, multiplier, "damageMultiplier", error)) {
				return std::nullopt;
			}
			call.multiplier = multiplier;
		}
		if (!IsAbsent(Arg(args, 8))) {
			std::array<float, 3> direction {};
			if (!ReadNumber(args, 8, direction[0], "dirX", error) ||
			    !ReadNumber(args, 9, direction[1], "dirY", error) ||
			    !ReadNumber(args, 10, direction[2], "dirZ", error)) {
				return std::nullopt;
			}
			call.direction = direction;
		}
		return call;
	}

	inline std::optional<Heal> ParseHealDamage(const sol::variadic_args& args, std::string& error) {
		using namespace Detail;
		Heal call;
		call.snapshot = Arg(args, 0);
		call.target = Arg(args, 1);
		if (!ReadTarget(call.target, error)) {
			return std::nullopt;
		}
		const auto range = ReadRange(Arg(args, 2));
		if (!range) {
			error = "heal must be a number or {min, max} table";
			return std::nullopt;
		}
		call.range = *range;
		if (!ReadNumber(args, 3, call.coefficient, "coefficient", error)) {
			return std::nullopt;
		}
		if (!IsAbsent(Arg(args, 4))) {
			uint32_t descriptors = 0;
			if (!ReadNumber(args, 4, descriptors, "descriptors", error)) {
				return std::nullopt;
			}
			call.descriptors = descriptors;
		}
		for (size_t index : { size_t { 5 }, size_t { 6 } }) {
			const sol::object flag = Arg(args, index);
			if (IsAbsent(flag)) {
				continue;
			}
			if (flag.get_type() != sol::type::boolean) {
				error = index == 5 ? "critical must be a boolean" : "canResurrect must be a boolean";
				return std::nullopt;
			}
			(index == 5 ? call.critical : call.canResurrect) = flag.as<bool>();
		}
		return call;
	}
}

#endif
