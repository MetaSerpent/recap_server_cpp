#ifndef RECAP_GAME_LUA_TYPE_FILTER_H
#define RECAP_GAME_LUA_TYPE_FILTER_H

#include <sol/sol.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

// Object-type filter argument of nObjectManager.GetObjectsInRadius and its
// sorted variants. Darkspore scripts pass it in three ways (measured over the
// build-103 scripts): omitted, a single type (kType_Creature, 60 calls), or a
// table of types (nSporeLabs.damageableObjectTypes, 48 calls). An empty result
// means "no filter". Every value is type-checked; non-numbers are ignored.
namespace Game::LuaTypeFilter {
	inline std::vector<uint32_t> Read(const sol::object& filter) {
		std::vector<uint32_t> types;
		if (filter.get_type() == sol::type::number) {
			types.push_back(filter.as<uint32_t>());
		} else if (filter.get_type() == sol::type::table) {
			const sol::table table = filter.as<sol::table>();
			for (const auto& entry : table) {
				if (entry.second.get_type() == sol::type::number) {
					types.push_back(entry.second.as<uint32_t>());
				}
			}
		}
		return types;
	}

	inline std::vector<uint32_t> Read(const sol::variadic_args& args, std::size_t index) {
		if (index >= args.size()) {
			return {};
		}
		return Read(sol::object(args[index]));
	}
}

#endif
