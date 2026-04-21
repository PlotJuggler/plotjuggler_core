#include "pj_plugins/host/message_parser_library.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "pj_base/plugin_data_api.h"
#include "pj_base/sdk/service_traits.hpp"
#include "pj_plugins/host/service_registry_builder.hpp"

#ifndef PJ_MOCK_JSON_PARSER_PLUGIN_PATH
#error "PJ_MOCK_JSON_PARSER_PLUGIN_PATH must be defined"
#endif

namespace {

struct ParserWriteRecorder {
  int append_record_calls = 0;
  int64_t last_timestamp = 0;
  double last_value = 0.0;
};

bool pwhEnsureField(void*, PJ_string_view_t, PJ_primitive_type_t, PJ_field_handle_t* out_field, PJ_error_t*) {
  *out_field = PJ_field_handle_t{PJ_topic_handle_t{1}, 1};
  return true;
}
bool pwhAppendRecord(
    void* ctx, int64_t timestamp, const PJ_named_field_value_t* fields, size_t field_count, PJ_error_t*) {
  auto* self = static_cast<ParserWriteRecorder*>(ctx);
  ++self->append_record_calls;
  self->last_timestamp = timestamp;
  if (field_count > 0 && fields[0].value.type == PJ_PRIMITIVE_TYPE_FLOAT64) {
    self->last_value = fields[0].value.data.as_float64;
  }
  return true;
}
bool pwhAppendBoundRecord(void*, int64_t, const PJ_bound_field_value_t*, size_t, PJ_error_t*) {
  return true;
}
bool pwhAppendArrowIpc(void*, PJ_bytes_view_t, PJ_string_view_t, PJ_error_t*) {
  return true;
}

PJ_parser_write_host_t makeParserWriteHost(ParserWriteRecorder* recorder) {
  static const PJ_parser_write_host_vtable_t vtable = {
      .abi_version = PJ_PLUGIN_DATA_API_VERSION,
      .struct_size = sizeof(PJ_parser_write_host_vtable_t),
      .ensure_field = pwhEnsureField,
      .append_record = pwhAppendRecord,
      .append_bound_record = pwhAppendBoundRecord,
      .append_arrow_ipc = pwhAppendArrowIpc,
  };
  return PJ_parser_write_host_t{.ctx = recorder, .vtable = &vtable};
}

TEST(MessageParserLibraryTest, LoadMockPlugin) {
  auto library = PJ::MessageParserLibrary::load(PJ_MOCK_JSON_PARSER_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  EXPECT_TRUE(library->valid());
  EXPECT_EQ(library->vtable()->protocol_version, PJ_MESSAGE_PARSER_PROTOCOL_VERSION);
}

TEST(MessageParserLibraryTest, ManifestRoundTrip) {
  auto library = PJ::MessageParserLibrary::load(PJ_MOCK_JSON_PARSER_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();

  auto handle = library->createHandle();
  EXPECT_TRUE(handle.valid());
  EXPECT_NE(handle.manifest().find("Mock JSON Parser"), std::string::npos);
  EXPECT_NE(handle.manifest().find("\"encoding\":\"json\""), std::string::npos);
}

TEST(MessageParserLibraryTest, BindAndParse) {
  auto library = PJ::MessageParserLibrary::load(PJ_MOCK_JSON_PARSER_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();

  auto handle = library->createHandle();
  ParserWriteRecorder recorder;

  PJ::ServiceRegistryBuilder reg;
  reg.registerService<PJ::sdk::ParserWriteHostService>(makeParserWriteHost(&recorder));

  ASSERT_TRUE(handle.bind(reg.view()));

  const uint8_t payload[] = {'3', '.', '1', '4'};
  ASSERT_TRUE(handle.parse(999, payload));

  EXPECT_EQ(recorder.append_record_calls, 1);
  EXPECT_EQ(recorder.last_timestamp, 999);
  EXPECT_DOUBLE_EQ(recorder.last_value, 3.14);
}

TEST(MessageParserLibraryTest, SaveLoadConfig) {
  auto library = PJ::MessageParserLibrary::load(PJ_MOCK_JSON_PARSER_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();

  auto handle = library->createHandle();
  std::string cfg;
  ASSERT_TRUE(handle.saveConfig(cfg));
  EXPECT_EQ(cfg, "{}");
  ASSERT_TRUE(handle.loadConfig(R"({"format":"compact"})"));
}

TEST(MessageParserLibraryTest, LoadNonexistentFails) {
  auto result = PJ::MessageParserLibrary::load("/nonexistent_path/fake_plugin.so");
  EXPECT_FALSE(result);
  EXPECT_FALSE(result.error().empty());
}

}  // namespace
