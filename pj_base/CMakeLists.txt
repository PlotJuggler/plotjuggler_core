# ---------------------------------------------------------------------------
# pj_base — vocabulary types, zero external dependencies
# ---------------------------------------------------------------------------

add_library(pj_base STATIC
  src/type_tree.cpp
)
target_include_directories(pj_base PUBLIC include)
target_compile_features(pj_base PUBLIC cxx_std_20)
target_compile_options(pj_base PRIVATE
  ${PJ_WARNING_FLAGS} ${PJ_SANITIZER_FLAGS}
)
if(PJ_ASSERT_THROWS)
  target_compile_definitions(pj_base PUBLIC PJ_ASSERT_THROWS)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

set(PJ_BASE_TESTS
  tests/types_test.cpp
  tests/type_tree_test.cpp
  tests/span_test.cpp
  tests/expected_test.cpp
  tests/plugin_data_api_test.cpp
  tests/data_source_protocol_test.cpp
  tests/data_source_plugin_base_test.cpp
)

foreach(test_src ${PJ_BASE_TESTS})
  get_filename_component(test_name ${test_src} NAME_WE)
  add_executable(${test_name} ${test_src})
  target_link_libraries(${test_name} PRIVATE pj_base GTest::gtest_main)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
