from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "darkspore_server/source"


class MissionProgressionContracts(unittest.TestCase):
    def test_beam_out_idempotently_counts_planets(self):
        source = (ROOT / "game/instance.cpp").read_text()
        block = source.split("void Instance::BeamOut(", 1)[1].split("void Instance::SendObjectCreate(", 1)[0]
        self.assertIn("if (!mChainData.IsCompleted())", block)
        self.assertIn("Progression::CompletePlanet(mChainData.GetProgression())", block)
        self.assertIn("mCashOutData.mPlanetsCompleted = mChainData.GetProgression()", block)
        self.assertIn("!mGameStarted || !mLevelLoaded", block)

    def test_bad_level_indices_cannot_access_level_names(self):
        source = (ROOT / "raknet/types.cpp").read_text()
        self.assertIn("return mLevelIndex < levelNames.size()", source)
        method = source.split("void ChainVoteData::SetLevelInstance(uint32_t index)", 1)[1].split(
            "uint32_t ChainVoteData::GetMarkerSet", 1
        )[0]
        self.assertIn("if (index < levelNames.size())", method)
