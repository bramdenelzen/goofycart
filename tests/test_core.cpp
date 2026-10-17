#include "gk/core/fixed_step.h"
#include "gk/core/math.h"

#include <doctest/doctest.h>

TEST_CASE("fixed step runs 60 ticks per simulated second") {
    gk::FixedStep step;
    int ticks = 0;
    for (int i = 0; i < 144; ++i) ticks += step.advance(1.0 / 144.0); // a 144 Hz display
    CHECK(ticks >= 59);
    CHECK(ticks <= 60);
}

TEST_CASE("fixed step drops time after a long hitch") {
    gk::FixedStep step(8);
    CHECK(step.advance(5.0) == 8);
    CHECK(step.advance(0.0) == 0);
}

TEST_CASE("approach clamps at the target") {
    CHECK(gk::approach(0.0f, 1.0f, 0.25f) == doctest::Approx(0.25f));
    CHECK(gk::approach(0.9f, 1.0f, 0.25f) == doctest::Approx(1.0f));
    CHECK(gk::approach(1.0f, -1.0f, 0.5f) == doctest::Approx(0.5f));
}
