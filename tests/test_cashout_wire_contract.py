"""Cash Out packet must use build-103 byte offsets and actual rarity bands."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "darkspore_server/source"
HEADER = (ROOT / "game/instance.h").read_text(encoding="utf-8")
GAME = (ROOT / "game/instance.cpp").read_text(encoding="utf-8")
RULES = (ROOT / "game/mission_results.h").read_text(encoding="utf-8")


class CashOutWireContracts(unittest.TestCase):
    def test_struct_carries_all_recovered_fields(self):
        for item in [
            "mStartingExperiences", "mFinalExperiences", "mGoldMedals",
            "mSilverMedals", "mBronzeMedals", "mUpperRarityBoundaries",
            "mLowerRarityBoundaries", "mDailyBonuses", "mRewards",
            "uint32_t mDna = 0;",
        ]:
            self.assertIn(item, HEADER)

    def test_binary_offsets_and_integer_dna(self):
        writer = GAME.split("void CashOutData::WriteTo(", 1)[1].split(
            "// Instance", 1
        )[0]
        self.assertIn("bytes_to_bits(0x2C8)", writer)
        for offset in ["0x14", "0x34", "0x64", "0x84", "0x88", "0x90", "0x24", "0x08"]:
            self.assertIn(offset, writer)
        self.assertIn("Write(stream, mDna);", writer)
        self.assertIn("Write(stream, reward.rigblock);", writer)
        self.assertIn("Write(stream, reward.roll);", writer)

    def test_scores_feed_chance_boundaries_not_fabricated_percentages(self):
        beam = GAME.split("void Instance::BeamOut(", 1)[1].split(
            "bool Instance::SendObjectCreate(", 1
        )[0]
        self.assertIn("MissionResults::CashOutRarityBoundaries(", beam)
        self.assertIn("mCashOutData.mUpperRarityBoundaries[index] = bands.upper;", beam)
        self.assertIn("mCashOutData.mLowerRarityBoundaries[index] = bands.lower;", beam)
        self.assertIn("weighted / planetsCompleted", RULES)
        self.assertIn("completedIndex >= 17", RULES)


if __name__ == "__main__":
    unittest.main()
