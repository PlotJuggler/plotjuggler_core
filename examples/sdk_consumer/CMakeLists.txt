cmake_minimum_required(VERSION 3.22)
project(sdk_consumer LANGUAGES CXX)

find_package(PlotJugglerSDK REQUIRED COMPONENTS DataSource)

add_library(minimal_data_source SHARED minimal_data_source.cpp)
target_link_libraries(minimal_data_source PRIVATE PlotJugglerSDK::DataSource)
