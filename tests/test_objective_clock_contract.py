"""Check game clock initialization and objective event-rate semantics."""
from pathlib import Path
import unittest

INSTANCE = (Path(__file__).resolve().parents[1] /
            "darkspore_server/source/game/instance.cpp").read_text(encoding="utf-8")


class ObjectiveClockContracts(unittest.TestCase):
    def test_tick_clock_starts_at_game_time(self):
        startup = INSTANCE.split("bool Instance::Start() {", 1)[1].split(
            "void Instance::Stop() {", 1
        )[0]
        self.assertIn("mGameTime = mGameStartTime;", startup)
        self.assertIn("mGameTimeLua = mGameStartTime;", startup)

    def test_timer_only_updates_when_whole_seconds_change(self):
        update = INSTANCE.split("bool Instance::Update() {", 1)[1].split(
            "void Instance::MoveObject(", 1
        )[0]
        self.assertIn("if (objective.value != seconds)", update)
        self.assertLess(update.index("if (objective.value != seconds)"),
                        update.index("SendObjectiveUpdate(client, 0, 0)"))
        self.assertNotIn('vo_ship_obelisk_accessed', update)
        self.assertIn('for (const auto& [_, client] : mServer->GetClients())', update)


if __name__ == "__main__":
    unittest.main()
