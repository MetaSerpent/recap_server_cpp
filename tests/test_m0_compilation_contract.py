"""M0 compile regression contracts; full compilation is enforced by platform CI."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
LUA = (ROOT / "darkspore_server/source/game/lua.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "darkspore_server/source/game/lua.h").read_text(encoding="utf-8")
BINDINGS = (ROOT / "darkspore_server/source/game/luafunctions.cpp").read_text(encoding="utf-8")
CMAKE = (ROOT / "darkspore_server/CMakeLists.txt").read_text(encoding="utf-8")
MAIN = (ROOT / "darkspore_server/source/main.cpp").read_text(encoding="utf-8")


class M0CompileContracts(unittest.TestCase):
    def test_lua_transpiler_types_match(self):
        self.assertIn("IsLua51Bytecode(sourceText)", LUA)
        self.assertIn("if (repaired && !repaired->empty())", LUA)
        self.assertIn("sourceText = std::move(*repaired);", LUA)
        self.assertIn("hexId && *hexId != 0", LUA)
        self.assertNotIn("std::span<const uint8_t>", LUA)

    def test_lua_uses_existing_game_api(self):
        self.assertNotIn("GetPlayerByObject", LUA)
        self.assertNotIn("AttributeType::Cooldown)", LUA)
        self.assertIn("mActiveAgent->AddCooldown(mId, durationMs)", LUA)
        self.assertIn("AttributeType::CooldownScale", LUA)
        self.assertIn("SendCooldownUpdate(mActiveAgent, mId, endMs - actualDuration, actualDuration)", LUA)

    def test_lua_coroutine_does_not_call_incomplete_type(self):
        self.assertNotIn("mLua.ReturnThreadToPool(this)", HEADER)
        self.assertIn("post_call();", HEADER)

    def test_bindings_use_existing_public_endpoints(self):
        self.assertNotIn("mGame.PickupLoot(loot, character)", BINDINGS)
        self.assertIn("mGame.InteractWithObject(player, loot->GetId())", BINDINGS)
        self.assertNotIn("mGame.CompleteObjective(1)", BINDINGS)
        self.assertIn("mGame.BeamOut(player)", BINDINGS)
        self.assertNotIn("value.get_or<", BINDINGS)
        self.assertNotIn("ev.as<sol::table>().get_or<", BINDINGS)

    def test_build_has_a_git_revision_identifier(self):
        self.assertIn("RECAP_BUILD_GIT_SHA", CMAKE)
        self.assertIn("git rev-parse --short=12 HEAD", CMAKE)
        self.assertIn("RECAP_BUILD_GIT_SHA", MAIN)


if __name__ == "__main__":
    unittest.main()
