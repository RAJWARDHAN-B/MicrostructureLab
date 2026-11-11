#pragma once

#include "mlab/memory/object_pool.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace mlab {

class OpenAddressingIndex {
public:
    explicit OpenAddressingIndex(std::size_t capacity)
        : slots_(table_size_for(capacity)), max_entries_(capacity) {}

    [[nodiscard]] bool insert(std::uint64_t id, std::uint32_t value) noexcept {
        if (size_ >= max_entries_) {
            return false;
        }

        const auto mask = slots_.size() - 1U;
        auto slot = static_cast<std::size_t>(hash(id)) & mask;
        for (std::size_t probes = 0; probes < slots_.size(); ++probes) {
            auto& candidate = slots_[slot];
            if (!candidate.occupied) {
                candidate = Slot{id, value, true};
                ++size_;
                return true;
            }
            if (candidate.id == id) {
                return false;
            }
            slot = (slot + 1U) & mask;
        }
        return false;
    }

    [[nodiscard]] std::optional<std::uint32_t> find(std::uint64_t id) const noexcept {
        const auto mask = slots_.size() - 1U;
        auto slot = static_cast<std::size_t>(hash(id)) & mask;
        for (std::size_t probes = 0; probes < slots_.size(); ++probes) {
            const auto& candidate = slots_[slot];
            if (!candidate.occupied) {
                return std::nullopt;
            }
            if (candidate.id == id) {
                return candidate.value;
            }
            slot = (slot + 1U) & mask;
        }
        return std::nullopt;
    }

    [[nodiscard]] bool erase(std::uint64_t id) noexcept {
        const auto mask = slots_.size() - 1U;
        auto hole = static_cast<std::size_t>(hash(id)) & mask;
        for (std::size_t probes = 0; probes < slots_.size(); ++probes) {
            const auto& candidate = slots_[hole];
            if (!candidate.occupied) {
                return false;
            }
            if (candidate.id == id) {
                break;
            }
            hole = (hole + 1U) & mask;
            if (probes + 1U == slots_.size()) {
                return false;
            }
        }

        auto scan = (hole + 1U) & mask;
        while (slots_[scan].occupied) {
            const auto home = static_cast<std::size_t>(hash(slots_[scan].id)) & mask;
            const auto scan_distance = (scan - home) & mask;
            const auto hole_distance = (hole - home) & mask;
            if (hole_distance < scan_distance) {
                slots_[hole] = slots_[scan];
                hole = scan;
            }
            scan = (scan + 1U) & mask;
        }
        slots_[hole] = Slot{};
        --size_;
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return size_;
    }

    [[nodiscard]] std::size_t capacity() const noexcept {
        return max_entries_;
    }

private:
    struct Slot {
        std::uint64_t id{0};
        std::uint32_t value{kNilIndex};
        bool occupied{false};
    };

    [[nodiscard]] static std::size_t table_size_for(std::size_t capacity) {
        if (capacity > std::numeric_limits<std::size_t>::max() / 2U) {
            throw std::length_error("OpenAddressingIndex capacity is too large");
        }
        const auto required = capacity == 0 ? std::size_t{1} : capacity * 2U;
        std::size_t table_size = 1;
        while (table_size < required) {
            if (table_size > std::numeric_limits<std::size_t>::max() / 2U) {
                throw std::length_error("OpenAddressingIndex capacity is too large");
            }
            table_size *= 2U;
        }
        return table_size;
    }

    [[nodiscard]] static constexpr std::uint64_t hash(std::uint64_t value) noexcept {
        value += 0x9e3779b97f4a7c15ULL;
        value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31U);
    }

    std::vector<Slot> slots_;
    std::size_t max_entries_;
    std::size_t size_{0};
};

class PagedDirectIndex {
public:
    static constexpr std::size_t kPageSize = 1024;

    explicit PagedDirectIndex(std::size_t capacity) : max_entries_(capacity) {}

    [[nodiscard]] bool insert(std::uint64_t id, std::uint32_t value) {
        if (size_ >= max_entries_) {
            return false;
        }
        const auto page_id = id / kPageSize;
        auto page_it = pages_.find(page_id);
        if (page_it == pages_.end()) {
            auto page = std::make_unique<Page>();
            page_it = pages_.emplace(page_id, std::move(page)).first;
        }
        auto& entry = page_it->second->entries[static_cast<std::size_t>(id % kPageSize)];
        if (entry != kNilIndex) {
            return false;
        }
        entry = value;
        ++size_;
        return true;
    }

    [[nodiscard]] std::optional<std::uint32_t> find(std::uint64_t id) const noexcept {
        const auto page_it = pages_.find(id / kPageSize);
        if (page_it == pages_.end()) {
            return std::nullopt;
        }
        const auto value = page_it->second->entries[static_cast<std::size_t>(id % kPageSize)];
        return value == kNilIndex ? std::nullopt : std::optional<std::uint32_t>{value};
    }

    [[nodiscard]] bool erase(std::uint64_t id) noexcept {
        const auto page_it = pages_.find(id / kPageSize);
        if (page_it == pages_.end()) {
            return false;
        }
        auto& entry = page_it->second->entries[static_cast<std::size_t>(id % kPageSize)];
        if (entry == kNilIndex) {
            return false;
        }
        entry = kNilIndex;
        --size_;
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return size_;
    }

    [[nodiscard]] std::size_t capacity() const noexcept {
        return max_entries_;
    }

    [[nodiscard]] std::size_t page_count() const noexcept {
        return pages_.size();
    }

private:
    struct Page {
        Page() {
            entries.fill(kNilIndex);
        }

        std::array<std::uint32_t, kPageSize> entries;
    };

    std::unordered_map<std::uint64_t, std::unique_ptr<Page>> pages_;
    std::size_t max_entries_;
    std::size_t size_{0};
};

}  // namespace mlab
