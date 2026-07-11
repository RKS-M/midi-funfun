#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "Audio/TakeManager.h"

using midi_funfun::core::TakeManager;

TEST_CASE("TakeManager rejects a new take once the memory budget would be exceeded", "[core][audio][takemanager]")
{
    // budget=2000 bytes, maxTakeSeconds=1.0, sampleRate=100 => 100 samples/take => 400 bytes/take
    TakeManager manager(2000, 1.0);

    std::vector<float> block(100, 0.1f);

    for (int i = 0; i < 5; ++i)
    {
        REQUIRE(manager.canStartNewTake(100.0));
        REQUIRE(manager.startNewTake(100.0));
        REQUIRE(manager.appendToCurrentTake(block.data(), (int) block.size()));
        manager.finishRecording();
    }

    // 5 takes * 400 bytes = 2000 bytes == budget, exactly at the limit
    REQUIRE(manager.getTotalBytesUsed() == 2000);

    // A 6th take would need 400 more bytes, exceeding the 2000-byte budget
    REQUIRE_FALSE(manager.canStartNewTake(100.0));
    REQUIRE_FALSE(manager.startNewTake(100.0));
    REQUIRE(manager.getNumTakes() == 5);
}

TEST_CASE("TakeManager signals when a take reaches its maximum length", "[core][audio][takemanager]")
{
    TakeManager manager(1'000'000, 1.0); // maxTakeSeconds=1.0, sampleRate=10 => 10 samples max

    REQUIRE(manager.startNewTake(10.0));

    std::vector<float> smallBlock(4, 0.2f);
    REQUIRE_FALSE(manager.appendToCurrentTake(smallBlock.data(), (int) smallBlock.size())); // 4/10
    REQUIRE_FALSE(manager.appendToCurrentTake(smallBlock.data(), (int) smallBlock.size())); // 8/10

    std::vector<float> tailBlock(2, 0.2f);
    REQUIRE(manager.appendToCurrentTake(tailBlock.data(), (int) tailBlock.size())); // 10/10 -> reached max
}

TEST_CASE("Deleting a take frees its share of the memory budget", "[core][audio][takemanager]")
{
    TakeManager manager(800, 1.0); // 100 samples/take => 400 bytes/take, room for exactly 2 takes

    std::vector<float> block(100, 0.1f);

    REQUIRE(manager.startNewTake(100.0));
    REQUIRE(manager.appendToCurrentTake(block.data(), (int) block.size()));
    manager.finishRecording();

    REQUIRE(manager.startNewTake(100.0));
    REQUIRE(manager.appendToCurrentTake(block.data(), (int) block.size()));
    manager.finishRecording();

    REQUIRE(manager.getTotalBytesUsed() == 800);
    REQUIRE_FALSE(manager.canStartNewTake(100.0));

    manager.deleteTake(0);

    REQUIRE(manager.getTotalBytesUsed() == 400);
    REQUIRE(manager.getNumTakes() == 1);
    REQUIRE(manager.canStartNewTake(100.0));
}

TEST_CASE("TakeManager tracks selection across deletes", "[core][audio][takemanager]")
{
    TakeManager manager(10'000, 1.0);
    std::vector<float> block(10, 0.1f);

    REQUIRE(manager.startNewTake(10.0));
    manager.appendToCurrentTake(block.data(), (int) block.size());
    manager.finishRecording();

    REQUIRE(manager.startNewTake(10.0));
    manager.appendToCurrentTake(block.data(), (int) block.size());
    manager.finishRecording();

    REQUIRE(manager.getSelectedTakeIndex() == 1);

    manager.selectTake(0);
    REQUIRE(manager.getSelectedTakeIndex() == 0);

    manager.deleteTake(0);
    REQUIRE(manager.getNumTakes() == 1);
    REQUIRE(manager.getSelectedTakeIndex() == 0);
}
