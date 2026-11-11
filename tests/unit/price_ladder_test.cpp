#ifdef NDEBUG
#undef NDEBUG
#endif

#include "mlab/book/price_ladder.hpp"

#include <cassert>
#include <cstdint>
#include <limits>

namespace {

using mlab::PriceLadder;

void test_dense_levels_and_best_prices() {
    PriceLadder ladder;
    assert(!ladder.best_price(true));
    assert(!ladder.best_price(false));

    auto* middle = ladder.get_or_create(1000);
    middle->total_qty = 12;
    assert(ladder.set_order_count(1000, 2));
    auto* lower = ladder.get_or_create(998);
    lower->total_qty = 5;
    assert(ladder.set_order_count(998, 1));
    auto* higher = ladder.get_or_create(1003);
    higher->total_qty = 7;
    assert(ladder.set_order_count(1003, 3));

    assert(ladder.find(1000)->total_qty == 12);
    assert(ladder.find(998)->order_count == 1);
    assert(ladder.best_price(true) == 1003);
    assert(ladder.best_price(false) == 998);
    assert(ladder.statistics().dense_accesses == 3);
    assert(ladder.statistics().recenter_count == 0);
    assert(ladder.statistics().sparse_fallback_count == 0);
}

void test_recentering_preserves_levels_and_occupancy() {
    PriceLadder ladder;
    auto* initial = ladder.get_or_create(0);
    initial->total_qty = 4;
    assert(ladder.set_order_count(0, 1));

    const auto drifted_price = static_cast<mlab::Price>(PriceLadder::kCenterSlot + 10U);
    auto* drifted = ladder.get_or_create(drifted_price);
    drifted->total_qty = 9;
    assert(ladder.set_order_count(drifted_price, 2));

    assert(ladder.find(0)->total_qty == 4);
    assert(ladder.find(drifted_price)->total_qty == 9);
    assert(ladder.best_price(true) == drifted_price);
    assert(ladder.best_price(false) == 0);
    assert(ladder.statistics().recenter_count == 1);
    assert(ladder.statistics().sparse_fallback_count == 0);
}

void test_sparse_fallback_and_migration_after_drift() {
    PriceLadder ladder;
    auto* near = ladder.get_or_create(100);
    near->total_qty = 1;
    assert(ladder.set_order_count(100, 1));

    const auto far_price = static_cast<mlab::Price>(100 + PriceLadder::kSlotCount);
    auto* far = ladder.get_or_create(far_price);
    far->total_qty = 6;
    assert(ladder.set_order_count(far_price, 1));
    assert(ladder.find(far_price)->total_qty == 6);
    assert(ladder.best_price(true) == far_price);
    assert(ladder.statistics().sparse_fallback_count == 1);
    assert(ladder.statistics().recenter_count == 0);

    assert(ladder.set_order_count(100, 0));
    assert(ladder.erase(100));
    const auto nearby_far_price = far_price - 1;
    auto* nearby_far = ladder.get_or_create(nearby_far_price);
    nearby_far->total_qty = 3;
    assert(ladder.set_order_count(nearby_far_price, 1));

    assert(ladder.find(far_price)->total_qty == 6);
    assert(ladder.find(nearby_far_price)->total_qty == 3);
    assert(ladder.best_price(false) == nearby_far_price);
    assert(ladder.statistics().recenter_count == 1);
    assert(ladder.statistics().sparse_fallback_count == 1);
}

void test_occupied_level_cannot_be_erased() {
    PriceLadder ladder;
    auto* level = ladder.get_or_create(42);
    level->total_qty = 1;
    assert(ladder.set_order_count(42, 1));
    assert(!ladder.erase(42));
    assert(ladder.set_order_count(42, 0));
    assert(ladder.erase(42));
    assert(ladder.find(42) == nullptr);
    assert(!ladder.best_price(true));
}

void test_extreme_prices_use_sparse_fallback_without_overflow() {
    PriceLadder ladder;
    auto* lowest = ladder.get_or_create(std::numeric_limits<mlab::Price>::min());
    lowest->total_qty = 2;
    assert(ladder.set_order_count(std::numeric_limits<mlab::Price>::min(), 1));

    auto* highest = ladder.get_or_create(std::numeric_limits<mlab::Price>::max());
    highest->total_qty = 3;
    assert(ladder.set_order_count(std::numeric_limits<mlab::Price>::max(), 1));

    assert(ladder.best_price(true) == std::numeric_limits<mlab::Price>::max());
    assert(ladder.best_price(false) == std::numeric_limits<mlab::Price>::min());
    assert(ladder.statistics().sparse_fallback_count == 1);
}

}  // namespace

int main() {
    test_dense_levels_and_best_prices();
    test_recentering_preserves_levels_and_occupancy();
    test_sparse_fallback_and_migration_after_drift();
    test_occupied_level_cannot_be_erased();
    test_extreme_prices_use_sparse_fallback_without_overflow();
}