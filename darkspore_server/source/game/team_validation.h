#ifndef RECAP_GAME_TEAM_VALIDATION_H
#define RECAP_GAME_TEAM_VALIDATION_H

#include <cstdint>

// nGameObject.ValidateHostileTarget(team, target) and
// nGameObject.ValidateFriendlyTarget(team, target): Darkspore scripts pass the
// caster's TEAM (from nGameObject.GetTeam) as the first argument in 208 of 224
// calls in build 103, not the caster object. Team 0 is treated as "no team".
namespace Game::TeamValidation {
	constexpr bool IsHostile(uint32_t casterTeam, uint32_t targetTeam) noexcept {
		return !(casterTeam != 0 && targetTeam != 0 && casterTeam == targetTeam);
	}

	constexpr bool IsFriendly(uint32_t casterTeam, uint32_t targetTeam) noexcept {
		if (casterTeam != 0 && targetTeam != 0) {
			return casterTeam == targetTeam;
		}
		return true;
	}
}

#endif
