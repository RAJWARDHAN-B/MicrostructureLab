#include "mlab/book/order_book.hpp"

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace {

constexpr std::size_t kBookCapacity = 1024;
constexpr std::size_t kLiveOrders = 512;
constexpr std::size_t kAddBatchSize = 256;
constexpr mlab::OrderId kAlternateIdBase = 1U << 20U;
constexpr mlab::OrderId kEphemeralIdBase = 1U << 40U;

template <typename Book>
[[nodiscard]] Book make_book() {
    if constexpr (std::is_same_v<Book, mlab::ReferenceBook>) {
        return {};
    } else {
        return Book{kBookCapacity};
    }
}

template <typename Book>
[[nodiscard]] bool populate(Book& book, benchmark::State& state,
                            std::array<mlab::OrderId, kLiveOrders>& ids) {
    for (std::size_t index = 0; index < kLiveOrders; ++index) {
        ids[index] = static_cast<mlab::OrderId>(index + 1U);
        const auto side = index % 2U == 0 ? mlab::Side::Buy : mlab::Side::Sell;
        const auto price = static_cast<mlab::Price>(100U + index % 32U);
        if (book.add(ids[index], side, price, std::numeric_limits<mlab::Qty>::max()) !=
            mlab::BookError::None) {
            state.SkipWithError("Failed to prepare benchmark book");
            return false;
        }
    }
    return true;
}

template <typename Book>
void BM_Add(benchmark::State& state) {
    auto book = make_book<Book>();
    std::array<mlab::OrderId, kLiveOrders> live_ids{};
    if (!populate(book, state, live_ids)) {
        return;
    }
    for (auto _ : state) {
        static_cast<void>(_);
        for (std::size_t index = 0; index < kAddBatchSize; ++index) {
            const auto id = kEphemeralIdBase + static_cast<mlab::OrderId>(index);
            const auto price = static_cast<mlab::Price>(100U + (index % 16U) * 2U);
            const auto result = book.add(id, mlab::Side::Buy, price, 1);
            if (result != mlab::BookError::None) {
                state.SkipWithError("Order add failed");
                return;
            }
            benchmark::DoNotOptimize(result);
        }
        state.PauseTiming();
        for (std::size_t index = 0; index < kAddBatchSize; ++index) {
            const auto id = kEphemeralIdBase + static_cast<mlab::OrderId>(index);
            if (book.remove(id) != mlab::BookError::None) {
                state.ResumeTiming();
                state.SkipWithError("Order cleanup failed");
                return;
            }
        }
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kAddBatchSize));
}

template <typename Book>
void BM_Cancel(benchmark::State& state) {
    auto book = make_book<Book>();
    std::array<mlab::OrderId, kLiveOrders> ids{};
    if (!populate(book, state, ids)) {
        return;
    }
    std::uint64_t iteration = 0;
    for (auto _ : state) {
        static_cast<void>(_);
        const auto id = ids[static_cast<std::size_t>(iteration++ % kLiveOrders)];
        const auto result = book.cancel(id, 1);
        if (result != mlab::BookError::None) {
            state.SkipWithError("Order cancel failed");
            return;
        }
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations());
}

template <typename Book>
void BM_Execute(benchmark::State& state) {
    auto book = make_book<Book>();
    std::array<mlab::OrderId, kLiveOrders> ids{};
    if (!populate(book, state, ids)) {
        return;
    }
    std::uint64_t iteration = 0;
    for (auto _ : state) {
        static_cast<void>(_);
        const auto id = ids[static_cast<std::size_t>(iteration++ % kLiveOrders)];
        const auto result = book.execute(id, 1);
        if (result != mlab::BookError::None) {
            state.SkipWithError("Order execution failed");
            return;
        }
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations());
}

template <typename Book>
void BM_Replace(benchmark::State& state) {
    auto book = make_book<Book>();
    std::array<mlab::OrderId, kLiveOrders> ids{};
    if (!populate(book, state, ids)) {
        return;
    }
    std::uint64_t iteration = 0;
    for (auto _ : state) {
        static_cast<void>(_);
        const auto slot = static_cast<std::size_t>(iteration++ % kLiveOrders);
        const auto old_id = ids[slot];
        const auto new_id =
            old_id < kAlternateIdBase ? old_id + kAlternateIdBase : old_id - kAlternateIdBase;
        const auto price = static_cast<mlab::Price>(100U + iteration % 32U);
        const auto result = book.replace(old_id, new_id, price,
                                         std::numeric_limits<mlab::Qty>::max());
        if (result != mlab::BookError::None) {
            state.SkipWithError("Order replacement failed");
            return;
        }
        ids[slot] = new_id;
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations());
}

template <typename Book>
void BM_BestPrice(benchmark::State& state) {
    auto book = make_book<Book>();
    std::array<mlab::OrderId, kLiveOrders> ids{};
    if (!populate(book, state, ids)) {
        return;
    }
    for (auto _ : state) {
        static_cast<void>(_);
        const auto bid = book.best_bid();
        const auto ask = book.best_ask();
        benchmark::DoNotOptimize(bid);
        benchmark::DoNotOptimize(ask);
    }
    state.SetItemsProcessed(state.iterations() * 2);
}

template <typename Book>
void BM_RealisticMix(benchmark::State& state) {
    auto book = make_book<Book>();
    std::array<mlab::OrderId, kLiveOrders> ids{};
    if (!populate(book, state, ids)) {
        return;
    }
    std::uint64_t iteration = 0;
    for (auto _ : state) {
        static_cast<void>(_);
        const auto step = iteration++;
        const auto slot = static_cast<std::size_t>(step % kLiveOrders);
        mlab::BookError result = mlab::BookError::None;
        switch (step % 5U) {
            case 0: {
                const auto id = kEphemeralIdBase + (step & 1U);
                result = book.add(id, mlab::Side::Buy,
                                  static_cast<mlab::Price>(100U + step % 32U), 10);
                if (result == mlab::BookError::None) {
                    result = book.remove(id);
                }
                break;
            }
            case 1:
                result = book.cancel(ids[slot], 1);
                break;
            case 2:
                result = book.execute(ids[slot], 1);
                break;
            case 3: {
                const auto old_id = ids[slot];
                const auto new_id = old_id < kAlternateIdBase
                                        ? old_id + kAlternateIdBase
                                        : old_id - kAlternateIdBase;
                result = book.replace(old_id, new_id,
                                      static_cast<mlab::Price>(100U + step % 32U),
                                      std::numeric_limits<mlab::Qty>::max());
                if (result == mlab::BookError::None) {
                    ids[slot] = new_id;
                }
                break;
            }
            default:
                benchmark::DoNotOptimize(book.best_bid());
                benchmark::DoNotOptimize(book.best_ask());
                break;
        }
        if (result != mlab::BookError::None) {
            state.SkipWithError("Operation in realistic mix failed");
            return;
        }
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations() + (state.iterations() + 4) / 5);
}

#define REGISTER_BOOK_BENCHMARKS(BookType)          \
    BENCHMARK_TEMPLATE(BM_Add, BookType);           \
    BENCHMARK_TEMPLATE(BM_Cancel, BookType);        \
    BENCHMARK_TEMPLATE(BM_Execute, BookType);       \
    BENCHMARK_TEMPLATE(BM_Replace, BookType);      \
    BENCHMARK_TEMPLATE(BM_BestPrice, BookType);     \
    BENCHMARK_TEMPLATE(BM_RealisticMix, BookType)

REGISTER_BOOK_BENCHMARKS(mlab::ReferenceBook);
REGISTER_BOOK_BENCHMARKS(mlab::OrderBook<mlab::OpenAddressingIndex>);
REGISTER_BOOK_BENCHMARKS(mlab::OrderBook<mlab::PagedDirectIndex>);

#undef REGISTER_BOOK_BENCHMARKS

}  // namespace
