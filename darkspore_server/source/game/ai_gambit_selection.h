#ifndef RECAP_AI_GAMBIT_SELECTION_H
#define RECAP_AI_GAMBIT_SELECTION_H

#include <cstddef>

namespace Game::AIGambitSelection {
    // The configured AI phase may contain fewer gambits after a level change.
    constexpr std::size_t NormalizeIndex(std::size_t index, std::size_t count) noexcept {
        return count == 0 ? 0 : index % count;
    }

    constexpr std::size_t AdvanceIndex(std::size_t index, std::size_t count) noexcept {
        if (count == 0) {
            return 0;
        }
        index = NormalizeIndex(index, count);
        return index + 1 == count ? 0 : index + 1;
    }
}

#endif
