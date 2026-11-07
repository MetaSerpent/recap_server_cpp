"""Sequential chain re-deployment must rebuild state and retain chain rewards."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
INSTANCE = (ROOT / "darkspore_server/source/game/instance.cpp").read_text(encoding="utf-8")
SERVER = (ROOT / "darkspore_server/source/raknet/server.cpp").read_text(encoding="utf-8")
PLAYER = (ROOT / "darkspore_server/source/game/player.cpp").read_text(encoding="utf-8")
TYPES = (ROOT / "darkspore_server/source/raknet/types.cpp").read_text(encoding="utf-8")


class NextPlanetContracts(unittest.TestCase):
    def test_campaign_level_count_matches_progression_limit(self):
        names = TYPES.split("constexpr std::array<std::string_view, 25> levelNames", 1)
        self.assertEqual(len(names), 2)
        self.assertIn("FinalCampaignLevelIndex = 24", (
            ROOT / "darkspore_server/source/game/mission_progression.h"
        ).read_text(encoding="utf-8"))

    def test_completed_chain_restarts_world_instead_of_reusing_objects(self):
        stage = INSTANCE.split("bool Instance::PrepareNextPlanet(", 1)[1].split(
            "MarkerPtr Instance::GetMarker(", 1
        )[0]
        for line in [
            "Progression::CanDeployNextPlanet(", "player->ClearDungeonObjects()",
            "mLua.reset();", "mObjectManager.reset();", "mObjects.clear();",
            "mEnemyClearTracker.Reset();", "mDamageObjectives.Reset();",
            "mTouchedObelisks.clear();", "mScoredPlayers.clear();",
            "mLevelLoaded = false;", "mGameStarted = false;",
            "mChainData.SetCompleted(false);",
            "mChainData.SetLevelByIndex(mChainData.GetLevelIndex() + 1)",
            "std::make_unique<ObjectManager>(*this)", "mLua->Initialize();",
        ]:
            self.assertIn(line, stage)
        self.assertIn("if (startNewChain)", stage)
        self.assertIn("mChainData = RakNet::ChainVoteData{};", stage)
        self.assertIn("mCashOutData = CashOutData{};", stage)
        self.assertIn("state != GameState::Spaceship && state != GameState::ChainCashOut", stage)
        self.assertIn("object.reset();", PLAYER)

    def test_redeployment_boundary_precedes_hero_instantiation(self):
        start = SERVER.split("void Server::PrepareGameStart(", 1)[1].split(
            "void Server::SendHelloPlayer(", 1
        )[0]
        self.assertLess(start.index("mGame.PrepareNextPlanet(client->GetId(),"),
                        start.index("player->SetSquad(squad);"))
        self.assertIn("state != GameState::Spaceship && state != GameState::ChainVoting", start)
        self.assertIn("state == GameState::Spaceship", start)

    def test_chain_medals_accumulate_once_per_player_per_planet(self):
        beam = INSTANCE.split("void Instance::BeamOut(", 1)[1].split(
            "bool Instance::SendObjectCreate(", 1
        )[0]
        self.assertIn("mScoredPlayers.insert(player->GetId()).second", beam)
        self.assertIn("mCashOutData.mGoldMedals[index] += totals.gold;", beam)
        self.assertIn("mCashOutData.mSilverMedals[index] += totals.silver;", beam)
        self.assertIn("mCashOutData.mBronzeMedals[index] += totals.bronze;", beam)


if __name__ == "__main__":
    unittest.main()
