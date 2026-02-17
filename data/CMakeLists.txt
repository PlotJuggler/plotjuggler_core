cmake_minimum_required(VERSION 3.22)
project(pj_engine LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(absl REQUIRED)
find_package(GTest REQUIRED)
find_package(benchmark CONFIG REQUIRED)

option(PJ_ASSERT_THROWS "Use exceptions instead of assert() for PJ_ASSERT" OFF)

# Sanitizers for Debug builds (opt-in via PJ_ENABLE_SANITIZERS)
# Note: ASAN/UBSan have compatibility issues with abseil on GCC 11.
# Use GCC 13+ or Clang for best results.
option(PJ_ENABLE_SANITIZERS "Enable ASAN and UBSan for Debug builds" OFF)
if(PJ_ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(PJ_SANITIZER_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_compile_options(${PJ_SANITIZER_FLAGS})
  add_link_options(-fsanitize=address,undefined)
endif()

# ---------------------------------------------------------------------------
# plotjuggler_base — vocabulary types, zero external dependencies
# ---------------------------------------------------------------------------

add_library(plotjuggler_base STATIC
  base/src/type_tree.cpp
)
target_include_directories(plotjuggler_base PUBLIC base/include)
target_compile_features(plotjuggler_base PUBLIC cxx_std_20)
target_compile_options(plotjuggler_base PRIVATE
  -Wall -Wextra -Wshadow -Wnon-virtual-dtor -Wold-style-cast
  -Wcast-qual -Wconversion -Woverloaded-virtual -Wpedantic
  ${PJ_SANITIZER_FLAGS}
)
if(PJ_ASSERT_THROWS)
  target_compile_definitions(plotjuggler_base PUBLIC PJ_ASSERT_THROWS)
endif()

# ---------------------------------------------------------------------------
# plotjuggler_engine — storage engine, depends on base + abseil
# ---------------------------------------------------------------------------

find_package(Arrow REQUIRED)

add_library(plotjuggler_engine STATIC
  engine/src/buffer.cpp
  engine/src/column_buffer.cpp
  engine/src/encoding.cpp
  engine/src/chunk.cpp
  engine/src/topic_storage.cpp
  engine/src/type_registry.cpp
  engine/src/query.cpp
  engine/src/writer.cpp
  engine/src/reader.cpp
  engine/src/engine.cpp
  engine/src/arrow_import.cpp
)
target_include_directories(plotjuggler_engine PUBLIC engine/include)
target_compile_features(plotjuggler_engine PUBLIC cxx_std_20)
target_compile_options(plotjuggler_engine PRIVATE
  -Wall -Wextra -Wshadow -Wnon-virtual-dtor -Wold-style-cast
  -Wcast-qual -Wconversion -Woverloaded-virtual -Wpedantic
  ${PJ_SANITIZER_FLAGS}
)
if(PJ_ASSERT_THROWS)
  target_compile_definitions(plotjuggler_engine PUBLIC PJ_ASSERT_THROWS)
endif()
target_link_libraries(plotjuggler_engine
  PUBLIC
    plotjuggler_base
    absl::flat_hash_map
    absl::flat_hash_set
    absl::btree
    absl::status
    absl::statusor
    absl::strings
    absl::str_format
    absl::hash
    absl::inlined_vector
    absl::span
    absl::time
    absl::function_ref
  PRIVATE
    Arrow::arrow_static
)
# Arrow headers needed by public header pj/engine/arrow_import.hpp
get_target_property(_arrow_inc Arrow::arrow_static INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(plotjuggler_engine PUBLIC ${_arrow_inc})

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

enable_testing()

# Base-only tests (link plotjuggler_base only)
set(BASE_TEST_SOURCES
  tests/types_test.cpp
  tests/type_tree_test.cpp
)

foreach(test_src ${BASE_TEST_SOURCES})
  get_filename_component(test_name ${test_src} NAME_WE)
  add_executable(${test_name} ${test_src})
  target_link_libraries(${test_name} PRIVATE plotjuggler_base GTest::gtest_main)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# Engine tests (link plotjuggler_engine, which transitively provides base)
set(ENGINE_TEST_SOURCES
  tests/type_registry_test.cpp
  tests/buffer_test.cpp
  tests/column_buffer_test.cpp
  tests/encoding_test.cpp
  tests/chunk_test.cpp
  tests/topic_storage_test.cpp
  tests/query_test.cpp
  tests/engine_integration_test.cpp
)

foreach(test_src ${ENGINE_TEST_SOURCES})
  get_filename_component(test_name ${test_src} NAME_WE)
  add_executable(${test_name} ${test_src})
  target_link_libraries(${test_name} PRIVATE plotjuggler_engine GTest::gtest_main)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# Arrow import test (needs Arrow explicitly since engine links it PRIVATE)
add_executable(arrow_import_test tests/arrow_import_test.cpp)
target_link_libraries(arrow_import_test PRIVATE
  plotjuggler_engine Arrow::arrow_static GTest::gtest_main)
add_test(NAME arrow_import_test COMMAND arrow_import_test)

# ---------------------------------------------------------------------------
# Benchmark
# ---------------------------------------------------------------------------

add_executable(read_benchmark benchmarks/read_benchmark.cpp)
target_link_libraries(read_benchmark PRIVATE plotjuggler_engine benchmark::benchmark)

add_executable(ingest_benchmark benchmarks/ingest_benchmark.cpp)
target_link_libraries(ingest_benchmark PRIVATE plotjuggler_engine benchmark::benchmark)

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------

add_executable(parquet_import examples/parquet_import.cpp)
target_link_libraries(parquet_import PRIVATE
  plotjuggler_engine Arrow::arrow_static Parquet::parquet_static
)
