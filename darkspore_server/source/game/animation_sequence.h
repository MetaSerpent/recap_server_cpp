#ifndef RECAP_GAME_ANIMATION_SEQUENCE_H
#define RECAP_GAME_ANIMATION_SEQUENCE_H

#include <cstdint>

// Darkspore abilities describe their attack animations as
//   ability.animationSequence = { selectionMethod = nAbilityAnimationSelection.Sequence,
//                                 { hit = 0.1, release = 0.7, animationstate = <id> }, ... }
// The engine provides nAbility.GetAnimationSequenceIndex() and
// nAbility.PlayAnimationSequence(). The game's own helpers
// (Global.lua: GetTimeToReleaseLua, GetTimeToHit) read
// animationSequence[index + 1], so the engine index is zero-based.
namespace Game::AnimationSequence {
	enum class Selection { Sequence, Random, Rank };

	// Returns the zero-based entry for this cast and advances the per-actor
	// cursor for the next one. Sequence cycles through the entries in order
	// (combo attacks); Random uses the supplied random number; Rank entries are
	// addressed by ability rank in the scripts, so the index is unused (0).
	constexpr uint32_t NextIndex(Selection selection, uint32_t entryCount, uint32_t& cursor, uint32_t random) noexcept {
		if (entryCount == 0) {
			return 0;
		}
		switch (selection) {
			case Selection::Sequence: {
				const uint32_t index = cursor % entryCount;
				cursor = (index + 1) % entryCount;
				return index;
			}
			case Selection::Random:
				return random % entryCount;
			case Selection::Rank:
			default:
				return 0;
		}
	}
}

#endif
