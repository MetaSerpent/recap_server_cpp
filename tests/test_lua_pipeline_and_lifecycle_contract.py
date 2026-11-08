"""Regression and contract tests for Lua asset transpilation, bindings, and coroutine lifecycle."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "darkspore_server/source"
TRANSPILER = (ROOT / "game/lua51_transpiler.h").read_text(encoding="utf-8")
LUA_CPP = (ROOT / "game/lua.cpp").read_text(encoding="utf-8")
LUA_H = (ROOT / "game/lua.h").read_text(encoding="utf-8")
LUA_FN = (ROOT / "game/luafunctions.cpp").read_text(encoding="utf-8")
INSTALLER = (ROOT / "installer.cpp").read_text(encoding="utf-8")


class LuaPipelineAndLifecycleContracts(unittest.TestCase):
    def test_transpiler_header_and_magic_present(self):
        self.assertIn("inline bool IsLua51Bytecode(std::string_view data)", TRANSPILER)
        self.assertIn('"\\x1bLua"', TRANSPILER)
        self.assertIn("ver == 0x51", TRANSPILER)

    def test_known_broken_script_repairs_present(self):
        self.assertIn("GetKnownBrokenScriptRepair", TRANSPILER)
        self.assertIn("ability_citadel_boss_launch", TRANSPILER)
        self.assertIn("template_ability_cone", TRANSPILER)
        self.assertIn("GetCleanCitadelBossLaunchLua()", TRANSPILER)
        self.assertIn("GetCleanTemplateAbilityConeLua()", TRANSPILER)

    def test_coroutine_lifecycle_cleans_stack_and_recycles_threads(self):
        # Coroutine return values must not leave values on the sol2 thread stack
        self.assertIn("lua_settop(lua_state(), 0);", LUA_CPP)
        self.assertIn("mLua.ReturnThreadToPool(this);", LUA_H)
        self.assertIn("mCoroutine.status() != sol::call_status::yielded", LUA_CPP)

    def test_native_engine_namespaces_registered(self):
        for ns in [
            'mState.create_named_table("nBit");',
            'mState.create_named_table("nMathUtil");',
            'mState.create_named_table("nUtil");',
            'mState.create_named_table("nGameObject");',
            'mState.create_named_table("nObjectManager");',
            'mState.create_named_table("nThread");',
            'mState.create_named_table("Ability");',
            'mState.create_named_table("Objective");',
        ]:
            self.assertIn(ns, LUA_FN)

    def test_installer_handles_modifiers_and_behaviors(self):
        self.assertIn("TranspileServerDataDirectory", INSTALLER)
        self.assertIn("data/serverdata/Modifiers", INSTALLER)
        self.assertIn("data/serverdata/behaviors", INSTALLER)

    def test_combat_damage_feeds_objective_tracking(self):
        self.assertIn("game.OnDamageDealt(attacker, object, appliedDamage);", LUA_FN)
        self.assertIn("mGame.OnDamageDealt(killer, obj, prevHp);", LUA_FN)


if __name__ == "__main__":
    unittest.main()
