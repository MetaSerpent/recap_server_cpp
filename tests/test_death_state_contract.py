"""Death and resurrection transitions must suppress dead AI without disabling respawns."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "darkspore_server/source/game"


class DeathStateContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.object = (ROOT / "object.cpp").read_text()
        cls.instance = (ROOT / "instance.cpp").read_text()

    def test_death_transition_is_called_by_game(self):
        block = self.instance.split("void Instance::OnObjectDeath(", 1)[1].split(
            "void Instance::OnPlayerStart(", 1
        )[0]
        self.assertIn("object->OnDeath();", block)
        self.assertIn('SendObjectGfxState(object, utils::hash_id("dead"))', block)

    def test_dead_creatures_stop_ticking(self):
        block = self.object.split("void Object::OnTick(float deltaTime)", 1)[1].split(
            "void Object::SetTickOverride(", 1
        )[0]
        self.assertLess(block.index("mCorpseState != CorpseState::Alive"), block.index("mAI->OnTick()"))

    def test_resurrection_clears_corpse_state(self):
        block = self.object.split("void Object::OnResurrect()", 1)[1].split(
            "// Interactable data", 1
        )[0]
        self.assertIn("mCorpseState = CorpseState::Alive", block)

    def test_ai_does_not_target_dead_players(self):
        block = self.object.split("bool AI::SearchForTarget()", 1)[1].split(
            "bool AI::UseAbility(", 1
        )[0]
        self.assertIn("mTargetObject->GetHealth() <= 0.f", block)
        self.assertIn("possibleTarget->GetHealth() > 0.f", block)
