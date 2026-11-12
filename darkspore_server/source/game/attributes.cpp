
// Include
#include "attributes.h"
#include "attribute_layout.h"

#include "raknet/server.h"

#include <algorithm>

// Game
namespace Game {
	// Attributes
	float Attributes::GetValue(uint8_t idx) const {
		// test performance some time
		auto end = mData.end();
		auto it = std::find_if(mData.begin(), end, [idx](const auto& data) { return std::get<0>(data) == idx; });
		if (it != end) {
			return std::get<1>(*it);
		}

		return 0;
	}

	float Attributes::GetValue(AttributeType idx) const {
		return GetValue(static_cast<uint8_t>(idx));
	}

	void Attributes::SetValue(uint8_t idx, float value) {
		// Every server attribute is stored (server-only ones from ClientCount
		// were dropped); which ones reach the client is decided on write.
		if (idx >= static_cast<uint8_t>(AttributeType::Count)) {
			return;
		}

		auto end = mData.end();
		auto it = std::find_if(mData.begin(), end, [idx](const auto& data) { return std::get<0>(data) == idx; });
		if (it != end) {
			if (value == 0) {
				mData.erase(it);
				mErasedData.push_back(idx);
			} else {
				std::get<1>(*it) = value;
			}
		} else if (value != 0) {
			// Not always present: setting an absent attribute to 0 marks it
			// without erasing anything.
			if (const auto erased = std::find(mErasedData.begin(), mErasedData.end(), idx); erased != mErasedData.end()) {
				mErasedData.erase(erased);
			}

			mData.push_back({ idx, value });
			std::sort(mData.begin(), mData.end(), [](const auto& lhs, const auto& rhs) { return std::get<0>(lhs) < std::get<0>(rhs); });
		}
		mDataBits.set(idx);
	}

	void Attributes::SetValue(AttributeType idx, float value) {
		SetValue(static_cast<uint8_t>(idx), value);
	}

	std::tuple<float, float> Attributes::GetWeaponDamage() const {
		return std::make_tuple(mMinWeaponDamage, mMaxWeaponDamage);
	}

	void Attributes::SetWeaponDamage(float minDamage, float maxDamage) {
		mMinWeaponDamage = minDamage;
		mMaxWeaponDamage = maxDamage;
	}

	ObjectPtr Attributes::GetOwnerObject() const {
		return mOwnerObject.lock();
	}

	void Attributes::SetOwnerObject(const ObjectPtr& object) {
		mOwnerObject = object;
	}

	void Attributes::WriteTo(RakNet::BitStream& stream) const {
		using RakNet::bytes_to_bits;
		using RakNet::ReallocateStream;

		constexpr auto size = bytes_to_bits(0x634);

		auto writeOffset = ReallocateStream(stream, size);

		// One float per attribute from 0x26C, slot = the game's attribute index
		// (game/attribute_layout.h), contiguous like Character's layout (slot
		// 101 at 0x400). The old second loop ran while i < mData.size() (a
		// count of non-zero values), so slots from 99 were almost never written.
		const auto& layout = AttributeLayout::Active();
		stream.SetWriteOffset(writeOffset + bytes_to_bits(0x26C));
		for (uint8_t slot = 0; slot < AttributeLayout::ReflectedLimit; ++slot) {
			Write(stream, GetValue(layout.ToServer(slot)));
		}

		stream.SetWriteOffset(writeOffset + bytes_to_bits(0x43C));
		Write(stream, mMinWeaponDamage);
		Write(stream, mMaxWeaponDamage);

		stream.SetWriteOffset(writeOffset + size);
	}

	void Attributes::WriteReflection(RakNet::BitStream& stream) const {
		// Fields are the game's attribute indices (game/attribute_layout.h);
		// attributes the client has no field for are not sent.
		const auto& layout = AttributeLayout::Active();
		std::vector<std::tuple<uint8_t, float>> fields;
		for (const auto& [idx, value] : mData) {
			if (mDataBits.test(idx) && layout.IsReflected(idx)) {
				fields.emplace_back(layout.ToGame(idx), value);
			}
		}
		for (const auto& idx : mErasedData) {
			if (layout.IsReflected(idx)) {
				fields.emplace_back(layout.ToGame(idx), 0.f);
			}
		}
		std::sort(fields.begin(), fields.end(), [](const auto& lhs, const auto& rhs) { return std::get<0>(lhs) < std::get<0>(rhs); });

		RakNet::reflection_serializer<sClientVisibleAttributeCount> reflector(stream);
		reflector.begin();
		for (const auto& [field, value] : fields) {
			reflector.write(field, value);
		}
		reflector.write<111>(mMinWeaponDamage);
		reflector.write<112>(mMaxWeaponDamage);
		reflector.end();
	}

	void Attributes::ResetReflectionBits() {
		mDataBits.reset();
		mErasedData.clear();
	}
}
