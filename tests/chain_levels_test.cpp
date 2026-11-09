// Standalone test for Game::ChainLevels (campaign order from game data).
// Build (see gameplay-guardrails.yml): g++ -std=c++23 -I<pugixml>/src
//   tests/chain_levels_test.cpp <pugixml>/src/pugixml.cpp
#include "../darkspore_server/source/game/chain_levels.h"
#include "../darkspore_server/source/game/mission_progression.h"

#include <iostream>
#include <string>

namespace {
	int failures = 0;
	void Check(bool condition, const std::string& what) {
		if (!condition) {
			std::cerr << "FAIL: " << what << '\n';
			++failures;
		}
	}
}

int main(int argc, char** argv) {
	if (argc != 2) {
		std::cerr << "usage: chain_levels_test <tests/fixtures/chainlevels>\n";
		return 2;
	}
	const std::string dir = argv[1];
	const auto levels = Game::ChainLevels::Load(dir + "/ChainLevels.synthetic.xml");
	Check(levels.size() == 5, "every <entry> becomes one campaign index");
	if (levels.size() == 5) {
		Check(levels[0] == "alpha_1", "'.Level' suffix is stripped (index 1)");
		Check(levels[1] == "alpha_2", "entries keep data order (index 2)");
		Check(levels[2].empty(), "an entry without a name keeps its position");
		Check(levels[3] == "beta_1", "names without '.Level' are kept as is");
		Check(levels[4] == "beta_2", "last entry is index 5");
	}
	Check(Game::ChainLevels::Load(dir + "/missing.xml").empty(), "missing data yields no levels (built-in fallback)");

	// With 72 data levels, a chain started at 7-1 (index 25) can continue.
	using Game::Progression::CanDeployNextPlanet;
	Check(!CanDeployNextPlanet(1, 25), "built-in 24-level limit blocks index 25");
	Check(CanDeployNextPlanet(1, 25, 72), "data-driven limit allows index 25 of 72");
	Check(!CanDeployNextPlanet(1, 72, 72), "the final data level ends the campaign");

	if (failures != 0) {
		return 1;
	}
	std::cout << "chain_levels_test: all checks passed\n";
	return 0;
}
