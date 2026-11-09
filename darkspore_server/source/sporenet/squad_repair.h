#ifndef RECAP_SPORENET_SQUAD_REPAIR_H
#define RECAP_SPORENET_SQUAD_REPAIR_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

// Squads store the ids of creatures the account owns. Accounts registered by
// earlier server builds instead stored a creature *template* (noun) id in the
// first slot of each squad, leaving the others empty; those squads resolve to
// no creature, so the client shows "undefined" heroes and crashes when the
// mission HUD loads them. Independent of game data so it can be unit-tested.
namespace SporeNet::SquadRepair {
	using SquadIds = std::array<uint32_t, 3>;

	struct OwnedCreature {
		uint32_t id = 0;
		uint32_t noun = 0;
	};

	// Squad k (0-based) of a new account: owned creatures 3k, 3k+1, 3k+2 in
	// unlock order; missing ones stay empty (0).
	inline SquadIds DefaultSquad(std::size_t squadIndex, std::span<const uint32_t> ownedIdsInOrder) {
		SquadIds ids {};
		for (std::size_t i = 0; i < ids.size(); ++i) {
			const std::size_t source = squadIndex * ids.size() + i;
			if (source < ownedIdsInOrder.size()) {
				ids[i] = ownedIdsInOrder[source];
			}
		}
		return ids;
	}

	inline bool IsOwned(uint32_t id, std::span<const OwnedCreature> owned) {
		return std::any_of(owned.begin(), owned.end(), [id](const auto& c) { return c.id == id; });
	}

	// True for the exact layout written by the old registration code: a template
	// noun that is not an owned creature id in slot 0, both other slots empty.
	inline bool IsLegacyRegistrationSquad(const SquadIds& ids, std::span<const OwnedCreature> owned) {
		if (ids[0] == 0 || ids[1] != 0 || ids[2] != 0 || IsOwned(ids[0], owned)) {
			return false;
		}
		return std::any_of(owned.begin(), owned.end(), [&](const auto& c) { return c.noun == ids[0]; });
	}

	// Repairs squad squadIndex in place. Legacy registration squads become the
	// default 3-creature squad; any other entry that is a template noun instead
	// of an owned id is mapped to an owned creature of that template not already
	// in the squad. Valid ids and empty slots are kept. Returns true if changed.
	inline bool Repair(SquadIds& ids, std::size_t squadIndex, std::span<const OwnedCreature> owned,
	                   std::span<const uint32_t> ownedIdsInOrder) {
		if (IsLegacyRegistrationSquad(ids, owned)) {
			ids = DefaultSquad(squadIndex, ownedIdsInOrder);
			return true;
		}
		bool changed = false;
		for (auto& id : ids) {
			if (id == 0 || IsOwned(id, owned)) {
				continue;
			}
			for (const auto& creature : owned) {
				if (creature.noun == id && std::find(ids.begin(), ids.end(), creature.id) == ids.end()) {
					id = creature.id;
					changed = true;
					break;
				}
			}
		}
		return changed;
	}
}

#endif
