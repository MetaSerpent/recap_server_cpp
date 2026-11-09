// Standalone test for SporeNet::SquadRepair (squads must hold owned creature ids).
#include "../darkspore_server/source/sporenet/squad_repair.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using namespace SporeNet::SquadRepair;

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
	// Owned creatures 1..10 created from template nouns 1000+id (unlock order).
	std::vector<OwnedCreature> owned;
	std::vector<uint32_t> order;
	for (uint32_t id = 1; id <= 10; ++id) {
		owned.push_back({ id, 1000 + id });
		order.push_back(id);
	}

	Check(DefaultSquad(0, order) == SquadIds { 1, 2, 3 }, "first default squad uses creatures 1-3");
	Check(DefaultSquad(2, order) == SquadIds { 7, 8, 9 }, "third default squad uses creatures 7-9");
	Check(DefaultSquad(3, order) == SquadIds { 10, 0, 0 }, "missing creatures stay empty");

	// Layout written by the old registration code: template noun + two empty slots.
	SquadIds legacy { 1001, 0, 0 };
	Check(IsLegacyRegistrationSquad(legacy, owned), "legacy registration squad is detected");
	Check(Repair(legacy, 0, owned, order) && legacy == SquadIds { 1, 2, 3 }, "legacy squad becomes a full default squad");
	SquadIds legacy2 { 1002, 0, 0 };
	Check(Repair(legacy2, 1, owned, order) && legacy2 == SquadIds { 4, 5, 6 }, "repair uses the squad's own slot");

	// User-edited squads keep their choices; only template nouns are mapped.
	SquadIds edited { 5, 1007, 0 };
	Check(Repair(edited, 0, owned, order) && edited == SquadIds { 5, 7, 0 }, "a template noun maps to the owned creature");
	SquadIds valid { 2, 9, 4 };
	Check(!Repair(valid, 0, owned, order) && valid == SquadIds { 2, 9, 4 }, "valid squads are unchanged");
	SquadIds unknown { 424242, 0, 0 };
	Check(!Repair(unknown, 0, owned, order) && unknown == SquadIds { 424242, 0, 0 }, "unknown ids are left for diagnosis");
	SquadIds ownedFirst { 3, 0, 0 };
	Check(!IsLegacyRegistrationSquad(ownedFirst, owned), "an owned single-creature squad is not legacy");
	// Two templates of the same kind map to distinct creatures.
	owned.push_back({ 11, 1001 });
	SquadIds twins { 1001, 1001, 0 };
	Check(Repair(twins, 0, owned, order) && twins == SquadIds { 1, 11, 0 }, "duplicate templates map to distinct creatures");

	if (failures != 0) {
		return 1;
	}
	std::cout << "squad_repair_test: all checks passed\n";
	return 0;
}
