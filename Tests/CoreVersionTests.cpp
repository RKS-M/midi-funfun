#include <catch2/catch_test_macros.hpp>
#include <string>

#include "Version.h"

TEST_CASE("libraryVersionString returns the expected version", "[core][version]")
{
    REQUIRE(std::string(midi_funfun::core::libraryVersionString()) == "0.1.0");
}
