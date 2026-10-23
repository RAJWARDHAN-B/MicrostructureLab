#ifdef NDEBUG
#undef NDEBUG
#endif

#include "mlab/book/level_bitmap.hpp"
#include "mlab/memory/object_pool.hpp"

#include <cassert>
#include <cstdint>
#include <iterator>
#include <optional>
#include <set>

namespace {

using mlab::kNilIndex;
using mlab::LevelBitmap;
using mlab::ObjectPool;

void test_object_pool_lifecycle() {
    ObjectPool pool(2);
    assert(pool.capacity() == 2);
    assert(pool.available() == 2);

    const auto first = pool.acquire();
    const auto second = pool.acquire();
    assert(first == 0);
    assert(second == 1);
    assert(pool.available() == 0);
    assert(pool.acquire() == kNilIndex);
    assert(pool.get(first) != nullptr);
    assert(pool.get(second) != nullptr);
    assert(pool.get(2) == nullptr);

    pool.get(first)->id = 42;
    pool.get(first)->price = 100;
    pool.get(first)->qty = 9;
    assert(pool.release(first));
    assert(!pool.release(first));
    assert(pool.get(first) == nullptr);
    assert(pool.available() == 1);
    assert(pool.release(second));
    assert(!pool.release(2));
    assert(pool.available() == 2);

    const auto reused = pool.acquire();
    assert(reused == second);
    assert(pool.get(reused)->id == 0);
    assert(pool.get(reused)->qty == 0);
    assert(pool.release(reused));
    assert(!pool.release(first));
}

void test_empty_and_boundary_bitmap_operations() {
    LevelBitmap bitmap;
    assert(bitmap.empty());
    assert(!bitmap.find_lowest());
    assert(!bitmap.find_highest());
    assert(!bitmap.find_next_above(0));
    assert(!bitmap.find_next_below(1));
    assert(!bitmap.set(LevelBitmap::kSlotCount));
    assert(!bitmap.clear(LevelBitmap::kSlotCount));
    assert(!bitmap.contains(LevelBitmap::kSlotCount));

    for (const std::uint32_t slot : {0U, 63U, 64U, 4095U, 4096U, LevelBitmap::kSlotCount - 1}) {
        assert(bitmap.set(slot));
        assert(bitmap.contains(slot));
    }
    assert(!bitmap.empty());
    assert(bitmap.find_lowest() == 0);
    assert(bitmap.find_highest() == LevelBitmap::kSlotCount - 1);
    assert(bitmap.find_next_above(63) == 64);
    assert(bitmap.find_next_above(4095) == 4096);
    assert(bitmap.find_next_above(LevelBitmap::kSlotCount - 1) == std::nullopt);
    assert(bitmap.find_next_below(64) == 63);
    assert(bitmap.find_next_below(4096) == 4095);
    assert(bitmap.find_next_below(LevelBitmap::kSlotCount) == LevelBitmap::kSlotCount - 1);
    assert(bitmap.find_next_below(0) == std::nullopt);

    for (const std::uint32_t slot : {0U, 63U, 64U, 4095U, 4096U, LevelBitmap::kSlotCount - 1}) {
        assert(bitmap.clear(slot));
        assert(!bitmap.contains(slot));
    }
    assert(bitmap.empty());
    assert(!bitmap.find_lowest());
    assert(!bitmap.find_highest());
}

void test_bitmap_queries_against_ordered_set() {
    LevelBitmap bitmap;
    std::set<std::uint32_t> reference;
    std::uint32_t random = 0x9E3779B9U;
    for (std::size_t iteration = 0; iteration < 10000; ++iteration) {
        random ^= random << 13U;
        random ^= random >> 17U;
        random ^= random << 5U;
        const auto slot = random % LevelBitmap::kSlotCount;
        if ((random & 1U) != 0) {
            assert(bitmap.set(slot));
            reference.insert(slot);
        } else {
            assert(bitmap.clear(slot));
            reference.erase(slot);
        }

        assert(bitmap.empty() == reference.empty());
        assert(bitmap.find_lowest() == (reference.empty() ? std::nullopt : std::optional{*reference.begin()}));
        assert(bitmap.find_highest() ==
               (reference.empty() ? std::nullopt : std::optional{*reference.rbegin()}));

        const auto above = reference.upper_bound(slot);
        const auto expected_above =
            above == reference.end() ? std::nullopt : std::optional<std::uint32_t>{*above};
        assert(bitmap.find_next_above(slot) == expected_above);

        const auto below = reference.lower_bound(slot);
        const auto expected_below =
            below == reference.begin() ? std::nullopt : std::optional<std::uint32_t>{*std::prev(below)};
        assert(bitmap.find_next_below(slot) == expected_below);
    }
}

void test_zero_capacity_pool() {
    ObjectPool pool(0);
    assert(pool.capacity() == 0);
    assert(pool.available() == 0);
    assert(pool.acquire() == kNilIndex);
    assert(!pool.release(0));
}

}  // namespace

int main() {
    test_object_pool_lifecycle();
    test_empty_and_boundary_bitmap_operations();
    test_bitmap_queries_against_ordered_set();
    test_zero_capacity_pool();
}
