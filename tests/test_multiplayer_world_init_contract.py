"""World creation happens once per instance; late clients receive state snapshots."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "darkspore_server/source/game"


class MultiplayerLevelInitialization(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.instance = (ROOT / "instance.cpp").read_text(encoding="utf-8")
        cls.level = (ROOT / "level.cpp").read_text(encoding="utf-8")

    def test_restart_resets_shared_state(self):
        fn = self.instance.split("bool Instance::Start() {", 1)[1].split(
            "void Instance::Stop()", 1
        )[0]
        self.assertIn("mGameStarted = false;", fn)
        self.assertIn("mLevelLoaded = false;", fn)
        self.assertIn("mPlayerSpawnpoints.clear();", fn)

    def test_shared_objects_spawn_once(self):
        fn = self.instance.split("void Instance::OnPlayerStart(", 1)[1].split(
            "uint32_t Instance::AddTask(", 1
        )[0]
        self.assertIn("const bool initializeWorld = !mGameStarted;", fn)
        self.assertIn("if (initializeWorld) {", fn)
        self.assertEqual(fn.count("mGameStarted = LoadLevel();"), 1)
        self.assertLess(fn.index("if (initializeWorld) {", fn.index("mGameStarted = LoadLevel();")),
                        fn.index("const auto TestEnemy"))

    def test_new_player_gets_existing_world_objects(self):
        fn = self.instance.split("void Instance::OnPlayerStart(", 1)[1].split(
            "uint32_t Instance::AddTask(", 1
        )[0]
        self.assertIn("mServer->SendObjectCreate(client, object);", fn)
        self.assertIn("mServer->SendCombatantDataUpdate(client, object, *data);", fn)
        self.assertIn("mServer->SendLootDataUpdate(client, object, *data);", fn)

    def test_level_reload_does_not_keep_old_markers(self):
        self.assertIn("mMarkersets.clear();", self.level)


if __name__ == "__main__":
    unittest.main()
