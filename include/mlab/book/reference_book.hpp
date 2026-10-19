#pragma once

#include "mlab/core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <locale>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace mlab {

enum class BookError : std::uint8_t {
    None,
    InvalidSide,
    InvalidQuantity,
    DuplicateOrderId,
    OrderNotFound,
    QuantityExceedsResting,
    ReplacementIdMatchesOld,
};

[[nodiscard]] constexpr bool succeeded(BookError error) noexcept {
    return error == BookError::None;
}

struct BookLevel {
    Price price;
    std::uint64_t total_qty;
    std::size_t order_count;

    friend constexpr bool operator==(const BookLevel&, const BookLevel&) = default;
};

struct BookOrder {
    OrderId id;
    Side side;
    Price price;
    Qty qty;

    friend constexpr bool operator==(const BookOrder&, const BookOrder&) = default;
};

class ReferenceBook {
public:
    /// Adds a nonzero-quantity order at the back of its price-level FIFO queue.
    /// Prices are accepted as supplied; duplicate IDs, invalid sides, and zero
    /// quantities are rejected without changing the book.
    [[nodiscard]] BookError add(OrderId id, Side side, Price price, Qty qty) {
        if (!is_valid_side(side)) {
            return BookError::InvalidSide;
        }
        if (qty == 0) {
            return BookError::InvalidQuantity;
        }
        if (side == Side::Buy) {
            return add_to_levels(bids_, id, side, price, qty);
        }
        return add_to_levels(asks_, id, side, price, qty);
    }

    /// Executes qty from an existing order. Zero or excessive quantities are
    /// errors; an exact fill removes the order, otherwise its FIFO priority stays.
    [[nodiscard]] BookError execute(OrderId id, Qty qty) {
        return reduce(id, qty);
    }

    /// Cancels qty from an existing order. Zero or excessive quantities are
    /// errors; a full cancel removes the order, otherwise FIFO priority stays.
    [[nodiscard]] BookError cancel(OrderId id, Qty qty) {
        return reduce(id, qty);
    }

    /// Removes an existing order in full; a missing ID is reported.
    [[nodiscard]] BookError remove(OrderId id) {
        const auto order_it = orders_.find(id);
        if (order_it == orders_.end()) {
            return BookError::OrderNotFound;
        }
        erase_order(order_it);
        return BookError::None;
    }

    /// Replaces an existing order with a new ID, price, and quantity. The side
    /// is retained and the new order joins the back of its new price-level queue.
    /// Invalid quantities, missing old IDs, and conflicting new IDs leave state
    /// unchanged. Replacing an order with its own ID is rejected.
    [[nodiscard]] BookError replace(OrderId old_id, OrderId new_id, Price price, Qty qty) {
        if (qty == 0) {
            return BookError::InvalidQuantity;
        }
        const auto old_order_it = orders_.find(old_id);
        if (old_order_it == orders_.end()) {
            return BookError::OrderNotFound;
        }
        if (old_id == new_id) {
            return BookError::ReplacementIdMatchesOld;
        }

        const auto side = old_order_it->second.side;
        const auto add_error = add(new_id, side, price, qty);
        if (!succeeded(add_error)) {
            return add_error;
        }
        const auto current_old_order_it = orders_.find(old_id);
        erase_order(current_old_order_it);
        return BookError::None;
    }

    /// Returns the best price level, or nullopt when that side is empty.
    [[nodiscard]] std::optional<BookLevel> best_bid() const noexcept {
        return best_level(bids_);
    }

    /// Returns the best price level, or nullopt when that side is empty.
    [[nodiscard]] std::optional<BookLevel> best_ask() const noexcept {
        return best_level(asks_);
    }

    /// Returns a price level's aggregate quantity and order count.
    [[nodiscard]] std::optional<BookLevel> level(Side side, Price price) const noexcept {
        if (!is_valid_side(side)) {
            return std::nullopt;
        }
        if (side == Side::Buy) {
            const auto level_it = bids_.find(price);
            if (level_it == bids_.end()) {
                return std::nullopt;
            }
            return BookLevel{price, level_it->second.total_qty, level_it->second.orders.size()};
        }
        const auto level_it = asks_.find(price);
        if (level_it == asks_.end()) {
            return std::nullopt;
        }
        return BookLevel{price, level_it->second.total_qty, level_it->second.orders.size()};
    }

    /// Returns up to count price levels from best to worst.
    [[nodiscard]] std::vector<BookLevel> depth(Side side, std::size_t count) const {
        std::vector<BookLevel> result;
        if (!is_valid_side(side) || count == 0) {
            return result;
        }
        const auto append_levels = [&result, count](const auto& levels) {
            result.reserve(count < levels.size() ? count : levels.size());
            for (const auto& [price, price_level] : levels) {
                result.push_back(BookLevel{price, price_level.total_qty, price_level.orders.size()});
                if (result.size() == count) {
                    break;
                }
            }
        };
        if (side == Side::Buy) {
            append_levels(bids_);
        } else {
            append_levels(asks_);
        }
        return result;
    }

    /// Returns the resting quantity ahead of an order in its price-level FIFO
    /// queue, or nullopt if the ID is not present.
    [[nodiscard]] std::optional<std::uint64_t> queue_position(OrderId id) const noexcept {
        const auto order_it = orders_.find(id);
        if (order_it == orders_.end()) {
            return std::nullopt;
        }
        const auto& handle = order_it->second;
        std::uint64_t qty_ahead = 0;
        if (handle.side == Side::Buy) {
            const auto level_it = bids_.find(handle.price);
            for (auto current = level_it->second.orders.begin(); current != handle.order; ++current) {
                qty_ahead += current->qty;
            }
        } else {
            const auto level_it = asks_.find(handle.price);
            for (auto current = level_it->second.orders.begin(); current != handle.order; ++current) {
                qty_ahead += current->qty;
            }
        }
        return qty_ahead;
    }

    /// Visits bids from highest to lowest, then asks from lowest to highest;
    /// orders within each price level are visited in FIFO order.
    template <typename Function>
    void for_each_order(Function&& function) const {
        const auto visit_side = [&function](const auto& levels) {
            for (const auto& [price, price_level] : levels) {
                static_cast<void>(price);
                for (const auto& order : price_level.orders) {
                    std::invoke(function, order);
                }
            }
        };
        visit_side(bids_);
        visit_side(asks_);
    }

    /// Returns the complete book in canonical text form: BIDS then ASKS, each
    /// price-ordered, with aggregate quantity/count and FIFO-ordered id:qty pairs.
    [[nodiscard]] std::string dump() const {
        std::ostringstream output;
        output.imbue(std::locale::classic());
        output << "BIDS\n";
        dump_side(output, bids_);
        output << "ASKS\n";
        dump_side(output, asks_);
        return output.str();
    }

private:
    using OrderList = std::list<BookOrder>;

    struct PriceLevel {
        OrderList orders;
        std::uint64_t total_qty{0};
    };

    using BidLevels = std::map<Price, PriceLevel, std::greater<Price>>;
    using AskLevels = std::map<Price, PriceLevel, std::less<Price>>;

    struct OrderHandle {
        Side side;
        Price price;
        OrderList::iterator order;
    };

    [[nodiscard]] static constexpr bool is_valid_side(Side side) noexcept {
        return side == Side::Buy || side == Side::Sell;
    }

    template <typename Levels>
    [[nodiscard]] BookError add_to_levels(Levels& levels, OrderId id, Side side, Price price, Qty qty) {
        const auto level_it = levels.try_emplace(price).first;
        struct Rollback {
            Levels& levels;
            typename Levels::iterator level_it;
            OrderList::iterator order_it;
            bool has_order{false};
            bool committed{false};

            Rollback(Levels& levels_arg, typename Levels::iterator level_it_arg)
                : levels(levels_arg), level_it(level_it_arg) {}

            ~Rollback() {
                if (!committed) {
                    if (has_order) {
                        level_it->second.orders.erase(order_it);
                    }
                    if (level_it->second.orders.empty()) {
                        levels.erase(level_it);
                    }
                }
            }
        } rollback{levels, level_it};

        auto& price_level = level_it->second;
        rollback.order_it = price_level.orders.emplace(price_level.orders.end(), BookOrder{id, side, price, qty});
        rollback.has_order = true;
        const auto inserted = orders_.emplace(id, OrderHandle{side, price, rollback.order_it}).second;
        if (!inserted) {
            return BookError::DuplicateOrderId;
        }
        price_level.total_qty += qty;
        rollback.committed = true;
        return BookError::None;
    }

    template <typename Levels>
    [[nodiscard]] static std::optional<BookLevel> best_level(const Levels& levels) noexcept {
        if (levels.empty()) {
            return std::nullopt;
        }
        const auto& [price, price_level] = *levels.begin();
        return BookLevel{price, price_level.total_qty, price_level.orders.size()};
    }

    [[nodiscard]] BookError reduce(OrderId id, Qty qty) {
        if (qty == 0) {
            return BookError::InvalidQuantity;
        }
        const auto order_it = orders_.find(id);
        if (order_it == orders_.end()) {
            return BookError::OrderNotFound;
        }
        const auto resting_qty = order_it->second.order->qty;
        if (qty > resting_qty) {
            return BookError::QuantityExceedsResting;
        }
        if (qty == resting_qty) {
            erase_order(order_it);
            return BookError::None;
        }

        order_it->second.order->qty -= qty;
        if (order_it->second.side == Side::Buy) {
            bids_.find(order_it->second.price)->second.total_qty -= qty;
        } else {
            asks_.find(order_it->second.price)->second.total_qty -= qty;
        }
        return BookError::None;
    }

    void erase_order(std::unordered_map<OrderId, OrderHandle>::iterator order_it) {
        const auto handle = order_it->second;
        const auto erase_from_levels = [&handle](auto& levels) {
            const auto level_it = levels.find(handle.price);
            auto& price_level = level_it->second;
            price_level.total_qty -= handle.order->qty;
            price_level.orders.erase(handle.order);
            if (price_level.orders.empty()) {
                levels.erase(level_it);
            }
        };
        if (handle.side == Side::Buy) {
            erase_from_levels(bids_);
        } else {
            erase_from_levels(asks_);
        }
        orders_.erase(order_it);
    }

    template <typename Levels>
    static void dump_side(std::ostringstream& output, const Levels& levels) {
        for (const auto& [price, price_level] : levels) {
            output << price << ' ' << price_level.total_qty << ' ' << price_level.orders.size() << ' ';
            bool first_order = true;
            for (const auto& order : price_level.orders) {
                if (!first_order) {
                    output << ',';
                }
                output << order.id << ':' << order.qty;
                first_order = false;
            }
            output << '\n';
        }
    }

    BidLevels bids_;
    AskLevels asks_;
    std::unordered_map<OrderId, OrderHandle> orders_;
};

}  // namespace mlab
