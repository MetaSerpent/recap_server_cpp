#ifndef RECAP_SPORENET_AUTH_TOKEN_H
#define RECAP_SPORENET_AUTH_TOKEN_H

#include <array>
#include <cstddef>
#include <string>

namespace SporeNet::Auth {
    // Render 128 bits of CSPRNG output into an ASCII token suitable for
    // RakNet/Blaze and HTTP cookie serialization. Randomness comes from
    // OpenSSL RAND_bytes at the call site, never from account IDs or time.
    template<std::size_t Size>
    std::string EncodeToken(const std::array<unsigned char, Size>& bytes) {
        static constexpr char digits[] = "0123456789abcdef";
        std::string out;
        out.reserve(Size * 2);
        for (unsigned char value : bytes) {
            out.push_back(digits[value >> 4]);
            out.push_back(digits[value & 0x0F]);
        }
        return out;
    }
}

#endif
