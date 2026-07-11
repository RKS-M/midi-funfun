#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <vector>

#include "Audio/PeakLevelTracker.h"

using midi_funfun::core::PeakLevelTracker;

namespace
{
    std::vector<float> constantBlock(float amplitude, int numSamples)
    {
        return std::vector<float>(static_cast<size_t>(numSamples), amplitude);
    }
}

TEST_CASE("PeakLevelTracker reports the current block's peak", "[core][audio][peaklevel]")
{
    PeakLevelTracker tracker;
    tracker.prepare(1000.0);

    auto block = constantBlock(0.5f, 100);
    tracker.pushBlock(block.data(), (int) block.size());

    REQUIRE(tracker.getCurrentLevel() == Catch::Approx(0.5f));
    REQUIRE(tracker.getPeakHoldLevel() == Catch::Approx(0.5f));
}

TEST_CASE("PeakLevelTracker holds the peak for the hold duration", "[core][audio][peaklevel]")
{
    PeakLevelTracker tracker;
    tracker.prepare(1000.0); // holdSamples = 1.5 * 1000 = 1500

    auto loud = constantBlock(0.8f, 100);
    tracker.pushBlock(loud.data(), (int) loud.size());

    auto quiet = constantBlock(0.0f, 200);
    tracker.pushBlock(quiet.data(), (int) quiet.size());

    REQUIRE(tracker.getCurrentLevel() == Catch::Approx(0.0f));
    REQUIRE(tracker.getPeakHoldLevel() == Catch::Approx(0.8f));
}

TEST_CASE("PeakLevelTracker decays the peak hold after the hold duration elapses", "[core][audio][peaklevel]")
{
    PeakLevelTracker tracker;
    tracker.prepare(1000.0); // holdSamples = 1500

    auto loud = constantBlock(0.8f, 100);
    tracker.pushBlock(loud.data(), (int) loud.size());

    auto quiet = constantBlock(0.0f, 4000); // well past the 1500-sample hold + decay window
    tracker.pushBlock(quiet.data(), (int) quiet.size());

    REQUIRE(tracker.getPeakHoldLevel() < 0.8f);
    REQUIRE(tracker.getPeakHoldLevel() >= 0.0f);
}

TEST_CASE("PeakLevelTracker immediately jumps to a new, larger peak", "[core][audio][peaklevel]")
{
    PeakLevelTracker tracker;
    tracker.prepare(1000.0);

    auto first = constantBlock(0.3f, 50);
    tracker.pushBlock(first.data(), (int) first.size());

    auto louder = constantBlock(0.9f, 50);
    tracker.pushBlock(louder.data(), (int) louder.size());

    REQUIRE(tracker.getCurrentLevel() == Catch::Approx(0.9f));
    REQUIRE(tracker.getPeakHoldLevel() == Catch::Approx(0.9f));
}
