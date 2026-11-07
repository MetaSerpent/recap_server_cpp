"""Original client build 103 cash-out uses AB subtype zero with 0x2C8 bytes."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "darkspore_server/source"
SERVER = (ROOT / "raknet/server.cpp").read_text(encoding="utf-8")
LUA = (ROOT / "game/luafunctions.cpp").read_text(encoding="utf-8")


class CashOutSubtypeAndLuaResolution(unittest.TestCase):
    def test_cashout_receipt_is_subtype_zero(self):
        sender = SERVER.split("void Server::SendChainCashOutMessages(", 1)[1].split(
            "void Server::SendObjectivesInitForLevel(", 1
        )[0]
        self.assertIn("outStream.Write(PacketID::ChainCashOutMsgs);", sender)
        self.assertIn("if (value == 0)", sender)
        self.assertNotIn("if (value != 0)", sender)
        self.assertIn("mGame.GetCashOutData().WriteTo(outStream);", sender)
        self.assertEqual(SERVER.count("SendChainCashOutMessages(client, 0);"), 2)
        self.assertNotIn("SendChainCashOutMessages(client, 1);", SERVER)

    def test_numeric_lua_ids_do_not_require_userdata_conversions(self):
        helpers = LUA.split("// Helpers", 1)[1].split("// LuaFunction", 1)[0]
        for fn, cls in [
            ("LuaGetPlayer(", "Game::PlayerPtr"),
            ("LuaGetObject(", "Game::ObjectPtr"),
            ("LuaGetTrigger(", "Game::TriggerVolumePtr"),
        ]:
            method = helpers.split("auto " + fn, 1)[1].split("\n\t}", 1)[0]
            self.assertIn("value.valid()", method)
            self.assertIn("value.is<" + cls + ">()", method)
            self.assertIn("value.is<uint", method)
            self.assertLess(method.index("value.is<" + cls + ">()"),
                            method.index("value.as<" + cls + ">()"))


if __name__ == "__main__":
    unittest.main()
