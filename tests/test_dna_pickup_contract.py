"""Only server-authored DNA amounts can modify durable account balances."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "darkspore_server/source"
OBJECT = (ROOT / "game/object.cpp").read_text(encoding="utf-8")
OBJECT_H = (ROOT / "game/object.h").read_text(encoding="utf-8")
INSTANCE = (ROOT / "game/instance.cpp").read_text(encoding="utf-8")


class DnaPickupContracts(unittest.TestCase):
    def test_loot_data_exposes_serialized_dna(self):
        self.assertIn("float GetDNAAmount() const;", OBJECT_H)
        self.assertIn("float LootData::GetDNAAmount() const", OBJECT)
        self.assertIn("return mDNAAmount;", OBJECT)
        self.assertIn("Write(stream, mDNAAmount);", OBJECT)

    def test_inventory_and_dna_saved_before_consuming_loot(self):
        body = INSTANCE.split("void Instance::PickupLoot(", 1)[1].split(
            "void Instance::PickupCatalyst(", 1
        )[0]
        for key in ["lootData->GetDNAAmount()", "MissionResults::DecodeDnaPickup(dnaAmount)",
                    "MissionResults::AddDna(", "user->AddPart(*part)", "if (!user->Save())",
                    "account.dna = previousDna;", "user->get_parts().data().pop_back();",
                    "object->MarkForDeletion();"]:
            self.assertIn(key, body)
        self.assertLess(body.index("if (!user->Save())"),
                        body.index("object->MarkForDeletion();"))
        self.assertLess(body.index("account.dna = previousDna;"),
                        body.index("object->MarkForDeletion();"))


if __name__ == "__main__":
    unittest.main()
