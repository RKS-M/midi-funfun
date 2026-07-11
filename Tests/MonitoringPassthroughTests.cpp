#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "Audio/MonitoringPassthrough.h"

using midi_funfun::core::applyMonitoring;

TEST_CASE("applyMonitoring leaves other channels silent when monitoring is disabled", "[core][audio][monitoring]")
{
    std::vector<float> ch0 { 0.1f, 0.2f, 0.3f };
    std::vector<float> ch1 { 0.0f, 0.0f, 0.0f };
    float* channels[] { ch0.data(), ch1.data() };

    applyMonitoring(channels, 2, 3, false);

    REQUIRE(ch0[0] == 0.0f);
    REQUIRE(ch0[1] == 0.0f);
    REQUIRE(ch0[2] == 0.0f);
    REQUIRE(ch1[0] == 0.0f);
    REQUIRE(ch1[1] == 0.0f);
    REQUIRE(ch1[2] == 0.0f);
}

TEST_CASE("applyMonitoring copies channel 0 into the remaining channels when monitoring is enabled", "[core][audio][monitoring]")
{
    std::vector<float> ch0 { 0.1f, 0.2f, 0.3f };
    std::vector<float> ch1 { 0.0f, 0.0f, 0.0f };
    float* channels[] { ch0.data(), ch1.data() };

    applyMonitoring(channels, 2, 3, true);

    REQUIRE(ch0[0] == 0.1f);
    REQUIRE(ch0[1] == 0.2f);
    REQUIRE(ch0[2] == 0.3f);
    REQUIRE(ch1[0] == 0.1f);
    REQUIRE(ch1[1] == 0.2f);
    REQUIRE(ch1[2] == 0.3f);
}

TEST_CASE("applyMonitoring is a no-op for zero channels or zero samples", "[core][audio][monitoring]")
{
    applyMonitoring(nullptr, 0, 10, true);

    std::vector<float> ch0 { 0.5f };
    float* channels[] { ch0.data() };
    applyMonitoring(channels, 1, 0, false);
    REQUIRE(ch0[0] == 0.5f);
}
