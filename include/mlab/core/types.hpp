#pragma once

#include <cstdint>

namespace mlab {

using Price = std::int64_t;
using Qty = std::uint32_t;
using OrderId = std::uint64_t;
using Locate = std::uint16_t;
using Timestamp = std::uint64_t;

enum class Side : std::uint8_t { Buy = 0, Sell = 1 };

}