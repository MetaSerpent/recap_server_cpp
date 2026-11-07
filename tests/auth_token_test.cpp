#include "../darkspore_server/source/sporenet/auth_token.h"
#include <array>
#include <cassert>
#include <string>

int main() {
    const std::array<unsigned char, 4> sample{0, 0x0F, 0xA5, 0xFF};
    assert(SporeNet::Auth::EncodeToken(sample) == "000fa5ff");
    const std::array<unsigned char, 16> token{};
    const std::string encoded = SporeNet::Auth::EncodeToken(token);
    assert(encoded.size() == 32);
    assert(encoded == std::string(32, '0'));
}
