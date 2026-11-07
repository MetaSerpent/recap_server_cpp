#ifndef RECAP_ACCOUNT_FILENAME_H
#define RECAP_ACCOUNT_FILENAME_H

#include <string_view>

// Restrict account identifiers to a single portable filename component.
// Valid characters include ordinary e-mail addresses and UTF-8 bytes.
namespace SporeNet::AccountFiles {
    constexpr bool IsSafeAccountIdentifier(std::string_view name) noexcept {
        if (name.empty() || name.size() > 240 || name.front() == '.' ||
            name.back() == '.' || name.back() == ' ') {
            return false;
        }

        for (unsigned char c : name) {
            if (c <= 0x20 || c == 0x7f || c == '/' || c == '\\' ||
                c == ':' || c == '*' || c == '?' || c == '"' ||
                c == '<' || c == '>' || c == '|') {
                return false;
            }
        }

        // Windows reserves these device names even when a suffix is present.
        const auto period = name.find('.');
        const auto stem = name.substr(0, period);
        const auto equalsIgnoreCase = [](std::string_view a, std::string_view b) constexpr {
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0; i < a.size(); ++i) {
                const auto c = a[i] >= 'A' && a[i] <= 'Z' ? char(a[i] + ('a' - 'A')) : a[i];
                if (c != b[i]) return false;
            }
            return true;
        };
        if (equalsIgnoreCase(stem, "con") || equalsIgnoreCase(stem, "prn") ||
            equalsIgnoreCase(stem, "aux") || equalsIgnoreCase(stem, "nul")) {
            return false;
        }
        if (stem.size() == 4 && (stem.substr(0, 3) == "COM" || stem.substr(0, 3) == "com" ||
                                  stem.substr(0, 3) == "LPT" || stem.substr(0, 3) == "lpt") &&
            stem[3] >= '1' && stem[3] <= '9') {
            return false;
        }
        return true;
    }
}

#endif
