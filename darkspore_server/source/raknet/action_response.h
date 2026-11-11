#ifndef RECAP_RAKNET_ACTION_RESPONSE_H
#define RECAP_RAKNET_ACTION_RESPONSE_H

#include <array>
#include <cstddef>
#include <cstdint>

// ActionCommandResponse payload for Darkspore 5.3.0.103 (56 bytes after the
// packet id). Layout and field meaning follow the independent build-103
// implementation in darkspin (MIT), server/raknet/application.go
// (ActionCommandResponseMessage) and server/zone/action/raknet103/response.go:
//
//   [0]  sync stamp: byte 1 of the client's action command
//        (ActionCommandCommonData::unk[0]); the client drops stale responses
//   [1]  response type
//   [4]  accepted/released: ability id (hash); rejected: the commanded object id
//   [8]  ability index
//   [16] start, [24] commit (hit), [32] end (release): server clock, milliseconds
//   [40] global cooldown, milliseconds
//   [52] user data (0xFFFFFFFF)
//
// Upstream ReCap sent sync stamp 0, index 1, start 0 and echoed user data; that
// packet crashed the client and was disabled in 08d2633.
namespace RakNet::ActionResponse {
	enum class Type : uint8_t {
		Accepted = 1,
		Rejected = 2,
		Released = 4,
		Pursuit = 8,
		ClearUserData = 16,
	};

	inline constexpr std::size_t PayloadSize = 56;
	inline constexpr uint32_t NoUserData = 0xFFFFFFFFu;

	struct Message {
		uint8_t syncStamp = 0;
		Type type = Type::Rejected;
		uint32_t objectId = 0;
		uint32_t abilityIndex = 0;
		uint64_t startMs = 0;
		uint64_t commitMs = 0;
		uint64_t endMs = 0;
		uint64_t globalCooldownMs = 0;
		uint32_t userData = NoUserData;
	};

	constexpr void PutLE(std::array<uint8_t, PayloadSize>& out, std::size_t offset, uint64_t value, std::size_t bytes) noexcept {
		for (std::size_t i = 0; i < bytes; ++i) {
			out[offset + i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFFu);
		}
	}

	constexpr std::array<uint8_t, PayloadSize> Encode(const Message& message) noexcept {
		std::array<uint8_t, PayloadSize> out {};
		out[0] = message.syncStamp;
		out[1] = static_cast<uint8_t>(message.type);
		PutLE(out, 4, message.objectId, 4);
		PutLE(out, 8, message.abilityIndex, 4);
		PutLE(out, 16, message.startMs, 8);
		PutLE(out, 24, message.commitMs, 8);
		PutLE(out, 32, message.endMs, 8);
		PutLE(out, 40, message.globalCooldownMs, 8);
		PutLE(out, 52, message.userData, 4);
		return out;
	}

	// A refused cast: the client cancels its pending action.
	constexpr Message Reject(uint8_t syncStamp, uint32_t commandedObjectId) noexcept {
		Message message;
		message.syncStamp = syncStamp;
		message.type = Type::Rejected;
		message.objectId = commandedObjectId;
		return message;
	}

	// An accepted (or, with Type::Released, finished) cast. hit/release are the
	// delays from the ability's animationSequence entry; release must be positive
	// and not earlier than hit, otherwise no timed response can be built.
	constexpr bool Timed(Message& message, Type type, uint8_t syncStamp, uint32_t abilityId, uint32_t abilityIndex,
	                     uint64_t nowMs, uint32_t hitDelayMs, uint32_t releaseDelayMs) noexcept {
		if (abilityId == 0 || releaseDelayMs == 0 || hitDelayMs > releaseDelayMs) {
			return false;
		}
		message = Message {};
		message.syncStamp = syncStamp;
		message.type = type;
		message.objectId = abilityId;
		message.abilityIndex = abilityIndex;
		message.startMs = nowMs;
		message.commitMs = nowMs + hitDelayMs;
		message.endMs = nowMs + releaseDelayMs;
		return true;
	}
}

#endif
