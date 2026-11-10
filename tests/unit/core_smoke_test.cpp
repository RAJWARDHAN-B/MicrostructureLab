#include "mlab/core/types.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>

TEST(CoreSmokeTest, PriceIsFixedPointInteger) {
    EXPECT_TRUE((std::is_same_v<mlab::Price, std::int64_t>));
    EXPECT_EQ(static_cast<mlab::Price>(12345), 12345);
}