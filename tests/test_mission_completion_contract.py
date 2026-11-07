"""Guard completion and cash-out message IDs, routing, and objective summaries."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "darkspore_server/source"
SERVER = (ROOT / "raknet/server.cpp").read_text(encoding="utf-8")
GAME = (ROOT / "game/instance.cpp").read_text(encoding="utf-8")

class MissionCompletionContract(unittest.TestCase):
    def test_cashout_has_distinct_packet_id(self):
        block = SERVER.split("void Server::SendChainCashOutMessages(", 1)[1].split(
            "void Server::SendObjectivesInitForLevel(", 1
        )[0]
        self.assertIn("outStream.Write(PacketID::ChainCashOutMsgs)", block)
        self.assertNotIn("outStream.Write(PacketID::ChainVoteMsgs)", block)

    def test_vote_two_enters_cashout_and_sends_payload(self):
        block = SERVER.split("void Server::OnChainPlayerMsgs(", 1)[1].split(
            "void Server::OnCrystalDragMessage(", 1
        )[0]
        self.assertIn("client->GetGameState() == GameState::ChainVoting", block)
        self.assertIn("SendReconnectPlayer(client, GameState::ChainCashOut);", block)
        self.assertIn("SendChainCashOutMessages(client, 1);", block)

    def test_repeated_or_out_of_dungeon_status_cannot_beam_out(self):
        block = SERVER.split("void Server::OnPlayerStatusUpdate(", 1)[1].split(
            "void Server::OnActionCommandMsgs(", 1
        )[0]
        self.assertIn("if (client->GetGameState() == GameState::Dungeon)", block)

    def test_objective_finish_sent_before_transition(self):
        block = GAME.split("void Instance::BeamOut(", 1)[1].split(
            "void Instance::", 1
        )[0]
        self.assertLess(block.index("SendObjectivesComplete(client);"),
                        block.index("SendReconnectPlayer(client, GameState::ChainVoting);"))

    def test_objective_summary_uses_bounded_shifts(self):
        block = SERVER.split("void Server::SendObjectivesComplete(", 1)[1].split(
            "void Server::SendObjectiveAdd(", 1
        )[0]
        self.assertIn("Game::MissionResults::AddPackedMedal(", block)
        self.assertNotIn("<< (8 * i)", block)

if __name__ == "__main__":
    unittest.main()
