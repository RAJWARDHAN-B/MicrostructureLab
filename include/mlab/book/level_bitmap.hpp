#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace mlab {

class LevelBitmap {
public:
    static constexpr std::uint32_t kSlotCount = 1U << 18U;

    [[nodiscard]] bool set(std::uint32_t slot) noexcept {
        if (slot >= kSlotCount) {
            return false;
        }

        const auto leaf_word_index = static_cast<std::size_t>(slot / kBitsPerWord);
        const auto leaf_mask = std::uint64_t{1} << (slot % kBitsPerWord);
        auto& leaf_word = leaf_[leaf_word_index];
        if ((leaf_word & leaf_mask) != 0) {
            return true;
        }
        const bool leaf_was_empty = leaf_word == 0;
        leaf_word |= leaf_mask;
        if (leaf_was_empty) {
            const auto middle_word_index = leaf_word_index / kBitsPerWord;
            auto& middle_word = middle_[middle_word_index];
            const bool middle_was_empty = middle_word == 0;
            middle_word |= std::uint64_t{1} << (leaf_word_index % kBitsPerWord);
            if (middle_was_empty) {
                top_ |= std::uint64_t{1} << middle_word_index;
            }
        }
        return true;
    }

    [[nodiscard]] bool clear(std::uint32_t slot) noexcept {
        if (slot >= kSlotCount) {
            return false;
        }

        const auto leaf_word_index = static_cast<std::size_t>(slot / kBitsPerWord);
        const auto leaf_mask = std::uint64_t{1} << (slot % kBitsPerWord);
        auto& leaf_word = leaf_[leaf_word_index];
        if ((leaf_word & leaf_mask) == 0) {
            return true;
        }
        leaf_word &= ~leaf_mask;
        if (leaf_word == 0) {
            const auto middle_word_index = leaf_word_index / kBitsPerWord;
            auto& middle_word = middle_[middle_word_index];
            middle_word &= ~(std::uint64_t{1} << (leaf_word_index % kBitsPerWord));
            if (middle_word == 0) {
                top_ &= ~(std::uint64_t{1} << middle_word_index);
            }
        }
        return true;
    }

    [[nodiscard]] bool contains(std::uint32_t slot) const noexcept {
        if (slot >= kSlotCount) {
            return false;
        }
        return (leaf_[slot / kBitsPerWord] & (std::uint64_t{1} << (slot % kBitsPerWord))) != 0;
    }

    [[nodiscard]] bool empty() const noexcept {
        return top_ == 0;
    }

    [[nodiscard]] std::optional<std::uint32_t> find_lowest() const noexcept {
        if (top_ == 0) {
            return std::nullopt;
        }
        const auto middle_word_index = kLowestBit(top_);
        const auto leaf_word_index =
            middle_word_index * kBitsPerWord + kLowestBit(middle_[middle_word_index]);
        const auto slot_index =
            leaf_word_index * kBitsPerWord + kLowestBit(leaf_[leaf_word_index]);
        return static_cast<std::uint32_t>(slot_index);
    }

    [[nodiscard]] std::optional<std::uint32_t> find_highest() const noexcept {
        if (top_ == 0) {
            return std::nullopt;
        }
        const auto middle_word_index = static_cast<std::size_t>(kHighestBit(top_));
        const auto leaf_word_index = middle_word_index * kBitsPerWord +
                                    static_cast<std::size_t>(kHighestBit(middle_[middle_word_index]));
        const auto slot_index = leaf_word_index * kBitsPerWord +
                                static_cast<std::size_t>(kHighestBit(leaf_[leaf_word_index]));
        return static_cast<std::uint32_t>(slot_index);
    }

    /// Finds the nearest set slot strictly greater than slot. An out-of-range
    /// starting slot has no successor.
    [[nodiscard]] std::optional<std::uint32_t> find_next_above(std::uint32_t slot) const noexcept {
        if (slot >= kSlotCount) {
            return std::nullopt;
        }

        const auto leaf_word_index = static_cast<std::size_t>(slot / kBitsPerWord);
        const auto bit_index = slot % kBitsPerWord;
        if (bit_index < kBitsPerWord - 1) {
            const auto candidates = leaf_[leaf_word_index] & (~std::uint64_t{0} << (bit_index + 1));
            if (candidates != 0) {
                return static_cast<std::uint32_t>(leaf_word_index * kBitsPerWord +
                                                  kLowestBit(candidates));
            }
        }

        const auto next_leaf_word = find_leaf_word_above(leaf_word_index);
        if (!next_leaf_word) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(*next_leaf_word * kBitsPerWord +
                                          kLowestBit(leaf_[*next_leaf_word]));
    }

    /// Finds the nearest set slot strictly less than slot. Starting at or above
    /// the slot count searches from the highest valid slot.
    [[nodiscard]] std::optional<std::uint32_t> find_next_below(std::uint32_t slot) const noexcept {
        if (top_ == 0 || slot == 0) {
            return std::nullopt;
        }
        if (slot >= kSlotCount) {
            return find_highest();
        }

        const auto leaf_word_index = static_cast<std::size_t>(slot / kBitsPerWord);
        const auto bit_index = slot % kBitsPerWord;
        if (bit_index != 0) {
            const auto candidates = leaf_[leaf_word_index] & ((std::uint64_t{1} << bit_index) - 1);
            if (candidates != 0) {
                return static_cast<std::uint32_t>(leaf_word_index * kBitsPerWord +
                                                  kHighestBit(candidates));
            }
        }

        const auto previous_leaf_word = find_leaf_word_below(leaf_word_index);
        if (!previous_leaf_word) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(*previous_leaf_word * kBitsPerWord +
                                          kHighestBit(leaf_[*previous_leaf_word]));
    }

private:
    static constexpr std::uint32_t kBitsPerWord = 64;
    static constexpr std::size_t kLeafWordCount = kSlotCount / kBitsPerWord;
    static constexpr std::size_t kMiddleWordCount = kLeafWordCount / kBitsPerWord;

    [[nodiscard]] static constexpr std::uint32_t kHighestBit(std::uint64_t value) noexcept {
        return 63U - static_cast<std::uint32_t>(std::countl_zero(value));
    }

    [[nodiscard]] static constexpr std::size_t kLowestBit(std::uint64_t value) noexcept {
        return static_cast<std::size_t>(std::countr_zero(value));
    }

    [[nodiscard]] std::optional<std::size_t> find_leaf_word_above(std::size_t leaf_word) const noexcept {
        const auto middle_word_index = leaf_word / kBitsPerWord;
        const auto middle_bit_index = leaf_word % kBitsPerWord;
        if (middle_bit_index < kBitsPerWord - 1) {
            const auto candidates = middle_[middle_word_index] & (~std::uint64_t{0} << (middle_bit_index + 1));
            if (candidates != 0) {
                return middle_word_index * kBitsPerWord + kLowestBit(candidates);
            }
        }

        const auto next_middle_words =
            middle_word_index < kBitsPerWord - 1
                ? top_ & (~std::uint64_t{0} << (middle_word_index + 1))
                : std::uint64_t{0};
        if (next_middle_words == 0) {
            return std::nullopt;
        }
        const auto next_middle_word = static_cast<std::size_t>(std::countr_zero(next_middle_words));
        return next_middle_word * kBitsPerWord + kLowestBit(middle_[next_middle_word]);
    }

    [[nodiscard]] std::optional<std::size_t> find_leaf_word_below(std::size_t leaf_word) const noexcept {
        const auto middle_word_index = leaf_word / kBitsPerWord;
        const auto middle_bit_index = leaf_word % kBitsPerWord;
        if (middle_bit_index != 0) {
            const auto candidates = middle_[middle_word_index] & ((std::uint64_t{1} << middle_bit_index) - 1);
            if (candidates != 0) {
                return middle_word_index * kBitsPerWord + kHighestBit(candidates);
            }
        }
        if (middle_word_index == 0) {
            return std::nullopt;
        }

        const auto previous_middle_words = top_ & ((std::uint64_t{1} << middle_word_index) - 1);
        if (previous_middle_words == 0) {
            return std::nullopt;
        }
        const auto previous_middle_word = static_cast<std::size_t>(kHighestBit(previous_middle_words));
        return previous_middle_word * kBitsPerWord + kHighestBit(middle_[previous_middle_word]);
    }

    std::array<std::uint64_t, kLeafWordCount> leaf_{};
    std::array<std::uint64_t, kMiddleWordCount> middle_{};
    std::uint64_t top_{0};
};

}  // namespace mlab
