"""Contracts for hit feedback and ability animation (client session 7 findings)."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "darkspore_server/source"
LUA_FN = (SRC / "game/luafunctions.cpp").read_text(encoding="utf-8")
LUA_CPP = (SRC / "game/lua.cpp").read_text(encoding="utf-8")
SERVER = (SRC / "raknet/server.cpp").read_text(encoding="utf-8")
EVENT_CPP = (SRC / "game/serverevent.cpp").read_text(encoding="utf-8")
EVENT_H = (SRC / "game/serverevent.h").read_text(encoding="utf-8")


class CombatEventWireContracts(unittest.TestCase):
    def test_flags_are_written_as_16_bits(self):
        write = EVENT_CPP.split("void CombatEvent::WriteReflection", 1)[1]
        self.assertIn("reflector.write<0>(static_cast<uint16_t>(", write)
        self.assertNotIn("reflector.write<0>(mFlags);", write)

    def test_damage_events_carry_source_and_hit_flags(self):
        self.assertIn("Hit = 0x000001,", EVENT_H)
        damage = LUA_FN.split("auto TakeDamage(sol::this_state L,", 1)[1].split("auto Heal(sol::this_state L,", 1)[0]
        self.assertIn("combatEvent.mSourceId = attacker ? attacker->GetId() : 0;", damage)
        self.assertIn("static_cast<uint32_t>(Game::CombatEventFlags::Hit)", damage)
        self.assertIn("Game::CombatEventFlags::KillingBlow", damage)

    def test_animation_state_packet_ends_with_zero(self):
        send = SERVER.split("void Server::SendAnimationState(", 1)[1].split("void Server::SendObjectGfxState", 1)[0]
        self.assertIn("Write<uint32_t>(outStream, 0);", send)
        self.assertNotIn("Write<uint32_t>(outStream, state); // some client data?", send)


class AbilityAnimationContracts(unittest.TestCase):
    def test_preload_animation_returns_the_animation_id(self):
        self.assertNotIn('["PreloadAnimation"] = [](sol::variadic_args) {};', LUA_FN)
        self.assertEqual(LUA_FN.count("return utils::hash_id(args[0].as<std::string>().c_str());"), 2)

    def test_sequence_index_is_zero_based_and_per_cast(self):
        self.assertNotIn('nAbility["GetAnimationSequenceIndex"] = [](sol::variadic_args) -> int32_t { return 1; };', LUA_FN)
        self.assertIn("ab->GetActiveSequenceIndex()", LUA_FN)
        tick = LUA_CPP.split("bool Ability::Tick(", 1)[1].split("thread->call<void>", 1)[0]
        self.assertIn("mActiveSequenceIndex = SelectSequenceIndex(object);", tick)
        self.assertIn("AnimationSequence::NextIndex(selection, entryCount", LUA_CPP)

    def test_play_animation_sequence_accepts_the_game_form(self):
        play = LUA_FN.split('nAbility["PlayAnimationSequence"] = ', 1)[1][:900]
        self.assertIn("sol::variadic_args args", play)
        self.assertIn("if (args.size() == 0)", play)
        self.assertIn("ab->GetActiveSequenceAnimation()", play)


class BindingCallFormContracts(unittest.TestCase):
    def test_attribute_modifiers_use_handles(self):
        block = LUA_FN.split('nAttribute["AddAttributeModifier"] = ', 1)[1].split('nAttribute["GetHealthMultiplierForDifficulty"]', 1)[0]
        self.assertIn("return mAttributeModifiers.Add(obj->GetId(), attrIdx, delta);", block)
        self.assertIn("mAttributeModifiers.Remove(handle.as<uint32_t>())", block)

    def test_thread_data_accepts_numeric_slots(self):
        block = LUA_FN.split('nThreadData["GetFloat"] = ', 1)[1].split("// nModifier", 1)[0]
        self.assertNotIn("std::string key", block)
        self.assertGreaterEqual(block.count("ThreadDataKey(key)"), 6)
        self.assertIn('thread->get_value("__private_table")', LUA_FN)

    def test_short_call_forms_do_not_raise(self):
        for name in ("RemoveCooldownTime", "ScaleCooldownTime"):
            self.assertIn('nAbility["%s"] = [this](sol::this_state L, sol::variadic_args args)' % name, LUA_FN)
        self.assertIn('gameObject["SetOrientation"] = [this](sol::variadic_args args)', LUA_FN)
        self.assertIn('nPlayer["GetCurrentDeckIndex"] = [scriptPlayer](sol::this_state L, sol::variadic_args args)', LUA_FN)
        self.assertIn("LogUnsupportedCallForm(", LUA_FN)

    def test_new_logic_is_tested_in_ci(self):
        workflow = (ROOT / ".github/workflows/gameplay-guardrails.yml").read_text(encoding="utf-8")
        self.assertIn("tests/combat_presentation_test.cpp", workflow)



class CastResponseContracts(unittest.TestCase):
    def setUp(self):
        self.handler = SERVER.split("case ActionCommand::UseCharacterAbility:", 1)[1].split("case ActionCommand::UseSquadAbility:", 1)[0]

    def test_every_refused_cast_is_rejected(self):
        self.assertIn("const uint8_t syncStamp = command.data.unk[0];", self.handler)
        self.assertIn("ActionResponse::Reject(syncStamp, command.data.objectId)", self.handler)
        # invalid index, no ability for the slot, and a refused UseAbility
        self.assertEqual(self.handler.count("rejectCast();"), 3)
        for refusal in ("IsValidAbilityIndex(command.ability.index)) {", "cast ignored.", "if (!mGame.UseAbility(object, combatData)) {"):
            after = self.handler.split(refusal, 1)[1]
            self.assertLess(after.index("rejectCast();"), after.index("break;"))

    def test_accepted_casts_are_timed_from_ability_data_and_released(self):
        self.assertIn("ability->GetActiveCastTiming(hitMs, releaseMs)", self.handler)
        self.assertIn("ActionResponse::Type::Accepted, syncStamp, combatData.abilityId", self.handler)
        self.assertIn("released.type = ActionResponse::Type::Released;", self.handler)
        self.assertIn("mGame.AddTask(releaseMs,", self.handler)
        self.assertIn("std::weak_ptr<Client> weakClient = client;", self.handler)
        # the disabled upstream packet must not come back
        self.assertNotIn("//SendActionCommandResponse(client, actionResponse);", self.handler)

    def test_timing_reads_the_sequence_entry(self):
        timing = LUA_CPP.split("bool Ability::GetActiveCastTiming(", 1)[1].split("// Objective", 1)[0]
        self.assertIn('entryTable["hit"]', timing)
        self.assertIn('entryTable["release"]', timing)
        self.assertIn('mSelf["timetorelease"]', timing)
        self.assertIn("if (release <= 0.f) {", timing)


class TargetValidationContracts(unittest.TestCase):
    def test_first_argument_is_a_team_number(self):
        block = LUA_FN.split("const auto casterTeam = ", 1)[1].split("// Game scripts call TakeDamage", 1)[0]
        self.assertIn("if (value.get_type() == sol::type::number) {", block)
        self.assertIn("TeamValidation::IsHostile(casterTeam(teamOrAgent), target->GetTeam())", block)
        self.assertIn("TeamValidation::IsFriendly(casterTeam(teamOrAgent), target->GetTeam())", block)



class PreloadAndCompatContracts(unittest.TestCase):
    def test_preload_asset_and_modifier_return_ids(self):
        # 1,016 PreloadAsset and 251 PreloadModifier call sites store the result
        # (effect / modifier ids); nil broke AddEffect and modifier requests.
        self.assertNotIn('["PreloadAsset"] = [](sol::variadic_args) {};', LUA_FN)
        self.assertNotIn('["PreloadModifier"] = [](sol::variadic_args) {};', LUA_FN)
        self.assertEqual(LUA_FN.count("LuaExtractAssetOrGuid(sol::object(args[0]), 0)"), 7)

    def test_lua51_math_mod_alias_for_luajit(self):
        self.assertIn("if math.mod == nil then math.mod = math.fmod end", LUA_CPP)



class HeroStatsAndTimeContracts(unittest.TestCase):
    def test_hero_weapon_damage_comes_from_class_data(self):
        obj = (SRC / "game/object.cpp").read_text(encoding="utf-8")
        self.assertNotIn("mAttributes->SetWeaponDamage(1, 5);", obj)
        self.assertIn("HeroStats::WeaponDamage(data->GetWeaponMinDamage(), data->GetWeaponMaxDamage())", obj)

    def test_sim_time_dt_is_the_lua_frame_time(self):
        self.assertIn('["GetSimTimeDt"] = [this](sol::variadic_args) -> float {', LUA_FN)
        self.assertIn("return mGame.GetLuaFrameSeconds();", LUA_FN)
        inst = (SRC / "game/instance.cpp").read_text(encoding="utf-8")
        self.assertIn("mLuaFrameSeconds = static_cast<float>(delta) / 1000.f;", inst)


if __name__ == "__main__":
    unittest.main()
