"""All objective packets must be rendered from a client-specific snapshot."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "darkspore_server/source"
GAME = (ROOT / "game/instance.cpp").read_text(encoding="utf-8")
SERVER = (ROOT / "raknet/server.cpp").read_text(encoding="utf-8")


class PlayerObjectiveIsolation(unittest.TestCase):
    def test_damage_events_only_send_to_correct_client(self):
        code = GAME.split("void Instance::OnDamageDealt(", 1)[1].split("void Instance::OnObjectDeath(", 1)[0]
        self.assertIn("mServer->GetClient(playerId)", code)
        self.assertNotIn("objective.value =", code)
        self.assertIn("SendObjectiveUpdate(client,", code)

    def test_current_and_final_snapshots_are_per_player(self):
        method = GAME.split("Instance::GetObjectivesForPlayer(", 1)[1].split(
            "void Instance::OnDamageDealt(", 1
        )[0]
        self.assertIn("auto objectives = mObjectives;", method)
        self.assertIn("GetHitCount(playerId)", method)
        self.assertIn("GetHugeHitCount(playerId)", method)
        self.assertIn("ScoreDamageOften(playerId, major)", method)
        self.assertIn("ScoreHugeDamage(playerId)", method)

    def test_every_packet_uses_own_client_snapshot(self):
        sections = (
            ("SendObjectivesInitForLevel", "SendObjectiveUpdate", False),
            ("SendObjectiveUpdate", "SendObjectivesComplete", False),
            ("SendObjectivesComplete", "SendObjectiveAdd", True),
        )
        for start, end, complete in sections:
            code = SERVER.split("void Server::" + start + "(", 1)[1].split(
                "void Server::" + end + "(", 1
            )[0]
            expected = "mGame.GetObjectivesForPlayer(client->GetId(), " + str(complete).lower() + ")"
            self.assertIn(expected, code, start)
            self.assertNotIn("mGame.GetObjectives();", code)

    def test_no_fabricated_cash_out_money(self):
        code = GAME.split("void Instance::BeamOut(", 1)[1].split(
            "bool Instance::SendObjectCreate(", 1
        )[0]
        self.assertIn("mCashOutData.mDna = 0;", code)
        self.assertIn("MissionResults::CountMedals(", code)
        self.assertNotIn("mCashOutData.mDna = 50;", code)


if __name__ == "__main__":
    unittest.main()
