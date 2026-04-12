# ---------------------------------------------------------------------------
# pj_media_core — media decode/playback core, no Qt dependency
# ---------------------------------------------------------------------------

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(TURBOJPEG IMPORTED_TARGET libturbojpeg)
endif()

add_library(pj_media_core STATIC
  src/image_decoder.cpp
)
target_include_directories(pj_media_core PUBLIC include)
target_compile_features(pj_media_core PUBLIC cxx_std_20)
target_compile_options(pj_media_core PRIVATE
  ${PJ_WARNING_FLAGS} ${PJ_SANITIZER_FLAGS}
)
target_link_libraries(pj_media_core
  PUBLIC pj_base
  PRIVATE PkgConfig::TURBOJPEG
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(PJ_BUILD_TESTS)
  set(PJ_MEDIA_CORE_TESTS
    tests/frame_slot_test.cpp
    tests/cancel_token_test.cpp
    tests/image_decoder_test.cpp
  )

  foreach(test_src ${PJ_MEDIA_CORE_TESTS})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE pj_media_core PkgConfig::TURBOJPEG GTest::gtest_main)
    target_compile_options(${test_name} PRIVATE ${PJ_WARNING_FLAGS} ${PJ_SANITIZER_FLAGS})
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()
endif()
