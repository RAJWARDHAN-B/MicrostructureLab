#pragma once

#include "mlab/book/level_bitmap.hpp"
#include "mlab/core/types.hpp"
#include "mlab/memory/object_pool.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <utility>
#include <vector>

namespace mlab {

class PriceLadder {
public:
    static constexpr std::uint32_t kSlotCount = LevelBitmap::kSlotCount;
    static constexpr std::uint32_t kCenterSlot = kSlotCount / 2U;

    struct Level {
        std::uint64_t total_qty{0};
        std::uint32_t order_count{0};
        std::uint32_t head{kNilIndex};
        std::uint32_t tail{kNilIndex};
    };

    struct Statistics {
        std::uint64_t dense_accesses{0};
        std::uint64_t recenter_count{0};
        std::uint64_t sparse_fallback_count{0};
    };

    PriceLadder() : dense_levels_(kSlotCount), present_(kSlotCount, 0) {}

    [[nodiscard]] Level* find(Price price) {
        if (const auto slot = slot_for(price); slot && present_[*slot] != 0) {
            return &dense_levels_[*slot];
        }
        const auto sparse_it = sparse_levels_.find(price);
        return sparse_it == sparse_levels_.end() ? nullptr : &sparse_it->second;
    }

    [[nodiscard]] const Level* find(Price price) const {
        if (const auto slot = slot_for(price); slot && present_[*slot] != 0) {
            return &dense_levels_[*slot];
        }
        const auto sparse_it = sparse_levels_.find(price);
        return sparse_it == sparse_levels_.end() ? nullptr : &sparse_it->second;
    }

    [[nodiscard]] Level* get_or_create(Price price) {
        if (!initialized_) {
            center_price_ = price;
            initialized_ = true;
        }

        auto slot = slot_for(price);
        if (!slot && recenter(price)) {
            slot = slot_for(price);
        }
        if (slot) {
            present_[*slot] = 1;
            ++statistics_.dense_accesses;
            return &dense_levels_[*slot];
        }

        ++statistics_.sparse_fallback_count;
        return &sparse_levels_.try_emplace(price).first->second;
    }

    /// Updates occupancy and keeps the dense best-price bitmap in sync.
    [[nodiscard]] bool set_order_count(Price price, std::uint32_t order_count) {
        auto* level = find(price);
        if (level == nullptr) {
            return false;
        }
        const bool was_occupied = level->order_count != 0;
        const bool is_occupied = order_count != 0;
        level->order_count = order_count;
        if (was_occupied == is_occupied) {
            return true;
        }
        if (const auto slot = slot_for(price)) {
            if (is_occupied) {
                static_cast<void>(occupied_.set(static_cast<std::uint32_t>(*slot)));
            } else {
                static_cast<void>(occupied_.clear(static_cast<std::uint32_t>(*slot)));
            }
        }
        return true;
    }

    /// Erases an empty level. Occupied levels must first have their orders removed.
    [[nodiscard]] bool erase(Price price) {
        if (const auto slot = slot_for(price); slot && present_[*slot] != 0) {
            if (dense_levels_[*slot].order_count != 0) {
                return false;
            }
            static_cast<void>(occupied_.clear(static_cast<std::uint32_t>(*slot)));
            dense_levels_[*slot] = Level{};
            present_[*slot] = 0;
            return true;
        }
        const auto sparse_it = sparse_levels_.find(price);
        if (sparse_it == sparse_levels_.end() || sparse_it->second.order_count != 0) {
            return false;
        }
        sparse_levels_.erase(sparse_it);
        return true;
    }

    /// Returns the best occupied price. `highest` is true for bids, false for asks.
    [[nodiscard]] std::optional<Price> best_price(bool highest) const noexcept {
        const auto dense_slot = highest ? occupied_.find_highest() : occupied_.find_lowest();
        const auto dense_price = dense_slot ? price_for_slot(center_price_, *dense_slot) : std::nullopt;
        const auto sparse_price = sparse_best(highest);
        if (!dense_price) {
            return sparse_price;
        }
        if (!sparse_price) {
            return dense_price;
        }
        return highest ? std::max(*dense_price, *sparse_price) : std::min(*dense_price, *sparse_price);
    }

    [[nodiscard]] const Statistics& statistics() const noexcept {
        return statistics_;
    }

private:
    using SparseLevels = std::map<Price, Level>;

    [[nodiscard]] std::optional<std::size_t> slot_for(Price price) const noexcept {
        if (!initialized_) {
            return std::nullopt;
        }
        if (price >= center_price_) {
            const auto distance = static_cast<std::uint64_t>(price) - static_cast<std::uint64_t>(center_price_);
            const auto maximum_distance = static_cast<std::uint64_t>(kSlotCount - kCenterSlot - 1U);
            if (distance > maximum_distance) {
                return std::nullopt;
            }
            return static_cast<std::size_t>(kCenterSlot) + static_cast<std::size_t>(distance);
        }

        const auto distance = static_cast<std::uint64_t>(center_price_) - static_cast<std::uint64_t>(price);
        if (distance > kCenterSlot) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(kCenterSlot - static_cast<std::uint32_t>(distance));
    }

    [[nodiscard]] static std::optional<Price> price_for_slot(Price center, std::size_t slot) noexcept {
        if (slot >= kCenterSlot) {
            const auto distance = static_cast<Price>(slot - kCenterSlot);
            if (center > std::numeric_limits<Price>::max() - distance) {
                return std::nullopt;
            }
            return center + distance;
        }

        const auto distance = static_cast<Price>(kCenterSlot - slot);
        if (center < std::numeric_limits<Price>::min() + distance) {
            return std::nullopt;
        }
        return center - distance;
    }

    [[nodiscard]] static std::uint64_t distance_between(Price low, Price high) noexcept {
        return static_cast<std::uint64_t>(high) - static_cast<std::uint64_t>(low);
    }

    [[nodiscard]] std::optional<Price> sparse_best(bool highest) const noexcept {
        if (highest) {
            for (auto current = sparse_levels_.rbegin(); current != sparse_levels_.rend(); ++current) {
                if (current->second.order_count != 0) {
                    return current->first;
                }
            }
        } else {
            for (auto current = sparse_levels_.begin(); current != sparse_levels_.end(); ++current) {
                if (current->second.order_count != 0) {
                    return current->first;
                }
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool recenter(Price incoming_price) {
        Price lowest = incoming_price;
        Price highest = incoming_price;
        std::vector<std::pair<Price, Level>> levels_to_move;
        levels_to_move.reserve(sparse_levels_.size() + dense_levels_.size() / 16U);

        for (std::size_t slot = 0; slot < present_.size(); ++slot) {
            if (present_[slot] == 0) {
                continue;
            }
            const auto price = price_for_slot(center_price_, slot);
            if (!price) {
                return false;
            }
            lowest = std::min(lowest, *price);
            highest = std::max(highest, *price);
            levels_to_move.emplace_back(*price, dense_levels_[slot]);
        }
        for (const auto& [price, level] : sparse_levels_) {
            lowest = std::min(lowest, price);
            highest = std::max(highest, price);
            levels_to_move.emplace_back(price, level);
        }

        if (distance_between(lowest, highest) >= kSlotCount) {
            return false;
        }

        const Price new_center = std::midpoint(highest, lowest);
        std::fill(dense_levels_.begin(), dense_levels_.end(), Level{});
        std::fill(present_.begin(), present_.end(), 0);
        occupied_ = LevelBitmap{};
        sparse_levels_.clear();
        center_price_ = new_center;

        for (const auto& [price, level] : levels_to_move) {
            const auto slot = slot_for(price);
            if (!slot) {
                sparse_levels_.emplace(price, level);
                continue;
            }
            dense_levels_[*slot] = level;
            present_[*slot] = 1;
            if (level.order_count != 0) {
                static_cast<void>(occupied_.set(static_cast<std::uint32_t>(*slot)));
            }
        }
        ++statistics_.recenter_count;
        return true;
    }

    std::vector<Level> dense_levels_;
    std::vector<std::uint8_t> present_;
    LevelBitmap occupied_;
    SparseLevels sparse_levels_;
    Statistics statistics_{};
    Price center_price_{0};
    bool initialized_{false};
};

}  // namespace mlab