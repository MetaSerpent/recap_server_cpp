// Byte-exact tests for the build-103 ActionCommandResponse payload, the
// team validation rules and the held basic attack policy (all pure, no game data).
#include "../darkspore_server/source/raknet/action_response.h"
#include "../darkspore_server/source/game/team_validation.h"
#include "../darkspore_server/source/game/cast_queue.h"

#include <cassert>
#include <cstdint>
#include <iostream>

using namespace RakNet::ActionResponse;

namespace {
	constexpr uint64_t U64(const std::array<uint8_t, PayloadSize>& p, std::size_t o) {
		uint64_t v = 0;
		for (std::size_t i = 0; i < 8; ++i) { v |= static_cast<uint64_t>(p[o + i]) << (8 * i); }
		return v;
	}
	constexpr uint32_t U32(const std::array<uint8_t, PayloadSize>& p, std::size_t o) {
		uint32_t v = 0;
		for (std::size_t i = 0; i < 4; ++i) { v |= static_cast<uint32_t>(p[o + i]) << (8 * i); }
		return v;
	}

	// Rejected: sync stamp echoed, type 2, commanded object id, user data all ones.
	constexpr auto rejected = Encode(Reject(0x5A, 0x00000020));
	static_assert(rejected[0] == 0x5A && rejected[1] == 2 && rejected[2] == 0 && rejected[3] == 0);
	static_assert(U32(rejected, 4) == 0x20 && U32(rejected, 8) == 0);
	static_assert(U64(rejected, 16) == 0 && U64(rejected, 24) == 0 && U64(rejected, 32) == 0 && U64(rejected, 40) == 0);
	static_assert(U32(rejected, 48) == 0 && U32(rejected, 52) == 0xFFFFFFFFu);

	// Accepted basic attack: hit after 100 ms, release after 700 ms.
	constexpr Message AcceptedBasic() {
		Message m;
		const bool ok = Timed(m, Type::Accepted, 0x07, 0xD4BF7C0Au, 0, 1'000'000, 100, 700);
		return ok ? m : Message {};
	}
	constexpr auto accepted = Encode(AcceptedBasic());
	static_assert(accepted[0] == 0x07 && accepted[1] == 1);
	static_assert(accepted[4] == 0x0A && accepted[5] == 0x7C && accepted[6] == 0xBF && accepted[7] == 0xD4);   // little endian
	static_assert(U32(accepted, 8) == 0 && U32(accepted, 12) == 0);
	static_assert(U64(accepted, 16) == 1'000'000 && U64(accepted, 24) == 1'000'100 && U64(accepted, 32) == 1'000'700);
	static_assert(U64(accepted, 40) == 0 && U32(accepted, 52) == 0xFFFFFFFFu);

	// Released keeps the same times and carries the ability index.
	constexpr Message ReleasedSpecial() {
		Message m;
		Timed(m, Type::Released, 0x07, 0xD4BF7C0Au, 2, 5'000, 250, 250);
		return m;
	}
	static_assert(Encode(ReleasedSpecial())[1] == 4 && U32(Encode(ReleasedSpecial()), 8) == 2);
	static_assert(U64(Encode(ReleasedSpecial()), 24) == 5'250 && U64(Encode(ReleasedSpecial()), 32) == 5'250);

	// Invalid timing never produces a packet (the upstream packet sent zeros).
	constexpr bool Invalid(uint32_t ability, uint32_t hit, uint32_t release) {
		Message m;
		return !Timed(m, Type::Accepted, 1, ability, 0, 10, hit, release);
	}
	static_assert(Invalid(0, 100, 700) && Invalid(5, 100, 0) && Invalid(5, 800, 700));

	using Game::TeamValidation::IsFriendly;
	using Game::TeamValidation::IsHostile;
	static_assert(IsHostile(1, 2) && !IsHostile(1, 1) && IsHostile(0, 1) && IsHostile(1, 0));
	static_assert(IsFriendly(1, 1) && !IsFriendly(1, 2) && IsFriendly(0, 2));

	// Held basic attack (index 0) arriving mid-swing is kept and started on
	// release; every other refusal is answered with Rejected at once.
	using Game::CastResult;
	using namespace Game::CastQueue;
	static_assert(OnRefused(CastResult::Busy, 0) == Refusal::KeepAsPending);
	static_assert(OnRefused(CastResult::Busy, 1) == Refusal::Reject);         // specials are not queued
	static_assert(OnRefused(CastResult::Busy, 5) == Refusal::Reject);
	static_assert(OnRefused(CastResult::OnCooldown, 0) == Refusal::Reject);
	static_assert(OnRefused(CastResult::Unavailable, 0) == Refusal::Reject);
	// Retries are bounded: a script that never ends cannot keep the client waiting.
	static_assert(ShouldRetry(CastResult::Busy, 0) && ShouldRetry(CastResult::Busy, MaxRetries - 1));
	static_assert(!ShouldRetry(CastResult::Busy, MaxRetries));
	static_assert(!ShouldRetry(CastResult::OnCooldown, 0) && !ShouldRetry(CastResult::Unavailable, 0));
	static_assert(RetryIntervalMs > 0 && RetryIntervalMs * MaxRetries <= 5'000);
}

int main() {
	assert(PayloadSize == 56);
	std::cout << "action_response_test: all checks passed\n";
	return 0;
}
