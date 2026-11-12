// Standalone tests for the animation sequence cursor and the attribute
// modifier handle registry used by the Lua bindings.
#include "../darkspore_server/source/game/animation_sequence.h"
#include "../darkspore_server/source/game/attribute_modifier_registry.h"
#include "../darkspore_server/source/game/hero_stats.h"

#include <cassert>
#include <cstdint>
#include <iostream>

using Game::AnimationSequence::NextIndex;
using Game::AnimationSequence::Selection;

namespace {
	// Three-hit combo: indices are zero-based and wrap (scripts read [index + 1]).
	constexpr bool ComboCycles() {
		uint32_t cursor = 0;
		return NextIndex(Selection::Sequence, 3, cursor, 0) == 0 && NextIndex(Selection::Sequence, 3, cursor, 0) == 1 &&
		       NextIndex(Selection::Sequence, 3, cursor, 0) == 2 && NextIndex(Selection::Sequence, 3, cursor, 0) == 0;
	}
	static_assert(ComboCycles());

	// A single-entry sequence must always yield 0 (a constant 1 indexed past the end).
	constexpr bool SingleEntry() {
		uint32_t cursor = 0;
		return NextIndex(Selection::Sequence, 1, cursor, 0) == 0 && NextIndex(Selection::Sequence, 1, cursor, 0) == 0;
	}
	static_assert(SingleEntry());

	constexpr bool OtherModes() {
		uint32_t cursor = 5;
		return NextIndex(Selection::Random, 4, cursor, 10) == 2 && cursor == 5 &&   // 10 % 4, cursor untouched
		       NextIndex(Selection::Rank, 4, cursor, 10) == 0 &&
		       NextIndex(Selection::Sequence, 0, cursor, 10) == 0;                   // no entries
	}
	static_assert(OtherModes());

	// A cursor left out of range by a shorter sequence is brought back in range.
	constexpr bool StaleCursor() {
		uint32_t cursor = 7;
		return NextIndex(Selection::Sequence, 2, cursor, 0) == 1 && cursor == 0;
	}
	static_assert(StaleCursor());
	// Hero weapon damage comes from the class data; bad data keeps the old fallback.
	static_assert(Game::HeroStats::WeaponDamage(6.f, 12.f) == std::pair<float, float> { 6.f, 12.f });
	static_assert(Game::HeroStats::WeaponDamage(9.f, 9.f) == std::pair<float, float> { 9.f, 9.f });
	static_assert(Game::HeroStats::WeaponDamage(0.f, 0.f) == Game::HeroStats::FallbackWeaponDamage);
	static_assert(Game::HeroStats::WeaponDamage(12.f, 6.f) == Game::HeroStats::FallbackWeaponDamage);
	static_assert(Game::HeroStats::WeaponDamage(-1.f, 6.f) == Game::HeroStats::FallbackWeaponDamage);
}

int main() {
	Game::AttributeModifierRegistry registry;

	const uint32_t first = registry.Add(32, 48, 0.25f);
	const uint32_t second = registry.Add(32, 48, 0.25f);
	assert(first != 0 && second != 0 && first != second);   // 0 means "no modifier" in scripts
	assert(registry.Size() == 2);

	const auto removed = registry.Remove(first);
	assert(removed && removed->objectId == 32 && removed->attribute == 48 && removed->delta == 0.25f);
	assert(!registry.Remove(first));       // removing twice must not subtract twice
	assert(!registry.Remove(0));           // nil/zero handle
	assert(!registry.Remove(999999));      // unknown handle
	assert(registry.Size() == 1);

	assert(registry.Remove(second));
	assert(registry.Size() == 0);

	std::cout << "combat_presentation_test: all checks passed\n";
	return 0;
}
