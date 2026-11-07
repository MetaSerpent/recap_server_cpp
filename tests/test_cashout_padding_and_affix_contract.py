"""Missing loot affixes and cash-out padding must serialize as literal zero."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "darkspore_server/source"
PART = (ROOT / "sporenet/part.cpp").read_text(encoding="utf-8")
INSTANCE = (ROOT / "game/instance.cpp").read_text(encoding="utf-8")


class EmptyLootFields(unittest.TestCase):
    def test_no_prefix_or_suffix_has_zero_hash(self):
        prefix = PART.split("void Part::SetPrefix(", 1)[1].split(
            "void Part::SetSuffix(", 1
        )[0]
        suffix = PART.split("void Part::SetSuffix(", 1)[1].split(
            "void Part::SetCreationDate(", 1
        )[0]
        self.assertIn("mPrefixAssetHash = prefix != 0 ? utils::hash_id(prefix_string) : 0;", prefix)
        self.assertIn("mPrefixSecondaryAssetHash = prefix != 0 ? utils::hash_id(prefix_string) : 0;", prefix)
        self.assertIn("mSuffixAssetHash = suffix != 0 ? utils::hash_id(suffix_string) : 0;", suffix)

    def test_cashout_wire_buffer_is_explicitly_zero_initialized(self):
        serializer = INSTANCE.split("void CashOutData::WriteTo(", 1)[1].split(
            "// Instance", 1
        )[0]
        self.assertIn("ReallocateStream(stream, size)", serializer)
        self.assertIn("for (std::size_t i = 0; i < 0x2C8; ++i)", serializer)
        self.assertIn("Write<uint8_t>(stream, 0);", serializer)
        self.assertLess(serializer.index("Write<uint8_t>(stream, 0);"),
                        serializer.index("Write(stream, mPlanetsCompleted);"))


if __name__ == "__main__":
    unittest.main()
