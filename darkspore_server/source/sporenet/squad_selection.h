#ifndef RECAP_SPORENET_SQUAD_SELECTION_H
#define RECAP_SPORENET_SQUAD_SELECTION_H

#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>
#include <system_error>

namespace SporeNet::SquadSelection {
    // Parse a 3-slot deck, keeping explicit zeroes as empty slots.
    // No partially parsed deck is ever returned.
    inline std::optional<std::array<uint32_t, 3>> ParseCreatureIds(std::string_view text) {
        if (text.empty()) {
            return std::nullopt;
        }
        std::array<uint32_t, 3> ids{};
        std::size_t index = 0;
        std::size_t start = 0;
        while (true) {
            if (index >= ids.size()) {
                return std::nullopt;
            }
            const auto separator = text.find(',', start);
            const auto token = text.substr(start, separator == std::string_view::npos ?
                std::string_view::npos : separator - start);
            if (token.empty()) {
                return std::nullopt;
            }
            uint32_t value = 0;
            const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), value);
            if (error != std::errc{} || end != token.data() + token.size()) {
                return std::nullopt;
            }
            ids[index++] = value;
            if (separator == std::string_view::npos) {
                return ids;
            }
            start = separator + 1;
        }
    }
}

#endif
