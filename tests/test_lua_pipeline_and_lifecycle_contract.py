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

    def test_no_darkspore_script_content_is_embedded(self):
        # Scripts are converted from the user's own ServerData.package. Reconstructed
        # copies of proprietary scripts must not be shipped (see AGENTS.md).
        for source in (TRANSPILER, INSTALLER, LUA_CPP):
            self.assertNotIn('R"lua(', source)
            self.assertNotIn("GetCleanCitadelBossLaunchLua", source)
            self.assertNotIn("GetCleanTemplateAbilityConeLua", source)
            self.assertNotIn("nAbility_Citadel_Boss_Launch", source)
            self.assertNotIn("nAbility_Cone_Template", source)
        self.assertNotIn('stem == "ability_citadel_boss_launch"', INSTALLER)
        self.assertNotIn('stem == "template_ability_cone"', INSTALLER)

    def test_multiple_results_keep_nil_holes(self):
        # '{f()}' + 'unpack(t)' drops values after a nil; the count must be explicit.
        self.assertIn('{n = __lua51_select(\\"#\\", ...), ...}', TRANSPILER)
        self.assertIn('"__lua51_unpack(" + multi + ", 1, " + multi + ".n)"', TRANSPILER)
        self.assertNotIn('"unpack(_v"', TRANSPILER)
        self.assertNotIn('"for _i = 1, #_v"', TRANSPILER)

    def test_upvalue_close_is_refused_not_ignored(self):
        close_case = TRANSPILER[TRANSPILER.index("case 35: // CLOSE"):TRANSPILER.index("case 36: { // CLOSURE")]
        self.assertIn("return false;", close_case)

    def test_in_place_transpile_is_atomic(self):
        transpile_file = TRANSPILER[TRANSPILER.index("inline bool TranspileFile("):]
        self.assertIn('tempPath += ".transpile.tmp";', transpile_file)
        self.assertIn("std::filesystem::rename(tempPath, outputPath, ec);", transpile_file)

    def test_register_ability_accepts_the_game_call_form(self):
        register = LUA_FN[LUA_FN.index("void Lua::RegisterAbility()"):]
        register = register[:register.index('nAbility["RegisterAbility"] = registerAbilityLambda;')]
        self.assertIn("sol::variadic_args args", register)
        self.assertIn("AbilityRegistration::Resolve(args)", register)
        self.assertNotIn("sol::this_state L, sol::table abilityTable)", register)

    def test_hero_abilities_fall_back_to_player_class_data(self):
        player = (ROOT / "game/player.cpp").read_text(encoding="utf-8")
        self.assertIn("GetPlayerClassData()->GetAbility(", player)
        self.assertIn("case 0: return CreatureAbility(creature, 0);", player)
        self.assertIn("return CreatureAbility(creature, 2);", player)

    def test_errored_lua_threads_are_replaced_before_reuse(self):
        pool = LUA_CPP[LUA_CPP.index("void Lua::ReturnThreadToPool("):]
        pool = pool[:pool.index("constexpr size_t MAX_LUA_THREADS")]
        self.assertIn("LuaThreadNeedsReset(lua_status(thread->lua_state()))", pool)
        self.assertIn("thread->stop();", pool)
        self.assertIn("mThreads.emplace(thread->lua_state(), thread);", pool)

    def test_server_side_debug_and_client_stubs(self):
        for member in ('debug["IsAbilityDebugEnabled"]', 'debug["LogToConsole"]', 'client["GetMyPlayerObjectId"]',
                       'mState["nScenarioManager"]["GetIsClient"]'):
            self.assertIn(member, LUA_FN)

    def test_unrequired_helper_modules_are_preloaded(self):
        init = LUA_CPP[LUA_CPP.index("void Lua::Initialize()"):]
        init = init[:init.index("void Lua::Reload()")]
        self.assertIn('"Lua!AttributeUtils.lua", "Lua!NavigationUtils.lua"', init)

    def test_damage_and_heal_bindings_use_the_game_argument_order(self):
        block = LUA_FN[LUA_FN.index('gameObject["TakeDamage"]'):LUA_FN.index('gameObject["FullHealObject"]')]
        self.assertIn("CombatCall::ParseTakeDamage(args, error)", block)
        self.assertIn("CombatCall::ParseHealDamage(args, error)", block)
        # No sol::table parameter may receive an unchecked script value.
        self.assertNotIn("sol::table damageRange,", block)
        self.assertNotIn("sol::table healRange,", block)

    def test_release_builds_ship_debug_symbols(self):
        cmake = (ROOT.parents[0] / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("$<$<CONFIG:Release>:/Zi>", cmake)
        self.assertIn("/DEBUG /OPT:REF /OPT:ICF", cmake)
        self.assertIn("$<TARGET_PDB_FILE:recap_server>", cmake)

    def test_sol2_checks_bound_function_arguments_in_release(self):
        cmake = (ROOT.parents[0] / "CMakeLists.txt").read_text(encoding="utf-8")
        defines = next(line for line in cmake.splitlines() if line.startswith("add_definitions(-DSOL_"))
        self.assertIn("-DSOL_SAFE_FUNCTION_CALLS=1", defines)
        self.assertIn("-DSOL_SAFE_USERTYPE=1", defines)
        # sol2 3.3.1 has no SOL_SAFE_FUNCTION macro; defining it enables nothing.
        self.assertNotIn("-DSOL_SAFE_FUNCTION=1", defines)
        # Strict integer conversion would reject the fractional numbers scripts pass.
        self.assertNotIn("SOL_SAFE_NUMERICS", defines)
        self.assertNotIn("SOL_ALL_SAFETIES_ON", defines)

    def test_radius_queries_accept_a_type_or_a_table_of_types(self):
        self.assertNotIn("sol::optional<sol::table> objectTypes", LUA_FN)
        self.assertNotIn("float width, sol::optional<sol::table> types", LUA_FN)
        self.assertEqual(LUA_FN.count("Game::LuaTypeFilter::Read(rest, 0)"), 3)
        self.assertIn("LuaTypeFilter::Read(rest, 0), LuaFunction::RadiusSort::None", LUA_FN)

    def test_redistributable_fixture_is_executed_in_ci(self):
        workflow = (ROOT.parents[1] / ".github/workflows/gameplay-guardrails.yml").read_text(encoding="utf-8")
        self.assertIn("tests/lua51_transpiler_test.cpp", workflow)
        self.assertIn("diff -u tests/fixtures/lua51/transpiler_fixture.expected", workflow)

    def test_coroutine_lifecycle_cleans_stack_and_recycles_threads(self):
        # Coroutine return values must not leave values on the sol2 thread stack
        self.assertIn("lua_settop(lua_state(), 0);", LUA_CPP)
        self.assertIn("post_call();", LUA_H)
        self.assertIn("mLua.ReturnThreadToPool(this);", LUA_CPP)
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
