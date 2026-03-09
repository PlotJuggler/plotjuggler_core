cmake_minimum_required(VERSION 3.22)
project(marketplace LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Test)
find_package(GTest REQUIRED)

# ---------------------------------------------------------------------------
# marketplace_core — RegistryManager and core functionality
# ---------------------------------------------------------------------------

add_library(marketplace_core STATIC
  src/core/RegistryManager.cpp
  src/core/RegistryManager.h
)

target_include_directories(marketplace_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(marketplace_core PUBLIC
  Qt6::Core
  Qt6::Network
)

target_compile_options(marketplace_core PRIVATE ${PJ_WARNING_FLAGS})

# ---------------------------------------------------------------------------
# marketplace — main executable
# ---------------------------------------------------------------------------

add_executable(marketplace
  main.cpp
)

target_link_libraries(marketplace PRIVATE
  marketplace_core
  Qt6::Widgets
  Qt6::Network
)

target_compile_options(marketplace PRIVATE ${PJ_WARNING_FLAGS})

# ---------------------------------------------------------------------------
# Unit tests
# ---------------------------------------------------------------------------

add_executable(registry_manager_test
  tests/registry_manager_test.cpp
)

target_link_libraries(registry_manager_test PRIVATE
  marketplace_core
  Qt6::Network
  Qt6::Test
  GTest::gtest
  GTest::gtest_main
)

target_compile_options(registry_manager_test PRIVATE ${PJ_WARNING_FLAGS})

enable_testing()
add_test(NAME registry_manager_test COMMAND registry_manager_test)
