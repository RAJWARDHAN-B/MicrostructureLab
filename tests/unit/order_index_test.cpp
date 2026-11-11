#ifdef NDEBUG
#undef NDEBUG
#endif

#include "mlab/book/order_index.hpp"

#include <cassert>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

template <typename Index>
void test_basic_index_operations() {
    Index index(3);
    assert(index.size() == 0);
    assert(index.capacity() == 3);
    assert(!index.find(10));
    assert(!index.erase(10));

    assert(index.insert(10, 4));
    assert(index.insert(20, 7));
    assert(index.insert(std::numeric_limits<std::uint64_t>::max(), 9));
    assert(!index.insert(10, 11));
    assert(!index.insert(30, 12));
    assert(index.find(10) == 4);
    assert(index.find(20) == 7);
    assert(index.find(std::numeric_limits<std::uint64_t>::max()) == 9);
    assert(index.size() == 3);

    assert(index.erase(20));
    assert(!index.erase(20));
    assert(!index.find(20));
    assert(index.insert(30, 12));
    assert(index.find(10) == 4);
    assert(index.find(30) == 12);
    assert(index.size() == 3);
}

void test_open_addressing_backshift_preserves_cluster() {
    mlab::OpenAddressingIndex index(64);
    std::vector<std::uint64_t> ids;
    for (std::uint64_t id = 0; id < 64; ++id) {
        assert(index.insert(id * 17U, static_cast<std::uint32_t>(id)));
        ids.push_back(id * 17U);
    }

    for (std::size_t position = 1; position < ids.size(); position += 2U) {
        assert(index.erase(ids[position]));
    }
    for (std::size_t position = 0; position < ids.size(); position += 2U) {
        assert(index.find(ids[position]) == position);
    }
    for (std::size_t position = 1; position < ids.size(); position += 2U) {
        assert(!index.find(ids[position]));
    }

    for (std::size_t position = 1; position < ids.size(); position += 2U) {
        assert(index.insert(ids[position] + 1U, static_cast<std::uint32_t>(position)));
    }
    assert(index.size() == 64);
}

void test_paged_index_keeps_sparse_pages_after_erase() {
    mlab::PagedDirectIndex index(4);
    const auto distant_id = (std::uint64_t{1} << 48U) + 77U;
    assert(index.insert(1, 3));
    assert(index.insert(distant_id, 8));
    assert(index.page_count() == 2);
    assert(index.find(distant_id) == 8);
    assert(index.erase(1));
    assert(index.page_count() == 2);
    assert(index.insert(2, 5));
    assert(index.page_count() == 2);
    assert(index.find(2) == 5);
}

}  // namespace

int main() {
    test_basic_index_operations<mlab::OpenAddressingIndex>();
    test_basic_index_operations<mlab::PagedDirectIndex>();
    test_open_addressing_backshift_preserves_cluster();
    test_paged_index_keeps_sparse_pages_after_erase();
}
