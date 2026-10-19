#ifdef NDEBUG
#undef NDEBUG
#endif

#include "mlab/book/reference_book.hpp"

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {

using mlab::BookError;
using mlab::BookLevel;
using mlab::BookOrder;
using mlab::OrderId;
using mlab::ReferenceBook;
using mlab::Side;

void test_empty_book_queries() {
    const ReferenceBook book;
    assert(mlab::succeeded(BookError::None));
    assert(!mlab::succeeded(BookError::OrderNotFound));
    assert(!book.best_bid());
    assert(!book.best_ask());
    assert(!book.level(Side::Buy, 100));
    assert(!book.level(Side::Sell, 100));
    assert(book.depth(Side::Buy, 3).empty());
    assert(book.depth(Side::Sell, 0).empty());
    assert(book.depth(static_cast<Side>(2), 3).empty());
    assert(!book.queue_position(1));
    assert(book.dump() == "BIDS\nASKS\n");

    std::size_t visited = 0;
    book.for_each_order([&](const BookOrder&) { ++visited; });
    assert(visited == 0);
}

void test_add_aggregation_queries_and_dump() {
    ReferenceBook book;
    assert(book.add(1, Side::Buy, 100, 10) == BookError::None);
    assert(book.add(2, Side::Buy, 100, 5) == BookError::None);
    assert(book.add(3, Side::Buy, 99, 7) == BookError::None);
    assert(book.add(4, Side::Sell, 101, 4) == BookError::None);
    assert(book.add(5, Side::Sell, 102, 8) == BookError::None);

    assert((book.best_bid() == std::optional<BookLevel>{{100, 15, 2}}));
    assert((book.best_ask() == std::optional<BookLevel>{{101, 4, 1}}));
    assert((book.level(Side::Sell, 101) == std::optional<BookLevel>{{101, 4, 1}}));
    assert(!book.level(Side::Buy, 98));
    assert(!book.level(static_cast<Side>(2), 100));

    const auto bid_depth = book.depth(Side::Buy, 1);
    assert((bid_depth == std::vector<BookLevel>{{100, 15, 2}}));
    const auto ask_depth = book.depth(Side::Sell, 5);
    assert((ask_depth == std::vector<BookLevel>{{101, 4, 1}, {102, 8, 1}}));
    assert((book.depth(Side::Buy, 5) == std::vector<BookLevel>{{100, 15, 2}, {99, 7, 1}}));

    assert(book.queue_position(1) == 0);
    assert(book.queue_position(2) == 10);
    assert(!book.queue_position(99));

    std::vector<OrderId> visited_ids;
    book.for_each_order([&](const BookOrder& order) { visited_ids.push_back(order.id); });
    assert((visited_ids == std::vector<OrderId>{1, 2, 3, 4, 5}));

    assert(book.dump() == "BIDS\n100 15 2 1:10,2:5\n99 7 1 3:7\n"
                          "ASKS\n101 4 1 4:4\n102 8 1 5:8\n");
}

void test_add_validation_is_non_mutating() {
    ReferenceBook book;
    assert(book.add(1, Side::Buy, 100, 0) == BookError::InvalidQuantity);
    assert(book.add(1, static_cast<Side>(2), 100, 1) == BookError::InvalidSide);
    assert(book.add(1, Side::Buy, 100, 1) == BookError::None);
    assert(book.add(1, Side::Buy, 100, 1) == BookError::DuplicateOrderId);
    assert(book.add(1, Side::Sell, 101, 1) == BookError::DuplicateOrderId);
    assert(book.dump() == "BIDS\n100 1 1 1:1\nASKS\n");
}

void test_execute_partial_full_and_invalid_quantities() {
    ReferenceBook book;
    assert(book.execute(1, 0) == BookError::InvalidQuantity);
    assert(book.execute(99, 1) == BookError::OrderNotFound);
    assert(book.add(1, Side::Buy, 100, 5) == BookError::None);
    assert(book.execute(1, 6) == BookError::QuantityExceedsResting);
    assert(book.execute(1, 0) == BookError::InvalidQuantity);
    assert((book.level(Side::Buy, 100) == std::optional<BookLevel>{{100, 5, 1}}));
    assert(book.execute(1, 2) == BookError::None);
    assert((book.level(Side::Buy, 100) == std::optional<BookLevel>{{100, 3, 1}}));
    assert(book.execute(1, 3) == BookError::None);
    assert(!book.level(Side::Buy, 100));
    assert(!book.best_bid());
}

void test_cancel_partial_and_last_order_cleanup() {
    ReferenceBook book;
    assert(book.cancel(1, 0) == BookError::InvalidQuantity);
    assert(book.cancel(99, 1) == BookError::OrderNotFound);
    assert(book.add(1, Side::Sell, 101, 8) == BookError::None);
    assert(book.add(2, Side::Sell, 101, 3) == BookError::None);
    assert(book.cancel(1, 9) == BookError::QuantityExceedsResting);
    assert(book.cancel(1, 2) == BookError::None);
    assert(book.queue_position(2) == 6);
    assert((book.level(Side::Sell, 101) == std::optional<BookLevel>{{101, 9, 2}}));
    assert(book.cancel(1, 6) == BookError::None);
    assert(book.queue_position(2) == 0);
    assert(book.cancel(2, 3) == BookError::None);
    assert(!book.best_ask());
}

void test_remove() {
    ReferenceBook book;
    assert(book.remove(99) == BookError::OrderNotFound);
    assert(book.add(1, Side::Buy, 100, 2) == BookError::None);
    assert(book.add(2, Side::Buy, 100, 3) == BookError::None);
    assert(book.remove(1) == BookError::None);
    assert(book.queue_position(2) == 0);
    assert((book.level(Side::Buy, 100) == std::optional<BookLevel>{{100, 3, 1}}));
    assert(book.remove(2) == BookError::None);
    assert(!book.best_bid());
}

void test_replace_loses_priority_and_reports_errors() {
    ReferenceBook book;
    assert(book.replace(99, 3, 100, 0) == BookError::InvalidQuantity);
    assert(book.replace(99, 3, 100, 2) == BookError::OrderNotFound);
    assert(book.add(1, Side::Buy, 100, 4) == BookError::None);
    assert(book.add(2, Side::Buy, 100, 6) == BookError::None);
    assert(book.add(7, Side::Buy, 99, 2) == BookError::None);
    assert(book.replace(1, 1, 100, 1) == BookError::ReplacementIdMatchesOld);
    assert(book.replace(1, 2, 100, 1) == BookError::DuplicateOrderId);
    assert(book.replace(1, 3, 100, 0) == BookError::InvalidQuantity);
    assert(book.replace(1, 3, 100, 5) == BookError::None);
    assert(!book.queue_position(1));
    assert(book.queue_position(2) == 0);
    assert(book.queue_position(3) == 6);
    assert((book.best_bid() == std::optional<BookLevel>{{100, 11, 2}}));
    assert((book.level(Side::Buy, 99) == std::optional<BookLevel>{{99, 2, 1}}));
    assert(book.replace(3, 6, 99, 5) == BookError::None);
    assert(book.queue_position(6) == 2);
    assert((book.level(Side::Buy, 99) == std::optional<BookLevel>{{99, 7, 2}}));

    assert(book.add(4, Side::Sell, 101, 3) == BookError::None);
    assert(book.replace(4, 5, 101, 7) == BookError::None);
    assert(book.queue_position(5) == 0);
    assert((book.best_ask() == std::optional<BookLevel>{{101, 7, 1}}));
}

}  // namespace

int main() {
    test_empty_book_queries();
    test_add_aggregation_queries_and_dump();
    test_add_validation_is_non_mutating();
    test_execute_partial_full_and_invalid_quantities();
    test_cancel_partial_and_last_order_cleanup();
    test_remove();
    test_replace_loses_priority_and_reports_errors();
}
