# ---------------------------------------------------------------------------
# pj_marker_protocol — schema + canonical wire-format codec for image
# annotations (markers), shared between loaders (producers) and pj_media
# (consumer). Depends on pj_base only so plugins can link it without dragging
# pj_media_core into their build.
#
# Wire format: foxglove.ImageAnnotations Protobuf (hand-rolled writer; the
# matching reader still lives in pj_media_core for now). See
# `pj_media/docs/datatypes_2D.md §8` for the spec we conform to.
# ---------------------------------------------------------------------------

add_library(pj_marker_protocol STATIC
  src/image_annotation_codec.cpp
)
target_include_directories(pj_marker_protocol PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(pj_marker_protocol PUBLIC cxx_std_20)
target_compile_options(pj_marker_protocol PRIVATE
  ${PJ_WARNING_FLAGS} ${PJ_SANITIZER_FLAGS}
)
target_link_libraries(pj_marker_protocol PUBLIC pj_base)
set_target_properties(pj_marker_protocol PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

# ---------------------------------------------------------------------------
# Install (guarded by PJ_INSTALL_SDK in root CMakeLists.txt)
# ---------------------------------------------------------------------------

if(PJ_INSTALL_SDK)
  install(TARGETS pj_marker_protocol EXPORT PlotJugglerSDKTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
  )
  install(DIRECTORY include/ DESTINATION include)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(PJ_BUILD_TESTS)
  set(PJ_MARKER_PROTOCOL_TESTS
    tests/image_annotation_codec_test.cpp
  )

  foreach(test_src ${PJ_MARKER_PROTOCOL_TESTS})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    # Round-trip tests link pj_media_core to use the existing reader. Production
    # code in pj_marker_protocol does NOT depend on pj_media_core — only the
    # test executable does, which is the standard pattern.
    target_link_libraries(${test_name} PRIVATE
      pj_marker_protocol pj_media_core GTest::gtest_main
    )
    target_compile_options(${test_name} PRIVATE
      ${PJ_WARNING_FLAGS} ${PJ_SANITIZER_FLAGS}
    )
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()
endif()
