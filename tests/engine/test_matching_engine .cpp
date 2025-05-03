#include "catch.hpp"
#include "../engine/engine.hpp"

TEST_CASE("Add Limit Order", "[matching]") {
    MatchingEngine engine;
    engine.addLimitOrder(1, 100.5, 10, true);
    // assertions...
}
