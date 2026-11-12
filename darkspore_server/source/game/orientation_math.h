#ifndef RECAP_GAME_ORIENTATION_MATH_H
#define RECAP_GAME_ORIENTATION_MATH_H

#include <array>
#include <cmath>

// nMathUtil.TransformVector(x, y, z, nGameObject.GetOrientation(object)):
// game scripts (projectile/toss templates, missile tempest, twin laser, ...)
// rotate a local offset (e.g. GetShotOffset) into world space with the
// object's orientation, passed as the four numbers GetOrientation returns
// (x, y, z, w), and read three numbers back.
namespace Game::OrientationMath {
	using Vec3 = std::array<float, 3>;

	// v' = v + 2w(u x v) + 2(u x (u x v)), u = (x, y, z), for a unit quaternion.
	// A zero quaternion leaves the vector unchanged; others are normalised.
	constexpr Vec3 Rotate(const Vec3& v, float x, float y, float z, float w) noexcept {
		const float lengthSquared = x * x + y * y + z * z + w * w;
		if (!(lengthSquared > 1e-12f)) {
			return v;
		}
		if (lengthSquared < 0.9999f || lengthSquared > 1.0001f) {
			const float length = std::sqrt(lengthSquared);
			x /= length; y /= length; z /= length; w /= length;
		}
		const Vec3 t {
			2.f * (y * v[2] - z * v[1]),
			2.f * (z * v[0] - x * v[2]),
			2.f * (x * v[1] - y * v[0]),
		};
		return {
			v[0] + w * t[0] + (y * t[2] - z * t[1]),
			v[1] + w * t[1] + (z * t[0] - x * t[2]),
			v[2] + w * t[2] + (x * t[1] - y * t[0]),
		};
	}
}

#endif
