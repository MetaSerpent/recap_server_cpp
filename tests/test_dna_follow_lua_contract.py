"""Lua nGameObject.DNAPickup must credit only validated live DNA objects."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
LUA = (ROOT / "darkspore_server/source/game/luafunctions.cpp").read_text(encoding="utf-8")
INSTANCE = (ROOT / "darkspore_server/source/game/instance.cpp").read_text(encoding="utf-8")
SCRIPT = (ROOT / "darkspore_server/res/data/serverdata/lua/DNA_Follow.lua").read_text(encoding="utf-8")


class DnaFollowLuaContract(unittest.TestCase):
    def test_original_lua_callback_exists_in_bridge(self):
        self.assertIn("L6_2 = L6_2.DNAPickup", SCRIPT)
        bridge = LUA.split("void Lua::RegisterFunctions() {", 1)[1].split(
            "void Lua::RegisterThread()", 1
        )[0]
        self.assertIn('mState["nGameObject"].get<sol::table>()', bridge)
        self.assertIn('gameObject["DNAPickup"]', bridge)
        self.assertIn("LuaGetObject(manager, lootValue)", bridge)
        self.assertIn("LuaGetObject(manager, characterValue)", bridge)
        self.assertIn("mGame.PickupDNA(loot, character)", bridge)

    def test_dna_pickup_requires_real_world_object_and_live_owner(self):
        handler = INSTANCE.split("bool Instance::PickupDNA(", 1)[1].split(
            "void Instance::DropLoot(const PlayerPtr&", 1
        )[0]
        for guard in [
            'loot->GetNounId() != utils::hash_id("DNA.noun")',
            "mObjectManager->Get(loot->GetId()) != loot",
            "mObjectManager->Get(character->GetId()) != character",
            "!character->IsPlayerControlled()",
            "character->GetHealth() <= 0.f",
            "player->GetDeployedCharacterObject() != character",
            "MissionResults::DecodeDnaPickup(lootData->GetDNAAmount())",
            "lootData->GetPart()",
        ]:
            self.assertIn(guard, handler)
        self.assertIn("PickupLoot(player, loot);", handler)
        self.assertIn("return loot->IsMarkedForDeletion();", handler)


if __name__ == "__main__":
    unittest.main()
