#ifndef RECAP_GAME_THREAD_TIMING_H
#define RECAP_GAME_THREAD_TIMING_H

#include <cstdint>

// nThread.WaitForXSeconds(s) waits s seconds from now. nThread.WaitUntilTime(t)
// waits until t seconds after the script started: ability templates call it
// with each hit time from GetTimeToHit and then the release time, all measured
// from the start of the cast (182 calls in 109 game scripts). Treating it as
// "wait t more seconds" added the waits up: a three-hit swing with hits at
// 0.5/1.0/1.5 s hit at 0.5/1.5/3.0 s and kept the hero busy for seconds after
// the client's Released.
namespace Game::ThreadTiming {
	enum class Wait : uint8_t { ForSeconds, UntilTime };

	// Time (ms) at which the waiting script resumes; a time already passed
	// resumes on the next update.
	constexpr uint64_t ResumeAt(Wait wait, uint64_t nowMs, uint64_t startMs, double seconds) noexcept {
		if (!(seconds > 0)) {
			return nowMs;
		}
		const auto offset = static_cast<uint64_t>(seconds * 1000.0);
		return wait == Wait::UntilTime ? startMs + offset : nowMs + offset;
	}
}

#endif
