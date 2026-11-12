#ifndef RECAP_GAME_CAST_QUEUE_H
#define RECAP_GAME_CAST_QUEUE_H

#include <cstdint>

// Outcome of Instance::UseAbility and what the server does with a refused cast.
namespace Game {
	enum class CastResult : uint8_t {
		Started,      // the ability script is running
		Busy,         // the hero's previous ability script is still running
		OnCooldown,
		Unavailable,  // no ability id, no script, or the script refused to start
	};

	namespace CastQueue {
		enum class Refusal : uint8_t { Reject, KeepAsPending };

		// Held basic attacks: the client re-sends the attack during the swing.
		// Keep the latest one and start it when the hero is free (darkspin,
		// build 103: latest held command repeated after the release), instead
		// of rejecting every other swing. Everything else is rejected at once.
		constexpr Refusal OnRefused(CastResult result, uint32_t abilityIndex) noexcept {
			return result == CastResult::Busy && abilityIndex == 0 ? Refusal::KeepAsPending : Refusal::Reject;
		}

		// A pending attack is retried this often until the hero is free, and
		// rejected after MaxRetries (a script that never finishes must not keep
		// the client waiting forever).
		inline constexpr uint32_t RetryIntervalMs = 50;
		inline constexpr uint32_t MaxRetries = 40;

		constexpr bool ShouldRetry(CastResult result, uint32_t attempt) noexcept {
			return result == CastResult::Busy && attempt < MaxRetries;
		}

		// Reason written to the server log for a refused cast.
		constexpr const char* Describe(CastResult result) noexcept {
			switch (result) {
				case CastResult::Started: return "started";
				case CastResult::Busy: return "busy";
				case CastResult::OnCooldown: return "on cooldown";
				case CastResult::Unavailable: return "unavailable";
			}
			return "unknown";
		}
	}
}

#endif
