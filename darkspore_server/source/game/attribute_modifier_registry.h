#ifndef RECAP_GAME_ATTRIBUTE_MODIFIER_REGISTRY_H
#define RECAP_GAME_ATTRIBUTE_MODIFIER_REGISTRY_H

#include <cstdint>
#include <optional>
#include <unordered_map>

// Darkspore scripts apply attribute changes with
//   handle = nAttribute.AddAttributeModifier(object, attributeType, value)
// and undo them with
//   nAttribute.RemoveAttributeModifier(handle)
// (79 call sites in build 103 pass only the handle). The handle identifies one
// applied change; removing it must subtract exactly what was added, once.
namespace Game {
	class AttributeModifierRegistry {
		public:
			struct Entry {
				uint32_t objectId = 0;
				uint8_t attribute = 0;
				float delta = 0.f;
			};

			// Never returns 0, which scripts use as "no modifier".
			uint32_t Add(uint32_t objectId, uint8_t attribute, float delta) {
				uint32_t handle = ++mLastHandle;
				if (handle == 0) {
					handle = ++mLastHandle;
				}
				mEntries[handle] = Entry { objectId, attribute, delta };
				return handle;
			}

			// Returns the change to undo, or nothing for unknown/already removed handles.
			std::optional<Entry> Remove(uint32_t handle) {
				const auto it = mEntries.find(handle);
				if (it == mEntries.end()) {
					return std::nullopt;
				}
				const Entry entry = it->second;
				mEntries.erase(it);
				return entry;
			}

			std::size_t Size() const { return mEntries.size(); }
			void Clear() { mEntries.clear(); }

		private:
			std::unordered_map<uint32_t, Entry> mEntries;
			uint32_t mLastHandle = 0;
	};
}

#endif
