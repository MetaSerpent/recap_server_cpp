#include "../darkspore_server/source/sporenet/squad_selection.h"
#include <cassert>
#include <cstdint>
#include <limits>

using SporeNet::SquadSelection::ParseCreatureIds;

int main() {
    auto ids = ParseCreatureIds("1,0,3");
    assert(ids && (*ids)[0] == 1 && (*ids)[1] == 0 && (*ids)[2] == 3);
    ids = ParseCreatureIds("42");
    assert(ids && (*ids)[0] == 42 && (*ids)[1] == 0 && (*ids)[2] == 0);
    ids = ParseCreatureIds("1,2");
    assert(ids && (*ids)[2] == 0);
    ids = ParseCreatureIds("4294967295,2,3");
    assert(ids && (*ids)[0] == std::numeric_limits<uint32_t>::max());

    for (auto invalid : {"", ",", "1,", ",1", "1,,2", "1,2,3,4", "a,2,3",
                         "1,2,-3", "1,2,+3", "1,2,4294967296", "1,2, 3"}) {
        assert(!ParseCreatureIds(invalid).has_value());
    }
}
