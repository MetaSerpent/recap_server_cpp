#ifndef RECAP_GAME_ENTRY_POSITION_H
#define RECAP_GAME_ENTRY_POSITION_H

#include <array>
#include <cstddef>
#include <string_view>

// Where a player's heroes enter a level (build 103, after darkspin's
// campaignEntryPosition): the level's CameraSpawnPoint markers by player slot;
// a slot without its own marker uses the first one, offset per slot, instead
// of the world origin (where heroes appeared before); zelems_1 uses a fixed
// position.
namespace Game::EntryPosition {
	using Vec3 = std::array<float, 3>;

	inline constexpr Vec3 Zelems1 { -123.8707f, -151.64705f, 10.037109f };

	constexpr Vec3 Offset(Vec3 position, std::size_t slot) noexcept {
		switch (slot) {
			case 1: position[0] += 3.f; break;
			case 2: position[1] += 3.f; break;
			case 3: position[0] -= 3.f; break;
			default: break;
		}
		return position;
	}

	constexpr bool EqualsNoCase(std::string_view a, std::string_view b) noexcept {
		if (a.size() != b.size()) {
			return false;
		}
		for (std::size_t i = 0; i < a.size(); ++i) {
			const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
			if (lower(a[i]) != lower(b[i])) {
				return false;
			}
		}
		return true;
	}

	// markers: the level's CameraSpawnPoint positions in marker set order.
	template<typename Markers>
	constexpr Vec3 Select(std::string_view levelName, const Markers& markers, std::size_t slot) noexcept {
		if (EqualsNoCase(levelName, "zelems_1")) {
			return Offset(Zelems1, slot);
		}
		if (slot < markers.size()) {
			return markers[slot];
		}
		if (!markers.empty()) {
			return Offset(markers[0], slot);
		}
		return Offset(Zelems1, slot);
	}
}

#endif
