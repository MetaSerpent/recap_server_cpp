// Standalone tests for the attribute numbering map (game/attribute_layout.h).
// The table below is synthetic: a few names at the indices build 103 documents
// around the point where its numbering leaves the server enum (index 74).
#include "../darkspore_server/source/game/attribute_layout.h"

#include <cassert>
#include <iostream>
#include <string_view>
#include <utility>

using namespace Game::AttributeLayout;

namespace {
	constexpr std::array<std::pair<std::string_view, int>, 11> kGameTable { {
		{ "Strength", 0 },
		{ "ImmuneToStunned", 73 },
		// 74 is unnamed in the game's scripts (ImmuneToShocked in the client)
		{ "ImmuneToSleep", 75 },
		{ "ImmuneToTaunted", 76 },
		{ "ImmuneToTerrified", 77 },
		{ "DistributeDamageAmongSquad", 100 },
		{ "MinWeaponDamage", 104 },
		{ "BodyScale", 113 },
		{ "DefenseBoostPhysicalDamage", 114 },
		{ "NotAServerAttribute", 50 },
		{ "Mind", 300 },                       // out of range: ignored
	} };

	constexpr Map kGame = Map::FromGameNames(kGameTable);
	constexpr Map kIdentity {};

	// Server names follow the enum (full order checked by the Python contract).
	static_assert(ServerIndex("Strength") == 0 && ServerIndex("ImmuneToStunned") == 73);
	static_assert(ServerIndex("ImmuneToSleep") == 74 && ServerIndex("BodyScale") == 110);
	static_assert(ServerIndex("ImmuneToShocked") == 111 && ServerIndex("DefenseBoostPhysicalDamage") == 114);
	static_assert(ServerIndex("NoSuchAttribute") == None);

	// Without a game table nothing changes: identity, reflected below 111.
	static_assert(kIdentity.IsIdentity());
	static_assert(kIdentity.ToServer(75) == 75 && kIdentity.ToGame(110) == 110);
	static_assert(kIdentity.IsReflected(110) && !kIdentity.IsReflected(111) && !kIdentity.IsReflected(113));

	// Script indices are translated by name.
	static_assert(!kGame.IsIdentity());
	static_assert(kGame.ToServer(0) == 0 && kGame.ToServer(73) == 73);
	static_assert(kGame.ToServer(75) == ServerIndex("ImmuneToSleep"));       // was read as ImmuneToTerrified
	static_assert(kGame.ToServer(76) == ServerIndex("ImmuneToTaunted"));     // server-only before
	static_assert(kGame.ToServer(113) == ServerIndex("BodyScale"));          // was dropped (>= ClientCount)
	static_assert(kGame.ToServer(104) == ServerIndex("MinWeaponDamage"));
	static_assert(kGame.ToServer(74) == None && kGame.ToServer(50) == None && kGame.ToServer(300) == None);

	// Reflection uses the game's index; 111/112 stay the weapon damage fields.
	static_assert(kGame.ToGame(ServerIndex("ImmuneToSleep")) == 75 && kGame.IsReflected(ServerIndex("ImmuneToSleep")));
	static_assert(kGame.ToGame(ServerIndex("DistributeDamageAmongSquad")) == 100);
	static_assert(kGame.IsReflected(ServerIndex("DistributeDamageAmongSquad")));
	static_assert(kGame.ToGame(ServerIndex("BodyScale")) == 113 && !kGame.IsReflected(ServerIndex("BodyScale")));
	static_assert(!kGame.IsReflected(ServerIndex("DefenseBoostPhysicalDamage")));
	static_assert(kGame.ToGame(ServerIndex("ImmuneToShocked")) == None && !kGame.IsReflected(ServerIndex("ImmuneToShocked")));
	// a server attribute the game does not name has no game index
	static_assert(kGame.ToGame(ServerIndex("Dexterity")) == None);
}

int main() {
	// The process-wide map starts as the identity and is set only once.
	assert(Active().IsIdentity());
	assert(SetActiveOnce(kGame));
	assert(!Active().IsIdentity() && Active().ToServer(75) == ServerIndex("ImmuneToSleep"));
	assert(!SetActiveOnce(kIdentity));
	assert(!Active().IsIdentity());

	std::cout << "attribute_layout_test: all checks passed\n";
	return 0;
}
