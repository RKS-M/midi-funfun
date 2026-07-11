#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Pitch/NoiseGateMapping.h"

using namespace midi_funfun::core;

TEST_CASE("default noise gate sensitivity maps to NoteSegmenter's documented default threshold", "[core][pitch][noisegate]")
{
    const double threshold = noiseGateSensitivityPercentToThreshold(defaultNoiseGateSensitivityPercent);
    REQUIRE(threshold == Catch::Approx(0.02).margin(0.001));
}

TEST_CASE("noiseGateSensitivityPercentToThreshold maps 0% and 100% to the documented endpoints", "[core][pitch][noisegate]")
{
    REQUIRE(noiseGateSensitivityPercentToThreshold(0.0) == Catch::Approx(0.0));
    REQUIRE(noiseGateSensitivityPercentToThreshold(100.0) == Catch::Approx(0.2));
}
