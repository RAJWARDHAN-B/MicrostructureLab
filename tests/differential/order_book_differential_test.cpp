#include "mlab/book/order_book.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

class DeterministicRandom {
public:
    explicit DeterministicRandom(std::uint64_t seed) : state_(seed) {}

    [[nodiscard]] std::uint64_t next() noexcept {
        auto value = (state_ += 0x9e3779b97f4a7c15ULL);
        value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31U);
    }

    [[nodiscard]] std::uint64_t bounded(std::uint64_t upper_bound) noexcept {
        return next() % upper_bound;
    }

private:
    std::uint64_t state_;
};

struct LiveOrder {
    mlab::OrderId id;
    mlab::Side side;
    mlab::Price price;
    mlab::Qty qty;
};

[[nodiscard]] mlab::Price next_price(DeterministicRandom& random) noexcept {
    const auto category = random.bounded(100);
    if (category < 5) {
        const auto magnitude = static_cast<mlab::Price>(200000U + random.bounded(100000));
        return random.bounded(2) == 0 ? 100000 - magnitude : 100000 + magnitude;
    }
    if (category < 10) {
        const auto drift = static_cast<mlab::Price>(50000U + random.bounded(75000));
        return random.bounded(2) == 0 ? 100000 - drift : 100000 + drift;
    }
    const auto cluster = static_cast<mlab::Price>(random.bounded(201)) - 100;
    return 100000 + cluster;
}

[[nodiscard]] std::size_t choose_live(DeterministicRandom& random,
                                      const std::vector<LiveOrder>& live) noexcept {
    return static_cast<std::size_t>(random.bounded(static_cast<std::uint64_t>(live.size())));
}

void erase_live(std::vector<LiveOrder>& live, std::size_t index) {
    live[index] = live.back();
    live.pop_back();
}

template <typename IndexPolicy>
[[nodiscard]] bool run_differential(std::uint64_t seed, std::uint64_t operations,
                                    std::uint64_t dump_interval, std::string_view policy_name) {
    constexpr std::size_t kBookCapacity = 1U << 15U;
    DeterministicRandom random(seed);
    mlab::ReferenceBook reference;
    mlab::OrderBook<IndexPolicy> optimized(kBookCapacity);
    std::vector<LiveOrder> live;
    live.reserve(kBookCapacity);
    mlab::OrderId next_id = 1;

    for (std::uint64_t step = 0; step < operations; ++step) {
        const auto action = random.bounded(100);
        mlab::BookError reference_error = mlab::BookError::None;
        mlab::BookError optimized_error = mlab::BookError::None;
        const char* operation_name = "unknown";
        std::size_t live_index = 0;
        mlab::Qty operation_qty = 0;

        if (action < 24) {
            operation_name = "add";
            auto id = next_id++;
            if (!live.empty() && random.bounded(100) == 0) {
                id = live[choose_live(random, live)].id;
            }
            const auto side = random.bounded(2) == 0 ? mlab::Side::Buy : mlab::Side::Sell;
            const auto price = next_price(random);
            const auto qty = static_cast<mlab::Qty>(1U + random.bounded(1000));
            reference_error = reference.add(id, side, price, qty);
            optimized_error = optimized.add(id, side, price, qty);
            if (reference_error == mlab::BookError::None) {
                live.push_back(LiveOrder{id, side, price, qty});
            }
        } else if (action < 74) {
            const bool is_cancel = action < 49;
            operation_name = is_cancel ? "cancel" : "execute";
            if (live.empty()) {
                const auto missing_id = next_id + random.bounded(1000);
                operation_qty = static_cast<mlab::Qty>(1U + random.bounded(1000));
                reference_error = is_cancel ? reference.cancel(missing_id, operation_qty)
                                            : reference.execute(missing_id, operation_qty);
                optimized_error = is_cancel ? optimized.cancel(missing_id, operation_qty)
                                            : optimized.execute(missing_id, operation_qty);
            } else {
                live_index = choose_live(random, live);
                const auto order = live[live_index];
                const auto choice = random.bounded(100);
                if (choice < 5) {
                    operation_qty = 0;
                } else if (choice < 10) {
                    operation_qty = order.qty + 1U;
                } else if (choice < 75) {
                    operation_qty = order.qty;
                } else {
                    operation_qty = static_cast<mlab::Qty>(1U + random.bounded(order.qty));
                }
                reference_error = is_cancel ? reference.cancel(order.id, operation_qty)
                                            : reference.execute(order.id, operation_qty);
                optimized_error = is_cancel ? optimized.cancel(order.id, operation_qty)
                                            : optimized.execute(order.id, operation_qty);
                if (reference_error == mlab::BookError::None) {
                    if (operation_qty == order.qty) {
                        erase_live(live, live_index);
                    } else {
                        live[live_index].qty -= operation_qty;
                    }
                }
            }
        } else if (action < 94) {
            operation_name = "replace";
            if (live.empty() || random.bounded(20) == 0) {
                const auto missing_id = next_id + random.bounded(1000);
                const auto new_id = next_id++;
                const auto price = next_price(random);
                const auto qty = static_cast<mlab::Qty>(1U + random.bounded(1000));
                reference_error = reference.replace(missing_id, new_id, price, qty);
                optimized_error = optimized.replace(missing_id, new_id, price, qty);
            } else {
                live_index = choose_live(random, live);
                const auto old_order = live[live_index];
                auto new_id = next_id++;
                const auto replacement_kind = random.bounded(100);
                if (replacement_kind == 0) {
                    new_id = old_order.id;
                } else if (replacement_kind < 3 && live.size() > 1) {
                    auto duplicate_index = choose_live(random, live);
                    while (duplicate_index == live_index) {
                        duplicate_index = choose_live(random, live);
                    }
                    new_id = live[duplicate_index].id;
                }
                const auto price = next_price(random);
                const auto qty = static_cast<mlab::Qty>(1U + random.bounded(1000));
                reference_error = reference.replace(old_order.id, new_id, price, qty);
                optimized_error = optimized.replace(old_order.id, new_id, price, qty);
                if (reference_error == mlab::BookError::None) {
                    live[live_index] = LiveOrder{new_id, old_order.side, price, qty};
                }
            }
        } else if (action < 97) {
            operation_name = "remove";
            if (live.empty() || random.bounded(10) == 0) {
                const auto missing_id = next_id + random.bounded(1000);
                reference_error = reference.remove(missing_id);
                optimized_error = optimized.remove(missing_id);
            } else {
                live_index = choose_live(random, live);
                const auto id = live[live_index].id;
                reference_error = reference.remove(id);
                optimized_error = optimized.remove(id);
                if (reference_error == mlab::BookError::None) {
                    erase_live(live, live_index);
                }
            }
        } else {
            operation_name = "invalid";
            const auto invalid_id = next_id++;
            if (random.bounded(2) == 0) {
                const auto price = next_price(random);
                reference_error = reference.add(invalid_id, mlab::Side::Buy, price, 0);
                optimized_error = optimized.add(invalid_id, mlab::Side::Buy, price, 0);
            } else {
                const auto missing_id = next_id + 1U;
                const auto price = next_price(random);
                reference_error = reference.replace(missing_id, invalid_id, price, 0);
                optimized_error = optimized.replace(missing_id, invalid_id, price, 0);
            }
        }

        if (reference_error != optimized_error) {
            std::cerr << policy_name << " error mismatch at seed " << seed << ", step " << step
                      << ", operation " << operation_name << ": reference "
                      << static_cast<unsigned>(reference_error) << ", optimized "
                      << static_cast<unsigned>(optimized_error) << '\n';
            return false;
        }
        if (reference.best_bid() != optimized.best_bid() ||
            reference.best_ask() != optimized.best_ask()) {
            std::cerr << policy_name << " top-of-book mismatch at seed " << seed << ", step " << step
                      << ", operation " << operation_name << '\n';
            return false;
        }
        if ((step + 1U) % dump_interval == 0 || step + 1U == operations) {
            if (reference.dump() != optimized.dump()) {
                std::cerr << policy_name << " full-book mismatch at seed " << seed << ", step "
                          << step << ", operation " << operation_name << '\n';
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool parse_unsigned(const char* text, std::uint64_t& value) {
    const auto end = text + std::char_traits<char>::length(text);
    const auto result = std::from_chars(text, end, value);
    return result.ec == std::errc{} && result.ptr == end;
}

}  // namespace

int main(int argc, char** argv) {
    std::uint64_t seed = 0xC0FFEEU;
    std::uint64_t operations = 100000;
    std::uint64_t dump_interval = 1000;

    for (int argument = 1; argument < argc; argument += 2) {
        if (argument + 1 >= argc) {
            std::cerr << "Expected a value after " << argv[argument] << '\n';
            return 2;
        }
        std::uint64_t value = 0;
        if (!parse_unsigned(argv[argument + 1], value)) {
            std::cerr << "Invalid unsigned integer: " << argv[argument + 1] << '\n';
            return 2;
        }
        const std::string_view option(argv[argument]);
        if (option == "--seed") {
            seed = value;
        } else if (option == "--operations") {
            operations = value;
        } else if (option == "--dump-interval") {
            dump_interval = value;
        } else {
            std::cerr << "Unknown option: " << option << '\n';
            return 2;
        }
    }

    if (operations == 0 || dump_interval == 0 ||
        operations == std::numeric_limits<std::uint64_t>::max()) {
        std::cerr << "Operations and dump interval must be positive and operations must be bounded\n";
        return 2;
    }

    const bool open_addressing_passed =
        run_differential<mlab::OpenAddressingIndex>(seed, operations, dump_interval,
                                                    "open-addressing");
    const bool paged_direct_passed =
        run_differential<mlab::PagedDirectIndex>(seed, operations, dump_interval, "paged-direct");
    if (!open_addressing_passed || !paged_direct_passed) {
        return 1;
    }
    std::cout << "Differential test passed: seed=" << seed << ", operations=" << operations
              << ", policies=2\n";
    return 0;
}
