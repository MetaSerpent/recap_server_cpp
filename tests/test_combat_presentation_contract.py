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


if __name__ == "__main__":
    unittest.main()
