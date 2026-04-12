# ---------------------------------------------------------------------------
# pj_media_core — media decode/playback core, no Qt dependency
# ---------------------------------------------------------------------------

add_library(pj_media_core INTERFACE)
target_include_directories(pj_media_core INTERFACE include)
target_compile_features(pj_media_core INTERFACE cxx_std_20)
target_link_libraries(pj_media_core INTERFACE pj_base)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(PJ_BUILD_TESTS)
  set(PJ_MEDIA_CORE_TESTS
    tests/frame_slot_test.cpp
    tests/cancel_token_test.cpp
  )

  foreach(test_src ${PJ_MEDIA_CORE_TESTS})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE pj_media_core GTest::gtest_main)
    target_compile_options(${test_name} PRIVATE ${PJ_WARNING_FLAGS} ${PJ_SANITIZER_FLAGS})
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()
endif()
