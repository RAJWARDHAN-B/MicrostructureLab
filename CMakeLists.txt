cmake_minimum_required(VERSION 3.25)

project(MicrostructureLab LANGUAGES CXX)

include(CTest)
include(FetchContent)

set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(googletest)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.8.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(googlebenchmark)

add_library(mlab_project_options INTERFACE)
target_include_directories(mlab_project_options INTERFACE "${PROJECT_SOURCE_DIR}/include")
target_compile_features(mlab_project_options INTERFACE cxx_std_20)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(mlab_project_options INTERFACE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
    )
endif()

if(BUILD_TESTING)
    add_executable(book_primitives_test tests/unit/book_primitives_test.cpp)
    target_link_libraries(book_primitives_test PRIVATE mlab_project_options)
    add_test(NAME book_primitives_test COMMAND book_primitives_test)

    add_executable(reference_book_test tests/unit/reference_book_test.cpp)
    target_link_libraries(reference_book_test PRIVATE mlab_project_options)
    add_test(NAME reference_book_test COMMAND reference_book_test)

    add_executable(price_ladder_test tests/unit/price_ladder_test.cpp)
    target_link_libraries(price_ladder_test PRIVATE mlab_project_options)
    add_test(NAME price_ladder_test COMMAND price_ladder_test)

    add_executable(order_index_test tests/unit/order_index_test.cpp)
    target_link_libraries(order_index_test PRIVATE mlab_project_options)
    add_test(NAME order_index_test COMMAND order_index_test)

    add_executable(order_book_test tests/unit/order_book_test.cpp)
    target_link_libraries(order_book_test PRIVATE mlab_project_options)
    add_test(NAME order_book_test COMMAND order_book_test)

    add_executable(order_book_differential_test
        tests/differential/order_book_differential_test.cpp
    )
    target_link_libraries(order_book_differential_test PRIVATE mlab_project_options)
    add_test(
        NAME order_book_differential_test
        COMMAND order_book_differential_test --seed 12648430 --operations 50000
            --dump-interval 1000
    )
    set_tests_properties(order_book_differential_test PROPERTIES TIMEOUT 120)

    add_executable(core_smoke_test tests/unit/core_smoke_test.cpp)
    target_link_libraries(core_smoke_test PRIVATE mlab_project_options GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(core_smoke_test)
endif()

add_executable(mlab_smoke_bench bench/smoke_bench.cpp)
target_link_libraries(mlab_smoke_bench PRIVATE mlab_project_options benchmark::benchmark_main)

add_executable(mlab_book_bench bench/book_bench.cpp)
target_link_libraries(mlab_book_bench PRIVATE mlab_project_options benchmark::benchmark_main)