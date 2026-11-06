"""Validate portable data shipped with the C++ ReCap server.

Only check parseability and presence. These do not assert proprietary game
assets are present or prove actual gameplay compatibility.
"""
import json
from pathlib import Path
import unittest
import xml.etree.ElementTree as ET

DATA = Path(__file__).resolve().parents[1] / "darkspore_server" / "res" / "data"


class BundledDataTests(unittest.TestCase):
    def test_assets_catalog_is_well_formed_xml(self):
        self.assertTrue((DATA / "assets_catalog.xml").is_file())
        root = ET.parse(DATA / "assets_catalog.xml").getroot()
        self.assertIsNotNone(root)
        self.assertTrue(root.tag)

    def test_creature_templates_are_parseable_json(self):
        for name in ("creature_templates.json", "creature_parts_templates.json"):
            with self.subTest(name=name):
                with (DATA / name).open(encoding="utf-8") as f:
                    parsed = json.load(f)
                self.assertIsInstance(parsed, (dict, list))
                self.assertTrue(parsed)

    def test_lua_resource_scripts_are_present(self):
        scripts = DATA / "serverdata" / "lua"
        self.assertTrue(scripts.is_dir())
        self.assertGreater(len(list(scripts.glob("*.lua"))), 10)


if __name__ == "__main__":
    unittest.main()
