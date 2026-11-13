#ifndef RECAP_GAME_CAST_FACING_H
#define RECAP_GAME_CAST_FACING_H

#include <cmath>
#include <optional>

// Direction a hero faces when it starts a cast.
//
// Hit arcs (TargetUtils.MakeArc -> nGameObject.GetFacing) are built from the
// caster's facing. For player heroes the server only had the locomotion facing,
// which movement updates reset to zero, so GetFacing fell back to world +X and
// melee swings only connected with enemies on that side. The client turns the
// hero toward what it attacks; the server now does the same: toward the target
// object, else the targeted point, else the cursor (XY plane).
namespace Game::CastFacing {
	struct Vec2 {
		float x = 0.f;
		float y = 0.f;
	};

	inline std::optional<Vec2> Toward(const Vec2& from, const Vec2& to) noexcept {
		const float dx = to.x - from.x;
		const float dy = to.y - from.y;
		const float lengthSquared = dx * dx + dy * dy;
		if (!(lengthSquared > 1e-4f) || !std::isfinite(lengthSquared)) {
			return std::nullopt;
		}
		const float length = std::sqrt(lengthSquared);
		return Vec2 { dx / length, dy / length };
	}

	inline std::optional<Vec2> Aim(const Vec2& hero, const std::optional<Vec2>& targetObject, const Vec2& targetPoint, const Vec2& cursor) noexcept {
		if (targetObject) {
			if (auto facing = Toward(hero, *targetObject)) {
				return facing;
			}
		}
		if (auto facing = Toward(hero, targetPoint)) {
			return facing;
		}
		return Toward(hero, cursor);
	}
}

#endif
