"""Attribute numbering: the server translates between its AttributeType and the
installed game's nAttributeType numbering (game/attribute_layout.h)."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "darkspore_server/source"
LAYOUT = (SRC / "game/attribute_layout.h").read_text(encoding="utf-8")
ATTR_H = (SRC / "game/attributes.h").read_text(encoding="utf-8")
ATTR_CPP = (SRC / "game/attributes.cpp").read_text(encoding="utf-8")
CHARACTER = (SRC / "game/character.cpp").read_text(encoding="utf-8")
LUA_FN = (SRC / "game/luafunctions.cpp").read_text(encoding="utf-8")
LUA_CPP = (SRC / "game/lua.cpp").read_text(encoding="utf-8")


def server_enum_names():
    body = ATTR_H.split("enum class AttributeType", 1)[1].split("{", 1)[1].split("};", 1)[0]
    names = []
    for raw in body.split(","):
        token = re.sub(r"//.*", "", raw).strip().strip("{").strip()
        if not token:
            continue
        name = token.split("=")[0].strip()
        if name in ("ClientCount", "Count"):
            continue
        names.append(name)
    return names


class AttributeNameTable(unittest.TestCase):
    def test_server_names_follow_the_enum(self):
        table = LAYOUT.split("ServerNames {", 1)[1].split("};", 1)[0]
        names = re.findall(r'"([A-Za-z]+)"', table)
        self.assertEqual(names, server_enum_names())
        self.assertEqual(len(names), 115)


class AttributeBoundaries(unittest.TestCase):
    def test_scripts_indices_are_translated(self):
        # nAttribute (3), nAbility (1), Attributes and Object usertype accessors (4)
        self.assertEqual(LUA_FN.count("Game::AttributeLayout::Active().ToServer("), 8)
        self.assertNotIn("sol::resolve<float(uint8_t) const>(&Object::GetAttributeValue)", LUA_FN)
        for binding in ('nAttribute["GetAttributeValue"]', 'nAttribute["AddAttributeModifier"]',
                        'nAbility["GetAbilityAttributeValue"]'):
            block = LUA_FN.split(binding, 1)[1][:500]
            self.assertIn("ToServer(scriptIdx)", block)

    def test_every_server_attribute_is_stored(self):
        set_value = ATTR_CPP.split("void Attributes::SetValue(uint8_t idx, float value) {", 1)[1].split("void Attributes::SetValue(AttributeType", 1)[0]
        self.assertIn("if (idx >= static_cast<uint8_t>(AttributeType::Count)) {", set_value)
        self.assertNotIn("if (idx < static_cast<uint8_t>(AttributeType::ClientCount)) {", set_value)
        # erasing a missing entry was undefined behaviour
        self.assertIn("erased != mErasedData.end()", set_value)
        self.assertIn("std::bitset<static_cast<std::size_t>(AttributeType::Count)> mDataBits;", ATTR_H)

    def test_client_sees_the_game_numbering(self):
        reflection = ATTR_CPP.split("void Attributes::WriteReflection(", 1)[1]
        self.assertIn("layout.IsReflected(idx)", reflection)
        self.assertIn("fields.emplace_back(layout.ToGame(idx), value);", reflection)
        self.assertIn("reflector.write<111>(mMinWeaponDamage);", reflection)
        write_to = ATTR_CPP.split("void Attributes::WriteTo(", 1)[1].split("void Attributes::WriteReflection(", 1)[0]
        self.assertIn("Write(stream, GetValue(layout.ToServer(slot)));", write_to)
        self.assertNotIn("for (uint8_t i = 0x63; i < mData.size(); ++i)", write_to)
        self.assertIn("reflector.write(attributeFieldOffset + layout.ToGame(idx), value);", CHARACTER)

    def test_layout_is_built_from_the_game_scripts(self):
        init = LUA_CPP.split("void Lua::Initialize(", 1)[1].split("void Lua::Reload()", 1)[0]
        self.assertIn('pcall(require, "Lua!GlobalDefinitions.lua")', init)
        self.assertIn('mState["nAttributeType"]', init)
        self.assertIn("Game::AttributeLayout::Map::FromGameNames(gameAttributes)", init)
        self.assertIn("Game::AttributeLayout::SetActiveOnce(layout)", init)

    def test_layout_is_tested_in_ci(self):
        workflow = (ROOT / ".github/workflows/gameplay-guardrails.yml").read_text(encoding="utf-8")
        self.assertIn("tests/attribute_layout_test.cpp", workflow)


if __name__ == "__main__":
    unittest.main()
