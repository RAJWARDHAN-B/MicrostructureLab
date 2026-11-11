#pragma once

#include "mlab/book/order_index.hpp"
#include "mlab/book/price_ladder.hpp"
#include "mlab/book/reference_book.hpp"
#include "mlab/core/assert.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace mlab {

template <typename IndexPolicy = OpenAddressingIndex>
class OrderBook {
public:
    static constexpr std::size_t kDefaultCapacity = 1U << 16U;

    explicit OrderBook(std::size_t capacity = kDefaultCapacity)
        : pool_(capacity), index_(capacity), order_sides_(capacity, Side::Buy) {}

    [[nodiscard]] BookError add(OrderId id, Side side, Price price, Qty qty) {
        if (!is_valid_side(side)) {
            return BookError::InvalidSide;
        }
        if (qty == 0) {
            return BookError::InvalidQuantity;
        }
        if (index_.find(id)) {
            return BookError::DuplicateOrderId;
        }
        if (pool_.available() == 0) {
            return BookError::CapacityExceeded;
        }

        auto& ladder = ladder_for(side);
        auto* level = ladder.get_or_create(price);
        const auto order_index = pool_.acquire();
        if (order_index == kNilIndex) {
            if (level->order_count == 0) {
                static_cast<void>(ladder.erase(price));
            }
            return BookError::CapacityExceeded;
        }

        struct Rollback {
            ObjectPool& pool;
            PriceLadder& ladder;
            Price price;
            std::uint32_t order_index;
            bool committed{false};

            ~Rollback() {
                if (!committed) {
                    static_cast<void>(pool.release(order_index));
                    const auto* level = ladder.find(price);
                    if (level != nullptr && level->order_count == 0) {
                        static_cast<void>(ladder.erase(price));
                    }
                }
            }
        } rollback{pool_, ladder, price, order_index};

        auto* order = pool_.get(order_index);
        order->id = id;
        order->price = price;
        order->qty = qty;
        order->level = kNilIndex;
        order_sides_[order_index] = side;
        if (!index_.insert(id, order_index)) {
            return index_.find(id) ? BookError::DuplicateOrderId : BookError::CapacityExceeded;
        }

        append_order(ladder, price, *level, order_index, *order, qty);
        rollback.committed = true;
        return BookError::None;
    }

    [[nodiscard]] BookError execute(OrderId id, Qty qty) {
        return reduce(id, qty);
    }

    [[nodiscard]] BookError cancel(OrderId id, Qty qty) {
        return reduce(id, qty);
    }

    [[nodiscard]] BookError remove(OrderId id) {
        const auto order_index = index_.find(id);
        if (!order_index) {
            return BookError::OrderNotFound;
        }
        remove_order(*order_index);
        return BookError::None;
    }

    [[nodiscard]] BookError replace(OrderId old_id, OrderId new_id, Price price, Qty qty) {
        if (qty == 0) {
            return BookError::InvalidQuantity;
        }
        const auto order_index = index_.find(old_id);
        if (!order_index) {
            return BookError::OrderNotFound;
        }
        if (old_id == new_id) {
            return BookError::ReplacementIdMatchesOld;
        }
        if (index_.find(new_id)) {
            return BookError::DuplicateOrderId;
        }

        auto* order = pool_.get(*order_index);
        const auto side = order_sides_[*order_index];
        auto& ladder = ladder_for(side);
        auto* target_level = ladder.get_or_create(price);
        MLAB_VERIFY(index_.replace(old_id, new_id, *order_index));

        const bool same_level = order->price == price;
        detach_order(ladder, order->price, *order_index, !same_level);
        order->id = new_id;
        append_order(ladder, price, *target_level, *order_index, *order, qty);
        return BookError::None;
    }

    [[nodiscard]] std::optional<BookLevel> best_bid() const noexcept {
        return best_level(bids_, true);
    }

    [[nodiscard]] std::optional<BookLevel> best_ask() const noexcept {
        return best_level(asks_, false);
    }

    [[nodiscard]] std::optional<BookLevel> level(Side side, Price price) const noexcept {
        if (!is_valid_side(side)) {
            return std::nullopt;
        }
        const auto* found = ladder_for(side).find(price);
        if (found == nullptr || found->order_count == 0) {
            return std::nullopt;
        }
        return BookLevel{price, found->total_qty, found->order_count};
    }

    [[nodiscard]] std::vector<BookLevel> depth(Side side, std::size_t count) const {
        std::vector<BookLevel> result;
        if (!is_valid_side(side) || count == 0) {
            return result;
        }
        const auto& ladder = ladder_for(side);
        ladder.for_each_level(side == Side::Buy, [&result, count](Price price,
                                                                  const PriceLadder::Level& level) {
            result.push_back(BookLevel{price, level.total_qty, level.order_count});
            return result.size() < count;
        });
        return result;
    }

    [[nodiscard]] std::optional<std::uint64_t> queue_position(OrderId id) const noexcept {
        const auto order_index = index_.find(id);
        if (!order_index) {
            return std::nullopt;
        }
        const auto* order = pool_.get(*order_index);
        std::uint64_t qty_ahead = 0;
        auto previous = order->prev;
        while (previous != kNilIndex) {
            const auto* preceding_order = pool_.get(previous);
            MLAB_VERIFY(preceding_order != nullptr);
            qty_ahead += preceding_order->qty;
            previous = preceding_order->prev;
        }
        return qty_ahead;
    }

    template <typename Function>
    void for_each_order(Function&& function) const {
        visit_side(bids_, Side::Buy, function);
        visit_side(asks_, Side::Sell, function);
    }

    [[nodiscard]] std::string dump() const {
        std::ostringstream output;
        output.imbue(std::locale::classic());
        output << "BIDS\n";
        dump_side(output, bids_, true);
        output << "ASKS\n";
        dump_side(output, asks_, false);
        return output.str();
    }

private:
    [[nodiscard]] static constexpr bool is_valid_side(Side side) noexcept {
        return side == Side::Buy || side == Side::Sell;
    }

    [[nodiscard]] PriceLadder& ladder_for(Side side) noexcept {
        return side == Side::Buy ? bids_ : asks_;
    }

    [[nodiscard]] const PriceLadder& ladder_for(Side side) const noexcept {
        return side == Side::Buy ? bids_ : asks_;
    }

    [[nodiscard]] static std::optional<BookLevel> best_level(const PriceLadder& ladder,
                                                              bool highest) noexcept {
        const auto price = ladder.best_price(highest);
        if (!price) {
            return std::nullopt;
        }
        const auto* level = ladder.find(*price);
        MLAB_VERIFY(level != nullptr);
        return BookLevel{*price, level->total_qty, level->order_count};
    }

    void append_order(PriceLadder& ladder, Price price, PriceLadder::Level& level,
                      std::uint32_t order_index, Order& order, Qty qty) noexcept {
        order.price = price;
        order.qty = qty;
        order.prev = level.tail;
        order.next = kNilIndex;
        if (level.tail == kNilIndex) {
            level.head = order_index;
        } else {
            auto* tail = pool_.get(level.tail);
            MLAB_VERIFY(tail != nullptr);
            tail->next = order_index;
        }
        level.tail = order_index;
        level.total_qty += qty;
        MLAB_VERIFY(ladder.set_order_count(price, level.order_count + 1U));
    }

    void detach_order(PriceLadder& ladder, Price price, std::uint32_t order_index,
                      bool erase_empty_level) noexcept {
        auto* order = pool_.get(order_index);
        MLAB_VERIFY(order != nullptr);
        auto* level = ladder.find(price);
        MLAB_VERIFY(level != nullptr);
        if (order->prev == kNilIndex) {
            level->head = order->next;
        } else {
            auto* previous = pool_.get(order->prev);
            MLAB_VERIFY(previous != nullptr);
            previous->next = order->next;
        }
        if (order->next == kNilIndex) {
            level->tail = order->prev;
        } else {
            auto* next = pool_.get(order->next);
            MLAB_VERIFY(next != nullptr);
            next->prev = order->prev;
        }
        level->total_qty -= order->qty;
        MLAB_VERIFY(level->order_count != 0);
        MLAB_VERIFY(ladder.set_order_count(price, level->order_count - 1U));
        if (erase_empty_level && level->order_count == 0) {
            MLAB_VERIFY(ladder.erase(price));
        }
        order->prev = kNilIndex;
        order->next = kNilIndex;
    }

    void remove_order(std::uint32_t order_index) noexcept {
        auto* order = pool_.get(order_index);
        MLAB_VERIFY(order != nullptr);
        MLAB_VERIFY(index_.erase(order->id));
        auto& ladder = ladder_for(order_sides_[order_index]);
        detach_order(ladder, order->price, order_index, true);
        MLAB_VERIFY(pool_.release(order_index));
    }

    [[nodiscard]] BookError reduce(OrderId id, Qty qty) {
        if (qty == 0) {
            return BookError::InvalidQuantity;
        }
        const auto order_index = index_.find(id);
        if (!order_index) {
            return BookError::OrderNotFound;
        }
        auto* order = pool_.get(*order_index);
        if (qty > order->qty) {
            return BookError::QuantityExceedsResting;
        }
        if (qty == order->qty) {
            remove_order(*order_index);
            return BookError::None;
        }

        order->qty -= qty;
        auto* level = ladder_for(order_sides_[*order_index]).find(order->price);
        MLAB_VERIFY(level != nullptr);
        level->total_qty -= qty;
        return BookError::None;
    }

    template <typename Function>
    void visit_side(const PriceLadder& ladder, Side side, Function& function) const {
        ladder.for_each_level(side == Side::Buy, [this, side, &function](Price price,
                                                                         const PriceLadder::Level& level) {
            auto order_index = level.head;
            while (order_index != kNilIndex) {
                const auto* order = pool_.get(order_index);
                MLAB_VERIFY(order != nullptr);
                std::invoke(function, BookOrder{order->id, side, price, order->qty});
                order_index = order->next;
            }
            return true;
        });
    }

    void dump_side(std::ostringstream& output, const PriceLadder& ladder, bool highest) const {
        ladder.for_each_level(highest, [this, &output](Price price,
                                                       const PriceLadder::Level& level) {
            output << price << ' ' << level.total_qty << ' ' << level.order_count << ' ';
            bool first_order = true;
            auto order_index = level.head;
            while (order_index != kNilIndex) {
                const auto* order = pool_.get(order_index);
                MLAB_VERIFY(order != nullptr);
                if (!first_order) {
                    output << ',';
                }
                output << order->id << ':' << order->qty;
                first_order = false;
                order_index = order->next;
            }
            output << '\n';
            return true;
        });
    }

    ObjectPool pool_;
    IndexPolicy index_;
    std::vector<Side> order_sides_;
    PriceLadder bids_;
    PriceLadder asks_;
};

}  // namespace mlab
