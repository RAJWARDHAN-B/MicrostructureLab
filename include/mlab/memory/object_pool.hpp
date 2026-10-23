#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace mlab {

inline constexpr std::uint32_t kNilIndex = std::numeric_limits<std::uint32_t>::max();

struct Order {
    std::uint64_t id{};
    std::int64_t price{};
    std::uint32_t qty{};
    std::uint32_t prev{kNilIndex};
    std::uint32_t next{kNilIndex};
    std::uint32_t level{kNilIndex};
};

static_assert(sizeof(Order) == 32);

class ObjectPool {
public:
    explicit ObjectPool(std::size_t capacity) {
        if (capacity > static_cast<std::size_t>(kNilIndex)) {
            throw std::length_error("ObjectPool capacity exceeds its 32-bit index range");
        }

        orders_.resize(capacity);
        allocated_.resize(capacity, 0);
        for (std::size_t index = 0; index < capacity; ++index) {
            orders_[index].next =
                index + 1 < capacity ? static_cast<std::uint32_t>(index + 1) : kNilIndex;
        }
        free_head_ = capacity == 0 ? kNilIndex : 0;
        available_ = capacity;
    }

    [[nodiscard]] std::uint32_t acquire() noexcept {
        if (free_head_ == kNilIndex) {
            return kNilIndex;
        }
        const auto index = free_head_;
        free_head_ = orders_[index].next;
        orders_[index] = Order{};
        allocated_[index] = 1;
        --available_;
        return index;
    }

    [[nodiscard]] bool release(std::uint32_t index) noexcept {
        if (index >= orders_.size() || allocated_[index] == 0) {
            return false;
        }
        orders_[index] = Order{};
        orders_[index].next = free_head_;
        allocated_[index] = 0;
        free_head_ = index;
        ++available_;
        return true;
    }

    [[nodiscard]] Order* get(std::uint32_t index) noexcept {
        if (index >= orders_.size() || allocated_[index] == 0) {
            return nullptr;
        }
        return &orders_[index];
    }

    [[nodiscard]] const Order* get(std::uint32_t index) const noexcept {
        if (index >= orders_.size() || allocated_[index] == 0) {
            return nullptr;
        }
        return &orders_[index];
    }

    [[nodiscard]] std::size_t capacity() const noexcept {
        return orders_.size();
    }

    [[nodiscard]] std::size_t available() const noexcept {
        return available_;
    }

private:
    std::vector<Order> orders_;
    std::vector<std::uint8_t> allocated_;
    std::uint32_t free_head_{kNilIndex};
    std::size_t available_{0};
};

}  // namespace mlab
