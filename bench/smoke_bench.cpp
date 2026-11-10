#include "mlab/book/reference_book.hpp"

#include <benchmark/benchmark.h>

namespace {

void reference_book_add(benchmark::State& state) {
    for (auto _ : state) {
        static_cast<void>(_);
        mlab::ReferenceBook book;
        benchmark::DoNotOptimize(book.add(1, mlab::Side::Buy, 100, 1));
    }
}

BENCHMARK(reference_book_add);

}  // namespace