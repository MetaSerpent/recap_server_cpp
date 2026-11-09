#ifndef RECAP_GAME_CHAIN_LEVELS_H
#define RECAP_GAME_CHAIN_LEVELS_H

#include <pugixml.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Campaign level order from the installed game's ChainLevels asset
// (data/serverdata/chainlevels/ChainLevels.ChainLevels.xml, produced by
// recap_parser). Campaign difficulty index i (1-based, as sent by the client in
// the SelectedDifficulty game attribute) is the i-th <entry>; index 0 is the
// tutorial. The 5.3.0.103 demo defines 72 entries (18 tiers of 4 levels).
namespace Game::ChainLevels {
	inline constexpr std::string_view InstalledPath = "data/serverdata/chainlevels/ChainLevels.ChainLevels.xml";

	// Returns level names without the ".Level" suffix. Entries without a name
	// stay as empty strings so later indices keep their position.
	inline std::vector<std::string> Parse(const pugi::xml_document& document) {
		std::vector<std::string> levels;
		const auto chain = document.child("chainlevels").child("ChainLevel");
		for (const auto entry : chain.children("entry")) {
			std::string name = entry.child_value("unk1");
			constexpr std::string_view suffix = ".Level";
			if (name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
				name.resize(name.size() - suffix.size());
			}
			levels.push_back(std::move(name));
		}
		return levels;
	}

	inline std::vector<std::string> Load(const std::filesystem::path& path) {
		pugi::xml_document document;
		if (!document.load_file(path.c_str())) {
			return {};
		}
		return Parse(document);
	}
}

#endif
