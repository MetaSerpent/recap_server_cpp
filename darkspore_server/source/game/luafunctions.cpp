
// Include
#include "lua.h"
#include "ability_registration.h"
#include "combat_call.h"
#include "attribute_layout.h"
#include "lua_type_filter.h"
#include "orientation_math.h"
#include "team_validation.h"

#include "instance.h"
#include "objectmanager.h"
#include "serverevent.h"

#include "utils/functions.h"
#include "utils/log.h"

#include <glm/gtx/norm.hpp>
#include <glm/gtx/compatibility.hpp>

#include <algorithm>
#include <iostream>
#include <string>
#include <unordered_set>
#include <filesystem>

// Helpers
namespace {
	// Reading fields through sol::object avoids ambiguous get_or<T> overloads
	// in sol2 3.3.1 and permits string or Lua-object table keys.
	template <typename T, typename Key>
	T LuaTableGetOr(const sol::table& table, const Key& key, T fallback) {
		sol::object entry = table[key];
		if (entry.valid() && entry != sol::nil && entry.is<T>()) {
			return entry.as<T>();
		}
		return fallback;
	}

	auto& LuaGetGame(sol::this_state L) {
		Game::Instance& game = sol::state_view(L)["Game"]["Instance"];
		return game;
	}

	auto LuaGetAbility(sol::this_state L) {
		Game::Ability* ability = sol::state_view(L)["_ABILITY"];
		return ability;
	}

	// Argument i of a variadic call, or nil when the script did not pass it.
	sol::object LuaArg(const sol::variadic_args& args, size_t index) {
		return index < args.size() ? sol::object(args[index]) : sol::make_object(args.lua_state(), sol::lua_nil);
	}

	// Scripts call a few engine functions in forms whose meaning is not known
	// yet. They are accepted (never a Lua error) and reported once per run.
	void LogUnsupportedCallForm(const char* function, size_t argumentCount) {
		static std::unordered_set<std::string> reported;
		if (reported.insert(std::string(function) + "/" + std::to_string(argumentCount)).second) {
			std::cout << "Lua: " << function << " called with " << argumentCount
			          << " argument(s); this form is not implemented, call ignored." << std::endl;
		}
	}

	// nThreadData slots are addressed by number in game scripts (SetGUID(0, id))
	// and by name in server scripts.
	std::string ThreadDataKey(const sol::object& key) {
		if (key.get_type() == sol::type::number) {
			return "#" + std::to_string(key.as<int64_t>());
		}
		if (key.get_type() == sol::type::string) {
			return key.as<std::string>();
		}
		return {};
	}

	auto LuaGetPlayer(const Game::Instance& instance, sol::object& value) {
		// Original decompiled Lua passes numeric player IDs as well as
		// object userdata. Never attempt an incompatible sol::as<T>.
		if (!value.valid() || value == sol::nil) {
			return Game::PlayerPtr{};
		}
		if (value.is<Game::PlayerPtr>()) {
			return value.as<Game::PlayerPtr>();
		}
		if (value.is<uint8_t>()) {
			return instance.GetPlayerByIndex(value.as<uint8_t>());
		}
		return Game::PlayerPtr{};
	}

	auto LuaGetObject(const Game::ObjectManager& objectManager, sol::object& value) {
		if (!value.valid() || value == sol::nil) {
			return Game::ObjectPtr{};
		}
		if (value.is<Game::ObjectPtr>()) {
			return value.as<Game::ObjectPtr>();
		}
		if (value.is<uint32_t>()) {
			return objectManager.Get(value.as<uint32_t>());
		}
		return Game::ObjectPtr{};
	}

	auto LuaGetTrigger(const Game::ObjectManager& objectManager, sol::object& value) {
		if (!value.valid() || value == sol::nil) {
			return Game::TriggerVolumePtr{};
		}
		if (value.is<Game::TriggerVolumePtr>()) {
			return value.as<Game::TriggerVolumePtr>();
		}
		if (value.is<uint32_t>()) {
			return objectManager.GetTrigger(value.as<uint32_t>());
		}
		return Game::TriggerVolumePtr{};
	}

	glm::vec3 LuaExtractVec3(const sol::object& value, const glm::vec3& fallback = glm::zero<glm::vec3>()) {
		if (!value.valid() || value == sol::nil) {
			return fallback;
		}
		if (value.is<glm::vec3>()) {
			return value.as<glm::vec3>();
		}
		if (value.is<sol::table>()) {
			sol::table tbl = value.as<sol::table>();
			float x = tbl.raw_get_or(1, tbl.get_or("x", fallback.x));
			float y = tbl.raw_get_or(2, tbl.get_or("y", fallback.y));
			float z = tbl.raw_get_or(3, tbl.get_or("z", fallback.z));
			return glm::vec3(x, y, z);
		}
		return fallback;
	}

	uint32_t LuaExtractAssetOrGuid(const sol::object& value, uint32_t fallback = 0) {
		if (!value.valid() || value == sol::nil) {
			return fallback;
		}
		if (value.is<uint32_t>()) {
			return value.as<uint32_t>();
		}
		if (value.is<double>()) {
			return static_cast<uint32_t>(value.as<double>());
		}
		if (value.is<std::string>()) {
			const std::string s = value.as<std::string>();
			if (s.starts_with("0x") || s.starts_with("0X")) {
				return static_cast<uint32_t>(std::strtoul(s.c_str(), nullptr, 16));
			}
			return utils::hash_id(s);
		}
		return fallback;
	}

	auto LuaGetServerEvent(const Game::ObjectManager& objectManager, const sol::table& value) {
		Game::ServerEvent serverEvent;
		serverEvent.mServerEventDef = LuaExtractAssetOrGuid(value["asset"], LuaTableGetOr<uint32_t>(value, "mServerEventDef", 0));
		serverEvent.mObjectId = LuaTableGetOr<uint32_t>(value, "objectId", LuaTableGetOr<uint32_t>(value, "mObjectId", 0));
		serverEvent.mSimpleSwarmEffectId = LuaExtractAssetOrGuid(value["simpleSwarmEffectId"], LuaTableGetOr<uint32_t>(value, "mSimpleSwarmEffectId", 0));

		if (sol::object eventData = value["position"]; eventData.valid() && eventData != sol::nil) {
			serverEvent.mPosition = LuaExtractVec3(eventData);
		} else if (sol::object mPos = value["mPosition"]; mPos.valid() && mPos != sol::nil) {
			serverEvent.mPosition = LuaExtractVec3(mPos);
		}
		if (sol::object eventData = value["facing"]; eventData.valid() && eventData != sol::nil) {
			serverEvent.mFacing = LuaExtractVec3(eventData);
		} else if (sol::object mFac = value["mFacing"]; mFac.valid() && mFac != sol::nil) {
			serverEvent.mFacing = LuaExtractVec3(mFac);
		}
		if (sol::object eventData = value["targetPoint"]; eventData.valid() && eventData != sol::nil) {
			serverEvent.mTargetPoint = LuaExtractVec3(eventData);
		} else if (sol::object mTgt = value["mTargetPoint"]; mTgt.valid() && mTgt != sol::nil) {
			serverEvent.mTargetPoint = LuaExtractVec3(mTgt);
		}

		const auto& object = objectManager.Get(serverEvent.mObjectId);
		if (object) {
			serverEvent.mEffectIndex = object->AddEffect(serverEvent.mServerEventDef);
			if (serverEvent.mEffectIndex != 0xFF) {
				serverEvent.mSecondaryObjectId = LuaTableGetOr<uint32_t>(value, "secondaryObjectId", LuaTableGetOr<uint32_t>(value, "mSecondaryObjectId", 0));
				serverEvent.mAttackerId = LuaTableGetOr<uint32_t>(value, "attackerId", LuaTableGetOr<uint32_t>(value, "mAttackerId", 0));

				serverEvent.mRemove = LuaTableGetOr<bool>(value, "remove", LuaTableGetOr<bool>(value, "mRemove", false));
				serverEvent.mHardStop = LuaTableGetOr<bool>(value, "hardStop", LuaTableGetOr<bool>(value, "mHardStop", false));
				serverEvent.mForceAttach = LuaTableGetOr<bool>(value, "forceAttach", LuaTableGetOr<bool>(value, "mForceAttach", false));
				serverEvent.mCritical = LuaTableGetOr<bool>(value, "critical", LuaTableGetOr<bool>(value, "mCritical", false));
			}
		} else {
			serverEvent.mObjectId = 0;
		}

		return serverEvent;
	}

	auto LuaGetProjectileParameters(const sol::table& value) {
		Game::ProjectileParameters parameters;

		if (sol::object eventData = value["direction"]; eventData.valid() && eventData != sol::nil) {
			parameters.mDirection = LuaExtractVec3(eventData);
		} else if (sol::object mDir = value["mDirection"]; mDir.valid() && mDir != sol::nil) {
			parameters.mDirection = LuaExtractVec3(mDir);
		}

		parameters.mJinkInfo = LuaTableGetOr<uint32_t>(value, "jinkInfo", LuaTableGetOr<uint32_t>(value, "mJinkInfo", 0));

		parameters.mSpeed = LuaTableGetOr<float>(value, "speed", LuaTableGetOr<float>(value, "mSpeed", 0.f));
		parameters.mAcceleration = LuaTableGetOr<float>(value, "acceleration", LuaTableGetOr<float>(value, "mAcceleration", 0.f));
		parameters.mRange = LuaTableGetOr<float>(value, "range", LuaTableGetOr<float>(value, "mRange", 0.f));
		parameters.mSpinRate = LuaTableGetOr<float>(value, "spinRate", LuaTableGetOr<float>(value, "mSpinRate", 0.f));
		parameters.mHomingDelay = LuaTableGetOr<float>(value, "homingDelay", LuaTableGetOr<float>(value, "mHomingDelay", 0.f));
		parameters.mTurnRate = LuaTableGetOr<float>(value, "turnRate", LuaTableGetOr<float>(value, "mTurnRate", 0.f));
		parameters.mTurnAcceleration = LuaTableGetOr<float>(value, "turnAcceleration", LuaTableGetOr<float>(value, "mTurnAcceleration", 0.f));
		parameters.mEccentricity = LuaTableGetOr<float>(value, "eccentricity", LuaTableGetOr<float>(value, "mEccentricity", 0.f));
		parameters.mCombatantSweepHeight = LuaTableGetOr<float>(value, "combatantSweepHeight", LuaTableGetOr<float>(value, "mCombatantSweepHeight", 0.f));

		parameters.mProjectileFlags = LuaTableGetOr<uint8_t>(value, "projectileFlags", LuaTableGetOr<uint8_t>(value, "mProjectileFlags", 0));

		parameters.mPiercing = LuaTableGetOr<bool>(value, "piercing", LuaTableGetOr<bool>(value, "mPiercing", false));
		parameters.mIgnoreGroundCollide = LuaTableGetOr<bool>(value, "ignoreGroundCollide", LuaTableGetOr<bool>(value, "mIgnoreGroundCollide", false));
		parameters.mIgnoreCreatureCollide = LuaTableGetOr<bool>(value, "ignoreCreatureCollide", LuaTableGetOr<bool>(value, "mIgnoreCreatureCollide", false));

		return parameters;
	}
}

// LuaFunction
namespace LuaFunction {
	// nMathUtil
	namespace nMathUtil {
		// Rotates v by the orientation (was v * eulerAngles(q), not a rotation).
		glm::vec3 TransformVector(const glm::vec3& v, const glm::quat& q) {
			const auto r = Game::OrientationMath::Rotate({ v.x, v.y, v.z }, q.x, q.y, q.z, q.w);
			return { r[0], r[1], r[2] };
		}

		// Game form: TransformVector(x, y, z, GetOrientation(object)) -> x, y, z.
		std::tuple<float, float, float> TransformVectorFloats(float vx, float vy, float vz, float qx, float qy, float qz, float qw) {
			const auto r = Game::OrientationMath::Rotate({ vx, vy, vz }, qx, qy, qz, qw);
			return { r[0], r[1], r[2] };
		}

		auto RotateVectorByAxisAngle(const glm::vec3& v, const glm::vec3& axis, float angle) {
			return v * glm::angleAxis(angle, axis);
		}

		std::tuple<float, float, float> RotateVectorByAxisAngleFloats(float vx, float vy, float vz, float ax, float ay, float az, float angle) {
			glm::vec3 axis(ax, ay, az);
			if (glm::length2(axis) <= 1e-6f) {
				axis = glm::vec3(0.f, 0.f, 1.f);
			} else {
				axis = glm::normalize(axis);
			}
			float rad = (std::abs(angle) > 6.2831855f) ? glm::radians(angle) : angle;
			glm::vec3 out = glm::vec3(vx, vy, vz) * glm::angleAxis(rad, axis);
			return { out.x, out.y, out.z };
		}

		auto CircleIntersectsArc(const glm::vec3& position, float radius, float arcX, float arcY, float arcFacingX, float arcFacingY, float arcLength, float arcAngle) {
			const auto somefunc = [](const glm::vec2& position, const glm::vec2& arc_position, glm::vec2 other) {
				auto delta = other - arc_position;
				auto something = (position - arc_position) * delta;

				auto var = something.x + something.y;
				auto var2 = glm::length2(something);
				if (var >= 0 && var2 >= var) {
					var /= var2;
					other = delta * var + arc_position;
				}

				return glm::length(position - other);
			};

			auto arcPosition = glm::vec2(arcX, arcY);
			auto delta = glm::vec2(position) - arcPosition;
			auto value = glm::length(delta);
			if (value < (arcLength + radius)) {
				if (value <= 1e-5f) {
					return true;
				}

				delta = (1.f / value) * delta;
				delta.x *= arcFacingX;
				delta.y *= arcFacingY;

				arcAngle *= 0.5f;
				float dotVal = std::clamp(delta.x + delta.y, -1.f, 1.f);
				if (arcAngle <= glm::acos(dotVal)) {
					glm::vec2 facingLength = glm::vec2(arcFacingX, arcFacingY) * arcLength;

					float arcCos = glm::cos(arcAngle);
					float arcSin = glm::sin(arcAngle);
					if (position.z <= somefunc(position, arcPosition, {
						(arcCos * facingLength.x) - (arcSin * facingLength.y) + arcX,
						(arcCos * facingLength.y) + (arcSin * facingLength.x) + arcY
					})) {
						float arcCosNeg = glm::cos(-arcAngle);
						float arcSinNeg = glm::sin(-arcAngle);
						if (position.z <= somefunc(position, arcPosition, {
							(arcCosNeg * facingLength.x) - (arcSinNeg * facingLength.y) + arcX,
							(arcCosNeg * facingLength.y) + (arcSinNeg * facingLength.x) + arcY
						})) {
							return false;
						}
					}
				}
				return true;

			}
			return false;
		}

		bool CircleIntersectsArcFloats(float posX, float posY, float radius, float arcX, float arcY, float arcFacingX, float arcFacingY, float arcLength, float arcAngle) {
			return CircleIntersectsArc(glm::vec3(posX, posY, radius), radius, arcX, arcY, arcFacingX, arcFacingY, arcLength, arcAngle);
		}

		std::tuple<float, float, float> ClosestPointOnLine(float px, float py, float pz, float ax, float ay, float az, float bx, float by, float bz) {
			glm::vec3 p(px, py, pz);
			glm::vec3 a(ax, ay, az);
			glm::vec3 b(bx, by, bz);
			glm::vec3 ab = b - a;
			float lenSq = glm::length2(ab);
			if (lenSq <= 1e-6f) {
				return { a.x, a.y, a.z };
			}
			float t = std::clamp(glm::dot(p - a, ab) / lenSq, 0.f, 1.f);
			glm::vec3 c = a + ab * t;
			return { c.x, c.y, c.z };
		}

		float DistanceToLine(float px, float py, float pz, float ax, float ay, float az, float bx, float by, float bz) {
			auto [cx, cy, cz] = ClosestPointOnLine(px, py, pz, ax, ay, az, bx, by, bz);
			return glm::distance(glm::vec3(px, py, pz), glm::vec3(cx, cy, cz));
		}

		std::tuple<float, float, float, float> GetQuaternionFromFacingAndPosition(float fx, float fy, float fz, sol::optional<float> px, sol::optional<float> py, sol::optional<float> pz) {
			float yaw = std::atan2(fy, fx);
			glm::quat q = glm::angleAxis(yaw, glm::vec3(0.f, 0.f, 1.f));
			return { q.x, q.y, q.z, q.w };
		}
	}

	// nObjectManager
	auto nObjectManager_GetObject(sol::this_state L, uint32_t id) {
		auto& game = LuaGetGame(L);
		return game.GetObjectManager().Get(id);
	}

	auto nObjectManager_CreateObject(sol::this_state L, uint32_t noun, glm::vec3 position) {
		auto& game = LuaGetGame(L);

		auto object = game.GetObjectManager().Create(noun);
		if (object) {
			object->SetPosition(position);
			game.SendObjectUpdate(object); // create and update the object
		}

		return object;
	}

	uint32_t nObjectManager_CreateObjectCoords(sol::this_state L, sol::object nounObj, float x, float y, float z, sol::variadic_args va) {
		auto& game = LuaGetGame(L);
		uint32_t noun = LuaExtractAssetOrGuid(nounObj, 0);
		auto object = game.GetObjectManager().Create(noun);
		if (object) {
			object->SetPosition(glm::vec3(x, y, z));
			game.SendObjectUpdate(object);
			return object->GetId();
		}
		return 0;
	}

	auto nObjectManager_GetTrigger(sol::this_state L, uint32_t id) {
		auto& game = LuaGetGame(L);
		return game.GetObjectManager().GetTrigger(id);
	}

	auto nObjectManager_CreateTriggerVolume(sol::this_state L, glm::vec3 position, float radius) {
		auto& game = LuaGetGame(L);
		return game.GetObjectManager().CreateTrigger(position, radius);
	}

	uint32_t nObjectManager_CreateTriggerVolumeCoords(sol::this_state L, float x, float y, float z, float radius, sol::variadic_args va) {
		auto& game = LuaGetGame(L);
		auto trigger = game.GetObjectManager().CreateTrigger(glm::vec3(x, y, z), radius);
		return trigger ? trigger->GetId() : 0;
	}

	uint32_t nObjectManager_CreateTriggerVolumeBoxCoords(sol::this_state L, float x, float y, float z, float ex, float ey, float ez, sol::variadic_args va) {
		auto& game = LuaGetGame(L);
		float radius = std::max({ std::abs(ex), std::abs(ey), std::abs(ez), 1.f });
		auto trigger = game.GetObjectManager().CreateTrigger(glm::vec3(x, y, z), radius);
		if (trigger) {
			trigger->SetExtent(glm::vec3(std::abs(ex), std::abs(ey), std::abs(ez)));
			return trigger->GetId();
		}
		return 0;
	}

	auto nObjectManager_GetObjectsInRadius(sol::this_state L, glm::vec3 position, float radius, sol::table objectTypes) {
		auto& game = LuaGetGame(L);

		std::vector<Game::NounType> types;
		for (const auto& entry : objectTypes) {
			types.push_back(static_cast<Game::NounType>(entry.second.as<uint32_t>()));
		}

		auto objects = game.GetObjectManager().GetObjectsInRadius(Game::BoundingSphere(position, radius), types);
		auto objectCount = static_cast<int>(objects.size());

		auto result = sol::state_view(L).create_table(objectCount, 0);
		for (int i = 0; i < objectCount; ++i) {
			result[i + 1] = objects[i];
		}

		return result;
	}

	// Shared implementation of the coordinate forms used by game scripts:
	// (x, y, z, radius[, type | {types}]). See game/lua_type_filter.h.
	enum class RadiusSort { None, Nearest, Furthest };

	sol::table ObjectIdsInRadius(sol::this_state L, float x, float y, float z, float radius,
	                             const std::vector<uint32_t>& typeFilter, RadiusSort sort) {
		auto& game = LuaGetGame(L);
		glm::vec3 position(x, y, z);

		std::vector<Game::NounType> types;
		types.reserve(typeFilter.size());
		for (const auto type : typeFilter) {
			types.push_back(static_cast<Game::NounType>(type));
		}

		auto objects = game.GetObjectManager().GetObjectsInRadius(Game::BoundingSphere(position, radius), types);
		if (sort != RadiusSort::None) {
			std::sort(objects.begin(), objects.end(), [&position, sort](const auto& lhs, const auto& rhs) {
				const float left = glm::distance(position, lhs->GetPosition());
				const float right = glm::distance(position, rhs->GetPosition());
				return sort == RadiusSort::Nearest ? left < right : left > right;
			});
		}

		auto objectCount = static_cast<int>(objects.size());
		auto result = sol::state_view(L).create_table(objectCount, 0);
		for (int i = 0; i < objectCount; ++i) {
			result[i + 1] = objects[i]->GetId();
		}
		return result;
	}

	sol::table nObjectManager_GetObjectsInRadiusCoords(sol::this_state L, float x, float y, float z, float radius, sol::variadic_args rest) {
		return ObjectIdsInRadius(L, x, y, z, radius, Game::LuaTypeFilter::Read(rest, 0), RadiusSort::None);
	}

	auto nObjectManager_GetObjectsInRadius_SortedByDistance(sol::this_state L, glm::vec3 position, float radius, sol::table objectTypes) {
		auto& game = LuaGetGame(L);

		std::vector<Game::NounType> types;
		for (const auto& entry : objectTypes) {
			types.push_back(static_cast<Game::NounType>(entry.second.as<uint32_t>()));
		}

		auto objects = game.GetObjectManager().GetObjectsInRadius(Game::BoundingSphere(position, radius), types);
		auto objectCount = static_cast<int>(objects.size());
		std::sort(objects.begin(), objects.end(), [&position](const auto& lhs, const auto& rhs) {
			return glm::distance(position, lhs->GetPosition()) < glm::distance(position, rhs->GetPosition());
		});

		auto result = sol::state_view(L).create_table(objectCount, 0);
		for (int i = 0; i < objectCount; ++i) {
			result[i + 1] = objects[i];
		}

		return result;
	}

	sol::table nObjectManager_GetObjectsInRadiusCoords_SortedByDistance(sol::this_state L, float x, float y, float z, float radius, sol::variadic_args rest) {
		return ObjectIdsInRadius(L, x, y, z, radius, Game::LuaTypeFilter::Read(rest, 0), RadiusSort::Nearest);
	}

	sol::table nObjectManager_GetObjectsInRadiusCoords_SortedByFurthestDistance(sol::this_state L, float x, float y, float z, float radius, sol::variadic_args rest) {
		return ObjectIdsInRadius(L, x, y, z, radius, Game::LuaTypeFilter::Read(rest, 0), RadiusSort::Furthest);
	}

	// Locomotion
	namespace Locomotion {
		auto On(sol::this_state L, sol::object objectValue, sol::variadic_args args) {
			auto& game = LuaGetGame(L);
			auto object = LuaGetObject(game.GetObjectManager(), objectValue);
			if (object && object->HasLocomotionData()) {

			}
		}

		auto TeleportAndFace(sol::this_state L, sol::object objectValue, glm::vec3 position, glm::vec3 facingPosition) {
			auto& game = LuaGetGame(L);
			auto object = LuaGetObject(game.GetObjectManager(), objectValue);
			if (object && object->HasLocomotionData()) {
				object->SetInputSyncStamp(object->GetInputSyncStamp() + 1);

				const auto& locomotion = object->GetLocomotionData();
				locomotion->Stop();

				locomotion->SetGoalPosition(position);
				locomotion->SetFacing(facingPosition);
				locomotion->SetGoalFlags(locomotion->GetGoalFlags() | 0x020);

				game.MoveObject(object, *locomotion);
			}
		}

		auto TeleportObject(sol::this_state L, sol::object objectValue, glm::vec3 position, sol::optional<bool> keepFacing) {
			auto& game = LuaGetGame(L);
			auto object = LuaGetObject(game.GetObjectManager(), objectValue);
			if (object && object->HasLocomotionData()) {
				object->SetInputSyncStamp(object->GetInputSyncStamp() + 1);

				const auto& locomotion = object->GetLocomotionData();
				locomotion->Stop();

				const auto& direction = position - object->GetPosition();
				object->SetPosition(position);

				if (keepFacing.value_or(false)) {
					locomotion->SetFacing(direction + object->GetPosition());
				}
			}
		}

		auto JumpInDirection(sol::this_state L, sol::object objectValue, glm::vec3 position, sol::optional<bool> keepFacing) {
			auto& game = LuaGetGame(L);
			auto object = LuaGetObject(game.GetObjectManager(), objectValue);
			if (object && object->HasLocomotionData()) {
				object->SetInputSyncStamp(object->GetInputSyncStamp() + 1);

				const auto& locomotion = object->GetLocomotionData();
				locomotion->Stop();

				const auto& direction = position - object->GetPosition();
				object->SetPosition(position);

				if (keepFacing.value_or(false)) {
					locomotion->SetFacing(direction + object->GetPosition());
				}
			}
		}

		auto SlideToPoint(sol::this_state L, sol::object objectValue, glm::vec3 position, float speed) {
			auto& game = LuaGetGame(L);
			auto object = LuaGetObject(game.GetObjectManager(), objectValue);
			if (object && object->HasLocomotionData()) {
				const auto& locomotion = object->GetLocomotionData();
				locomotion->SlideToPoint(position, speed);

				game.MoveObject(object, *locomotion);
			}
		}

		auto MoveToPointExact(sol::this_state L, sol::object objectValue, glm::vec3 position, sol::optional<bool> setSomeFlag) {
			auto& game = LuaGetGame(L);
			auto object = LuaGetObject(game.GetObjectManager(), objectValue);
			if (object && object->HasLocomotionData()) {
				const auto& locomotion = object->GetLocomotionData();
				locomotion->SetGoalPosition(position);

				if (setSomeFlag.value_or(false)) {
					locomotion->SetGoalFlags(locomotion->GetGoalFlags() | 0x800);
				}

				game.MoveObject(object, *locomotion);
			}
		}

		auto MoveToCircleEdge(sol::this_state L, sol::object objectValue, glm::vec3 position, float distance, sol::optional<bool> setSomeFlag) {
			auto& game = LuaGetGame(L);
			auto object = LuaGetObject(game.GetObjectManager(), objectValue);
			if (object && object->HasLocomotionData()) {
				const auto& locomotion = object->GetLocomotionData();
				locomotion->SetGoalPositionWithDistance(position, distance);

				if (setSomeFlag.value_or(false)) {
					locomotion->SetGoalFlags(locomotion->GetGoalFlags() | 0x800);
				}

				game.MoveObject(object, *locomotion);
			}
		}

		auto MoveToPointWithinRange(sol::this_state L, sol::object objectValue, glm::vec3 position, float distance, sol::optional<bool> setSomeFlag) {
			auto& game = LuaGetGame(L);
			auto& lua = game.GetLua();

			auto thread = lua.GetThread(L);
			if (!thread) {
				return;
			}

			auto& objectManager = game.GetObjectManager();
			auto object = LuaGetObject(objectManager, objectValue);
			if (object && object->HasLocomotionData()) {
				const auto& locomotion = object->GetLocomotionData();
				locomotion->SetGoalPositionWithDistance(position, distance);

				if (setSomeFlag.value_or(false)) {
					locomotion->SetGoalFlags(locomotion->GetGoalFlags() | 0x800);
				}

				game.MoveObject(object, *locomotion);

				thread->set_resume_condition([&objectManager, objectId = object->GetId(), position, distance](lua_State* L, sol::variadic_results& results) {
					auto object = objectManager.Get(objectId);
					if (!object) {
						return true;
					}

					auto delta = object->GetPosition() - position;
					if ((object->GetFootprintRadius() + distance) <= glm::length(delta)) {
						return false;
					}

					return true;
				});

				lua.YieldThread(thread);
			}
		}

		auto MoveToObject(sol::this_state L, sol::object objectValue, sol::object targetValue, sol::optional<float> distance, sol::optional<bool> setSomeFlag) {
			auto& game = LuaGetGame(L);
			auto& objectManager = game.GetObjectManager();

			auto object = LuaGetObject(objectManager, objectValue);
			if (object && object->HasLocomotionData()) {
				auto target = LuaGetObject(objectManager, targetValue);
				if (!target) {
					return;
				}

				const auto& locomotion = object->GetLocomotionData();
				locomotion->SetGoalObject(target, distance.value_or(0.f) + target->GetFootprintRadius(), false, false);

				if (setSomeFlag.value_or(false)) {
					locomotion->SetGoalFlags(locomotion->GetGoalFlags() | 0x800);
				}

				game.MoveObject(object, *locomotion);
			}
		}

		auto FollowAsPet(sol::this_state L, sol::object objectValue, sol::object targetValue, sol::optional<float> distance) {
			auto& game = LuaGetGame(L);
			auto& objectManager = game.GetObjectManager();

			auto object = LuaGetObject(objectManager, objectValue);
			if (object && object->HasLocomotionData()) {
				auto target = LuaGetObject(objectManager, targetValue);
				if (!target) {
					return;
				}

				const auto& locomotion = object->GetLocomotionData();
				locomotion->SetGoalObjectEx(target, distance.value_or(0.f) + target->GetFootprintRadius(), false);

				game.MoveObject(object, *locomotion);
			}
		}

		auto ApplyExternalVelocity(sol::this_state L, sol::object objectValue, glm::vec3 velocity) {
			auto& game = LuaGetGame(L);
			auto& objectManager = game.GetObjectManager();

			auto object = LuaGetObject(objectManager, objectValue);
			if (object && object->HasLocomotionData()) {
				const auto& locomotion = object->GetLocomotionData();
				locomotion->ApplyExternalVelocity(velocity);

				game.MoveObject(object, *locomotion);
			}
		}

		auto ClearExternalVelocity(sol::this_state L, sol::object objectValue) {
			auto& game = LuaGetGame(L);
			auto& objectManager = game.GetObjectManager();

			auto object = LuaGetObject(objectManager, objectValue);
			if (object && object->HasLocomotionData()) {
				const auto& locomotion = object->GetLocomotionData();
				locomotion->ClearExternalVelocity();

				game.MoveObject(object, *locomotion);
			}
		}

		auto ClearTargetObject(sol::this_state L, sol::object objectValue) {
			auto& game = LuaGetGame(L);
			auto& objectManager = game.GetObjectManager();

			auto object = LuaGetObject(objectManager, objectValue);
			if (object && object->HasLocomotionData()) {
				const auto& locomotion = object->GetLocomotionData();
				locomotion->ClearTargetObject();

				game.MoveObject(object, *locomotion);
			}
		}

		auto FaceObjectDuringMove(sol::this_state L, sol::object objectValue, sol::object targetValue) {
			auto& game = LuaGetGame(L);
			auto& objectManager = game.GetObjectManager();

			auto object = LuaGetObject(objectManager, objectValue);
			if (object && object->HasLocomotionData()) {
				auto target = LuaGetObject(objectManager, targetValue);
				if (!target) {
					return;
				}

				const auto& locomotion = object->GetLocomotionData();
				locomotion->FaceObjectDuringMove(target);

				game.MoveObject(object, *locomotion);
			}
		}

		auto TurnToFace(sol::this_state L, sol::object objectValue, glm::vec3 position, sol::optional<bool> setSomeFlag) {
			auto& game = LuaGetGame(L);
			auto& objectManager = game.GetObjectManager();

			auto object = LuaGetObject(objectManager, objectValue);
			if (object && object->HasLocomotionData()) {
				const auto& locomotion = object->GetLocomotionData();
				locomotion->SetFacing(position);

				if (setSomeFlag.value_or(false)) {
					locomotion->SetGoalFlags(locomotion->GetGoalFlags() | 0x800);
				}
			}
		}

		auto TurnToFaceTargetObject(sol::this_state L, sol::object objectValue, sol::object targetValue) {
			auto& game = LuaGetGame(L);
			auto& objectManager = game.GetObjectManager();

			auto object = LuaGetObject(objectManager, objectValue);
			if (object && object->HasLocomotionData()) {
				auto target = LuaGetObject(objectManager, targetValue);
				if (!target) {
					return;
				}

				const auto& locomotion = object->GetLocomotionData();
				locomotion->TurnToFaceTargetObject(target);
			}
		}

		auto MoveToPointWhileFacingTarget(sol::this_state L, sol::object objectValue, glm::vec3 position, sol::object targetValue) {
			auto& game = LuaGetGame(L);
			auto& objectManager = game.GetObjectManager();

			auto object = LuaGetObject(objectManager, objectValue);
			if (object && object->HasLocomotionData()) {
				auto target = LuaGetObject(objectManager, targetValue);
				if (!target) {
					return;
				}

				const auto& locomotion = object->GetLocomotionData();
				locomotion->MoveToPointWhileFacingTarget(position, target);

				game.MoveObject(object, *locomotion);
			}
		}

		auto Stop(sol::this_state L, sol::object objectValue) {
			auto& game = LuaGetGame(L);
			auto object = LuaGetObject(game.GetObjectManager(), objectValue);
			if (object && object->HasLocomotionData()) {
				const auto& locomotion = object->GetLocomotionData();
				locomotion->Stop();

				game.MoveObject(object, *locomotion);
			}
		}

		auto GetClosestPosition(sol::this_state L, glm::vec3 position) {
			// TODO: add Locomotion::GetClosestPosition (not sure how the fuck I'm gonna do that without getting navmesh data)
			return position;
		}

		auto GetClosestPositionFromPoint(sol::this_state L, glm::vec3 position) {
			// TODO: add Locomotion::GetClosestPositionFromPoint (not sure how the fuck I'm gonna do that without getting navmesh data)
			return position;
		}

		auto GetTargetPosition(sol::this_state L, sol::object objectValue) {
			auto& game = LuaGetGame(L);
			auto object = LuaGetObject(game.GetObjectManager(), objectValue);
			if (object && object->HasLocomotionData()) {
				const auto& locomotion = object->GetLocomotionData();
				return locomotion->GetTargetPosition();
			}

			return glm::zero<glm::vec3>();
		}

		auto SetTargetPosition(sol::this_state L, sol::object objectValue, glm::vec3 position) {
			auto& game = LuaGetGame(L);
			auto object = LuaGetObject(game.GetObjectManager(), objectValue);
			if (object && object->HasLocomotionData()) {
				const auto& locomotion = object->GetLocomotionData();
				locomotion->SetTargetPosition(position);
			}
		}
	}

	// Object
	namespace Object {
		auto IsValid(sol::this_state L, sol::object objectValue) {
			// object:IsValid() could be useful later.
			if (objectValue.is<Game::ObjectPtr>()) {
				return objectValue.as<Game::ObjectPtr>() != nullptr;
			}
			return false;
		}

		auto GetName(sol::this_state L, sol::object objectValue) {
			std::string result;

			auto& game = LuaGetGame(L);
			auto object = LuaGetObject(game.GetObjectManager(), objectValue);
			if (object) {
				const auto& noun = object->GetNoun();
				if (noun) {
					result = noun->GetName();
				}
			}

			return result;
		}

		auto GetPrivateTable(sol::this_state L, sol::object objectValue) {
			auto& game = LuaGetGame(L);
			auto object = LuaGetObject(game.GetObjectManager(), objectValue);
			if (object) {
				return game.GetLua().CreatePrivateTable(object->GetId());
			}
			return sol::table(sol::nil);
		}

		auto GetFacing(sol::this_state L, sol::object objectValue) {
			auto& game = LuaGetGame(L);
			auto object = LuaGetObject(game.GetObjectManager(), objectValue);
			if (object && object->HasLocomotionData()) {
				return object->GetLocomotionData()->GetFacing();
			}
			return glm::zero<glm::vec3>();
		}

		auto GetFootprintRadius(sol::this_state L, sol::object objectValue) {
			auto& game = LuaGetGame(L);
			auto object = LuaGetObject(game.GetObjectManager(), objectValue);
			if (object) {
				return object->GetFootprintRadius();
			}
			return 0.f;
		}

		auto Move(sol::this_state L, sol::object objectValue, glm::vec3 position, bool teleport) {
			auto& game = LuaGetGame(L);
			auto object = LuaGetObject(game.GetObjectManager(), objectValue);
			if (object && object->HasLocomotionData()) {
				const auto& locomotionData = object->GetLocomotionData();
				locomotionData->SetGoalPosition(position);
				if (teleport) {
					locomotionData->SetGoalFlags(locomotionData->GetGoalFlags() | 0x020);
				}
				game.MoveObject(object, *locomotionData);
			}
		}

		auto SetAnimationState(sol::this_state L, sol::object objectValue, uint32_t animationState, sol::optional<bool> overlay, sol::optional<float> scale) {
			if (animationState == 0) {
				return;
			}

			auto& game = LuaGetGame(L);
			game.SendAnimationState(LuaGetObject(game.GetObjectManager(), objectValue), animationState, overlay.value_or(false), scale.value_or(1.f));
		}

		auto SetAnimationStateToDeath(sol::this_state L, sol::object objectValue) {
			// TODO: 
		}

		auto SetAnimationStateToAggro(sol::this_state L, sol::object objectValue, sol::optional<bool> subsequent) {
			float duration = 0;

			auto& game = LuaGetGame(L);
			auto object = LuaGetObject(game.GetObjectManager(), objectValue);
			if (!object) {
				return duration;
			}

			const auto& noun = object->GetNoun();
			if (!noun) {
				return duration;
			}

			const auto& animations = noun->GetCharacterAnimation();
			if (!animations) {
				return duration;
			}

			uint32_t state;
			if (subsequent.value_or(false)) {
				std::tie(state, duration) = animations->GetAnimationData(Game::CharacterAnimationType::SubsequentAggro);
			} else {
				std::tie(state, duration) = animations->GetAnimationData(Game::CharacterAnimationType::Aggro);
			}

			game.SendAnimationState(object, state);
			return duration;
		}

		auto SetGraphicsState(sol::this_state L, sol::object objectValue, uint32_t graphicsState) {
			if (graphicsState == 0) {
				return;
			}

			auto& game = LuaGetGame(L);
			game.SendObjectGfxState(LuaGetObject(game.GetObjectManager(), objectValue), graphicsState);
		}

		auto GetAttributeSnapshot(sol::this_state L, sol::object objectValue) {
			const auto& game = LuaGetGame(L);
			const auto& object = LuaGetObject(game.GetObjectManager(), objectValue);

			Game::AttributesPtr attributes;
			if (object && object->HasAttributeData()) {
				attributes = std::make_shared<Game::Attributes>(*object->GetAttributeData());
				attributes->SetOwnerObject(object);
			}

			return attributes;
		}

		auto SetAttributeSnapshot(sol::this_state L, sol::object objectValue, sol::object attributeSnapshotValue) {
			auto& game = LuaGetGame(L);

			const auto& object = LuaGetObject(game.GetObjectManager(), objectValue);
			if (object) {
				// attributes = std::make_shared<Game::Attributes>(*object->GetAttributeData());
				// attributes->SetOwnerObject(object);
			}
		}

		auto GetWeaponDamage(sol::this_state L, sol::object objectValue) {
			auto& game = LuaGetGame(L);
			auto object = LuaGetObject(game.GetObjectManager(), objectValue);

			sol::table damageRange = sol::state_view(L).create_table(2);
			if (object && object->HasAttributeData()) {
				const auto& weaponDamage = object->GetAttributeData()->GetWeaponDamage();
				damageRange[1] = std::get<0>(weaponDamage);
				damageRange[2] = std::get<1>(weaponDamage);
			}

			return damageRange;
		}

		auto IsCombatant(sol::this_state L, sol::object objectValue) {
			auto& game = LuaGetGame(L);
			auto object = LuaGetObject(game.GetObjectManager(), objectValue);
			if (object) {
				return object->HasCombatantData();
			}
			return false;
		}

		auto AddCooldown(sol::this_state L, sol::object objectValue, uint32_t abilityId, uint32_t milliseconds) {
			auto& game = LuaGetGame(L);
			auto object = LuaGetObject(game.GetObjectManager(), objectValue);
			if (object) {
				const auto& [end, duration] = object->AddCooldown(abilityId, milliseconds);
				game.SendCooldownUpdate(object, abilityId, end - duration, duration);
			}
		}

		auto RemoveCooldown(sol::this_state L, sol::object objectValue, uint32_t abilityId, uint32_t milliseconds) {
			auto& game = LuaGetGame(L);
			auto object = LuaGetObject(game.GetObjectManager(), objectValue);
			if (object) {
				const auto& [end, duration] = object->RemoveCooldown(abilityId, milliseconds);
				game.SendCooldownUpdate(object, abilityId, end - duration, duration);
			}
		}

		auto ScaleCooldown(sol::this_state L, sol::object objectValue, uint32_t abilityId, float scale) {
			auto& game = LuaGetGame(L);
			auto object = LuaGetObject(game.GetObjectManager(), objectValue);
			if (object) {
				const auto [end, duration] = object->ScaleCooldown(abilityId, scale);
				game.SendCooldownUpdate(object, abilityId, end - duration, duration);
			}
		}

		auto TakeDamage(sol::this_state L,
			sol::object objectValue,
			sol::object attackerValue,
			sol::object attributeSnapshotValue,
			sol::table damageRange,
			Game::DamageType damageType,
			Game::DamageSource damageSource,
			float damageCoefficient,
			Game::Descriptors descriptors,
			sol::optional<float> damageMultiplier,
			sol::optional<glm::vec3> direction
		) {
			auto& game = LuaGetGame(L);
			const auto& objectManager = game.GetObjectManager();

			auto object = LuaGetObject(objectManager, objectValue);
			if (object) {
				auto attacker = LuaGetObject(objectManager, attackerValue);
				const float healthBeforeHit = object->GetHealth();

				Game::AttributesPtr attributes;
				if (attributeSnapshotValue.is<Game::AttributesPtr>()) {
					attributes = attributeSnapshotValue.as<Game::AttributesPtr>();
				} else if (attributeSnapshotValue.is<uint32_t>()) {
					if (auto snapObj = objectManager.Get(attributeSnapshotValue.as<uint32_t>()); snapObj && snapObj->HasAttributeData()) {
						attributes = std::make_shared<Game::Attributes>(*snapObj->GetAttributeData());
						attributes->SetOwnerObject(snapObj);
					}
				}
				if (!attributes && attacker && attacker->HasAttributeData()) {
					attributes = std::make_shared<Game::Attributes>(*attacker->GetAttributeData());
					attributes->SetOwnerObject(attacker);
				}

				float minDmg = damageRange.valid() ? damageRange.raw_get_or(1, 0.f) : 0.f;
				float maxDmg = damageRange.valid() ? damageRange.raw_get_or(2, 0.f) : 0.f;
				if (minDmg <= 0.f && maxDmg <= 0.f && attacker && attacker->HasAttributeData()) {
					std::tie(minDmg, maxDmg) = attacker->GetAttributeData()->GetWeaponDamage();
				}
				if (minDmg <= 0.f && maxDmg <= 0.f) {
					minDmg = 15.f;
					maxDmg = 25.f;
				}

				Game::CombatEvent combatEvent;
				combatEvent.mTargetId = object->GetId();
				combatEvent.mDirection = direction.value_or(glm::zero<glm::vec3>());

				const auto& result = object->TakeDamage(
					attributes,
					std::make_tuple(minDmg, maxDmg),
					damageType,
					damageSource,
					damageCoefficient,
					descriptors,
					damageMultiplier.value_or(1.0f),
					combatEvent.mDirection
				);

				combatEvent.mDeltaHealth = std::get<1>(result);
				combatEvent.mIntegerHpChange = static_cast<int32_t>(combatEvent.mDeltaHealth);
				combatEvent.mSourceId = attacker ? attacker->GetId() : 0;

				uint32_t flags = static_cast<uint32_t>(Game::CombatEventFlags::Hit);
				if (std::get<2>(result)) {
					flags |= static_cast<uint32_t>(Game::CombatEventFlags::Critical);
				}
				if (healthBeforeHit > 0.f && object->GetHealth() <= 0.f) {
					flags |= static_cast<uint32_t>(Game::CombatEventFlags::KillingBlow);
				}
				combatEvent.mFlags = static_cast<Game::CombatEventFlags>(flags);

				game.SendCombatEvent(combatEvent);
				const float appliedDamage = std::max(0.f, healthBeforeHit - object->GetHealth());
				game.OnDamageDealt(attacker, object, appliedDamage);
				return result;
			}

			return std::make_tuple(false, 0.f, false);
		}

		auto Heal(sol::this_state L,
			sol::object objectValue,
			sol::object attackerValue,
			sol::object attributeSnapshotValue,
			sol::table healRange,
			float healCoefficient,
			sol::optional<Game::Descriptors> descriptors,
			sol::optional<bool> critical,
			sol::optional<bool> canResurrect
		) {
			auto& game = LuaGetGame(L);
			const auto& objectManager = game.GetObjectManager();

			auto object = LuaGetObject(objectManager, objectValue);
			if (object) {
				auto attacker = LuaGetObject(objectManager, attackerValue);

				Game::AttributesPtr attributes;
				if (attributeSnapshotValue.is<Game::AttributesPtr>()) {
					attributes = attributeSnapshotValue.as<Game::AttributesPtr>();
				}

				Game::CombatEvent combatEvent;
				combatEvent.mTargetId = object->GetId();

				const auto& result = object->Heal(
					attributes,
					std::make_tuple(healRange.raw_get_or(1, 0.f), healRange.raw_get_or(2, 0.f)),
					healCoefficient,
					descriptors.value_or(Game::Descriptors::IsBasic),
					critical.value_or(false),
					false
				);

				combatEvent.mDeltaHealth = std::get<0>(result);
				combatEvent.mIntegerHpChange = static_cast<int32_t>(combatEvent.mDeltaHealth);

				if (std::get<1>(result)) {
					combatEvent.mFlags = Game::CombatEventFlags::Critical;
				}

				game.SendCombatEvent(combatEvent);
				return result;
			}

			return std::make_tuple(0.f, false);
		}

		auto AddEffect(sol::this_state L, sol::object objectValue, sol::object serverEventDef, sol::optional<sol::object> secondaryObjectValue, sol::optional<glm::vec3> targetPointValue) {
			auto& game = LuaGetGame(L);
			const auto& objectManager = game.GetObjectManager();

			auto object = LuaGetObject(objectManager, objectValue);
			if (object) {
				uint32_t effect;
				if (serverEventDef.is<uint32_t>()) {
					effect = serverEventDef.as<uint32_t>();
				} else if (serverEventDef.is<std::string>()) {
					effect = utils::hash_id(serverEventDef.as<std::string>());
				} else {
					effect = 0;
				}

				auto index = object->AddEffect(effect);
				if (index == 0) {
					return index;
				}

				Game::ServerEvent serverEvent;
				serverEvent.mServerEventDef = effect;
				serverEvent.mEffectIndex = index;
				serverEvent.mForceAttach = true;
				serverEvent.mObjectId = object->GetId();

				if (targetPointValue.has_value()) {
					serverEvent.mTargetPoint = targetPointValue.value();
				} else {
					serverEvent.mTargetPoint = object->GetPosition();
				}

				// serverEvent.SetIgnoredPlayerBits(ignoredPlayerBits);
				if (secondaryObjectValue) {
					auto secondaryObject = LuaGetObject(objectManager, *secondaryObjectValue);
					if (secondaryObject) {
						serverEvent.mSecondaryObjectId = secondaryObject->GetId();
					}
				}

				game.SendServerEvent(serverEvent);
				return serverEvent.mEffectIndex;
			}

			return static_cast<uint8_t>(0);
		}

		auto RemoveEffect(sol::this_state L, sol::object objectValue, sol::object effectValue, sol::optional<bool> hardStop) {
			uint32_t effect;
			if (effectValue.is<uint32_t>()) {
				effect = effectValue.as<uint32_t>();
			} else if (effectValue.is<std::string>()) {
				effect = utils::hash_id(effectValue.as<std::string>());
			} else {
				return;
			}

			auto& game = LuaGetGame(L);
			auto object = LuaGetObject(game.GetObjectManager(), objectValue);
			if (object) {
				auto index = object->RemoveEffect(effect);
				if (index != 0) {
					Game::ServerEvent serverEvent;
					serverEvent.mObjectId = object->GetId();
					serverEvent.mEffectIndex = index;
					serverEvent.mRemove = true;
					serverEvent.mHardStop = hardStop.value_or(true);
					// serverEvent.SetIgnoredPlayerBits(ignoredPlayerBits);

					game.SendServerEvent(serverEvent);
				}
			}
		}

		auto RemoveEffectIndex(sol::this_state L, sol::object objectValue, uint8_t index, sol::optional<bool> hardStop) {
			if (index == 0) {
				return;
			}

			auto& game = LuaGetGame(L);
			auto object = LuaGetObject(game.GetObjectManager(), objectValue);
			if (object) {
				auto removed = object->RemoveEffectByIndex(index);
				if (removed) {
					Game::ServerEvent serverEvent;
					serverEvent.mObjectId = object->GetId();
					serverEvent.mEffectIndex = index;
					serverEvent.mRemove = true;
					serverEvent.mHardStop = hardStop.value_or(true);
					// serverEvent.SetIgnoredPlayerBits(ignoredPlayerBits);

					game.SendServerEvent(serverEvent);
				}
			}
		}

		auto UseAbility(sol::this_state L, sol::object objectValue, uint32_t abilityId, sol::object targetObjectValue, glm::vec3 targetPosition) {
			auto& game = LuaGetGame(L);

			const auto& ability = game.GetLua().GetAbility(abilityId);
			if (ability) {
				const auto& objectManager = game.GetObjectManager();

				auto object = LuaGetObject(objectManager, objectValue);
				if (object) {
					auto targetObject = LuaGetObject(objectManager, targetObjectValue);
					ability->Tick(object, targetObject, targetPosition, 1);
				}
			}
		}

		auto DropLoot(sol::this_state L, sol::object objectValue, sol::object targetObjectValue, Game::DropType dropType) {
			auto& game = LuaGetGame(L);
			const auto& objectManager = game.GetObjectManager();

			auto object = LuaGetObject(objectManager, objectValue);
			auto targetObject = LuaGetObject(objectManager, targetObjectValue);
			if (object && targetObject) {
				game.DropLoot(object, targetObject, dropType);
			}
		}

		auto GetSharedAbilityOffset(sol::this_state L, sol::object objectValue) {
			auto& game = LuaGetGame(L);
			auto object = LuaGetObject(game.GetObjectManager(), objectValue);

			glm::vec3 offset {};
			if (object) {
				const auto& noun = object->GetNoun();
				if (noun) {
					const auto& playerClassData = noun->GetPlayerClassData();
					if (playerClassData) {
						offset = playerClassData->GetSharedAbilityOffset();
					}
				}
			}

			return offset;
		}

		auto IncrementNumTimesUsed(sol::this_state L, sol::object objectValue) {
			auto& game = LuaGetGame(L);
			auto object = LuaGetObject(game.GetObjectManager(), objectValue);

			if (object) {
				const auto& interactableData = object->GetInteractableData();
				if (interactableData) {
					interactableData->SetTimesUsed(interactableData->GetTimesUsed() + 1);
				}
			}
		}

		auto GetTeleporterDestination(sol::this_state L, sol::object objectValue) {
			auto& game = LuaGetGame(L);
			auto object = LuaGetObject(game.GetObjectManager(), objectValue);

			glm::vec3 destination {};
			if (object) {
				const auto& marker = game.GetMarker(object->GetMarkerId());
				if (marker) {
					const auto& teleporterData = marker->GetTeleporterData();
					if (teleporterData) {
						const auto& destinationMarker = game.GetMarker(teleporterData->GetDestinationId());
						if (destinationMarker) {
							destination = destinationMarker->GetPosition();
						}
					}
				}
			}

			return destination;
		}

		auto IsStealthed(sol::this_state L, sol::object objectValue) {
			auto& game = LuaGetGame(L);
			auto object = LuaGetObject(game.GetObjectManager(), objectValue);
			if (object) {
				return object->GetStealthType() != Game::StealthType::None;
			}
			return false;
		}
	}

	// nPlayer
	auto nPlayerPickupCrystal() {

	}
}

// Game
namespace Game {
	// LuaBase
	void LuaBase::RegisterFunctions() {
		// os
		auto osTable = mState.create_named_table("os");
		osTable["mtime"] = &utils::get_milliseconds;

		// glm
		{
			mState.new_usertype<glm::vec3>("vec3",
				sol::constructors<glm::vec3(), glm::vec3(float, float, float)>(),

				sol::meta_function::to_string, [](const glm::vec3& v) { return std::format("vec3<{}, {}, {}>", v.x, v.y, v.z); },
				sol::meta_function::addition, [](const glm::vec3& v1, const glm::vec3& v2) -> glm::vec3 { return v1 + v2; },
				sol::meta_function::subtraction, [](const glm::vec3& v1, const glm::vec3& v2) -> glm::vec3 { return v1 - v2; },
				sol::meta_function::multiplication, sol::overload(
					[](const glm::vec3& v1, const glm::vec3& v2) -> glm::vec3 { return v1 * v2; },
					[](const glm::vec3& v1, float f) -> glm::vec3 { return v1 * f; },
					[](float f, const glm::vec3& v1) -> glm::vec3 { return f * v1; }
				),
				sol::meta_function::division, sol::overload(
					[](const glm::vec3& v1, const glm::vec3& v2) -> glm::vec3 { return v1 / v2; },
					[](const glm::vec3& v1, float f) -> glm::vec3 { return v1 / f; },
					[](float f, const glm::vec3& v1) -> glm::vec3 { return f / v1; }
				),
				
				"x", &glm::vec3::x,
				"y", &glm::vec3::y,
				"z", &glm::vec3::z,
					
				"dot", [](const glm::vec3& v1, const glm::vec3& v2) -> float { return glm::dot(v1, v2); },
				"len", [](const glm::vec3& v) -> float { return glm::length(v); },
				"sqrlen", [](const glm::vec3& v) -> float { return glm::length2(v); },
				"distance", [](const glm::vec3& v1, const glm::vec3& v2) -> float { return glm::distance(v1, v2); },
				"distance2", [](const glm::vec3& v1, const glm::vec3& v2) -> float { return glm::distance2(v1, v2); },
				"normalize", [](const glm::vec3& v) -> glm::vec3 { return glm::normalize(v); },
				"cross", [](const glm::vec3& v1, const glm::vec3& v2) -> glm::vec3 { return glm::cross(v1, v2); },
				"direction", [](const glm::vec3& v1, const glm::vec3& v2) -> glm::vec3 { return glm::normalize(v1 - v2); },
				"lerp", [](const glm::vec3& v1, const glm::vec3& v2, float a) -> glm::vec3 { return glm::lerp(v1, v2, a); },
				"scale", [](const glm::vec3& v, float scale) -> glm::vec3 { return v * scale; }
			);

			mState.new_usertype<glm::quat>("quat",
				sol::constructors<glm::quat(), glm::quat(float, float, float, float)>(),

				sol::meta_function::to_string, [](const glm::quat& q) { return std::format("quat<{}, {}, {}, {}>", q.x, q.y, q.z, q.w); },

				"x", &glm::quat::x,
				"y", &glm::quat::y,
				"z", &glm::quat::z,
				"w", &glm::quat::w
			);
		}

		// nBit
		{
			auto bitTable = mState.create_named_table("nBit");
			bitTable["Or"] = [](sol::variadic_args va) -> uint32_t {
				uint32_t acc = 0;
				for (auto v : va) {
					if (v.is<uint32_t>()) {
						acc |= v.as<uint32_t>();
					} else if (v.is<double>()) {
						acc |= static_cast<uint32_t>(v.as<double>());
					}
				}
				return acc;
			};
			bitTable["And"] = [](sol::variadic_args va) -> uint32_t {
				uint32_t acc = 0xFFFFFFFFu;
				bool any = false;
				for (auto v : va) {
					any = true;
					if (v.is<uint32_t>()) {
						acc &= v.as<uint32_t>();
					} else if (v.is<double>()) {
						acc &= static_cast<uint32_t>(v.as<double>());
					}
				}
				return any ? acc : 0u;
			};
			bitTable["Mask"] = [](uint32_t bit) -> uint32_t {
				return (bit < 32) ? (1u << bit) : 0u;
			};
		}

		RegisterMath();
		RegisterUtil();
		RegisterClient();
		RegisterAbility();
		RegisterPhysics();
		RegisterGameObject();
		RegisterAbilityAnimationSelection();
		RegisterDebug();
		RegisterNPCType();
		RegisterTuning();
		RegisterPlayer();
		RegisterLocomotion();

		// Ensure all top-level n* tables exist during script load in both GlobalLua and Lua
		for (const char* tableName : {
			"nAttribute", "nEvent", "nModifier", "nCondition", "nAffix",
			"nPopupTip", "nTimeManager", "nScenarioManager", "nUIManager",
			"nGameSimulator", "nGameDirector", "nThreadData", "nObjectManager", "nThread"
		}) {
			if (!mState[tableName].valid() || mState[tableName] == sol::nil) {
				mState.create_named_table(tableName);
			}
		}
		// Scripts branch on this to skip client-only work.
		mState["nScenarioManager"]["GetIsClient"] = [](sol::variadic_args) { return false; };
		mState["nModifier"]["PreloadAsset"] = [](sol::variadic_args args) -> uint32_t {
			// Asset/modifier id: the resource hash of the name (a number passes through).
			return args.size() > 0 ? LuaExtractAssetOrGuid(sol::object(args[0]), 0) : 0u;
		};
		mState["nModifier"]["RegisterModifier"] = [](sol::variadic_args) {};
		mState["nCondition"]["RegisterCondition"] = [](sol::variadic_args) {};
		mState["nAffix"]["PreloadModifier"] = [](sol::variadic_args args) -> uint32_t {
			// Asset/modifier id: the resource hash of the name (a number passes through).
			return args.size() > 0 ? LuaExtractAssetOrGuid(sol::object(args[0]), 0) : 0u;
		};
		mState["nAffix"]["RegisterAffix"] = [](sol::variadic_args) {};
		mState["nPopupTip"]["RegisterPopupTip"] = [](sol::variadic_args) {};
	}

	void LuaBase::RegisterMath() {
		auto mathUtil = mState.create_named_table("nMathUtil");
		mathUtil["TransformVector"] = sol::overload(
			&LuaFunction::nMathUtil::TransformVector,
			&LuaFunction::nMathUtil::TransformVectorFloats
		);
		mathUtil["RotateVectorByAxisAngle"] = sol::overload(
			&LuaFunction::nMathUtil::RotateVectorByAxisAngle,
			&LuaFunction::nMathUtil::RotateVectorByAxisAngleFloats
		);
		mathUtil["CircleIntersectsArc"] = sol::overload(
			&LuaFunction::nMathUtil::CircleIntersectsArc,
			&LuaFunction::nMathUtil::CircleIntersectsArcFloats
		);
		mathUtil["ClosestPointOnLine"] = &LuaFunction::nMathUtil::ClosestPointOnLine;
		mathUtil["DistanceToLine"] = &LuaFunction::nMathUtil::DistanceToLine;
		mathUtil["GetQuaternionFromFacingAndPosition"] = &LuaFunction::nMathUtil::GetQuaternionFromFacingAndPosition;
	}

	void LuaBase::RegisterUtil() {
		auto util = mState.create_named_table("nUtil");

		util["SPID"] = [](std::string value) -> uint32_t {
			return utils::hash_id(value);
		};

		util["ToGUID"] = [](sol::object value) -> uint32_t {
			return LuaExtractAssetOrGuid(value, 0);
		};

		util["GetAsset"] = [this](std::string value) -> uint32_t {
			uint32_t fullHash = utils::hash_id(value);
			if (NounDatabase::Instance().Get(fullHash)) {
				return fullHash;
			}
			if (value.size() > 5 && (value.ends_with(".Noun") || value.ends_with(".noun"))) {
				uint32_t stemHash = utils::hash_id(value.substr(0, value.size() - 5));
				if (NounDatabase::Instance().Get(stemHash)) {
					return stemHash;
				}
			}
			return fullHash;
		};
	}

	void LuaBase::RegisterClient() {
		auto client = mState.create_named_table("nClient");
		// The server has no local player or HUD: client-only queries return
		// "none" and targeting reticles draw nothing.
		client["GetMyPlayerObjectId"] = [](sol::variadic_args) { return 0u; };
		client["GetPlayerObjectAttribute"] = [](sol::variadic_args) { return 0.f; };
		client["DrawReticle_Circle"] = [](sol::variadic_args) {};
		client["DrawReticle_PointBlankCircle"] = [](sol::variadic_args) {};
		client["DrawReticle_Cone"] = [](sol::variadic_args) {};
		client["CreateClientEvent"] = [this](uint32_t eventType) {
			sol::table ev = mState.create_table();
			ev["eventType"] = eventType;
			return ev;
		};
		client["SetClientEventIntData"] = [](sol::object ev, sol::object k, sol::object v) {
			if (ev.is<sol::table>()) { ev.as<sol::table>()[k] = v; }
		};
		client["SetClientEventFloatData"] = [](sol::object ev, sol::object k, sol::object v) {
			if (ev.is<sol::table>()) { ev.as<sol::table>()[k] = v; }
		};
		client["SetClientEventGUIDData"] = [](sol::object ev, sol::object k, sol::object v) {
			if (ev.is<sol::table>()) { ev.as<sol::table>()[k] = v; }
		};
		client["SendClientEvent"] = [](sol::variadic_args) {};
		client["DestroyClientEvent"] = [](sol::variadic_args) {};
	}

	void LuaBase::RegisterAbility() {
		auto ability = mState.create_named_table("nAbility");
		ability["PreloadAnimation"] = [](sol::variadic_args args) -> uint32_t {
			// Animation state ids are the resource hash of the animation name.
			if (args.size() > 0 && args[0].get_type() == sol::type::string) {
				return utils::hash_id(args[0].as<std::string>().c_str());
			}
			return 0u;
		};
		ability["PreloadAsset"] = [](sol::variadic_args args) -> uint32_t {
			// Asset/modifier id: the resource hash of the name (a number passes through).
			return args.size() > 0 ? LuaExtractAssetOrGuid(sol::object(args[0]), 0) : 0u;
		};
		ability["PreloadModifier"] = [](sol::variadic_args args) -> uint32_t {
			// Asset/modifier id: the resource hash of the name (a number passes through).
			return args.size() > 0 ? LuaExtractAssetOrGuid(sol::object(args[0]), 0) : 0u;
		};
		ability["RegisterAbility"] = [](sol::variadic_args) {};
	}

	void LuaBase::RegisterPhysics() {
		auto physics = mState.create_named_table("nPhysics");
		physics["IsInLineOfSight"] = [](sol::variadic_args) { return true; };
		physics["AddPhysicsForObject"] = [](sol::variadic_args) {};
		physics["RemovePhysicsForObject"] = [](sol::variadic_args) {};
		physics["SetObjectAsCollidable"] = [](sol::variadic_args) {};
		physics["IsDynamic"] = [](sol::variadic_args) { return false; };
		physics["ApplyImpulse"] = [](sol::variadic_args) {};
		physics["DistanceToTerrain"] = [](sol::variadic_args) { return 0.f; };
		physics["ForceClientUpdate"] = [](sol::variadic_args) {};
	}

	void LuaBase::RegisterGameObject() {
		auto gameObject = mState.create_named_table("nGameObject");
	}

	void LuaBase::RegisterAbilityAnimationSelection() {
		auto abilityAnimationSelection = mState.create_named_table("nAbilityAnimationSelection");
		abilityAnimationSelection["PickAnimation"] = [](sol::variadic_args) -> std::tuple<uint32_t, float> {
			return { 0u, 0.35f };
		};
	}

	void LuaBase::RegisterDebug() {
		auto debug = mState.create_named_table("nDebug");
		debug["Log"] = [](sol::variadic_args) {};
		debug["DrawSphere"] = [](sol::variadic_args) {};
		debug["DrawCircle"] = [](sol::variadic_args) {};
		debug["DrawLine"] = [](sol::variadic_args) {};
		debug["DrawBox"] = [](sol::variadic_args) {};
		debug["DrawArc"] = [](sol::variadic_args) {};
		debug["Print"] = [](sol::variadic_args) {};
		// Server-side: debug visualisation is never enabled and draws nothing.
		// IsAbilityDebugEnabled is called by every melee hit (DebugDrawDamageArc).
		debug["IsAbilityDebugEnabled"] = [](sol::variadic_args) { return false; };
		debug["LogToConsole"] = [](sol::variadic_args) {};
		debug["SetColor"] = [](sol::variadic_args) {};
		debug["SetAlpha"] = [](sol::variadic_args) {};
		debug["SetDepthEnabled"] = [](sol::variadic_args) {};
		debug["Assert"] = [](sol::variadic_args) {};
		debug["DrawAxis"] = [](sol::variadic_args) {};
		debug["DrawOrientedBox"] = [](sol::variadic_args) {};
		debug["FillArc"] = [](sol::variadic_args) {};
	}

	void LuaBase::RegisterNPCType() {
		auto npcType = mState.create_named_table("nNPCType");
		npcType["Invalid"] = static_cast<uint32_t>(NpcType::Invalid);
		npcType["Minion"] = static_cast<uint32_t>(NpcType::Minion);
		npcType["Special"] = static_cast<uint32_t>(NpcType::Special);
		npcType["Boss"] = static_cast<uint32_t>(NpcType::Boss);
		npcType["Destructible"] = static_cast<uint32_t>(NpcType::Destructible);
		npcType["Interactable"] = static_cast<uint32_t>(NpcType::Interactable);
		npcType["Agent"] = static_cast<uint32_t>(NpcType::Agent);
		npcType["Victim"] = static_cast<uint32_t>(NpcType::Victim);
		npcType["Captain"] = static_cast<uint32_t>(NpcType::Captain);
	}

	void LuaBase::RegisterTuning() {
		auto tuning = mState.create_named_table("nTuning");
		tuning["GetOverdriveCooldownMultiplier"] = []() { return 0.5f; };
		tuning["GetOverdriveDamageMultiplier"] = []() { return 1.5f; };
		tuning["GetHealthOrbHealPercentage"] = []() { return 0.25f; };
		tuning["GetManaOrbManaPercentage"] = []() { return 0.25f; };
		tuning["GetChainBonusMultiplier"] = []() { return 1.0f; };
		tuning["GetEliteDamageMultiplier"] = []() { return 1.25f; };
		tuning["GetEliteHealthMultiplier"] = []() { return 1.5f; };
		tuning["GetMinionHealthMultiplier"] = []() { return 1.0f; };
	}

	void LuaBase::RegisterPlayer() {
		auto player = mState.create_named_table("nPlayer");
	}

	void LuaBase::RegisterLocomotion() {
		auto locomotion = mState.create_named_table("nLocomotion");
	}

	void LuaBase::LoadAssetsCatalog() {
		std::string filepath = "data/assets_catalog.xml";

		pugi::xml_document document;
		if (!document.load_file(filepath.c_str())) {
			return;
		}

		if (auto catalog = document.child("catalog")) {
			for (const auto& entry : catalog.children("entry")) {
				auto asset = utils::xml_get_text_node(entry, "asset");
				auto sourceFilePath = utils::xml_get_text_node(entry, "source_file");
				if (!asset.empty() && !sourceFilePath.empty()) {
					mAssets.emplace(std::move(asset), std::move(sourceFilePath));
				}
			}
		}
	}

	// Lua
	void Lua::RegisterEnums() {
		// Attribute
		mState.new_enum<AttributeType>("Attribute", {
			{ "Strength", AttributeType::Strength },
			{ "Dexterity", AttributeType::Dexterity },
			{ "Mind", AttributeType::Mind },
			{ "MaxHealthIncrease", AttributeType::MaxHealthIncrease },
			{ "MaxHealth", AttributeType::MaxHealth },
			{ "MaxMana", AttributeType::MaxMana },
			{ "DamageReduction", AttributeType::DamageReduction },
			{ "PhysicalDefense", AttributeType::PhysicalDefense },
			{ "PhysicalDamageReduction", AttributeType::PhysicalDamageReduction },
			{ "EnergyDefense", AttributeType::EnergyDefense },
			{ "CriticalRating", AttributeType::CriticalRating },
			{ "NonCombatSpeed", AttributeType::NonCombatSpeed },
			{ "CombatSpeed", AttributeType::CombatSpeed },
			{ "DamageBuff", AttributeType::DamageBuff },
			{ "Silence", AttributeType::Silence },
			{ "Immobilized", AttributeType::Immobilized },
			{ "DefenseBoostBasicDamage", AttributeType::DefenseBoostBasicDamage },
			{ "PhysicalDamageIncrease", AttributeType::PhysicalDamageIncrease },
			{ "PhysicalDamageIncreaseFlat", AttributeType::PhysicalDamageIncreaseFlat },
			{ "AutoCrit", AttributeType::AutoCrit },
			{ "BehindDirectDamageIncrease", AttributeType::BehindDirectDamageIncrease },
			{ "BehindOrSideDirectDamageIncrease", AttributeType::BehindOrSideDirectDamageIncrease },
			{ "CriticalDamageIncrease", AttributeType::CriticalDamageIncrease },
			{ "AttackSpeedScale", AttributeType::AttackSpeedScale },
			{ "CooldownScale", AttributeType::CooldownScale },
			{ "Frozen", AttributeType::Frozen },
			{ "ProjectileSpeedIncrease", AttributeType::ProjectileSpeedIncrease },
			{ "AoEResistance", AttributeType::AoEResistance },
			{ "EnergyDamageBuff", AttributeType::EnergyDamageBuff },
			{ "Intangible", AttributeType::Intangible },
			{ "HealingReduction", AttributeType::HealingReduction },
			{ "EnergyDamageIncrease", AttributeType::EnergyDamageIncrease },
			{ "EnergyDamageIncreaseFlat", AttributeType::EnergyDamageIncreaseFlat },
			{ "Immune", AttributeType::Immune },
			{ "StealthDetection", AttributeType::StealthDetection },
			{ "LifeSteal", AttributeType::LifeSteal },
			{ "RejectModifier", AttributeType::RejectModifier },
			{ "AoEDamage", AttributeType::AoEDamage },
			{ "TechnologyTypeDamage", AttributeType::TechnologyTypeDamage },
			{ "SpacetimeTypeDamage", AttributeType::SpacetimeTypeDamage },
			{ "LifeTypeDamage", AttributeType::LifeTypeDamage },
			{ "ElementsTypeDamage", AttributeType::ElementsTypeDamage },
			{ "SupernaturalTypeDamage", AttributeType::SupernaturalTypeDamage },
			{ "TechnologyTypeResistance", AttributeType::TechnologyTypeResistance },
			{ "SpacetimeTypeResistance", AttributeType::SpacetimeTypeResistance },
			{ "LifeTypeResistance", AttributeType::LifeTypeResistance },
			{ "ElementsTypeResistance", AttributeType::ElementsTypeResistance },
			{ "SupernaturalTypeResistance", AttributeType::SupernaturalTypeResistance },
			{ "MovementSpeedBuff", AttributeType::MovementSpeedBuff },
			{ "ImmuneToDebuffs", AttributeType::ImmuneToDebuffs },
			{ "BuffDuration", AttributeType::BuffDuration },
			{ "DebuffDuration", AttributeType::DebuffDuration },
			{ "ManaSteal", AttributeType::ManaSteal },
			{ "DebuffDurationIncrease", AttributeType::DebuffDurationIncrease },
			{ "EnergyDamageReduction", AttributeType::EnergyDamageReduction },
			{ "Incorporeal", AttributeType::Incorporeal },
			{ "DoTDamageIncrease", AttributeType::DoTDamageIncrease },
			{ "MindControlled", AttributeType::MindControlled },
			{ "SwapDisabled", AttributeType::SwapDisabled },
			{ "ImmuneToRandomTeleport", AttributeType::ImmuneToRandomTeleport },
			{ "ImmuneToBanish", AttributeType::ImmuneToBanish },
			{ "ImmuneToKnockback", AttributeType::ImmuneToKnockback },
			{ "AoeRadius", AttributeType::AoeRadius },
			{ "PetDamage", AttributeType::PetDamage },
			{ "PetHealth", AttributeType::PetHealth },
			{ "CrystalFind", AttributeType::CrystalFind },
			{ "DNADropped", AttributeType::DNADropped },
			{ "RangeIncrease", AttributeType::RangeIncrease },
			{ "OrbEffectiveness", AttributeType::OrbEffectiveness },
			{ "OverdriveBuildup", AttributeType::OverdriveBuildup },
			{ "OverdriveDuration", AttributeType::OverdriveDuration },
			{ "LootFind", AttributeType::LootFind },
			{ "Surefooted", AttributeType::Surefooted },
			{ "ImmuneToStunned", AttributeType::ImmuneToStunned },
			{ "ImmuneToSleep", AttributeType::ImmuneToSleep },
			{ "ImmuneToTerrified", AttributeType::ImmuneToTerrified },
			{ "ImmuneToSilence", AttributeType::ImmuneToSilence },
			{ "ImmuneToCursed", AttributeType::ImmuneToCursed },
			{ "ImmuneToPoisonOrDisease", AttributeType::ImmuneToPoisonOrDisease },
			{ "ImmuneToBurning", AttributeType::ImmuneToBurning },
			{ "ImmuneToRooted", AttributeType::ImmuneToRooted },
			{ "ImmuneToSlow", AttributeType::ImmuneToSlow },
			{ "ImmuneToPull", AttributeType::ImmuneToPull },
			{ "DoTDamageDoneIncrease", AttributeType::DoTDamageDoneIncrease },
			{ "AggroIncrease", AttributeType::AggroIncrease },
			{ "AggroDecrease", AttributeType::AggroDecrease },
			{ "PhysicalDamageDoneIncrease", AttributeType::PhysicalDamageDoneIncrease },
			{ "PhysicalDamageDoneByAbilityIncrease", AttributeType::PhysicalDamageDoneByAbilityIncrease },
			{ "EnergyDamageDoneIncrease", AttributeType::EnergyDamageDoneIncrease },
			{ "EnergyDamageDoneByAbilityIncrease", AttributeType::EnergyDamageDoneByAbilityIncrease },
			{ "ChannelTimeDecrease", AttributeType::ChannelTimeDecrease },
			{ "CrowdControlDurationDecrease", AttributeType::CrowdControlDurationDecrease },
			{ "DoTDurationDecrease", AttributeType::DoTDurationDecrease },
			{ "AoEDurationIncrease", AttributeType::AoEDurationIncrease },
			{ "HealIncrease", AttributeType::HealIncrease },
			{ "OnLockdown", AttributeType::OnLockdown },
			{ "HoTDoneIncrease", AttributeType::HoTDoneIncrease },
			{ "ProjectileDamageIncrease", AttributeType::ProjectileDamageIncrease },
			{ "DeployBonusInvincibilityTime", AttributeType::DeployBonusInvincibilityTime },
			{ "PhysicalDamageDecreaseFlat", AttributeType::PhysicalDamageDecreaseFlat },
			{ "EnergyDamageDecreaseFlat", AttributeType::EnergyDamageDecreaseFlat },
			{ "MinWeaponDamage", AttributeType::MinWeaponDamage },
			{ "MaxWeaponDamage", AttributeType::MaxWeaponDamage },
			{ "MinWeaponDamagePercent", AttributeType::MinWeaponDamagePercent },
			{ "MaxWeaponDamagePercent", AttributeType::MaxWeaponDamagePercent },
			{ "DirectAttackDamage", AttributeType::DirectAttackDamage },
			{ "DirectAttackDamagePercent", AttributeType::DirectAttackDamagePercent },
			{ "GetHitAnimDisabled", AttributeType::GetHitAnimDisabled },
			{ "XPBoost", AttributeType::XPBoost },
			{ "InvisibleToSecurityTeleporters", AttributeType::InvisibleToSecurityTeleporters },
			{ "BodyScale", AttributeType::BodyScale }
		});

		// NounType
		mState.new_enum<NounType>("NounType", {
			{ "None", NounType::None },
			{ "Creature", NounType::Creature },
			{ "Vehicle", NounType::Vehicle },
			{ "Obstacle", NounType::Obstacle },
			{ "SpawnPoint", NounType::SpawnPoint },
			{ "PathPoint", NounType::PathPoint },
			{ "Trigger", NounType::Trigger },
			{ "PointLight", NounType::PointLight },
			{ "SpotLight", NounType::SpotLight },
			{ "LineLight", NounType::LineLight },
			{ "ParallelLight", NounType::ParallelLight },
			{ "HemisphereLight", NounType::HemisphereLight },
			{ "Animator", NounType::Animator },
			{ "Animated", NounType::Animated },
			{ "GraphicsControl", NounType::GraphicsControl },
			{ "Material", NounType::Material },
			{ "Flora", NounType::Flora },
			{ "LevelshopObject", NounType::LevelshopObject },
			{ "Terrain", NounType::Terrain },
			{ "Weapon", NounType::Weapon },
			{ "Building", NounType::Building },
			{ "Handle", NounType::Handle },
			{ "HealthOrb", NounType::HealthOrb },
			{ "ManaOrb", NounType::ManaOrb },
			{ "ResurrectOrb", NounType::ResurrectOrb },
			{ "Movie", NounType::Movie },
			{ "Loot", NounType::Loot },
			{ "PlacableEffect", NounType::PlacableEffect },
			{ "LuaJob", NounType::LuaJob },
			{ "AbilityObject", NounType::AbilityObject },
			{ "LevelExitPoint", NounType::LevelExitPoint },
			{ "Decal", NounType::Decal },
			{ "Water", NounType::Water },
			{ "Grass", NounType::Grass },
			{ "Door", NounType::Door },
			{ "Crystal", NounType::Crystal },
			{ "Interactable", NounType::Interactable },
			{ "Projectile", NounType::Projectile },
			{ "DestructibleOrnament", NounType::DestructibleOrnament },
			{ "MapCamera", NounType::MapCamera },
			{ "Occluder", NounType::Occluder },
			{ "SplineCamera", NounType::SplineCamera },
			{ "SplineCameraNode", NounType::SplineCameraNode },
			{ "BossPortal", NounType::BossPortal }
		});

		// ClientEventID
		mState.new_enum<ClientEventID>("ClientEventID", {
			{ "AbilityRequiresTarget", ClientEventID::AbilityRequiresTarget },
			{ "AbilityNotReady", ClientEventID::AbilityNotReady },
			{ "SquadAbilityUnavailable", ClientEventID::SquadAbilityUnavailable },
			{ "OverdriveNotCharged", ClientEventID::OverdriveNotCharged },
			{ "OverdriveReady", ClientEventID::OverdriveReady },
			{ "PlayerReady", ClientEventID::PlayerReady },
			{ "PlayerCashOut", ClientEventID::PlayerCashOut },
			{ "PlayerDeath", ClientEventID::PlayerDeath },
			{ "PlayerFreed", ClientEventID::PlayerFreed },
			{ "PlayerLeftGame", ClientEventID::PlayerLeftGame },
			{ "PlayerTrapped", ClientEventID::PlayerTrapped },
			{ "PlayerUnlockedSecondCreature", ClientEventID::PlayerUnlockedSecondCreature },
			{ "PlayerEnteredTunnel", ClientEventID::PlayerEnteredTunnel },
			{ "PlayerExitedTunnel", ClientEventID::PlayerExitedTunnel },
			{ "GameOver", ClientEventID::GameOver },
			{ "LootRoll", ClientEventID::LootRoll },
			{ "LootPickup", ClientEventID::LootPickup },
			{ "LootPlayerDrop", ClientEventID::LootPlayerDrop },
			{ "CatalystPickup", ClientEventID::CatalystPickup },
			{ "TeleportersDeactivated", ClientEventID::TeleportersDeactivated },
			{ "SwitchUnavailable", ClientEventID::SwitchUnavailable },
			{ "InvalidTargetArea", ClientEventID::InvalidTargetArea },
			{ "CannotSwitchHeroPerished", ClientEventID::CannotSwitchHeroPerished },
			{ "WarningFinalHero", ClientEventID::WarningFinalHero },
			{ "SwitchOnCooldown", ClientEventID::SwitchOnCooldown },
			{ "AllyReconnected", ClientEventID::AllyReconnected },
			{ "AllyDisconnected", ClientEventID::AllyDisconnected },
			{ "NotEnoughMana", ClientEventID::NotEnoughMana },
			{ "HealthLow", ClientEventID::HealthLow },
			{ "ManaLow", ClientEventID::ManaLow },
			{ "HordeIncoming", ClientEventID::HordeIncoming },
			{ "HordeDefeated", ClientEventID::HordeDefeated }
		});

		// StealthType
		mState.new_enum<StealthType>("StealthType", {
			{ "None", StealthType::None },
			{ "Technology", StealthType::Technology },
			{ "Supernatural", StealthType::Supernatural },
			{ "FullyInvisible", StealthType::FullyInvisible }
		});

		// DamageSource
		mState.new_enum<DamageSource>("DamageSource", {
			{ "Physical", DamageSource::Physical },
			{ "Energy", DamageSource::Energy }
		});

		// DamageType
		mState.new_enum<DamageType>("DamageType", {
			{ "Technology", DamageType::Technology },
			{ "Spacetime", DamageType::Spacetime },
			{ "Life", DamageType::Life },
			{ "Elements", DamageType::Elements },
			{ "Supernatural", DamageType::Supernatural },
			{ "Generic", DamageType::Generic }
		});

		// Descriptor
		mState.new_enum<Descriptors>("Descriptors", {
			{ "IsMelee", Descriptors::IsMelee },
			{ "IsBasic", Descriptors::IsBasic },
			{ "IsDoT", Descriptors::IsDoT },
			{ "IsAoE", Descriptors::IsAoE },
			{ "IsBuff", Descriptors::IsBuff },
			{ "IsDebuff", Descriptors::IsDebuff },
			{ "IsPhysicalDamage", Descriptors::IsPhysicalDamage },
			{ "IsEnergyDamage", Descriptors::IsEnergyDamage },
			{ "IsCosmetic", Descriptors::IsCosmetic },
			{ "IsHaste", Descriptors::IsHaste },
			{ "IsChannel", Descriptors::IsChannel },
			{ "HitReactNone", Descriptors::HitReactNone },
			{ "IsHoT", Descriptors::IsHoT },
			{ "IsProjectile", Descriptors::IsProjectile },
			{ "IgnorePlayerCount", Descriptors::IgnorePlayerCount },
			{ "IgnoreDifficulty", Descriptors::IgnoreDifficulty },
			{ "IsSelfResurrect", Descriptors::IsSelfResurrect },
			{ "IsInteract", Descriptors::IsInteract },
			{ "IsThorns", Descriptors::IsThorns }
		});

		// NpcType
		mState.new_enum<NpcType>("NpcType", {
			{ "Invalid", NpcType::Invalid },
			{ "Minion", NpcType::Minion },
			{ "Special", NpcType::Special },
			{ "Boss", NpcType::Boss },
			{ "Destructible", NpcType::Destructible },
			{ "Interactable", NpcType::Interactable },
			{ "Agent", NpcType::Agent },
			{ "Victim", NpcType::Victim },
			{ "Captain", NpcType::Captain }
		});

		// DropType
		mState.new_enum<DropType>("DropType", {
			{ "None", DropType::None },
			{ "Orbs", DropType::Orbs },
			{ "Catalysts", DropType::Catalysts },
			{ "Loot", DropType::Loot },
			{ "DNA", DropType::DNA }
		});
	}

	void Lua::RegisterFunctions() {
		// Seconds covered by the current simulation step. Scripts accumulate it
		// in per-frame loops: elapsed += GetSimTimeDt(); coroutine.yield().
		mState["nTimeManager"].get_or_create<sol::table>()["GetSimTimeDt"] = [this](sol::variadic_args) -> float {
			return mGame.GetLuaFrameSeconds();
		};

		// global functions
		mState["AddEvent"] = [this](sol::this_state L, sol::protected_function callback, uint32_t delay, sol::variadic_args args) {
			if (delay > 0) {
				auto& game = mGame;
				return game.AddTask(delay, [&game, callback = std::move(callback), args = std::vector<sol::object>(args.begin(), args.end())](uint32_t id) mutable {
					game.AddServerTask([&game, id, callback = std::move(callback), args = std::move(args)]() mutable {
						game.CancelTask(id);
						callback.call<void>(sol::as_args(args));
					});
				});
			} else {
				callback.call<void>(args);
				return static_cast<uint32_t>(0);
			}
		};

		mState["StopEvent"] = [this](sol::this_state L, uint32_t id) {
			mGame.CancelTask(id);
		};

		RegisterThread();
		RegisterGame();
		RegisterObjectManager();
		RegisterLocomotion();
		RegisterPlayer();
		RegisterAttributes();
		RegisterObject();

		// The original DNA_Follow.lua invokes nGameObject.DNAPickup.
		// LuaBase creates this table, but its legacy method was missing.
		// Resolve both object handles against the active server world;
		// all ownership and amount validation occurs in PickupDNA.
		auto gameObject = mState["nGameObject"].get<sol::table>();
		gameObject["DNAPickup"] = [this](sol::object lootValue, sol::object characterValue) {
			auto& manager = mGame.GetObjectManager();
			auto loot = LuaGetObject(manager, lootValue);
			auto character = LuaGetObject(manager, characterValue);
			return mGame.PickupDNA(loot, character);
		};

		// Populate nGameObject legacy functions used across original Darkspore ability/modifier scripts
		gameObject["GetPosition"] = [this](sol::object objVal) -> std::tuple<float, float, float> {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				const auto& p = obj->GetPosition();
				return { p.x, p.y, p.z };
			}
			return { 0.f, 0.f, 0.f };
		};
		gameObject["SetPosition"] = [this](sol::object objVal, float x, float y, float z, sol::optional<bool> sendUpdate) {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				obj->SetPosition(glm::vec3(x, y, z));
				if (sendUpdate.value_or(true)) {
					mGame.SendObjectUpdate(obj);
				}
			}
		};
		gameObject["GetCenterPoint"] = [this](sol::object objVal) -> std::tuple<float, float, float> {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				const auto p = obj->GetCenterPoint();
				return { p.x, p.y, p.z };
			}
			return { 0.f, 0.f, 0.f };
		};
		gameObject["GetGroundPosition"] = [this](sol::object objVal) -> std::tuple<float, float, float> {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				const auto& p = obj->GetPosition();
				return { p.x, p.y, p.z };
			}
			return { 0.f, 0.f, 0.f };
		};
		gameObject["GetFacing"] = [this](sol::object objVal) -> std::tuple<float, float, float> {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				glm::vec3 f = obj->HasLocomotionData() ? obj->GetLocomotionData()->GetFacing() : glm::vec3(1.f, 0.f, 0.f);
				if (glm::length2(f) <= 1e-6f) {
					f = glm::vec3(1.f, 0.f, 0.f);
				}
				return { f.x, f.y, f.z };
			}
			return { 1.f, 0.f, 0.f };
		};
		gameObject["GetRightDirection"] = [this](sol::object objVal) -> std::tuple<float, float, float> {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				glm::vec3 f = obj->HasLocomotionData() ? obj->GetLocomotionData()->GetFacing() : glm::vec3(1.f, 0.f, 0.f);
				if (glm::length2(f) <= 1e-6f) {
					f = glm::vec3(1.f, 0.f, 0.f);
				} else {
					f = glm::normalize(f);
				}
				glm::vec3 r = glm::cross(f, glm::vec3(0.f, 0.f, 1.f));
				return { r.x, r.y, r.z };
			}
			return { 0.f, -1.f, 0.f };
		};
		gameObject["GetLinearVelocity"] = [this](sol::object objVal) -> std::tuple<float, float, float> {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				const auto& v = obj->GetLinearVelocity();
				return { v.x, v.y, v.z };
			}
			return { 0.f, 0.f, 0.f };
		};
		gameObject["SetLinearVelocity"] = [this](sol::object objVal, float vx, float vy, float vz) {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				obj->SetLinearVelocity(glm::vec3(vx, vy, vz));
			}
		};
		gameObject["GetOrientation"] = [this](sol::object objVal) -> std::tuple<float, float, float, float> {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				const auto& q = obj->GetOrientation();
				return { q.x, q.y, q.z, q.w };
			}
			return { 0.f, 0.f, 0.f, 1.f };
		};
		gameObject["GetBaseOrientation"] = gameObject["GetOrientation"];
		gameObject["SetOrientation"] = [this](sol::variadic_args args) {
			float q[4] {};
			for (size_t i = 0; i < 4; ++i) {
				sol::object component = LuaArg(args, i + 1);
				if (component.get_type() != sol::type::number) {
					LogUnsupportedCallForm("nGameObject.SetOrientation", args.size());
					return;
				}
				q[i] = component.as<float>();
			}
			sol::object objVal = LuaArg(args, 0);
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				obj->SetOrientation(glm::quat(q[3], q[0], q[1], q[2]));
			}
		};
		gameObject["GetObjectDirection"] = [this](sol::object fromVal, sol::object toVal) -> std::tuple<float, float, float> {
			auto fromObj = LuaGetObject(mGame.GetObjectManager(), fromVal);
			auto toObj = LuaGetObject(mGame.GetObjectManager(), toVal);
			if (fromObj && toObj) {
				glm::vec3 d = toObj->GetPosition() - fromObj->GetPosition();
				if (glm::length2(d) > 1e-6f) {
					d = glm::normalize(d);
					return { d.x, d.y, d.z };
				}
			}
			return { 1.f, 0.f, 0.f };
		};
		gameObject["GetObjectDistance"] = [this](sol::object fromVal, sol::object toVal) -> float {
			auto fromObj = LuaGetObject(mGame.GetObjectManager(), fromVal);
			auto toObj = LuaGetObject(mGame.GetObjectManager(), toVal);
			if (fromObj && toObj) {
				return glm::distance(fromObj->GetPosition(), toObj->GetPosition());
			}
			return 0.f;
		};
		gameObject["GetObjectAngleRad"] = [this](sol::object fromVal, sol::object toVal) -> float {
			auto fromObj = LuaGetObject(mGame.GetObjectManager(), fromVal);
			auto toObj = LuaGetObject(mGame.GetObjectManager(), toVal);
			if (fromObj && toObj) {
				glm::vec3 d = toObj->GetPosition() - fromObj->GetPosition();
				if (glm::length2(d) > 1e-6f) {
					d = glm::normalize(d);
					glm::vec3 f = fromObj->HasLocomotionData() ? fromObj->GetLocomotionData()->GetFacing() : glm::vec3(1.f, 0.f, 0.f);
					if (glm::length2(f) > 1e-6f) {
						f = glm::normalize(f);
					} else {
						f = glm::vec3(1.f, 0.f, 0.f);
					}
					return std::acos(std::clamp(glm::dot(f, d), -1.f, 1.f));
				}
			}
			return 0.f;
		};
		gameObject["GetObjectAngleDeg"] = [this](sol::object fromVal, sol::object toVal) -> float {
			auto fromObj = LuaGetObject(mGame.GetObjectManager(), fromVal);
			auto toObj = LuaGetObject(mGame.GetObjectManager(), toVal);
			if (fromObj && toObj) {
				glm::vec3 d = toObj->GetPosition() - fromObj->GetPosition();
				if (glm::length2(d) > 1e-6f) {
					d = glm::normalize(d);
					glm::vec3 f = fromObj->HasLocomotionData() ? fromObj->GetLocomotionData()->GetFacing() : glm::vec3(1.f, 0.f, 0.f);
					if (glm::length2(f) > 1e-6f) {
						f = glm::normalize(f);
					} else {
						f = glm::vec3(1.f, 0.f, 0.f);
					}
					return glm::degrees(std::acos(std::clamp(glm::dot(f, d), -1.f, 1.f)));
				}
			}
			return 0.f;
		};
		gameObject["GetSharedAbilityOffset"] = [this](sol::this_state L, sol::object objVal) -> std::tuple<float, float, float> {
			glm::vec3 offset = LuaFunction::Object::GetSharedAbilityOffset(L, objVal);
			return { offset.x, offset.y, offset.z };
		};
		gameObject["GetTeleporterDestination"] = [this](sol::this_state L, sol::object objVal) -> std::tuple<float, float, float> {
			glm::vec3 dest = LuaFunction::Object::GetTeleporterDestination(L, objVal);
			return { dest.x, dest.y, dest.z };
		};
		gameObject["GetPositionWithinRangeOfObject"] = [this](sol::object objVal, float range) -> std::tuple<float, float, float> {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				const auto& p = obj->GetPosition();
				return { p.x, p.y, p.z };
			}
			return { 0.f, 0.f, 0.f };
		};
		gameObject["SetTargetPosition"] = [this](sol::object objVal, float x, float y, float z) {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal); obj && obj->HasLocomotionData()) {
				obj->GetLocomotionData()->SetTargetPosition(glm::vec3(x, y, z));
			}
		};
		// First argument: the caster's team number (game scripts, from
		// nGameObject.GetTeam) or a caster object. A number is never an object id here.
		const auto casterTeam = [this](sol::object value) -> uint32_t {
			if (value.get_type() == sol::type::number) {
				return value.as<uint32_t>();
			}
			if (auto caster = LuaGetObject(mGame.GetObjectManager(), value)) {
				return caster->GetTeam();
			}
			return 0u;
		};
		gameObject["ValidateHostileTarget"] = [this, casterTeam](sol::object teamOrAgent, sol::object targetVal, sol::variadic_args) -> bool {
			auto target = LuaGetObject(mGame.GetObjectManager(), targetVal);
			if (!target || target->IsMarkedForDeletion() || !target->HasCombatantData() || target->GetHealth() <= 0.f || !target->IsTargetable()) {
				return false;
			}
			if (teamOrAgent.get_type() != sol::type::number) {
				if (auto agent = LuaGetObject(mGame.GetObjectManager(), teamOrAgent); agent && agent == target) {
					return false;
				}
			}
			return TeamValidation::IsHostile(casterTeam(teamOrAgent), target->GetTeam());
		};
		gameObject["ValidateFriendlyTarget"] = [this, casterTeam](sol::object teamOrAgent, sol::object targetVal, sol::variadic_args) -> bool {
			auto target = LuaGetObject(mGame.GetObjectManager(), targetVal);
			if (!target || target->IsMarkedForDeletion() || !target->HasCombatantData() || target->GetHealth() <= 0.f) {
				return false;
			}
			return TeamValidation::IsFriendly(casterTeam(teamOrAgent), target->GetTeam());
		};
		// Game scripts call TakeDamage(snapshot, target, damage, ...) and
		// HealDamage(snapshot, target, heal, ...); see game/combat_call.h.
		// The attacker is the snapshot's owner object.
		const auto snapshotOwner = [](sol::this_state L, const sol::object& snapshot) -> sol::object {
			if (snapshot.is<Game::AttributesPtr>()) {
				if (const auto& attributes = snapshot.as<Game::AttributesPtr>()) {
					if (const auto owner = attributes->GetOwnerObject()) {
						return sol::make_object(L, owner);
					}
				}
			}
			return sol::make_object(L, sol::lua_nil);
		};
		gameObject["TakeDamage"] = [this, snapshotOwner](sol::this_state L, sol::variadic_args args) {
			std::string error;
			const auto call = CombatCall::ParseTakeDamage(args, error);
			if (!call) {
				LogError(L, "nGameObject.TakeDamage", error);
				return std::make_tuple(false, 0.f, false);
			}
			sol::state_view state(L);
			sol::table range = state.create_table_with(1, call->range.min, 2, call->range.max);
			sol::optional<glm::vec3> direction;
			if (call->direction) {
				direction = glm::vec3((*call->direction)[0], (*call->direction)[1], (*call->direction)[2]);
			}
			sol::optional<float> multiplier;
			if (call->multiplier) {
				multiplier = *call->multiplier;
			}
			return LuaFunction::Object::TakeDamage(
				L,
				call->target,
				snapshotOwner(L, call->snapshot),
				call->snapshot,
				range,
				static_cast<Game::DamageType>(call->damageType),
				static_cast<Game::DamageSource>(call->damageSource),
				call->coefficient,
				static_cast<Game::Descriptors>(call->descriptors),
				multiplier,
				direction
			);
		};
		gameObject["HealDamage"] = [this, snapshotOwner](sol::this_state L, sol::variadic_args args) {
			std::string error;
			const auto call = CombatCall::ParseHealDamage(args, error);
			if (!call) {
				LogError(L, "nGameObject.HealDamage", error);
				return std::make_tuple(0.f, false);
			}
			sol::state_view state(L);
			sol::table range = state.create_table_with(1, call->range.min, 2, call->range.max);
			sol::optional<Game::Descriptors> descriptors;
			if (call->descriptors) {
				descriptors = static_cast<Game::Descriptors>(*call->descriptors);
			}
			sol::optional<bool> critical;
			if (call->critical) {
				critical = *call->critical;
			}
			sol::optional<bool> canResurrect;
			if (call->canResurrect) {
				canResurrect = *call->canResurrect;
			}
			return LuaFunction::Object::Heal(L, call->target, snapshotOwner(L, call->snapshot), call->snapshot,
				range, call->coefficient, descriptors, critical, canResurrect);
		};
		gameObject["FullHealObject"] = [this](sol::object objVal) {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				obj->SetHealth(obj->GetMaxHealth());
				obj->SetMana(obj->GetMaxMana());
			}
		};
		gameObject["KillObject"] = [this](sol::object objVal, sol::optional<sol::object> killerVal) {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				const float prevHp = obj->GetHealth();
				obj->SetHealth(0.f);
				Game::ObjectPtr killer;
				if (killerVal.has_value()) {
					killer = LuaGetObject(mGame.GetObjectManager(), *killerVal);
				}
				if (prevHp > 0.f) {
					mGame.OnDamageDealt(killer, obj, prevHp);
				}
			}
		};
		gameObject["IsAlive"] = [this](sol::object objVal) -> bool {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				return !obj->IsMarkedForDeletion() && (!obj->HasCombatantData() || obj->GetHealth() > 0.f);
			}
			return false;
		};
		gameObject["IsCombatant"] = &LuaFunction::Object::IsCombatant;
		gameObject["IsInCombat"] = [this](sol::object objVal) -> bool {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				return obj->IsInCombat();
			}
			return false;
		};
		gameObject["SetInCombat"] = [this](sol::object objVal, bool val) {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				obj->SetInCombat(val);
			}
		};
		gameObject["GetIsTargetable"] = [this](sol::object objVal) -> bool {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				return obj->IsTargetable();
			}
			return false;
		};
		gameObject["SetIsTargetable"] = [this](sol::object objVal, bool val) {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				obj->SetTargetable(val);
			}
		};
		gameObject["GetIsStealthed"] = &LuaFunction::Object::IsStealthed;
		gameObject["SetStealthType"] = [this](sol::object objVal, uint32_t stealthType) {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				obj->SetStealthType(static_cast<Game::StealthType>(stealthType));
			}
		};
		gameObject["SetIsVisible"] = [this](sol::object objVal, bool val) {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				obj->SetVisible(val);
			}
		};
		gameObject["IsMarkedForDelete"] = [this](sol::object objVal) -> bool {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				return obj->IsMarkedForDeletion();
			}
			return true;
		};
		gameObject["MarkForDelete"] = [this](sol::object objVal) {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				obj->MarkForDeletion();
			}
		};
		gameObject["GetTeam"] = [this](sol::object objVal) -> uint8_t {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				return obj->GetTeam();
			}
			return 0;
		};
		gameObject["SetTeam"] = [this](sol::object objVal, uint8_t team) {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				obj->SetTeam(team);
			}
		};
		gameObject["GetType"] = [this](sol::object objVal) -> uint32_t {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				return static_cast<uint32_t>(obj->GetType());
			}
			return 0;
		};
		gameObject["GetNPCType"] = [this](sol::object objVal) -> uint32_t {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				return static_cast<uint32_t>(obj->GetNpcType());
			}
			return static_cast<uint32_t>(NpcType::Invalid);
		};
		gameObject["GetNPCRank"] = [this](sol::object objVal) -> uint32_t { return 1; };
		gameObject["GetFootprintRadius"] = &LuaFunction::Object::GetFootprintRadius;
		gameObject["GetScale"] = [this](sol::object objVal) -> float {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				return obj->GetScale();
			}
			return 1.f;
		};
		gameObject["SetScale"] = [this](sol::object objVal, float scale) {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				obj->SetScale(scale);
			}
		};
		gameObject["GetGraphicsScale"] = gameObject["GetScale"];
		gameObject["GetHitPoints"] = [this](sol::object objVal) -> float {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				return obj->GetHealth();
			}
			return 0.f;
		};
		gameObject["GetMaxHitPoints"] = [this](sol::object objVal) -> float {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				return obj->GetMaxHealth();
			}
			return 100.f;
		};
		gameObject["GetManaPoints"] = [this](sol::object objVal) -> float {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				return obj->GetMana();
			}
			return 0.f;
		};
		gameObject["GetMaxManaPoints"] = [this](sol::object objVal) -> float {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				return obj->GetMaxMana();
			}
			return 100.f;
		};
		gameObject["SetManaPoints"] = [this](sol::object objVal, float mana) {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				obj->SetMana(mana);
			}
		};
		gameObject["GetOwnerID"] = [this](sol::object objVal) -> uint32_t {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				if (const auto& owner = obj->GetOwnerObject()) {
					return owner->GetId();
				}
			}
			return 0;
		};
		gameObject["SetOwnerID"] = [this](sol::object objVal, sol::object ownerVal) {
			auto obj = LuaGetObject(mGame.GetObjectManager(), objVal);
			auto owner = LuaGetObject(mGame.GetObjectManager(), ownerVal);
			if (obj) {
				obj->SetOwnerObject(owner);
			}
		};
		gameObject["GetTargetID"] = [this](sol::object objVal) -> uint32_t {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				return obj->GetTargetId();
			}
			return 0;
		};
		gameObject["SetTargetID"] = [this](sol::object objVal, uint32_t targetId) {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				obj->SetTargetId(targetId);
			}
		};
		gameObject["GetMarkerID"] = [this](sol::object objVal) -> uint32_t {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				return obj->GetMarkerId();
			}
			return 0;
		};
		gameObject["GetAssetID"] = [this](sol::object objVal) -> uint32_t {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				return obj->GetAssetId();
			}
			return 0;
		};
		gameObject["GetAssetNameWithType"] = &LuaFunction::Object::GetName;
		gameObject["GetAttributeSnapshot"] = &LuaFunction::Object::GetAttributeSnapshot;
		gameObject["SetAttributeSnapshot"] = &LuaFunction::Object::SetAttributeSnapshot;
		gameObject["GetWeaponDamage"] = &LuaFunction::Object::GetWeaponDamage;
		gameObject["SetAnimationState"] = &LuaFunction::Object::SetAnimationState;
		gameObject["SetOverlayAnimationState"] = [this](sol::this_state L, sol::object objVal, uint32_t animState, sol::optional<float> scale) {
			LuaFunction::Object::SetAnimationState(L, objVal, animState, true, scale);
		};
		gameObject["SetOverrideMoveAnimationState"] = [](sol::variadic_args) {};
		gameObject["ResetOverrideMoveAnimationState"] = [](sol::variadic_args) {};
		gameObject["ResetAnimationState"] = [](sol::variadic_args) {};
		gameObject["SetAnimationStateToDeath"] = &LuaFunction::Object::SetAnimationStateToDeath;
		gameObject["SetAnimationStateToAggro"] = &LuaFunction::Object::SetAnimationStateToAggro;
		gameObject["SetAnimationStateToEnterPassiveIdle"] = [](sol::variadic_args) { return 0.f; };
		gameObject["SetAnimationStateToPreAggroIdle"] = [](sol::variadic_args) { return 0.f; };
		gameObject["GetDanceEmoteAnimationState"] = [](sol::variadic_args) { return 0u; };
		gameObject["SetGraphicsState"] = &LuaFunction::Object::SetGraphicsState;
		gameObject["AddEffect"] = [this](sol::this_state L, sol::object objVal, sol::object effectDef, sol::optional<sol::object> secObj, sol::optional<float> tx, sol::optional<float> ty, sol::optional<float> tz) {
			sol::optional<glm::vec3> tp;
			if (tx.has_value()) {
				tp = glm::vec3(tx.value_or(0.f), ty.value_or(0.f), tz.value_or(0.f));
			}
			return LuaFunction::Object::AddEffect(L, objVal, effectDef, secObj, tp);
		};
		gameObject["RemoveEffect"] = &LuaFunction::Object::RemoveEffect;
		gameObject["RemoveEffectIndex"] = &LuaFunction::Object::RemoveEffectIndex;
		gameObject["DropLoot"] = [this](sol::this_state L, sol::object objVal, sol::object targetVal, uint32_t dropType) {
			LuaFunction::Object::DropLoot(L, objVal, targetVal, static_cast<Game::DropType>(dropType));
		};
		gameObject["DropStuffForObject"] = [](sol::variadic_args) {};
		gameObject["DropObject"] = [](sol::variadic_args) {};
		gameObject["PickupObject"] = [this](sol::object lootVal, sol::object charVal) {
			auto& manager = mGame.GetObjectManager();
			auto loot = LuaGetObject(manager, lootVal);
			auto character = LuaGetObject(manager, charVal);
			if (loot && character && character->IsPlayerControlled()) {
				const auto& player = character->GetPlayer();
				if (player && player->GetDeployedCharacterObject() == character) {
					// The public interaction entry point performs loot validation
					// and delegates to the private, transactional pickup method.
					mGame.InteractWithObject(player, loot->GetId());
				}
			}
		};
		gameObject["HasInteractableUsesLeft"] = [this](sol::object objVal) -> bool {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				return obj->HasInteractableUsesLeft();
			}
			return false;
		};
		gameObject["IncrementNumTimesUsed"] = &LuaFunction::Object::IncrementNumTimesUsed;
		gameObject["CompareIDs"] = [this](sol::object a, sol::object b) -> bool {
			auto oa = LuaGetObject(mGame.GetObjectManager(), a);
			auto ob = LuaGetObject(mGame.GetObjectManager(), b);
			if (oa && ob) {
				return oa->GetId() == ob->GetId();
			}
			if (a.is<uint32_t>() && b.is<uint32_t>()) {
				return a.as<uint32_t>() == b.as<uint32_t>();
			}
			return false;
		};
		gameObject["GetCurrentSpeed"] = [this](sol::object objVal) -> float {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				return obj->GetCurrentSpeed();
			}
			return 0.f;
		};
		gameObject["GetModifiedMoveSpeed"] = [this](sol::object objVal) -> float {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				return obj->GetModifiedMovementSpeed();
			}
			return 0.f;
		};
		gameObject["SetNavCollision"] = [](sol::variadic_args) {};
		gameObject["SetGravityForce"] = [](sol::variadic_args) {};
		gameObject["SetInitialDirection"] = [](sol::variadic_args) {};
		gameObject["AddAggroForObject"] = [](sol::variadic_args) {};
		gameObject["GetAggroForObject"] = [](sol::variadic_args) { return 0.f; };
		gameObject["GetAggroDelegate"] = [](sol::variadic_args) { return 0u; };
		gameObject["SetAggroDelegate"] = [](sol::variadic_args) {};
		gameObject["AlertObject"] = [](sol::variadic_args) {};
		gameObject["IgnoreFirstAggro"] = [](sol::variadic_args) {};
		gameObject["OverrideFirstAggro"] = [](sol::variadic_args) {};
		gameObject["AddDependency"] = [](sol::variadic_args) {};
		gameObject["GetTriggerOwner"] = [this](sol::object trigVal) -> uint32_t {
			if (auto trig = LuaGetTrigger(mGame.GetObjectManager(), trigVal)) {
				if (const auto& owner = trig->GetOwnerObject()) {
					return owner->GetId();
				}
			}
			return 0;
		};
		gameObject["HasAbilityWithDescriptor"] = [](sol::variadic_args) { return false; };
		gameObject["IntersectsLine"] = [](sol::variadic_args) { return false; };
		gameObject["IsChanneling"] = [](sol::variadic_args) { return false; };
		gameObject["IsCorpseFadingAway"] = [](sol::variadic_args) { return false; };
		gameObject["SetCorpseFadingAway"] = [](sol::variadic_args) {};
		gameObject["SetDeathTimer"] = [](sol::variadic_args) {};
		gameObject["IsDNALoot"] = [this](sol::object objVal) -> bool {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal); obj && obj->HasLootData()) {
				return obj->GetLootData()->GetDNAAmount() > 0.f;
			}
			return false;
		};
		gameObject["IsPartLoot"] = [this](sol::object objVal) -> bool {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal); obj && obj->HasLootData()) {
				return obj->GetLootData()->GetRigblockAsset() != 0;
			}
			return false;
		};
		gameObject["IsModifierActive"] = [](sol::variadic_args) { return false; };
		gameObject["IsSameSpecies"] = [](sol::variadic_args) { return false; };
		gameObject["IsStimulusActive"] = [](sol::variadic_args) { return false; };
		gameObject["IsVaporized"] = [](sol::variadic_args) { return false; };
		gameObject["MakeElite"] = [](sol::variadic_args) {};
		gameObject["MarkCantDropLoot"] = [](sol::variadic_args) {};
		gameObject["MarkNotWorthXP"] = [](sol::variadic_args) {};
		gameObject["MirrorBaseAttributes"] = [](sol::variadic_args) {};
		gameObject["PressSwitch"] = [](sol::variadic_args) {};
		gameObject["ToggleDoor"] = [](sol::variadic_args) {};
		gameObject["TriggerFalseCollision"] = [](sol::variadic_args) {};
		gameObject["UndeployPlayerObject"] = [](sol::variadic_args) {};
		gameObject["GetHitDescriptors"] = [](sol::variadic_args) { return 0u; };
		gameObject["GetHitDirection"] = [](sol::variadic_args) -> std::tuple<float, float, float> { return { 1.f, 0.f, 0.f }; };
		gameObject["GetRecentDamage"] = [](sol::variadic_args) { return 0.f; };
		gameObject["GetCreatureType"] = [](sol::variadic_args) { return 0u; };

		// nEvent
		{
			auto nEvent = mState["nEvent"].get_or_create<sol::table>();
			nEvent["Notify"] = [this](sol::table serverEventTable) {
				auto serverEvent = LuaGetServerEvent(mGame.GetObjectManager(), serverEventTable);
				mGame.SendServerEvent(serverEvent);
			};
			nEvent["NotifyPlayer"] = [this](sol::object playerVal, sol::table serverEventTable) {
				auto player = LuaGetPlayer(mGame, playerVal);
				if (player) {
					auto serverEvent = LuaGetServerEvent(mGame.GetObjectManager(), serverEventTable);
					mGame.SendServerEvent(player, serverEvent);
				}
			};
			nEvent["NotifyOptionalInteractibleEvent"] = [](sol::variadic_args) {};
		}

		// nAttribute
		{
			auto nAttribute = mState["nAttribute"].get_or_create<sol::table>();
			// Scripts pass the game's attribute numbers (nAttributeType); the
			// server stores AttributeType values (game/attribute_layout.h).
			nAttribute["GetAttributeValue"] = [this](sol::object objOrSnap, uint8_t scriptIdx) -> float {
				const auto attrIdx = Game::AttributeLayout::Active().ToServer(scriptIdx);
				if (attrIdx == Game::AttributeLayout::None) {
					return 0.f;
				}
				if (objOrSnap.is<Game::AttributesPtr>()) {
					if (const auto& attr = objOrSnap.as<Game::AttributesPtr>()) {
						return attr->GetValue(attrIdx);
					}
				}
				if (auto obj = LuaGetObject(mGame.GetObjectManager(), objOrSnap)) {
					return obj->GetAttributeValue(attrIdx);
				}
				return 0.f;
			};
			nAttribute["GetAttributeValue_FromSnapshot"] = nAttribute["GetAttributeValue"];
			nAttribute["GetPrimaryAttributeValue"] = [this](sol::object objOrSnap) -> float {
				if (objOrSnap.is<Game::AttributesPtr>()) {
					if (const auto& attr = objOrSnap.as<Game::AttributesPtr>()) {
						if (const auto& owner = attr->GetOwnerObject()) {
							return owner->GetPrimaryAttributeValue();
						}
					}
				}
				if (auto obj = LuaGetObject(mGame.GetObjectManager(), objOrSnap)) {
					return obj->GetPrimaryAttributeValue();
				}
				return 0.f;
			};
			nAttribute["CreateAttributeSnapshot_FromObject"] = &LuaFunction::Object::GetAttributeSnapshot;
			// handle = AddAttributeModifier(object, attributeType, value);
			// RemoveAttributeModifier(handle) undoes exactly that change.
			nAttribute["AddAttributeModifier"] = [this](sol::object objVal, uint8_t scriptIdx, float delta) -> uint32_t {
				const auto attrIdx = Game::AttributeLayout::Active().ToServer(scriptIdx);
				if (attrIdx == Game::AttributeLayout::None) {
					return 0u; // no such attribute: no handle, nothing to undo
				}
				if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
					obj->SetAttributeValue(attrIdx, obj->GetAttributeValue(attrIdx) + delta);
					return mAttributeModifiers.Add(obj->GetId(), attrIdx, delta);
				}
				return 0u;
			};
			nAttribute["RemoveAttributeModifier"] = [this](sol::variadic_args args) {
				if (args.size() >= 3) {
					// Explicit (object, attributeType, value) form.
					sol::object objVal = LuaArg(args, 0);
					sol::object attr = LuaArg(args, 1);
					sol::object delta = LuaArg(args, 2);
					if (attr.get_type() == sol::type::number && delta.get_type() == sol::type::number) {
						if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
							const auto attrIdx = Game::AttributeLayout::Active().ToServer(attr.as<uint8_t>());
							if (attrIdx != Game::AttributeLayout::None) {
								obj->SetAttributeValue(attrIdx, obj->GetAttributeValue(attrIdx) - delta.as<float>());
							}
						}
					}
					return;
				}
				sol::object handle = LuaArg(args, 0);
				if (handle.get_type() != sol::type::number) {
					return; // nil handle: nothing was applied
				}
				if (const auto entry = mAttributeModifiers.Remove(handle.as<uint32_t>())) {
					if (auto obj = mGame.GetObjectManager().Get(entry->objectId)) {
						obj->SetAttributeValue(entry->attribute, obj->GetAttributeValue(entry->attribute) - entry->delta);
					}
				}
			};
			nAttribute["GetHealthMultiplierForDifficulty"] = []() -> float { return 1.0f; };
		}

		// nThreadData
		{
			auto nThreadData = mState["nThreadData"].get_or_create<sol::table>();
			// CreatePrivateTable() / GetPrivateTable(): storage private to the
			// running script thread (game form, no arguments). With an object or
			// id argument the table is shared per object (server form).
			const auto privateTable = [this](sol::this_state L, sol::variadic_args args, bool create) -> sol::object {
				sol::object objVal = LuaArg(args, 0);
				if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
					return sol::make_object(L, CreatePrivateTable(obj->GetId()));
				}
				if (objVal.get_type() == sol::type::number) {
					return sol::make_object(L, CreatePrivateTable(objVal.as<uint32_t>()));
				}
				auto thread = GetThread(L);
				if (!thread) {
					return sol::make_object(L, CreatePrivateTable(0));
				}
				sol::object existing = thread->get_value("__private_table");
				if (existing.get_type() == sol::type::table || !create) {
					return existing;
				}
				sol::table table = sol::state_view(L).create_table();
				thread->set_value("__private_table", sol::make_object(L, table));
				return sol::make_object(L, table);
			};
			nThreadData["CreatePrivateTable"] = [privateTable](sol::this_state L, sol::variadic_args args) { return privateTable(L, args, true); };
			nThreadData["GetPrivateTable"] = [privateTable](sol::this_state L, sol::variadic_args args) { return privateTable(L, args, false); };
			nThreadData["GetFloat"] = [this](sol::this_state L, sol::object key) -> float {
				if (auto thread = GetThread(L)) {
					sol::object v = thread->get_value(ThreadDataKey(key));
					if (v.get_type() == sol::type::number) { return v.as<float>(); }
				}
				return 0.f;
			};
			nThreadData["SetFloat"] = [this](sol::this_state L, sol::object key, float val) {
				if (auto thread = GetThread(L)) {
					thread->set_value(ThreadDataKey(key), sol::make_object(L, val));
				}
			};
			nThreadData["GetInt"] = [this](sol::this_state L, sol::object key) -> int32_t {
				if (auto thread = GetThread(L)) {
					sol::object v = thread->get_value(ThreadDataKey(key));
					if (v.get_type() == sol::type::number) { return v.as<int32_t>(); }
				}
				return 0;
			};
			nThreadData["SetInt"] = [this](sol::this_state L, sol::object key, int32_t val) {
				if (auto thread = GetThread(L)) {
					thread->set_value(ThreadDataKey(key), sol::make_object(L, val));
				}
			};
			nThreadData["GetGUID"] = [this](sol::this_state L, sol::object key) -> uint32_t {
				if (auto thread = GetThread(L)) {
					sol::object v = thread->get_value(ThreadDataKey(key));
					if (v.get_type() == sol::type::number) { return v.as<uint32_t>(); }
				}
				return 0u;
			};
			nThreadData["SetGUID"] = [this](sol::this_state L, sol::object key, uint32_t val) {
				if (auto thread = GetThread(L)) {
					thread->set_value(ThreadDataKey(key), sol::make_object(L, val));
				}
			};
		}

		// nModifier
		{
			auto nModifier = mState["nModifier"].get_or_create<sol::table>();
			nModifier["PreloadAsset"] = [](sol::variadic_args args) -> uint32_t {
			// Asset/modifier id: the resource hash of the name (a number passes through).
			return args.size() > 0 ? LuaExtractAssetOrGuid(sol::object(args[0]), 0) : 0u;
		};
			nModifier["RegisterModifier"] = [](sol::variadic_args) {};
			nModifier["RequestModifier"] = [](sol::variadic_args) { return 0u; };
			nModifier["MarkForDelete"] = [](sol::variadic_args) {};
			nModifier["GetFirstModifierByGUID"] = [](sol::variadic_args) { return sol::lua_nil; };
			nModifier["AgentHasModifierMatchingGUID"] = [](sol::variadic_args) { return false; };
			nModifier["GetModifiersMatchingDescriptor"] = [this](sol::variadic_args) { return mState.create_table(); };
			nModifier["CheckForModifierDebuffDescriptor"] = [](sol::variadic_args) { return false; };
			nModifier["CallFunctionInContext"] = [](sol::variadic_args) {};
			nModifier["GetFloatProperty"] = [](sol::variadic_args) { return 0.f; };
			nModifier["GetInitiatorAttributeSnapshot"] = [](sol::variadic_args) { return Game::AttributesPtr{}; };
			nModifier["GetInitiatorID"] = [](sol::variadic_args) { return 0u; };
			nModifier["GetModifierGUID"] = [](sol::variadic_args) { return 0u; };
			nModifier["GetModifierInstanceID"] = [](sol::variadic_args) { return 0u; };
			nModifier["GetMyAgentID"] = [](sol::variadic_args) { return 0u; };
			nModifier["GetMyInitiatorID"] = [](sol::variadic_args) { return 0u; };
			nModifier["GetMyStackCount"] = [](sol::variadic_args) { return 1; };
			nModifier["GetRank"] = [](sol::variadic_args) { return 1; };
			nModifier["GetRemainingDuration"] = [](sol::variadic_args) { return 0.f; };
			nModifier["GetStackCount"] = [](sol::variadic_args) { return 1; };
			nModifier["IncrementStackCount"] = [](sol::variadic_args) {};
			nModifier["SetStackCount"] = [](sol::variadic_args) {};
			nModifier["IsModifierRunning"] = [](sol::variadic_args) { return false; };
			nModifier["ResetDuration"] = [](sol::variadic_args) {};
		}

		// nGameSimulator & nGameDirector
		{
			auto nGameSim = mState["nGameSimulator"].get_or_create<sol::table>();
			nGameSim["GetDifficulty"] = []() { return 1; };
			nGameSim["GetMajorDifficulty"] = []() { return 1; };
			nGameSim["GetMinorDifficulty"] = []() { return 1; };
			nGameSim["GetGameTime"] = [this]() -> float { return static_cast<float>(mGame.GetTime()) / 1000.f; };
			nGameSim["GetGameObjectiveCompletionTime"] = []() { return 0.f; };
			nGameSim["IsChainGame"] = []() { return true; };
			nGameSim["IsGameSidekicking"] = []() { return false; };
			nGameSim["LevelComplete"] = [this](sol::optional<bool> victory) {
				if (!victory.value_or(true)) {
					return;
				}
				// Use the existing guarded, idempotent completion pipeline.
				// Client-side mission flow still requires real-game verification.
				for (uint8_t index = 0; index < 4; ++index) {
					if (auto player = mGame.GetPlayerByIndex(index)) {
						mGame.BeamOut(player);
					}
				}
			};
			nGameSim["SendActionCancelMessage"] = [](sol::variadic_args) {};
			nGameSim["SetFirstLootPickedUp"] = [](sol::variadic_args) {};
			nGameSim["StartCinematic"] = [](sol::variadic_args) {};

			auto nGameDir = mState["nGameDirector"].get_or_create<sol::table>();
			nGameDir["ActivateHordeSpawn"] = [](sol::variadic_args) {};
			nGameDir["ActivateLieutenantSpawn"] = [](sol::variadic_args) {};
			nGameDir["ActivateMinionSpawn"] = [](sol::variadic_args) {};
			nGameDir["GetKillPercent"] = []() { return 0.f; };
			nGameDir["IsHordeActive"] = []() { return false; };
			nGameDir["SetBossId"] = [](sol::variadic_args) {};
		}

		RegisterAbility();
		RegisterObjective();
		RegisterTriggerVolume();
	}

	void Lua::RegisterThread() {
		auto thread = mState.create_named_table("nThread");

		// nThread.Create(owner, callback, ...)
		thread["Create"] = [this](sol::this_state L, sol::object objectValue, sol::protected_function callback, sol::variadic_args args) {
			LuaThread* luaThread;

			auto& objectManager = mGame.GetObjectManager();
			auto object = LuaGetObject(objectManager, objectValue);
			if (object) {
				luaThread = object->GetLuaThread();
				if (luaThread && luaThread->status() != sol::thread_status::dead) {
					luaThread = SpawnThread();
				} else {
					luaThread = SpawnThread();
					luaThread->set_owner_object(object);
					object->SetLuaThread(luaThread);
				}
			} else {
				luaThread = SpawnThread();
			}

			luaThread->call<void>(callback, args);
		};
		thread["CreateThreadForObject"] = thread["Create"];
		thread["GetCurrentThreadID"] = [this](sol::this_state L) -> uint32_t {
			auto t = GetThread(L);
			return t ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(t) & 0xFFFFFFFFu) : 0u;
		};
		thread["GetDurationElapsed"] = [](sol::variadic_args) -> float { return 0.f; };
		thread["WakeUp"] = [](sol::variadic_args) {};

		thread["GetValue"] = [this](sol::this_state L, std::string key) {
			auto thread = GetThread(L);
			return (thread != nullptr) ? thread->get_value(key) : sol::make_object(L, sol::lua_nil);
		};

		thread["SetValue"] = [this](sol::this_state L, std::string key, sol::object value) {
			auto thread = GetThread(L);
			if (thread) {
				thread->set_value(key, std::move(value));
			}
		};

		thread["Sleep"] = sol::yielding([this](sol::this_state L, int32_t milliseconds) {
			auto thread = GetThread(L);
			if (!thread) {
				return;
			}

			if (milliseconds > 0) {
				uint64_t time = utils::get_milliseconds() + milliseconds;
				thread->set_resume_condition([time](lua_State* L, sol::variadic_results& results) { return utils::get_milliseconds() >= time; });
			}

			YieldThread(thread);
		});

		thread["WaitForCallback"] = sol::yielding([this](sol::this_state L, sol::function callback) {
			auto thread = GetThread(L);
			if (!thread) {
				return;
			}

			thread->set_resume_condition([callback](lua_State* L, sol::variadic_results& results) {
				return callback.call<bool>();
			});

			YieldThread(thread);
		});

		thread["WaitForHitpointsAbove"] = sol::yielding([this](sol::this_state L, sol::object objectValue, float value, sol::optional<float> deathTimer) {
			auto thread = GetThread(L);
			if (!thread) {
				return;
			}

			auto& objectManager = mGame.GetObjectManager();
			auto object = LuaGetObject(objectManager, objectValue);
			if (object) {
				thread->set_resume_condition([&objectManager, objectId = object->GetId(), value](lua_State* L, sol::variadic_results& results) {
					auto object = objectManager.Get(objectId);
					if (!object) {
						return true;
					}
					return object->GetHealth() > value;
				});
			}

			YieldThread(thread);
		});

		thread["WaitForXSeconds"] = sol::yielding([this](sol::this_state L, double seconds, sol::optional<sol::object> attributesSnapshotValue, sol::optional<bool> channeling) {
			auto thread = GetThread(L);
			if (!thread) {
				return;
			}

			if (seconds > 0) {
				auto ability = LuaGetAbility(L);
				if (ability && attributesSnapshotValue.has_value() && attributesSnapshotValue->is<Game::AttributesPtr>()) {
					const auto descriptors = ability->GetDescriptors();
					const auto& attributes = attributesSnapshotValue->as<Game::AttributesPtr>();

					if (attributes) {
						if (utils::enum_helper<Game::Descriptors>::test(descriptors, Game::Descriptors::IsBasic)) {
							seconds /= std::max(-0.9f, attributes->GetValue(Game::AttributeType::AttackSpeedScale)) + 1;
						}

						if (utils::enum_helper<Game::Descriptors>::test(descriptors, Game::Descriptors::IsChannel) && channeling.value_or(false)) {
							seconds *= 1.f - attributes->GetValue(Game::AttributeType::ChannelTimeDecrease);
							seconds = std::max(0.05, seconds);
						}
					}
				}
			}

			if (seconds > 0) {
				uint64_t time = utils::get_milliseconds() + static_cast<uint64_t>(seconds * 1000);
				thread->set_resume_condition([time](lua_State* L, sol::variadic_results& results) { return utils::get_milliseconds() >= time; });
			}

			YieldThread(thread);
		});

		thread["WaitUntilTime"] = thread["WaitForXSeconds"];
		thread["WaitForFadeOutInXSeconds"] = thread["WaitForXSeconds"];
		thread["WaitForJumpComplete"] = sol::yielding([this](sol::this_state L, sol::variadic_args) {
			if (auto t = GetThread(L)) {
				uint64_t time = utils::get_milliseconds() + 250;
				t->set_resume_condition([time](lua_State*, sol::variadic_results&) { return utils::get_milliseconds() >= time; });
				YieldThread(t);
			}
		});
		thread["WaitForControlOfAgent"] = [](sol::variadic_args) { return true; };
		thread["WaitForNearGoal"] = sol::yielding([this](sol::this_state L, sol::variadic_args) {
			if (auto t = GetThread(L)) {
				uint64_t time = utils::get_milliseconds() + 100;
				t->set_resume_condition([time](lua_State*, sol::variadic_results&) { return utils::get_milliseconds() >= time; });
				YieldThread(t);
			}
		});
		thread["WaitForNotAlive"] = sol::yielding([this](sol::this_state L, sol::object objVal) {
			auto t = GetThread(L);
			if (!t) { return; }
			auto& om = mGame.GetObjectManager();
			auto obj = LuaGetObject(om, objVal);
			if (obj) {
				t->set_resume_condition([&om, id = obj->GetId()](lua_State*, sol::variadic_results&) {
					auto o = om.Get(id);
					return !o || o->IsMarkedForDeletion() || o->GetHealth() <= 0.f;
				});
			}
			YieldThread(t);
		});
		thread["WaitForNotValidTarget"] = thread["WaitForNotAlive"];
		thread["WaitForValidTargetWithinRange"] = [](sol::variadic_args) { return 0u; };
		thread["WaitForever"] = sol::yielding([this](sol::this_state L) {
			if (auto t = GetThread(L)) {
				t->set_resume_condition([](lua_State*, sol::variadic_results&) { return false; });
				YieldThread(t);
			}
		});
		thread["MoveTowardObject"] = [this](sol::this_state L, sol::object agentVal, sol::object targetVal, sol::optional<float> dist, sol::optional<bool> flag) {
			LuaFunction::Locomotion::MoveToObject(L, agentVal, targetVal, dist, flag);
			return true;
		};
		thread["MoveTowardPoint"] = [this](sol::this_state L, sol::object agentVal, float x, float y, float z, sol::optional<float> dist, sol::optional<bool> flag) {
			LuaFunction::Locomotion::MoveToCircleEdge(L, agentVal, glm::vec3(x, y, z), dist.value_or(0.5f), flag);
			return true;
		};

		thread["WaitForProjectile"] = sol::yielding([this](sol::this_state L,
			sol::object arg1,
			sol::object arg2,
			sol::optional<sol::object> arg3,
			sol::optional<sol::object> arg4,
			sol::optional<sol::object> arg5
		) {
			// Support both 4-arg (projectile, params, target, callback) and
			// 5-arg original Darkspore (agent, projectile, params, target, validator)
			const bool isFiveArg = arg3.has_value() && arg3->is<sol::table>();
			sol::object agentObjectValue = isFiveArg ? arg1 : sol::make_object(L, sol::lua_nil);
			sol::object projectileObjectValue = isFiveArg ? arg2 : arg1;
			sol::table projectileParametersTable = isFiveArg
				? arg3->as<sol::table>()
				: (arg2.is<sol::table>() ? arg2.as<sol::table>() : mState.create_table());
			sol::optional<sol::object> targetValue = isFiveArg ? arg4 : arg3;

			auto thread = GetThread(L);
			if (!thread) {
				return;
			}

			auto& objectManager = mGame.GetObjectManager();
			auto agent = LuaGetObject(objectManager, agentObjectValue);
			auto projectile = LuaGetObject(objectManager, projectileObjectValue);
			if (projectile) {
				if (!projectile->HasLocomotionData()) {
					projectile->CreateLocomotionData();
				}
				if (agent && !projectile->GetOwnerObject()) {
					projectile->SetOwnerObject(agent);
					projectile->SetTeam(agent->GetTeam());
				}
				const auto& locomotion = projectile->GetLocomotionData();

				auto& parameters = locomotion->GetProjectileParameters();
				parameters = LuaGetProjectileParameters(projectileParametersTable);
				if (parameters.mSpeed <= 0.f) {
					parameters.mSpeed = 20.f;
				}
				if (parameters.mRange <= 0.f) {
					parameters.mRange = 12.f;
				}
				if (glm::length2(parameters.mDirection) <= 1e-6f) {
					parameters.mDirection = (agent && agent->HasLocomotionData())
						? agent->GetLocomotionData()->GetFacing()
						: glm::vec3(1.f, 0.f, 0.f);
				}
				if (glm::length2(parameters.mDirection) > 1e-6f) {
					parameters.mDirection = glm::normalize(parameters.mDirection);
				}

				const auto& owner = projectile->GetOwnerObject();
				if (owner) {
					parameters.mRange *= 1.f + owner->GetAttributeValue(Game::AttributeType::RangeIncrease);
				}

				Game::ObjectPtr explicitTarget;
				if (targetValue.has_value()) {
					explicitTarget = LuaGetObject(objectManager, *targetValue);
				}
				if (explicitTarget && (parameters.mProjectileFlags & 1)) {
					glm::vec3 toTarget = explicitTarget->GetPosition() - projectile->GetPosition();
					if (glm::length2(toTarget) > 1e-6f) {
						parameters.mDirection = glm::normalize(toTarget);
					}
				}

				projectile->SetMovementType(Game::MovementType::Projectile);
				projectile->SetLinearVelocity(parameters.mDirection * parameters.mSpeed);
				projectile->SetAngularVelocity(parameters.mDirection * parameters.mSpinRate);
				locomotion->SetHasCollidedWithCreature(false);
				locomotion->SetHasCollided(false);

				const uint32_t agentId = agent ? agent->GetId() : (owner ? owner->GetId() : 0u);
				const uint8_t agentTeam = agent ? agent->GetTeam() : (owner ? owner->GetTeam() : 1u);
				const uint32_t explicitTargetId = explicitTarget ? explicitTarget->GetId() : 0u;

				thread->set_resume_condition([&objectManager, id = projectile->GetId(), startPos = projectile->GetPosition(), range = parameters.mRange, isFiveArg, agentId, agentTeam, explicitTargetId](lua_State* L, sol::variadic_results& results) {
					const Game::ObjectPtr& object = objectManager.Get(id);
					if (!object) {
						if (isFiveArg) {
							results.push_back({ L, sol::in_place, sol::lua_nil });
							results.push_back({ L, sol::in_place, true });
							results.push_back({ L, sol::in_place, startPos.x });
							results.push_back({ L, sol::in_place, startPos.y });
							results.push_back({ L, sol::in_place, startPos.z });
							results.push_back({ L, sol::in_place, 0.f });
						} else {
							results.push_back({ L, sol::in_place, Game::ObjectPtr() });
							results.push_back({ L, sol::in_place, true });
							results.push_back({ L, sol::in_place, true });
							results.push_back({ L, sol::in_place, startPos });
							results.push_back({ L, sol::in_place, 0.f });
						}
						return true;
					}

					const glm::vec3 curPos = object->GetPosition();
					const float traveled = glm::distance(startPos, curPos);
					const float remainingDistance = range - traveled;

					// Check hostile creature collision near projectile position
					Game::ObjectPtr hitCreature;
					auto nearby = objectManager.GetObjectsInRadius(Game::BoundingSphere(curPos, 2.0f), { Game::NounType::Creature });
					for (const auto& cand : nearby) {
						if (!cand || cand->GetId() == id || cand->GetId() == agentId || cand->IsMarkedForDeletion()) {
							continue;
						}
						if (!cand->HasCombatantData() || cand->GetHealth() <= 0.f || !cand->IsTargetable()) {
							continue;
						}
						if (agentTeam != 0 && cand->GetTeam() != 0 && cand->GetTeam() == agentTeam) {
							continue;
						}
						float hitRadius = 1.25f + cand->GetFootprintRadius();
						if (glm::distance(curPos, cand->GetPosition()) <= hitRadius) {
							hitCreature = cand;
							break;
						}
					}
					if (!hitCreature && explicitTargetId != 0 && remainingDistance <= 0.f) {
						if (auto expTgt = objectManager.Get(explicitTargetId); expTgt && !expTgt->IsMarkedForDeletion() && expTgt->GetHealth() > 0.f) {
							hitCreature = expTgt;
						}
					}

					const auto& locomotionData = object->GetLocomotionData();
					const bool hasCollidedWithCreature = (hitCreature != nullptr) || (locomotionData && locomotionData->HasCollidedWithCreature());
					const bool hasCollidedGeo = locomotionData && locomotionData->HasCollided();

					if (remainingDistance > 0.f && !hasCollidedWithCreature && !hasCollidedGeo) {
						return false;
					}

					const glm::vec3 impactPos = hitCreature ? hitCreature->GetPosition() : curPos;
					const float remDist = std::max(0.f, remainingDistance);
					if (isFiveArg) {
						if (hitCreature) {
							results.push_back({ L, sol::in_place, hitCreature->GetId() });
						} else {
							results.push_back({ L, sol::in_place, sol::lua_nil });
						}
						results.push_back({ L, sol::in_place, hasCollidedGeo });
						results.push_back({ L, sol::in_place, impactPos.x });
						results.push_back({ L, sol::in_place, impactPos.y });
						results.push_back({ L, sol::in_place, impactPos.z });
						results.push_back({ L, sol::in_place, remDist });
					} else {
						results.push_back({ L, sol::in_place, hitCreature });
						results.push_back({ L, sol::in_place, hasCollidedWithCreature });
						results.push_back({ L, sol::in_place, hasCollidedGeo });
						results.push_back({ L, sol::in_place, impactPos });
						results.push_back({ L, sol::in_place, remDist });
					}
					return true;
				});
			}

			YieldThread(thread);
		});

		thread["WaitForLobbedProjectile"] = sol::yielding([this](sol::this_state L, sol::object projVal, sol::table lobParams, sol::variadic_args) {
			auto thread = GetThread(L);
			if (!thread) { return; }
			auto& om = mGame.GetObjectManager();
			auto proj = LuaGetObject(om, projVal);
			glm::vec3 endPos = proj ? proj->GetPosition() : glm::zero<glm::vec3>();
			uint64_t finishTime = utils::get_milliseconds() + 250;
			thread->set_resume_condition([finishTime, endPos](lua_State* L, sol::variadic_results& results) {
				if (utils::get_milliseconds() < finishTime) {
					return false;
				}
				results.push_back({ L, sol::in_place, sol::lua_nil });
				results.push_back({ L, sol::in_place, true });
				results.push_back({ L, sol::in_place, endPos.x });
				results.push_back({ L, sol::in_place, endPos.y });
				results.push_back({ L, sol::in_place, endPos.z });
				return true;
			});
			YieldThread(thread);
		});
	}

	void Lua::RegisterGame() {
		auto gameMetatable = mState.create_table_with();

		// variables
		gameMetatable["Instance"] = &mGame;
		gameMetatable["Level"] = mGame.GetChainData().GetLevelIndex();

		// functions
		gameMetatable["GetPlayer"] = [this](sol::this_state L, uint8_t index) {
			return mGame.GetPlayerByIndex(index);
		};

		gameMetatable["Notify"] = [this](sol::this_state L, sol::table serverEventTable) {
			auto serverEvent = LuaGetServerEvent(mGame.GetObjectManager(), serverEventTable);
			mGame.SendServerEvent(serverEvent);
		};

		// meta
		gameMetatable[sol::meta_function::new_index] = [](lua_State* L) { return luaL_error(L, "You may not edit the Game table."); };
		gameMetatable[sol::meta_function::index] = gameMetatable;

		mState.create_named_table("Game")[sol::metatable_key] = gameMetatable;
	}

	void Lua::RegisterObjectManager() {
		auto objectManager = mState.create_named_table("nObjectManager");
		objectManager["GetObject"] = &LuaFunction::nObjectManager_GetObject;
		objectManager["CreateObject"] = sol::overload(
			&LuaFunction::nObjectManager_CreateObject,
			&LuaFunction::nObjectManager_CreateObjectCoords,
			// Game form CreateObject(noun): the script positions the object afterwards.
			[this](sol::object nounObj) -> uint32_t {
				auto object = mGame.GetObjectManager().Create(LuaExtractAssetOrGuid(nounObj, 0));
				return object ? object->GetId() : 0u;
			}
		);
		objectManager["CreateCreature"] = &LuaFunction::nObjectManager_CreateObjectCoords;

		objectManager["GetTrigger"] = &LuaFunction::nObjectManager_GetTrigger;
		objectManager["CreateTriggerVolume"] = sol::overload(
			&LuaFunction::nObjectManager_CreateTriggerVolume,
			&LuaFunction::nObjectManager_CreateTriggerVolumeCoords
		);
		objectManager["CreateTriggerVolumeBox"] = &LuaFunction::nObjectManager_CreateTriggerVolumeBoxCoords;
		objectManager["AttachTriggerVolume"] = [this](sol::object trigVal, sol::object objVal) {
			auto& om = mGame.GetObjectManager();
			auto trig = LuaGetTrigger(om, trigVal);
			auto obj = LuaGetObject(om, objVal);
			if (trig && obj) { trig->Attach(obj); }
		};
		objectManager["DetachTriggerVolume"] = [this](sol::object trigVal, sol::object objVal) {
			auto& om = mGame.GetObjectManager();
			auto trig = LuaGetTrigger(om, trigVal);
			auto obj = LuaGetObject(om, objVal);
			if (trig && obj) { trig->Detach(obj); }
		};
		objectManager["DestroyTriggerVolume"] = [this](sol::object trigVal) {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), trigVal)) {
				obj->MarkForDeletion();
			}
		};

		objectManager["GetObjectsInRadius"] = sol::overload(
			&LuaFunction::nObjectManager_GetObjectsInRadius,
			&LuaFunction::nObjectManager_GetObjectsInRadiusCoords
		);
		objectManager["GetObjectsInRadius_SortedByDistance"] = sol::overload(
			&LuaFunction::nObjectManager_GetObjectsInRadius_SortedByDistance,
			&LuaFunction::nObjectManager_GetObjectsInRadiusCoords_SortedByDistance
		);
		objectManager["GetObjectsInRadius_SortedByFurthestDistance"] = &LuaFunction::nObjectManager_GetObjectsInRadiusCoords_SortedByFurthestDistance;
		objectManager["GetObjectsAlongLine"] = [this](float ax, float ay, float az, float bx, float by, float bz, float width, sol::variadic_args rest) {
			glm::vec3 mid = (glm::vec3(ax, ay, az) + glm::vec3(bx, by, bz)) * 0.5f;
			float r = glm::distance(glm::vec3(ax, ay, az), glm::vec3(bx, by, bz)) * 0.5f + std::max(width, 1.f);
			return LuaFunction::ObjectIdsInRadius(mState.lua_state(), mid.x, mid.y, mid.z, r,
				LuaTypeFilter::Read(rest, 0), LuaFunction::RadiusSort::None);
		};
		objectManager["GetObjectsOfOwner"] = [this](sol::object ownerVal, sol::variadic_args) {
			sol::table res = mState.create_table();
			auto owner = LuaGetObject(mGame.GetObjectManager(), ownerVal);
			if (!owner) { return res; }
			int idx = 1;
			for (const auto& obj : mGame.GetObjectManager().GetActiveObjects()) {
				if (obj && obj->GetOwnerObject() == owner) {
					res[idx++] = obj->GetId();
				}
			}
			return res;
		};
		objectManager["GetObjectsOfType"] = [this](uint32_t nounType) {
			sol::table res = mState.create_table();
			int idx = 1;
			for (const auto& obj : mGame.GetObjectManager().GetActiveObjects()) {
				if (obj && static_cast<uint32_t>(obj->GetType()) == nounType) {
					res[idx++] = obj->GetId();
				}
			}
			return res;
		};
		objectManager["GetInteractableObjects"] = [this](sol::variadic_args) {
			sol::table res = mState.create_table();
			int idx = 1;
			for (const auto& obj : mGame.GetObjectManager().GetActiveObjects()) {
				if (obj && obj->HasInteractableData()) {
					res[idx++] = obj->GetId();
				}
			}
			return res;
		};
		objectManager["GetNumInteractableObjects"] = [this](sol::variadic_args) -> int32_t {
			int32_t count = 0;
			for (const auto& obj : mGame.GetObjectManager().GetActiveObjects()) {
				if (obj && obj->HasInteractableData()) {
					++count;
				}
			}
			return count;
		};
		objectManager["IsValidObject"] = [this](sol::object objVal) -> bool {
			auto obj = LuaGetObject(mGame.GetObjectManager(), objVal);
			return obj != nullptr && !obj->IsMarkedForDeletion();
		};
		objectManager["RemoveObjectById"] = [this](sol::object objVal) {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				obj->MarkForDeletion();
			}
		};
	}

	void Lua::RegisterLocomotion() {
		auto locomotion = mState.create_named_table("Locomotion");
		auto nLocomotion = mState["nLocomotion"].get_or_create<sol::table>();

		// event functions
		locomotion["On"] = &LuaFunction::Locomotion::On;

		// object functions (object as arg0)
		locomotion["TeleportAndFace"] = &LuaFunction::Locomotion::TeleportAndFace;
		locomotion["TeleportObject"] = &LuaFunction::Locomotion::TeleportObject;
		locomotion["JumpInDirection"] = &LuaFunction::Locomotion::JumpInDirection;
		locomotion["SlideToPoint"] = &LuaFunction::Locomotion::SlideToPoint;
		locomotion["MoveToPointExact"] = &LuaFunction::Locomotion::MoveToPointExact;
		locomotion["MoveToCircleEdge"] = &LuaFunction::Locomotion::MoveToCircleEdge;
		locomotion["MoveToPointWithinRange"] = &LuaFunction::Locomotion::MoveToPointWithinRange;
		locomotion["MoveToObject"] = &LuaFunction::Locomotion::MoveToObject;
		locomotion["FollowAsPet"] = &LuaFunction::Locomotion::FollowAsPet;

		locomotion["ApplyExternalVelocity"] = &LuaFunction::Locomotion::ApplyExternalVelocity;
		locomotion["ClearExternalVelocity"] = &LuaFunction::Locomotion::ClearExternalVelocity;
		locomotion["ClearTargetObject"] = &LuaFunction::Locomotion::ClearTargetObject;

		locomotion["FaceObjectDuringMove"] = &LuaFunction::Locomotion::FaceObjectDuringMove;
		locomotion["TurnToFace"] = &LuaFunction::Locomotion::TurnToFace;
		locomotion["TurnToFaceTargetObject"] = &LuaFunction::Locomotion::TurnToFaceTargetObject;
		locomotion["MoveToPointWhileFacingTarget"] = &LuaFunction::Locomotion::MoveToPointWhileFacingTarget;

		locomotion["Stop"] = &LuaFunction::Locomotion::Stop;

		// position functions (vec3 as arg0)
		locomotion["GetClosestPosition"] = &LuaFunction::Locomotion::GetClosestPosition;
		locomotion["GetClosestPositionFromPoint"] = &LuaFunction::Locomotion::GetClosestPositionFromPoint;

		locomotion["GetTargetPosition"] = &LuaFunction::Locomotion::GetTargetPosition;
		locomotion["SetTargetPosition"] = &LuaFunction::Locomotion::SetTargetPosition;

		// Also populate nLocomotion with float-coordinate overloads expected by original Darkspore scripts
		nLocomotion["On"] = &LuaFunction::Locomotion::On;
		nLocomotion["TeleportAndFace"] = [this](sol::this_state L, sol::object objVal, float px, float py, float pz, float fx, float fy, float fz) {
			LuaFunction::Locomotion::TeleportAndFace(L, objVal, glm::vec3(px, py, pz), glm::vec3(fx, fy, fz));
		};
		nLocomotion["TeleportObject"] = [this](sol::this_state L, sol::object objVal, float px, float py, float pz, sol::optional<bool> keepFacing) {
			LuaFunction::Locomotion::TeleportObject(L, objVal, glm::vec3(px, py, pz), keepFacing);
		};
		nLocomotion["JumpInDirection"] = [this](sol::this_state L, sol::object objVal, float px, float py, float pz, sol::optional<bool> keepFacing) {
			LuaFunction::Locomotion::JumpInDirection(L, objVal, glm::vec3(px, py, pz), keepFacing);
		};
		nLocomotion["SlideToPoint"] = [this](sol::this_state L, sol::object objVal, float px, float py, float pz, float speed) {
			LuaFunction::Locomotion::SlideToPoint(L, objVal, glm::vec3(px, py, pz), speed);
		};
		nLocomotion["MoveToPointExact"] = [this](sol::this_state L, sol::object objVal, float px, float py, float pz, sol::optional<bool> flag) {
			LuaFunction::Locomotion::MoveToPointExact(L, objVal, glm::vec3(px, py, pz), flag);
		};
		nLocomotion["MoveToCircleEdge"] = [this](sol::this_state L, sol::object objVal, float px, float py, float pz, float dist, sol::optional<bool> flag) {
			LuaFunction::Locomotion::MoveToCircleEdge(L, objVal, glm::vec3(px, py, pz), dist, flag);
		};
		nLocomotion["MoveToPointWithinRange"] = [this](sol::this_state L, sol::object objVal, float px, float py, float pz, float dist, sol::optional<bool> flag) {
			LuaFunction::Locomotion::MoveToPointWithinRange(L, objVal, glm::vec3(px, py, pz), dist, flag);
		};
		nLocomotion["MoveToObject"] = &LuaFunction::Locomotion::MoveToObject;
		nLocomotion["FollowAsPet"] = &LuaFunction::Locomotion::FollowAsPet;
		nLocomotion["ApplyExternalVelocity"] = [this](sol::this_state L, sol::object objVal, float vx, float vy, float vz) {
			LuaFunction::Locomotion::ApplyExternalVelocity(L, objVal, glm::vec3(vx, vy, vz));
		};
		nLocomotion["ClearExternalVelocity"] = &LuaFunction::Locomotion::ClearExternalVelocity;
		nLocomotion["ClearTargetObject"] = &LuaFunction::Locomotion::ClearTargetObject;
		nLocomotion["FaceObjectDuringMove"] = &LuaFunction::Locomotion::FaceObjectDuringMove;
		nLocomotion["TurnToFace"] = [this](sol::this_state L, sol::object objVal, float px, float py, float pz, sol::optional<bool> flag) {
			LuaFunction::Locomotion::TurnToFace(L, objVal, glm::vec3(px, py, pz), flag);
		};
		nLocomotion["TurnToFaceTargetObject"] = &LuaFunction::Locomotion::TurnToFaceTargetObject;
		nLocomotion["MoveToPointWhileFacingTarget"] = [this](sol::this_state L, sol::object objVal, float px, float py, float pz, sol::object targetVal) {
			LuaFunction::Locomotion::MoveToPointWhileFacingTarget(L, objVal, glm::vec3(px, py, pz), targetVal);
		};
		nLocomotion["Stop"] = &LuaFunction::Locomotion::Stop;
		nLocomotion["GetClosestPosition"] = [](float x, float y, float z, sol::variadic_args) -> std::tuple<float, float, float> {
			return { x, y, z };
		};
		nLocomotion["GetClosestPositionFromPoint"] = [](float x, float y, float z, sol::variadic_args) -> std::tuple<float, float, float> {
			return { x, y, z };
		};
		nLocomotion["GetTargetPosition"] = [this](sol::this_state L, sol::object objVal) -> std::tuple<float, float, float> {
			glm::vec3 p = LuaFunction::Locomotion::GetTargetPosition(L, objVal);
			return { p.x, p.y, p.z };
		};
		nLocomotion["SetTargetPosition"] = [this](sol::this_state L, sol::object objVal, float x, float y, float z) {
			LuaFunction::Locomotion::SetTargetPosition(L, objVal, glm::vec3(x, y, z));
		};
	}

	void Lua::RegisterPlayer() {
		auto player = mState.new_usertype<Player>("Player", sol::no_constructor);
		player["GetCharacter"] = &Player::GetCharacterObject;
		player["GetDeployedCharacter"] = &Player::GetDeployedCharacterObject;

		player["Notify"] = [this](sol::this_state L, sol::object playerValue, sol::table serverEventTable) {
			auto player = LuaGetPlayer(mGame, playerValue);
			if (player) {
				auto serverEvent = LuaGetServerEvent(mGame.GetObjectManager(), serverEventTable);
				mGame.SendServerEvent(player, serverEvent);
			}
		};

		auto nPlayer = mState["nPlayer"].get_or_create<sol::table>();
		nPlayer["PickupCrystal"] = [](sol::variadic_args) {};
		nPlayer["PickUpResurrectOrb"] = [](sol::variadic_args) {};
		nPlayer["GetPlayerControlledObjects"] = [this]() {
			sol::table res = mState.create_table();
			int idx = 1;
			for (uint8_t i = 0; i < 4; ++i) {
				if (auto p = mGame.GetPlayerByIndex(i)) {
					if (auto c = p->GetDeployedCharacterObject()) {
						res[idx++] = c->GetId();
					}
				}
			}
			return res;
		};
		nPlayer["GetPlayerControlledObjectID"] = [this](sol::object playerVal) -> uint32_t {
			if (auto p = LuaGetPlayer(mGame, playerVal)) {
				if (auto c = p->GetDeployedCharacterObject()) {
					return c->GetId();
				}
			}
			return 0u;
		};
		nPlayer["GetPlayerIdForObject"] = [this](sol::object objVal) -> uint8_t {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				return obj->GetPlayerIndex();
			}
			return 0;
		};
		nPlayer["GetPlayerIds"] = [this]() {
			sol::table res = mState.create_table();
			int idx = 1;
			for (uint8_t i = 0; i < 4; ++i) {
				if (mGame.GetPlayerByIndex(i)) {
					res[idx++] = i;
				}
			}
			return res;
		};
		nPlayer["GetTotalNumberOfPlayers"] = [this]() -> int32_t {
			int32_t count = 0;
			for (uint8_t i = 0; i < 4; ++i) {
				if (mGame.GetPlayerByIndex(i)) {
					++count;
				}
			}
			return std::max(1, count);
		};
		nPlayer["IsPlayerControlledObject"] = [this](sol::object objVal) -> bool {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				return obj->IsPlayerControlled();
			}
			return false;
		};
		nPlayer["IsOverdriveActive"] = [this](sol::object objVal) -> bool {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				return obj->IsOverdriveCharged();
			}
			return false;
		};
		nPlayer["CanUseOverdrive"] = [this](sol::object objVal) -> bool {
			if (auto obj = LuaGetObject(mGame.GetObjectManager(), objVal)) {
				return obj->IsOverdriveCharged();
			}
			return false;
		};
		nPlayer["StartOverdrive"] = [](sol::variadic_args) {};
		nPlayer["UnlockOverdrive"] = [](sol::variadic_args) {};
		nPlayer["CanDeployCreatureFromCurrentDeck"] = [](sol::variadic_args) { return true; };
		nPlayer["DeployNextCreature"] = [](sol::variadic_args) {};
		// Game scripts omit the player: it is the player of the casting hero.
		const auto scriptPlayer = [this](sol::this_state L, sol::object playerVal) -> Game::PlayerPtr {
			if (auto p = LuaGetPlayer(mGame, playerVal)) {
				return p;
			}
			if (auto ab = LuaGetAbility(L)) {
				if (auto agent = ab->GetActiveAgent()) {
					return agent->GetPlayer();
				}
			}
			return {};
		};
		nPlayer["GetCurrentDeckIndex"] = [scriptPlayer](sol::this_state L, sol::variadic_args args) -> int32_t {
			if (auto p = scriptPlayer(L, LuaArg(args, 0))) {
				return p->GetCurrentDeckIndex();
			}
			return 0;
		};
		nPlayer["CanUseAbility"] = [](sol::variadic_args) { return true; };
		nPlayer["IsAbilityIndexUnlocked"] = [](sol::variadic_args) { return true; };
		nPlayer["UnlockNextAbility"] = [](sol::variadic_args) {};
		nPlayer["UnlockSecondCreature"] = [](sol::variadic_args) {};
		nPlayer["IsCrystalSlotAvailable"] = [](sol::variadic_args) { return true; };
		nPlayer["UnlockCrystals"] = [](sol::variadic_args) {};
		nPlayer["DropCrystals"] = [](sol::variadic_args) {};
		nPlayer["GetHealthOfCreatureAtDeckIndex"] = [scriptPlayer](sol::this_state L, sol::variadic_args args) -> float {
			// (player, deckIndex) or the game form (deckIndex).
			const bool hasPlayer = args.size() >= 2;
			sol::object index = LuaArg(args, hasPlayer ? 1 : 0);
			if (index.get_type() == sol::type::number) {
				if (auto p = scriptPlayer(L, hasPlayer ? LuaArg(args, 0) : sol::make_object(L, sol::lua_nil))) {
					if (auto c = p->GetCharacterObject(index.as<uint32_t>())) {
						return c->GetHealth();
					}
				}
			}
			return 100.f;
		};
		nPlayer["GetMaxHealthOfCreatureAtDeckIndex"] = [scriptPlayer](sol::this_state L, sol::variadic_args args) -> float {
			// (player, deckIndex) or the game form (deckIndex).
			const bool hasPlayer = args.size() >= 2;
			sol::object index = LuaArg(args, hasPlayer ? 1 : 0);
			if (index.get_type() == sol::type::number) {
				if (auto p = scriptPlayer(L, hasPlayer ? LuaArg(args, 0) : sol::make_object(L, sol::lua_nil))) {
					if (auto c = p->GetCharacterObject(index.as<uint32_t>())) {
						return c->GetMaxHealth();
					}
				}
			}
			return 100.f;
		};
		nPlayer["GetManaOfCreatureAtDeckIndex"] = [this](sol::object playerVal, uint32_t idx) -> float {
			if (auto p = LuaGetPlayer(mGame, playerVal)) {
				if (auto c = p->GetCharacterObject(idx)) {
					return c->GetMana();
				}
			}
			return 100.f;
		};
		nPlayer["GetMaxManaOfCreatureAtDeckIndex"] = [this](sol::object playerVal, uint32_t idx) -> float {
			if (auto p = LuaGetPlayer(mGame, playerVal)) {
				if (auto c = p->GetCharacterObject(idx)) {
					return c->GetMaxMana();
				}
			}
			return 100.f;
		};
		nPlayer["GetSimDataAsInt"] = [](sol::variadic_args) { return 0; };
		nPlayer["SetSimDataAsInt"] = [](sol::variadic_args) {};
		nPlayer["GetSquadPassiveGUIDs"] = [this](sol::variadic_args) { return mState.create_table(); };
		nPlayer["HasBeatenThisLevel"] = [](sol::variadic_args) { return false; };
		nPlayer["LockCamera"] = [](sol::variadic_args) {};
		nPlayer["UnlockCamera"] = [](sol::variadic_args) {};
		nPlayer["PlayersRollForLoot"] = [](sol::variadic_args) {};
	}

	void Lua::RegisterAttributes() {
		auto attributes = mState.new_usertype<Game::Attributes>("Attributes", sol::no_constructor);
		// Script attribute numbers -> AttributeType (game/attribute_layout.h).
		attributes["GetValue"] = [](const Game::Attributes& self, uint8_t scriptIdx) -> float {
			const auto idx = Game::AttributeLayout::Active().ToServer(scriptIdx);
			return idx == Game::AttributeLayout::None ? 0.f : self.GetValue(idx);
		};
		attributes["SetValue"] = [](Game::Attributes& self, uint8_t scriptIdx, float value) {
			if (const auto idx = Game::AttributeLayout::Active().ToServer(scriptIdx); idx != Game::AttributeLayout::None) {
				self.SetValue(idx, value);
			}
		};

		attributes["GetOwnerObject"] = &Game::Attributes::GetOwnerObject;
	}

	void Lua::RegisterObject() {
		auto object = mState.new_usertype<Object>("Object", sol::no_constructor);
		object["IsValid"] = &LuaFunction::Object::IsValid;

		object["GetId"] = &Object::GetId;
		object["GetName"] = &LuaFunction::Object::GetName;
		object["GetPrivateTable"] = &LuaFunction::Object::GetPrivateTable;

		object["GetPosition"] = &Object::GetPosition;
		object["SetPosition"] = &Object::SetPosition;

		object["GetOrientation"] = &Object::GetOrientation;
		object["SetOrientation"] = &Object::SetOrientation;

		object["GetExtent"] = &Object::GetExtent;
		object["SetExtent"] = &Object::SetExtent;

		object["GetType"] = &Object::GetType;
		object["GetNpcType"] = &Object::GetNpcType;

		object["GetFacing"] = &LuaFunction::Object::GetFacing;
		object["GetFootprintRadius"] = &Object::GetFootprintRadius;
		object["GetCenterPoint"] = &Object::GetCenterPoint;

		object["SetTickOverride"] = sol::resolve<void(sol::protected_function)>(&Object::SetTickOverride);

		object["Move"] = &LuaFunction::Object::Move;

		object["GetHealth"] = &Object::GetHealth;
		object["GetMaxHealth"] = &Object::GetMaxHealth;
		object["SetHealth"] = &Object::SetHealth;

		object["GetMana"] = &Object::GetMana;
		object["GetMaxMana"] = &Object::GetMaxMana;
		object["SetMana"] = &Object::SetMana;

		object["GetOwnerObject"] = &Object::GetOwnerObject;
		object["SetOwnerObject"] = &Object::SetOwnerObject;

		object["PlayAnimationSequence"] = [this](sol::this_state L, sol::object objectValue, sol::table sequenceTable) {
			auto object = LuaGetObject(mGame.GetObjectManager(), objectValue);
			if (object && sequenceTable.valid()) {
				uint32_t animState = 0;
				if (sol::object first = sequenceTable[1]; first.is<uint32_t>()) {
					animState = first.as<uint32_t>();
				} else if (sol::object idObj = sequenceTable["id"]; idObj.is<uint32_t>()) {
					animState = idObj.as<uint32_t>();
				}
				if (animState != 0) {
					mGame.SendAnimationState(object, animState);
				}
			}
		};

		object["SetAnimationState"] = &LuaFunction::Object::SetAnimationState;
		object["SetAnimationStateToDeath"] = &LuaFunction::Object::SetAnimationStateToDeath;
		object["SetAnimationStateToAggro"] = &LuaFunction::Object::SetAnimationStateToAggro;
		object["SetGraphicsState"] = &LuaFunction::Object::SetGraphicsState;

		object["GetAttributeSnapshot"] = &LuaFunction::Object::GetAttributeSnapshot;
		object["SetAttributeSnapshot"] = &LuaFunction::Object::SetAttributeSnapshot;

		object["GetAttributeValue"] = [](const Object& self, uint8_t scriptIdx) -> float {
			const auto idx = Game::AttributeLayout::Active().ToServer(scriptIdx);
			return idx == Game::AttributeLayout::None ? 0.f : self.GetAttributeValue(idx);
		};
		object["SetAttributeValue"] = [](Object& self, uint8_t scriptIdx, float value) {
			if (const auto idx = Game::AttributeLayout::Active().ToServer(scriptIdx); idx != Game::AttributeLayout::None) {
				self.SetAttributeValue(idx, value);
			}
		};

		object["GetWeaponDamage"] = &LuaFunction::Object::GetWeaponDamage;

		object["GetMarkerId"] = &Object::GetMarkerId;
		object["SetMarkerId"] = &Object::SetMarkerId;

		object["GetScale"] = &Object::GetScale;
		object["SetScale"] = &Object::SetScale;

		object["GetTeam"] = &Object::GetTeam;
		object["SetTeam"] = &Object::SetTeam;

		object["IsPlayerControlled"] = &Object::IsPlayerControlled;
		object["GetPlayerIndex"] = &Object::GetPlayerIndex;

		object["IsVisible"] = &Object::IsVisible;
		object["SetVisible"] = &Object::SetVisible;

		object["IsMarkedForDeletion"] = &Object::IsMarkedForDeletion;
		object["MarkForDeletion"] = &Object::MarkForDeletion;

		object["IsCombatant"] = &LuaFunction::Object::IsCombatant;

		object["AddCooldown"] = &LuaFunction::Object::AddCooldown;
		object["RemoveCooldown"] = &LuaFunction::Object::RemoveCooldown;
		object["ScaleCooldown"] = &LuaFunction::Object::ScaleCooldown;

		object["TakeDamage"] = &LuaFunction::Object::TakeDamage;
		object["Heal"] = &LuaFunction::Object::Heal;
		
		object["RequestAbility"] = [this](sol::this_state L, sol::object objectValue, sol::object attackerValue, uint32_t abilityId, int32_t rank) {
			const auto& ability = GetAbility(abilityId);
			if (!ability) {
				return;
			}
			const auto& objectManager = mGame.GetObjectManager();
			auto object = LuaGetObject(objectManager, objectValue);
			auto attacker = LuaGetObject(objectManager, attackerValue);
			if (object) {
				ability->Tick(attacker ? attacker : object, object, object->GetPosition(), rank);
			}
		};

		object["RequestModifier"] = [this](sol::this_state L, sol::object objectValue, sol::object attackerValue, uint32_t abilityId, int32_t rank) {
			//
		};

		object["AddEffect"] = &LuaFunction::Object::AddEffect;
		object["RemoveEffect"] = &LuaFunction::Object::RemoveEffect;
		object["RemoveEffectIndex"] = &LuaFunction::Object::RemoveEffectIndex;

		object["UseAbility"] = &LuaFunction::Object::UseAbility;
		object["DropLoot"] = &LuaFunction::Object::DropLoot;

		object["GetSharedAbilityOffset"] = &LuaFunction::Object::GetSharedAbilityOffset;

		// Interactable data
		object["HasInteractableUsesLeft"] = &Object::HasInteractableUsesLeft;
		object["IncrementNumTimesUsed"] = &LuaFunction::Object::IncrementNumTimesUsed;

		// Agent blackboard
		object["GetTargetId"] = &Object::GetTargetId;
		object["SetTargetId"] = &Object::SetTargetId;

		object["IsStealthed"] = &LuaFunction::Object::IsStealthed;
		object["GetStealthType"] = &Object::GetStealthType;
		object["SetStealthType"] = &Object::SetStealthType;

		object["IsInCombat"] = &Object::IsInCombat;
		object["SetInCombat"] = &Object::SetInCombat;

		object["IsTargetable"] = &Object::IsTargetable;
		object["SetTargetable"] = &Object::SetTargetable;

		// Markers
		object["GetTeleporterDestination"] = &LuaFunction::Object::GetTeleporterDestination;
	}

	void Lua::RegisterAbility() {
		// Accepts the game's RegisterAbility("Name", table) and Ability.Register(table).
		auto registerAbilityLambda = [this](sol::this_state L, sol::variadic_args args) {
			auto request = AbilityRegistration::Resolve(args);
			if (!request) {
				LogError(L, "Ability.Register", "expected (name, ability table) or (ability table)");
				return;
			}

			sol::table abilityTable = request->table;
			const std::string name = request->name;
			const uint32_t id = utils::hash_id(name);
			abilityTable["id"] = id;

			auto abilityPtr = std::make_shared<Ability>(*this, sol::table(abilityTable), name, id);
			mAbilities[id] = abilityPtr;
			if (!name.starts_with("ability_") && !name.starts_with("Ability_")) {
				mAbilities.try_emplace(utils::hash_id("ability_" + name), abilityPtr);
			}
			SetLastRegisteredAbility(abilityPtr);
		};

		auto ability = mState.create_named_table("Ability");
		ability["Register"] = registerAbilityLambda;

		ability["TargetInRangeAtStart"] = [this](sol::this_state L) -> bool {
			return true;
		};

		auto nAbility = mState["nAbility"].get_or_create<sol::table>();
		nAbility["PreloadAnimation"] = [](sol::variadic_args args) -> uint32_t {
			// Animation state ids are the resource hash of the animation name.
			if (args.size() > 0 && args[0].get_type() == sol::type::string) {
				return utils::hash_id(args[0].as<std::string>().c_str());
			}
			return 0u;
		};
		nAbility["PreloadAsset"] = [](sol::variadic_args args) -> uint32_t {
			// Asset/modifier id: the resource hash of the name (a number passes through).
			return args.size() > 0 ? LuaExtractAssetOrGuid(sol::object(args[0]), 0) : 0u;
		};
		nAbility["PreloadModifier"] = [](sol::variadic_args args) -> uint32_t {
			// Asset/modifier id: the resource hash of the name (a number passes through).
			return args.size() > 0 ? LuaExtractAssetOrGuid(sol::object(args[0]), 0) : 0u;
		};
		nAbility["RegisterAbility"] = registerAbilityLambda;

		nAbility["GetAgentID"] = [this](sol::this_state L, sol::variadic_args) -> uint32_t {
			if (auto ab = LuaGetAbility(L); ab && ab->GetActiveAgent()) {
				return ab->GetActiveAgent()->GetId();
			}
			return 0u;
		};
		nAbility["GetTargetID"] = [this](sol::this_state L, sol::variadic_args) -> uint32_t {
			if (auto ab = LuaGetAbility(L); ab && ab->GetActiveTarget()) {
				return ab->GetActiveTarget()->GetId();
			}
			return 0u;
		};
		nAbility["GetTargetPosition"] = [this](sol::this_state L, sol::variadic_args) -> std::tuple<float, float, float> {
			if (auto ab = LuaGetAbility(L)) {
				glm::vec3 pos = ab->GetActiveCursorPosition();
				if (glm::length2(pos) <= 1e-6f && ab->GetActiveTarget()) {
					pos = ab->GetActiveTarget()->GetPosition();
				}
				return { pos.x, pos.y, pos.z };
			}
			return { 0.f, 0.f, 0.f };
		};
		nAbility["GetAgentAttributeSnapshot"] = [this](sol::this_state L, sol::optional<sol::object> agentVal) -> Game::AttributesPtr {
			Game::ObjectPtr agent;
			if (agentVal.has_value()) {
				agent = LuaGetObject(mGame.GetObjectManager(), *agentVal);
			}
			if (!agent) {
				if (auto ab = LuaGetAbility(L)) {
					agent = ab->GetActiveAgent();
				}
			}
			if (agent && agent->HasAttributeData()) {
				auto snap = std::make_shared<Game::Attributes>(*agent->GetAttributeData());
				snap->SetOwnerObject(agent);
				return snap;
			}
			return {};
		};
		nAbility["RetakeAgentSnapshot"] = nAbility["GetAgentAttributeSnapshot"];
		nAbility["GetRank"] = [this](sol::this_state L, sol::variadic_args) -> int32_t {
			if (auto ab = LuaGetAbility(L)) {
				return std::max(1, ab->GetActiveRank());
			}
			return 1;
		};
		// Zero-based: the game helpers read animationSequence[index + 1].
		nAbility["GetAnimationSequenceIndex"] = [this](sol::this_state L, sol::variadic_args) -> int32_t {
			if (auto ab = LuaGetAbility(L)) {
				return static_cast<int32_t>(ab->GetActiveSequenceIndex());
			}
			return 0;
		};
		nAbility["GetAbilityInstanceID"] = [this](sol::this_state L, sol::variadic_args) -> uint32_t {
			if (auto ab = LuaGetAbility(L)) {
				return ab->GetId();
			}
			return 0u;
		};
		nAbility["GetAbilityNamespace"] = [this](sol::this_state L, sol::variadic_args) -> std::string {
			if (auto ab = LuaGetAbility(L)) {
				return ab->GetName();
			}
			return {};
		};
		nAbility["GetDescriptors"] = [this](sol::this_state L, sol::variadic_args) -> uint32_t {
			if (auto ab = LuaGetAbility(L)) {
				return static_cast<uint32_t>(ab->GetDescriptors());
			}
			return 0u;
		};
		nAbility["GetProperty"] = [this](sol::this_state L, std::string propName, sol::optional<int32_t> rankOpt) -> sol::object {
			if (auto ab = LuaGetAbility(L)) {
				const auto& tbl = ab->GetTable();
				int32_t r = rankOpt.value_or(ab->GetActiveRank());
				if (sol::object rankTblObj = tbl[r]; rankTblObj.is<sol::table>()) {
					sol::table rankTbl = rankTblObj.as<sol::table>();
					if (sol::object v = rankTbl[propName]; v.valid() && v != sol::nil) {
						return v;
					}
				}
				if (sol::object v = tbl[propName]; v.valid() && v != sol::nil) {
					return v;
				}
			}
			return sol::make_object(L, 0.f);
		};
		nAbility["GetAbilityAttributeValue"] = [this](sol::this_state L, uint8_t scriptIdx) -> float {
			const auto attrIdx = Game::AttributeLayout::Active().ToServer(scriptIdx);
			if (auto ab = LuaGetAbility(L); ab && ab->GetActiveAgent() && attrIdx != Game::AttributeLayout::None) {
				return ab->GetActiveAgent()->GetAttributeValue(attrIdx);
			}
			return 0.f;
		};
		nAbility["TargetInRangeAtStart"] = [](sol::variadic_args) -> bool { return true; };
		nAbility["PayCooldownAndMana"] = [this](sol::this_state L, sol::variadic_args) {
			if (auto ab = LuaGetAbility(L)) {
				ab->PayCooldownAndMana();
			}
		};
		nAbility["PlayAnimationSequence"] = [this](sol::this_state L, sol::variadic_args args) {
			if (args.size() == 0) {
				// Game form: play the current entry of the casting ability
				// animationSequence on its agent.
				if (auto ab = LuaGetAbility(L)) {
					const auto animation = ab->GetActiveSequenceAnimation();
					if (auto agent = ab->GetActiveAgent(); agent && animation != 0) {
						mGame.SendAnimationState(agent, animation);
					}
				}
				return;
			}
			sol::object agentVal = LuaArg(args, 0);
			sol::object seqVal = LuaArg(args, 1);
			auto agent = LuaGetObject(mGame.GetObjectManager(), agentVal);
			if (!agent) {
				if (auto ab = LuaGetAbility(L)) {
					agent = ab->GetActiveAgent();
				}
			}
			if (agent && seqVal.is<sol::table>()) {
				sol::table seq = seqVal.as<sol::table>();
				uint32_t animState = 0;
				if (sol::object first = seq[1]; first.is<uint32_t>()) {
					animState = first.as<uint32_t>();
				} else if (sol::object idObj = seq["id"]; idObj.is<uint32_t>()) {
					animState = idObj.as<uint32_t>();
				}
				if (animState != 0) {
					mGame.SendAnimationState(agent, animState);
				}
			}
		};
		nAbility["GetWarmupAnimation"] = [](sol::variadic_args) -> std::tuple<uint32_t, float> { return { 0u, 0.25f }; };
		nAbility["IsAbilityRunning"] = [](sol::variadic_args) -> bool { return false; };
		nAbility["IsControlledByAbility"] = [](sol::variadic_args) -> bool { return false; };
		nAbility["BindAgent"] = [](sol::variadic_args) {};
		nAbility["ReleaseAgent"] = [](sol::variadic_args) {};
		nAbility["AddCooldownTime"] = [this](sol::this_state L, sol::object objVal, uint32_t abilityId, float seconds) {
			LuaFunction::Object::AddCooldown(L, objVal, abilityId, static_cast<uint32_t>(std::max(0.f, seconds) * 1000.f));
		};
		nAbility["RemoveCooldownTime"] = [this](sol::this_state L, sol::variadic_args args) {
			sol::object abilityId = LuaArg(args, 1);
			sol::object seconds = LuaArg(args, 2);
			if (abilityId.get_type() != sol::type::number || seconds.get_type() != sol::type::number) {
				LogUnsupportedCallForm("nAbility.RemoveCooldownTime", args.size());
				return;
			}
			LuaFunction::Object::RemoveCooldown(L, LuaArg(args, 0), abilityId.as<uint32_t>(),
				static_cast<uint32_t>(std::max(0.f, seconds.as<float>()) * 1000.f));
		};
		nAbility["ScaleCooldownTime"] = [this](sol::this_state L, sol::variadic_args args) {
			sol::object abilityId = LuaArg(args, 1);
			sol::object scale = LuaArg(args, 2);
			if (abilityId.get_type() != sol::type::number || scale.get_type() != sol::type::number) {
				LogUnsupportedCallForm("nAbility.ScaleCooldownTime", args.size());
				return;
			}
			LuaFunction::Object::ScaleCooldown(L, LuaArg(args, 0), abilityId.as<uint32_t>(), scale.as<float>());
		};
		nAbility["ResetAbilityCooldown"] = [this](sol::this_state L, sol::object objVal, uint32_t abilityId) {
			LuaFunction::Object::ScaleCooldown(L, objVal, abilityId, 0.f);
		};
		nAbility["RequestAbility"] = [this](sol::object agentVal, sol::object abilityNameOrId, sol::optional<sol::object> targetVal, sol::optional<float> x, sol::optional<float> y, sol::optional<float> z) {
			uint32_t abilityId = LuaExtractAssetOrGuid(abilityNameOrId, 0);
			auto abilityPtr = GetAbility(abilityId);
			if (!abilityPtr) { return false; }
			auto& om = mGame.GetObjectManager();
			auto agent = LuaGetObject(om, agentVal);
			auto target = targetVal.has_value() ? LuaGetObject(om, *targetVal) : Game::ObjectPtr{};
			glm::vec3 pos = x.has_value()
				? glm::vec3(x.value_or(0.f), y.value_or(0.f), z.value_or(0.f))
				: (target ? target->GetPosition() : (agent ? agent->GetPosition() : glm::zero<glm::vec3>()));
			return abilityPtr->Tick(agent, target, pos, 1);
		};
		nAbility["CallFunctionInContext"] = [](sol::variadic_args) {};
		nAbility["CheckDescriptors_AnyMatch"] = [](uint32_t a, uint32_t b) -> bool { return (a & b) != 0; };
		nAbility["CreateAbilityEvent"] = [this](uint32_t eventType) {
			sol::table ev = mState.create_table();
			ev["eventType"] = eventType;
			return ev;
		};
		nAbility["SendAbilityEvent"] = [](sol::variadic_args) {};
		nAbility["GetAbilityEventType"] = [](sol::object ev) -> uint32_t {
			return ev.is<sol::table>() ? LuaTableGetOr<uint32_t>(ev.as<sol::table>(), "eventType", 0u) : 0u;
		};
		nAbility["GetAbilityEventIntData"] = [](sol::object ev, sol::object k) -> int32_t {
			return ev.is<sol::table>() ? LuaTableGetOr<int32_t>(ev.as<sol::table>(), k, 0) : 0;
		};
		nAbility["GetAbilityEventFloatData"] = [](sol::object ev, sol::object k) -> float {
			return ev.is<sol::table>() ? LuaTableGetOr<float>(ev.as<sol::table>(), k, 0.f) : 0.f;
		};
		nAbility["GetAbilityEventGUIDData"] = [](sol::object ev, sol::object k) -> uint32_t {
			return ev.is<sol::table>() ? LuaTableGetOr<uint32_t>(ev.as<sol::table>(), k, 0u) : 0u;
		};
	}

	void Lua::RegisterObjective() {
		auto objective = mState.create_named_table("Objective");
		objective["Register"] = [this](sol::this_state L, sol::table objective) {
			// Registered objective script table
		};
		objective["RegisterObjective"] = objective["Register"];
		objective["SetObjectiveIntData"] = [](sol::variadic_args) {};
		objective["SetObjectiveIntDataFromSPID"] = [](sol::variadic_args) {};
		objective["GetObjectiveIntData"] = [](sol::variadic_args) { return 0; };
		objective["SetObjectiveFloatData"] = [](sol::variadic_args) {};
		objective["SetObjectiveFloatDataFromSPID"] = [](sol::variadic_args) {};
		objective["GetObjectiveFloatData"] = [](sol::variadic_args) { return 0.f; };
		objective["SetObjectiveGUIDData"] = [](sol::variadic_args) {};
		objective["SetObjectiveGUIDDataFromSPID"] = [](sol::variadic_args) {};
		objective["GetObjectiveGUIDData"] = [](sol::variadic_args) { return 0u; };
		objective["CreateObjectiveEvent"] = [this](uint32_t eventType) {
			sol::table ev = mState.create_table();
			ev["eventType"] = eventType;
			return ev;
		};
		objective["SendObjectiveEvent"] = [](sol::variadic_args) {};
		objective["DestroyObjectiveEvent"] = [](sol::variadic_args) {};
		objective["SetObjectiveEventIntData"] = [](sol::variadic_args) {};
		objective["GetObjectiveEventIntData"] = [](sol::variadic_args) { return 0; };
		objective["SetObjectiveEventFloatData"] = [](sol::variadic_args) {};
		objective["GetObjectiveEventFloatData"] = [](sol::variadic_args) { return 0.f; };
		objective["SetObjectiveEventGUIDData"] = [](sol::variadic_args) {};
		objective["GetObjectiveEventGUIDData"] = [](sol::variadic_args) { return 0u; };
		objective["GetRegisteredDestructibles"] = [this](sol::variadic_args) { return mState.create_table(); };
		mState["nObjective"] = objective;
	}

	void Lua::RegisterTriggerVolume() {
		auto triggerVolume = mState.new_usertype<TriggerVolume>("TriggerVolume", sol::no_constructor, sol::base_classes, sol::bases<Object>());

		triggerVolume["Attach"] = [this](sol::this_state L, sol::object triggerValue, sol::object objectValue) {
			const auto& objectManager = mGame.GetObjectManager();

			auto trigger = LuaGetTrigger(objectManager, triggerValue);
			auto object = LuaGetObject(objectManager, objectValue);
			if (trigger && object) {
				trigger->Attach(object);
			}
		};

		triggerVolume["Detach"] = [this](sol::this_state L, sol::object triggerValue, sol::object objectValue) {
			const auto& objectManager = mGame.GetObjectManager();

			auto trigger = LuaGetTrigger(objectManager, triggerValue);
			auto object = LuaGetObject(objectManager, objectValue);
			if (trigger && object) {
				trigger->Detach(object);
			}
		};

		triggerVolume["SetOnEnterCallback"] = &TriggerVolume::SetOnEnterCallback;
		triggerVolume["SetOnExitCallback"] = &TriggerVolume::SetOnExitCallback;
		triggerVolume["SetOnStayCallback"] = &TriggerVolume::SetOnStayCallback;
	}
}
