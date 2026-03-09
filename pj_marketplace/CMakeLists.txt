cmake_minimum_required(VERSION 3.22)
project(marketplace LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)
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
