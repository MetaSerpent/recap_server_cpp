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
        self.case = SERVER.split("case ActionCommand::UseCharacterAbility:", 1)[1].split("case ActionCommand::UseSquadAbility:", 1)[0]
        self.handler = SERVER.split("void Server::HandleCharacterAbility(", 1)[1].split("void Server::ScheduleBasicAttackRetry(", 1)[0]
        self.start = SERVER.split("Game::CastResult Server::StartAbilityCast(", 1)[1].split("void Server::HandleCharacterAbility(", 1)[0]
        self.squad = SERVER.split("case ActionCommand::UseSquadAbility:", 1)[1].split("// Catalyst pickup", 1)[0]

    def test_hero_case_goes_through_the_handler(self):
        self.assertIn("HandleCharacterAbility(client, player, object, command, 0);", self.case)

    def test_every_refused_cast_is_rejected(self):
        self.assertIn("ActionResponse::Reject(command.data.unk[0], command.data.objectId)", self.handler)
        # invalid index, no ability for the slot, and a refusal that is not queued
        self.assertEqual(self.handler.count("rejectCast();"), 3)
        for refusal in ("IsValidAbilityIndex(command.ability.index)) {", "cast ignored."):
            after = self.handler.split(refusal, 1)[1]
            self.assertLess(after.index("rejectCast();"), after.index("return;"))
        self.assertTrue(self.handler.rstrip().endswith("rejectCast();\n\t}"))

    def test_accepted_casts_are_timed_from_ability_data_and_released(self):
        self.assertIn("const auto result = mGame.UseAbility(object, combatData);", self.start)
        self.assertIn("ability->GetActiveCastTiming(hitMs, releaseMs)", self.start)
        self.assertIn("ActionResponse::Type::Accepted, command.data.unk[0], abilityId", self.start)
        self.assertIn("released.type = ActionResponse::Type::Released;", self.start)
        self.assertIn("mGame.AddTask(releaseMs,", self.start)
        self.assertIn("std::weak_ptr<Client> weakClient = client;", self.start)
        # the disabled upstream packet must not come back
        self.assertNotIn("//SendActionCommandResponse(client, actionResponse);", SERVER)

    def test_squad_casts_use_the_build_103_responses(self):
        self.assertIn("StartAbilityCast(client, object, command, abilityId); result != Game::CastResult::Started", self.squad)
        self.assertEqual(self.squad.count("reject();"), 3)
        for body in (self.squad, self.start, self.handler):
            self.assertNotIn("AbilityCommandResponse response", body)


class HeldBasicAttackContracts(unittest.TestCase):
    def setUp(self):
        self.inst = (SRC / "game/instance.cpp").read_text(encoding="utf-8")
        self.handler = SERVER.split("void Server::HandleCharacterAbility(", 1)[1].split("void Server::ScheduleBasicAttackRetry(", 1)[0]
        self.retry = SERVER.split("void Server::ScheduleBasicAttackRetry(", 1)[1].split("void Server::ReplayPendingBasicAttack(", 1)[0]
        self.replay = SERVER.split("void Server::ReplayPendingBasicAttack(", 1)[1].split("void Server::OnActionCommandMsgs(", 1)[0]

    def test_use_ability_reports_why_a_cast_did_not_start(self):
        use = self.inst.split("CastResult Instance::UseAbility(", 1)[1].split("\n\t}\n", 1)[0]
        self.assertIn("return CastResult::OnCooldown;", use)
        self.assertIn("thread->status() != sol::thread_status::dead) {\n\t\t\treturn CastResult::Busy;", use)
        self.assertIn("? CastResult::Started : CastResult::Unavailable;", use)

    def test_busy_basic_attack_is_kept_with_one_retry_chain(self):
        self.assertIn("Game::CastQueue::OnRefused(result, command.ability.index) == Game::CastQueue::Refusal::KeepAsPending", self.handler)
        self.assertIn("Game::CastQueue::ShouldRetry(result, attempt)", self.handler)
        self.assertIn("mPendingBasicAttacks.try_emplace(player->GetId(), command); !inserted) {", self.handler)
        # the superseded attack is answered, then replaced by the latest one
        superseded = self.handler.split("!inserted) {", 1)[1]
        self.assertLess(superseded.index("ActionResponse::Reject(pending->second.data.unk[0]"), superseded.index("pending->second = command;"))
        self.assertIn("if (mBasicAttackRetries.insert(player->GetId()).second) {", self.handler)
        self.assertIn("mGame.AddTask(Game::CastQueue::RetryIntervalMs,", self.retry)
        self.assertIn("mBasicAttackRetries.erase(playerId);", self.retry)

    def test_release_starts_the_pending_attack(self):
        start = SERVER.split("Game::CastResult Server::StartAbilityCast(", 1)[1].split("void Server::HandleCharacterAbility(", 1)[0]
        release = start.split("released.type = ActionResponse::Type::Released;", 1)[1]
        self.assertLess(release.index("SendActionCommandResponse(target, released);"), release.index("ReplayPendingBasicAttack(target, 0);"))

    def test_replay_revalidates_ownership_and_disconnect_clears_it(self):
        self.assertIn("mPendingBasicAttacks.erase(it);", self.replay)
        self.assertIn("object->GetPlayerIndex() != player->GetId()", self.replay)
        self.assertIn("IsMarkedForDeletion()", self.replay)
        remove = SERVER.split("void Server::RemoveClient(Packet* packet) {", 1)[1].split("ClientPtr Server::GetClient(", 1)[0]
        self.assertLess(remove.index("mPendingBasicAttacks.erase(player->GetId());"), remove.index("mClients.erase(packet->systemAddress);"))

    def test_policy_is_tested_in_ci(self):
        test = (ROOT / "tests/action_response_test.cpp").read_text(encoding="utf-8")
        self.assertIn('#include "../darkspore_server/source/game/cast_queue.h"', test)
        self.assertIn("OnRefused(CastResult::Busy, 0) == Refusal::KeepAsPending", test)

class CastTimingContracts(unittest.TestCase):
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


class Session10Contracts(unittest.TestCase):
    def test_transform_vector_accepts_the_game_float_form(self):
        # 7 scripts call TransformVector(x, y, z, GetOrientation(obj)); the
        # vec3/quat-only binding raised in template_ability_projectile.lua.
        self.assertIn("&LuaFunction::nMathUtil::TransformVectorFloats", LUA_FN)
        self.assertNotIn("return v * glm::eulerAngles(q);", LUA_FN)
        self.assertEqual(LUA_FN.count("Game::OrientationMath::Rotate("), 2)
        test = (ROOT / "tests/combat_presentation_test.cpp").read_text(encoding="utf-8")
        self.assertIn('#include "../darkspore_server/source/game/orientation_math.h"', test)

    def test_refused_casts_are_logged_with_a_reason(self):
        handler = SERVER.split("void Server::HandleCharacterAbility(", 1)[1].split("void Server::ScheduleBasicAttackRetry(", 1)[0]
        squad = SERVER.split("case ActionCommand::UseSquadAbility:", 1)[1].split("// Catalyst pickup", 1)[0]
        self.assertIn("Game::CastQueue::Describe(result)", handler)
        self.assertIn("superseded by a newer held attack", handler)
        self.assertIn("Game::CastQueue::Describe(result)", squad)


if __name__ == "__main__":
    unittest.main()
