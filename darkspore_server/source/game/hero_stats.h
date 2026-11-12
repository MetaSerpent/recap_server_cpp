#ifndef RECAP_GAME_HERO_STATS_H
#define RECAP_GAME_HERO_STATS_H

#include <utility>

// Hero stats taken from the installed game's PlayerClass data.
namespace Game::HeroStats {
	// Placeholder the server used for every hero before class data was read.
	inline constexpr std::pair<float, float> FallbackWeaponDamage { 1.f, 5.f };

	// weaponMinDamage / weaponMaxDamage of the hero's class (e.g. Arborus
	// 6-12, Zrin 9-14, Vex 4-8 in build 103; all 100 classes define them).
	constexpr std::pair<float, float> WeaponDamage(float classMin, float classMax) noexcept {
		if (classMax <= 0.f || classMin < 0.f || classMin > classMax) {
			return FallbackWeaponDamage;
		}
		return { classMin, classMax };
	}
}

#endif
