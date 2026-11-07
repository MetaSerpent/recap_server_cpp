"""Guard the vendor's unfinished transaction parser against malformed input."""
from pathlib import Path
import unittest

API = (Path(__file__).resolve().parents[1] /
       "darkspore_server/source/game/api.cpp").read_text(encoding="utf-8")


class VendorTransactionContracts(unittest.TestCase):
    def test_vendor_checks_tokens_before_reading(self):
        fn = API.split("void API::game_inventory_vendorParts(", 1)[1].split(
            "void API::game_inventory_updatePartStatus(", 1
        )[0]
        self.assertLess(fn.index("if (transaction.size() < 2)"),
                        fn.index("transaction.front()"))
        self.assertIn("std::from_chars(", fn)
        self.assertIn("number == 0 || number > offers.size()", fn)
        self.assertIn("offers.GetPart(static_cast<size_t>(number - 1))", fn)
        self.assertNotIn("GetPart(data)", fn)

    def test_unknown_vendor_actions_do_not_create_blank_parts(self):
        fn = API.split("void API::game_inventory_vendorParts(", 1)[1].split(
            "void API::game_inventory_updatePartStatus(", 1
        )[0]
        self.assertIn("default:", fn)
        self.assertIn("continue;", fn.split("default:", 1)[1].split(
            "parts.data().push_back", 1
        )[0])


if __name__ == "__main__":
    unittest.main()
