#ifdef NDEBUG
#undef NDEBUG
#endif

#include "mlab/book/order_book.hpp"

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using mlab::BookError;
using mlab::BookOrder;
using mlab::OrderId;
using mlab::ReferenceBook;
using mlab::Side;

template <typename IndexPolicy>
void assert_same_state(const ReferenceBook& reference,
                       const mlab::OrderBook<IndexPolicy>& book) {
    assert(reference.dump() == book.dump());
    assert(reference.best_bid() == book.best_bid());
    assert(reference.best_ask() == book.best_ask());
    for (const auto side : {Side::Buy, Side::Sell, static_cast<Side>(2)}) {
        assert(reference.depth(side, 0) == book.depth(side, 0));
        assert(reference.depth(side, 1) == book.depth(side, 1));
        assert(reference.depth(side, 100) == book.depth(side, 100));
        for (mlab::Price price = 90; price <= 112; ++price) {
            assert(reference.level(side, price) == book.level(side, price));
        }
    }

    for (OrderId id = 0; id <= 24; ++id) {
        assert(reference.queue_position(id) == book.queue_position(id));
    }
    assert(reference.queue_position(1000) == book.queue_position(1000));

    std::vector<BookOrder> reference_orders;
    std::vector<BookOrder> book_orders;
    reference.for_each_order([&reference_orders](const BookOrder& order) {
        reference_orders.push_back(order);
    });
    book.for_each_order([&book_orders](const BookOrder& order) {
        book_orders.push_back(order);
    });
    assert(reference_orders == book_orders);
}

template <typename IndexPolicy>
void test_reference_book_compatibility() {
    ReferenceBook reference;
    mlab::OrderBook<IndexPolicy> book(16);
    const auto apply = [&reference, &book](BookError expected, auto&& operation) {
        assert(std::forward<decltype(operation)>(operation)(reference) == expected);
        assert(std::forward<decltype(operation)>(operation)(book) == expected);
        assert_same_state(reference, book);
    };

    apply(BookError::InvalidSide, [](auto& target) {
        return target.add(1, static_cast<Side>(2), 100, 1);
    });
    apply(BookError::InvalidQuantity, [](auto& target) {
        return target.add(1, Side::Buy, 100, 0);
    });
    apply(BookError::None, [](auto& target) { return target.add(1, Side::Buy, 100, 10); });
    apply(BookError::None, [](auto& target) { return target.add(2, Side::Buy, 100, 5); });
    apply(BookError::None, [](auto& target) { return target.add(3, Side::Buy, 99, 7); });
    apply(BookError::None, [](auto& target) { return target.add(4, Side::Sell, 101, 4); });
    apply(BookError::None, [](auto& target) { return target.add(5, Side::Sell, 102, 8); });
    apply(BookError::DuplicateOrderId, [](auto& target) {
        return target.add(1, Side::Sell, 101, 1);
    });
    apply(BookError::InvalidQuantity, [](auto& target) { return target.execute(1, 0); });
    apply(BookError::QuantityExceedsResting, [](auto& target) { return target.execute(1, 11); });
    apply(BookError::None, [](auto& target) { return target.execute(1, 3); });
    apply(BookError::None, [](auto& target) { return target.cancel(2, 2); });
    apply(BookError::OrderNotFound, [](auto& target) { return target.remove(1000); });
    apply(BookError::None, [](auto& target) { return target.remove(3); });
    apply(BookError::ReplacementIdMatchesOld, [](auto& target) {
        return target.replace(2, 2, 100, 1);
    });
    apply(BookError::DuplicateOrderId, [](auto& target) {
        return target.replace(2, 4, 100, 1);
    });
    apply(BookError::None, [](auto& target) { return target.replace(2, 6, 100, 9); });
    apply(BookError::None, [](auto& target) { return target.replace(4, 7, 103, 6); });

    apply(BookError::None, [](auto& target) { return target.add(8, Side::Buy, -300000, 3); });
    apply(BookError::None, [](auto& target) { return target.add(9, Side::Buy, 500000, 2); });
    apply(BookError::None, [](auto& target) { return target.add(10, Side::Sell, 500000, 4); });
    apply(BookError::None, [](auto& target) { return target.replace(8, 11, 500001, 3); });
}

template <typename IndexPolicy>
void test_capacity_exhaustion_and_replace_at_capacity() {
    mlab::OrderBook<IndexPolicy> book(1);
    assert(book.add(1, Side::Buy, 100, 1) == BookError::None);
    assert(book.add(2, Side::Buy, 100, 1) == BookError::CapacityExceeded);
    const auto new_id = (std::uint64_t{1} << 48U) + 2U;
    assert(book.replace(1, new_id, 101, 2) == BookError::None);
    assert(book.dump() == "BIDS\n101 2 1 " + std::to_string(new_id) + ":2\nASKS\n");
    assert(book.add(3, Side::Sell, 102, 1) == BookError::CapacityExceeded);
}

}  // namespace

int main() {
    test_reference_book_compatibility<mlab::OpenAddressingIndex>();
    test_reference_book_compatibility<mlab::PagedDirectIndex>();
    test_capacity_exhaustion_and_replace_at_capacity<mlab::OpenAddressingIndex>();
    test_capacity_exhaustion_and_replace_at_capacity<mlab::PagedDirectIndex>();
}
