#ifndef RECAP_GAME_ATTRIBUTE_LAYOUT_H
#define RECAP_GAME_ATTRIBUTE_LAYOUT_H

#include <array>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <utility>

// Attribute numbering of the installed game.
//
// The server's AttributeType (game/attributes.h) agrees with build 103 for
// 0-73 only. From 74 the game's nAttributeType (GlobalDefinitions.lua) and the
// client's attribute table use another order (ImmuneToShocked 74, ImmuneToSleep
// 75, ImmuneToTaunted 76, ..., DistributeDamageAmongSquad 100, BodyScale 113,
// DefenseBoostPhysicalDamage 114). Scripts pass the game's numbers, and the
// client reads reflected attributes by the game's numbers (darkspin, build
// 103), so the server translates by name at both boundaries. With no game
// table the map is the identity (the server's previous behaviour).
namespace Game::AttributeLayout {
	inline constexpr uint8_t None = 0xFF;

	// AttributeType names; index = AttributeType value (checked against the
	// enum by tests/test_attribute_layout_contract.py).
	inline constexpr std::array<std::string_view, 115> ServerNames {
		"Strength", "Dexterity", "Mind", "MaxHealthIncrease",
		"MaxHealth", "MaxMana", "DamageReduction", "PhysicalDefense",
		"PhysicalDamageReduction", "EnergyDefense", "CriticalRating", "NonCombatSpeed",
		"CombatSpeed", "DamageBuff", "Silence", "Immobilized",
		"DefenseBoostBasicDamage", "PhysicalDamageIncrease", "PhysicalDamageIncreaseFlat", "AutoCrit",
		"BehindDirectDamageIncrease", "BehindOrSideDirectDamageIncrease", "CriticalDamageIncrease", "AttackSpeedScale",
		"CooldownScale", "Frozen", "ProjectileSpeedIncrease", "AoEResistance",
		"EnergyDamageBuff", "Intangible", "HealingReduction", "EnergyDamageIncrease",
		"EnergyDamageIncreaseFlat", "Immune", "StealthDetection", "LifeSteal",
		"RejectModifier", "AoEDamage", "TechnologyTypeDamage", "SpacetimeTypeDamage",
		"LifeTypeDamage", "ElementsTypeDamage", "SupernaturalTypeDamage", "TechnologyTypeResistance",
		"SpacetimeTypeResistance", "LifeTypeResistance", "ElementsTypeResistance", "SupernaturalTypeResistance",
		"MovementSpeedBuff", "ImmuneToDebuffs", "BuffDuration", "DebuffDuration",
		"ManaSteal", "DebuffDurationIncrease", "EnergyDamageReduction", "Incorporeal",
		"DoTDamageIncrease", "MindControlled", "SwapDisabled", "ImmuneToRandomTeleport",
		"ImmuneToBanish", "ImmuneToKnockback", "AoeRadius", "PetDamage",
		"PetHealth", "CrystalFind", "DNADropped", "RangeIncrease",
		"OrbEffectiveness", "OverdriveBuildup", "OverdriveDuration", "LootFind",
		"Surefooted", "ImmuneToStunned", "ImmuneToSleep", "ImmuneToTerrified",
		"ImmuneToSilence", "ImmuneToCursed", "ImmuneToPoisonOrDisease", "ImmuneToBurning",
		"ImmuneToRooted", "ImmuneToSlow", "ImmuneToPull", "DoTDamageDoneIncrease",
		"AggroIncrease", "AggroDecrease", "PhysicalDamageDoneIncrease", "PhysicalDamageDoneByAbilityIncrease",
		"EnergyDamageDoneIncrease", "EnergyDamageDoneByAbilityIncrease", "ChannelTimeDecrease", "CrowdControlDurationDecrease",
		"DoTDurationDecrease", "AoEDurationIncrease", "HealIncrease", "OnLockdown",
		"HoTDoneIncrease", "ProjectileDamageIncrease", "DeployBonusInvincibilityTime", "PhysicalDamageDecreaseFlat",
		"EnergyDamageDecreaseFlat", "MinWeaponDamage", "MaxWeaponDamage", "MinWeaponDamagePercent",
		"MaxWeaponDamagePercent", "DirectAttackDamage", "DirectAttackDamagePercent", "GetHitAnimDisabled",
		"XPBoost", "InvisibleToSecurityTeleporters", "BodyScale", "ImmuneToShocked",
		"ImmuneToTaunted", "DistributeDamageAmongSquad", "DefenseBoostPhysicalDamage",
	};

	// Reflected attribute fields are the game's indices below 111; fields 111
	// and 112 carry the min/max weapon damage (upstream and darkspin).
	inline constexpr uint8_t ReflectedLimit = 111;

	// Same attribute, spelled differently by the game's nAttributeType (build
	// 103: indices 27, 36 and 62, where both numberings agree).
	inline constexpr std::array<std::pair<std::string_view, std::string_view>, 3> GameSpellings { {
		{ "AoeResistance", "AoEResistance" },
		{ "RejectModifiers", "RejectModifier" },
		{ "AoERadius", "AoeRadius" },
	} };

	constexpr uint8_t ServerIndex(std::string_view name) noexcept {
		for (const auto& [game, server] : GameSpellings) {
			if (name == game) {
				name = server;
				break;
			}
		}
		for (std::size_t i = 0; i < ServerNames.size(); ++i) {
			if (ServerNames[i] == name) {
				return static_cast<uint8_t>(i);
			}
		}
		return None;
	}

	class Map {
		public:
			constexpr Map() noexcept {
				for (std::size_t i = 0; i < 256; ++i) {
					mToServer[i] = static_cast<uint8_t>(i);
					mToGame[i] = static_cast<uint8_t>(i);
				}
			}

			// entries: (name, game index) pairs from the game's nAttributeType.
			// Names the server does not know, and server attributes the game
			// does not name, map to None.
			template<typename Entries>
			static constexpr Map FromGameNames(const Entries& entries) noexcept {
				Map map;
				map.mToServer.fill(None);
				map.mToGame.fill(None);
				map.mIdentity = false;
				for (const auto& [name, gameIndex] : entries) {
					const auto server = ServerIndex(name);
					if (server == None || gameIndex < 0 || gameIndex >= None) {
						continue;
					}
					map.mToServer[static_cast<uint8_t>(gameIndex)] = server;
					map.mToGame[server] = static_cast<uint8_t>(gameIndex);
				}
				return map;
			}

			// Index a script passed (nAttributeType.X) -> AttributeType value.
			constexpr uint8_t ToServer(uint32_t gameIndex) const noexcept {
				return gameIndex < 256 ? mToServer[gameIndex] : None;
			}

			// AttributeType value -> the game's index (None: the game has no such attribute).
			constexpr uint8_t ToGame(uint8_t serverIndex) const noexcept {
				return mToGame[serverIndex];
			}

			// Whether the client has a reflected field for this attribute.
			constexpr bool IsReflected(uint8_t serverIndex) const noexcept {
				const auto game = ToGame(serverIndex);
				return game != None && game < ReflectedLimit;
			}

			constexpr bool IsIdentity() const noexcept { return mIdentity; }

		private:
			std::array<uint8_t, 256> mToServer {};
			std::array<uint8_t, 256> mToGame {};
			bool mIdentity = true;
	};

	// Process-wide map, set once from the installed game's scripts at startup
	// (before any game runs) and read-only afterwards.
	inline Map& ActiveStorage() noexcept {
		static Map map;
		return map;
	}

	inline const Map& Active() noexcept {
		return ActiveStorage();
	}

	inline bool SetActiveOnce(const Map& map) {
		static std::once_flag once;
		bool set = false;
		std::call_once(once, [&]() { ActiveStorage() = map; set = true; });
		return set;
	}
}

#endif
