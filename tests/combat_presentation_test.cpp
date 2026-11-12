// Standalone tests for the animation sequence cursor and the attribute
// modifier handle registry used by the Lua bindings.
#include "../darkspore_server/source/game/animation_sequence.h"
#include "../darkspore_server/source/game/attribute_modifier_registry.h"
#include "../darkspore_server/source/game/hero_stats.h"
#include "../darkspore_server/source/game/orientation_math.h"
#include "../darkspore_server/source/game/thread_timing.h"

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

	// nMathUtil.TransformVector(x, y, z, qx, qy, qz, qw): rotation by the
	// orientation GetOrientation returns (x, y, z, w).
	using Game::OrientationMath::Rotate;
	using Game::OrientationMath::Vec3;
	constexpr bool Near(const Vec3& a, const Vec3& b) {
		for (int i = 0; i < 3; ++i) {
			const float d = a[i] - b[i];
			if (d > 1e-5f || d < -1e-5f) { return false; }
		}
		return true;
	}
	constexpr float kHalfSqrt2 = 0.70710678f;
	static_assert(Near(Rotate({ 1.f, 2.f, 3.f }, 0.f, 0.f, 0.f, 1.f), { 1.f, 2.f, 3.f }));             // identity
	static_assert(Near(Rotate({ 1.f, 0.f, 0.f }, 0.f, 0.f, kHalfSqrt2, kHalfSqrt2), { 0.f, 1.f, 0.f }));  // 90 deg about z
	static_assert(Near(Rotate({ 1.f, 0.f, 0.f }, 0.f, 1.f, 0.f, 0.f), { -1.f, 0.f, 0.f }));             // 180 deg about y
	static_assert(Near(Rotate({ 0.f, 1.f, 0.f }, kHalfSqrt2, 0.f, 0.f, kHalfSqrt2), { 0.f, 0.f, 1.f }));  // 90 deg about x
	static_assert(Near(Rotate({ 4.f, 5.f, 6.f }, 0.f, 0.f, 0.f, 0.f), { 4.f, 5.f, 6.f }));             // zero quaternion

	// Melee template: hits at 0.5/1.0/1.5 s and release at 1.8 s, all counted
	// from the start of the cast, must not add up.
	using Game::ThreadTiming::ResumeAt;
	using Game::ThreadTiming::Wait;
	constexpr uint64_t kStart = 10'000;
	static_assert(ResumeAt(Wait::UntilTime, kStart, kStart, 0.5) == 10'500);
	static_assert(ResumeAt(Wait::UntilTime, 10'500, kStart, 1.0) == 11'000);   // not 11'500
	static_assert(ResumeAt(Wait::UntilTime, 11'000, kStart, 1.5) == 11'500);   // not 13'000
	static_assert(ResumeAt(Wait::UntilTime, 11'500, kStart, 1.8) == 11'800);   // released at 1.8 s
	static_assert(ResumeAt(Wait::UntilTime, 12'000, kStart, 1.0) == 11'000);   // already passed: next update
	static_assert(ResumeAt(Wait::ForSeconds, 10'500, kStart, 1.0) == 11'500);  // WaitForXSeconds stays relative
	static_assert(ResumeAt(Wait::ForSeconds, 10'500, kStart, 0.0) == 10'500 && ResumeAt(Wait::UntilTime, 10'500, kStart, -1.0) == 10'500);
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

	// A non-unit orientation is normalised first (same result as the unit one).
	assert(Near(Rotate({ 1.f, 0.f, 0.f }, 0.f, 0.f, 2.f * kHalfSqrt2, 2.f * kHalfSqrt2), { 0.f, 1.f, 0.f }));

	std::cout << "combat_presentation_test: all checks passed\n";
	return 0;
}
