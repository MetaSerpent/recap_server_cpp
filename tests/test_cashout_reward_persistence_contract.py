"""Cash-out reward claims must be durably committed once per chain/user."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "darkspore_server/source"
INSTANCE = (ROOT / "game/instance.cpp").read_text(encoding="utf-8")
SERVER = (ROOT / "raknet/server.cpp").read_text(encoding="utf-8")
ACCOUNT_H = (ROOT / "sporenet/user.h").read_text(encoding="utf-8")
ACCOUNT = (ROOT / "sporenet/user.cpp").read_text(encoding="utf-8")


class CashOutRewardPersistence(unittest.TestCase):
    def test_receipt_identity_is_durable(self):
        self.assertIn("uint64_t lastCashOutReceiptId = 0;", ACCOUNT_H)
        self.assertIn('last_cashout_receipt_id");', ACCOUNT)
        self.assertIn('last_cashout_receipt_id", lastCashOutReceiptId', ACCOUNT)

    def test_generated_rewards_are_saved_before_client_receipt(self):
        method = INSTANCE.split("bool Instance::CommitCashOut(", 1)[1].split(
            "void Instance::BeamOut(", 1
        )[0]
        for key in [
            "MissionResults::MakeCashOutReceiptId(",
            "account.lastCashOutReceiptId == receiptId",
            "MissionResults::CashOutRewardCount(planets)",
            "MissionResults::CashOutRewardLevel(stage, planets)",
            "MissionResults::CashOutRewardRoll(receiptId, i)",
            "MissionResults::CashOutRewardRarity(roll, bands)",
            "SporeNet::Part part(",
            "part.SetLevel(", "part.SetRarity(",
            "user->AddPart(part);", "if (!user->Save())",
            "account.lastCashOutReceiptId = previousReceiptId;",
            "account.dna = previousDna;",
            "user->get_parts().data().resize(initialPartCount);",
            "mCashOutData.mRewards[slot] = receiptItems;",
        ]:
            self.assertIn(key, method)
        self.assertLess(method.index("if (!user->Save())"),
                        method.index("mCashOutData.mRewards[slot] = receiptItems;"))

        handler = SERVER.split("void Server::OnChainPlayerMsgs(", 1)[1].split(
            "void Server::OnCrystalDragMessage(", 1
        )[0]
        self.assertLess(handler.index("mGame.CommitCashOut(client->GetPlayer())"),
                        handler.index("SendChainCashOutMessages(client, 0);"))

    def test_chain_id_persists_over_planets_but_rotates_on_new_run(self):
        start = INSTANCE.split("bool Instance::Start()", 1)[1].split(
            "void Instance::Stop()", 1
        )[0]
        self.assertIn("RAND_bytes(", start)
        continue_chain = INSTANCE.split("bool Instance::PrepareNextPlanet(", 1)[1].split(
            "MarkerPtr Instance::GetMarker(", 1
        )[0]
        self.assertIn("if (startNewChain)", continue_chain)
        self.assertIn("mRunNonce = newNonce;", continue_chain)
        self.assertNotIn("mRunNonce = newNonce;", continue_chain.split(
            "if (startNewChain) {", 1)[0])


if __name__ == "__main__":
    unittest.main()
