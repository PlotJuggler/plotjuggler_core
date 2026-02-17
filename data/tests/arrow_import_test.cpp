#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <arrow/api.h>
#include <arrow/builder.h>

#include "pj/base/dataset.hpp"
#include "pj/base/type_tree.hpp"
#include "pj/base/types.hpp"
#include "pj/engine/arrow_import.hpp"
#include "pj/engine/engine.hpp"
#include "pj/engine/query.hpp"
#include "pj/engine/reader.hpp"
#include "pj/engine/writer.hpp"

namespace pj::engine::arrow_import {
namespace {

// Helper: create an Arrow RecordBatch with float32 columns
std::shared_ptr<arrow::RecordBatch> make_float32_batch(
    const std::vector<std::string>& col_names,
    const std::vector<std::vector<float>>& col_data,
    int64_t num_rows) {
  std::vector<std::shared_ptr<arrow::Field>> fields;
  std::vector<std::shared_ptr<arrow::Array>> arrays;

  for (std::size_t i = 0; i < col_names.size(); ++i) {
    fields.push_back(arrow::field(col_names[i], arrow::float32()));
    arrow::FloatBuilder builder;
    (void)builder.AppendValues(col_data[i]);
    std::shared_ptr<arrow::Array> arr;
    (void)builder.Finish(&arr);
    arrays.push_back(arr);
  }

  auto schema = arrow::schema(fields);
  return arrow::RecordBatch::Make(schema, num_rows, arrays);
}

// ===========================================================================
// Test: arrow_type_to_primitive
// ===========================================================================

TEST(ArrowImportTest, ArrowTypeToPrimitive) {
  EXPECT_EQ(*arrow_type_to_primitive(arrow::int8()), PrimitiveType::kInt8);
  EXPECT_EQ(*arrow_type_to_primitive(arrow::int16()), PrimitiveType::kInt16);
  EXPECT_EQ(*arrow_type_to_primitive(arrow::int32()), PrimitiveType::kInt32);
  EXPECT_EQ(*arrow_type_to_primitive(arrow::int64()), PrimitiveType::kInt64);
  EXPECT_EQ(*arrow_type_to_primitive(arrow::uint8()), PrimitiveType::kUint8);
  EXPECT_EQ(*arrow_type_to_primitive(arrow::uint16()), PrimitiveType::kUint16);
  EXPECT_EQ(*arrow_type_to_primitive(arrow::uint32()), PrimitiveType::kUint32);
  EXPECT_EQ(*arrow_type_to_primitive(arrow::uint64()), PrimitiveType::kUint64);
  EXPECT_EQ(*arrow_type_to_primitive(arrow::float32()), PrimitiveType::kFloat32);
  EXPECT_EQ(*arrow_type_to_primitive(arrow::float64()), PrimitiveType::kFloat64);
  EXPECT_EQ(*arrow_type_to_primitive(arrow::boolean()), PrimitiveType::kBool);
  EXPECT_EQ(*arrow_type_to_primitive(arrow::utf8()), PrimitiveType::kString);
  EXPECT_EQ(*arrow_type_to_primitive(arrow::large_utf8()), PrimitiveType::kString);

  // Unsupported types
  EXPECT_FALSE(arrow_type_to_primitive(arrow::list(arrow::int32())).has_value());
  EXPECT_FALSE(arrow_type_to_primitive(arrow::binary()).has_value());
}

// ===========================================================================
// Test: schema_from_arrow
// ===========================================================================

TEST(ArrowImportTest, SchemaFromArrow) {
  auto schema = arrow::schema({
      arrow::field("x", arrow::float32()),
      arrow::field("y", arrow::float64()),
      arrow::field("name", arrow::utf8()),
      arrow::field("unsupported", arrow::list(arrow::int32())),  // skipped
  });

  auto result_or = schema_from_arrow(*schema);
  ASSERT_TRUE(result_or.ok()) << result_or.status();

  const auto& [type_tree, mappings] = *result_or;
  ASSERT_EQ(mappings.size(), 3u);

  EXPECT_EQ(mappings[0].field_name, "x");
  EXPECT_EQ(mappings[0].pj_type, PrimitiveType::kFloat32);
  EXPECT_EQ(mappings[0].pj_column_index, 0u);
  EXPECT_EQ(mappings[0].arrow_column_index, 0);

  EXPECT_EQ(mappings[1].field_name, "y");
  EXPECT_EQ(mappings[1].pj_type, PrimitiveType::kFloat64);

  EXPECT_EQ(mappings[2].field_name, "name");
  EXPECT_EQ(mappings[2].pj_type, PrimitiveType::kString);
  // Arrow column 3 (list) is skipped, so name maps to Arrow index 2
  EXPECT_EQ(mappings[2].arrow_column_index, 2);

  EXPECT_EQ(type_tree->name, "arrow_row");
  EXPECT_EQ(type_tree->children.size(), 3u);
}

// ===========================================================================
// Test: import_record_batch float32 columns
// ===========================================================================

TEST(ArrowImportTest, ImportRecordBatchFloat32) {
  DataEngine engine;
  auto ds_or = engine.create_dataset(
      DatasetDescriptor{.source_name = "test", .time_domain_id = 0});
  ASSERT_TRUE(ds_or.ok());

  DataWriter writer = engine.create_writer();

  // Create Arrow batch
  constexpr int64_t N = 100;
  std::vector<float> x_vals(N), y_vals(N);
  for (int64_t i = 0; i < N; ++i) {
    x_vals[static_cast<std::size_t>(i)] = static_cast<float>(i) * 0.1F;
    y_vals[static_cast<std::size_t>(i)] = static_cast<float>(i) * 0.2F;
  }

  auto batch = make_float32_batch({"x", "y"}, {x_vals, y_vals}, N);

  // Convert schema and register
  auto [type_tree, mappings] = *schema_from_arrow(*batch->schema());
  auto schema_id = *writer.register_schema("test_schema", type_tree);

  TopicDescriptor desc;
  desc.name = "test_topic";
  desc.schema_id = schema_id;
  auto topic_id = *writer.register_topic(*ds_or, desc);

  // Import (using sequential timestamps)
  auto status = import_record_batch(writer, topic_id, *batch, mappings);
  ASSERT_TRUE(status.ok()) << status;

  auto flushed = writer.flush_all();
  engine.commit_chunks(std::move(flushed));

  // Verify round-trip
  DataReader reader = engine.create_reader();
  std::size_t count = 0;
  auto cursor_or = reader.range_query(
      QueryRange{.topic_id = topic_id, .t_min = 0, .t_max = N - 1});
  ASSERT_TRUE(cursor_or.ok()) << cursor_or.status();
  cursor_or->for_each([&](const SampleRow& row) {
    auto x = static_cast<float>(row.chunk->read_numeric_as_double(0, row.row_index));
    EXPECT_FLOAT_EQ(x, x_vals[count]);
    ++count;
  });
  EXPECT_EQ(count, static_cast<std::size_t>(N));
}

// ===========================================================================
// Test: import_record_batch with timestamp column
// ===========================================================================

TEST(ArrowImportTest, ImportWithTimestampColumn) {
  DataEngine engine;
  auto ds_or = engine.create_dataset(
      DatasetDescriptor{.source_name = "test", .time_domain_id = 0});
  ASSERT_TRUE(ds_or.ok());

  DataWriter writer = engine.create_writer();

  // Build batch: int64 timestamps + float64 values
  constexpr int64_t N = 50;
  arrow::Int64Builder ts_builder;
  arrow::DoubleBuilder val_builder;
  for (int64_t i = 0; i < N; ++i) {
    (void)ts_builder.Append(i * 1000);
    (void)val_builder.Append(static_cast<double>(i) * 0.5);
  }

  std::shared_ptr<arrow::Array> ts_arr, val_arr;
  (void)ts_builder.Finish(&ts_arr);
  (void)val_builder.Finish(&val_arr);

  auto schema = arrow::schema({
      arrow::field("timestamp", arrow::int64()),
      arrow::field("value", arrow::float64()),
  });
  auto batch = arrow::RecordBatch::Make(schema, N, {ts_arr, val_arr});

  // Only map "value" column (not timestamp)
  std::vector<ArrowColumnMapping> mappings = {{
      .arrow_column_index = 1,
      .pj_column_index = 0,
      .pj_type = PrimitiveType::kFloat64,
      .field_name = "value",
  }};

  auto val_tree = make_primitive("value", PrimitiveType::kFloat64);
  auto sid = *writer.register_schema("ts_schema", val_tree);
  TopicDescriptor desc;
  desc.name = "ts_topic";
  desc.schema_id = sid;
  auto tid = *writer.register_topic(*ds_or, desc);

  // Import with timestamp_column=0
  auto status = import_record_batch(writer, tid, *batch, mappings, 0);
  ASSERT_TRUE(status.ok()) << status;

  auto flushed = writer.flush_all();
  engine.commit_chunks(std::move(flushed));

  // Verify timestamps
  DataReader reader = engine.create_reader();
  auto latest_or = reader.latest_at(
      QueryPoint{.topic_id = tid, .t = 25000});
  ASSERT_TRUE(latest_or.ok()) << latest_or.status();
  ASSERT_TRUE(latest_or->found);
  EXPECT_EQ(latest_or->timestamp, 25000);
  EXPECT_DOUBLE_EQ(
      latest_or->chunk->read_numeric_as_double(0, latest_or->row_index),
      25.0 * 0.5);
}

// ===========================================================================
// Test: import_record_batch with string columns
// ===========================================================================

TEST(ArrowImportTest, ImportStrings) {
  DataEngine engine;
  auto ds_or = engine.create_dataset(
      DatasetDescriptor{.source_name = "test", .time_domain_id = 0});
  ASSERT_TRUE(ds_or.ok());

  DataWriter writer = engine.create_writer();

  // Build batch with string column
  arrow::StringBuilder str_builder;
  (void)str_builder.Append("alpha");
  (void)str_builder.Append("bravo");
  (void)str_builder.Append("charlie");

  std::shared_ptr<arrow::Array> str_arr;
  (void)str_builder.Finish(&str_arr);

  auto schema = arrow::schema({arrow::field("name", arrow::utf8())});
  auto batch = arrow::RecordBatch::Make(schema, 3, {str_arr});

  auto [type_tree, mappings] = *schema_from_arrow(*batch->schema());
  auto sid = *writer.register_schema("str_schema", type_tree);
  TopicDescriptor desc;
  desc.name = "str_topic";
  desc.schema_id = sid;
  auto tid = *writer.register_topic(*ds_or, desc);

  auto status = import_record_batch(writer, tid, *batch, mappings);
  ASSERT_TRUE(status.ok()) << status;

  auto flushed = writer.flush_all();
  engine.commit_chunks(std::move(flushed));

  // Verify strings
  DataReader reader = engine.create_reader();
  std::vector<std::string> read_strings;
  auto cursor_or = reader.range_query(
      QueryRange{.topic_id = tid, .t_min = 0, .t_max = 10});
  ASSERT_TRUE(cursor_or.ok());
  cursor_or->for_each([&](const SampleRow& row) {
    read_strings.emplace_back(row.chunk->read_string(0, row.row_index));
  });
  ASSERT_EQ(read_strings.size(), 3u);
  EXPECT_EQ(read_strings[0], "alpha");
  EXPECT_EQ(read_strings[1], "bravo");
  EXPECT_EQ(read_strings[2], "charlie");
}

// ===========================================================================
// Test: import with narrow integer widening (int8 -> int64)
// ===========================================================================

TEST(ArrowImportTest, ImportNarrowIntegerWidening) {
  DataEngine engine;
  auto ds_or = engine.create_dataset(
      DatasetDescriptor{.source_name = "test", .time_domain_id = 0});
  ASSERT_TRUE(ds_or.ok());

  DataWriter writer = engine.create_writer();

  // Build batch with int8 column
  arrow::Int8Builder i8_builder;
  (void)i8_builder.Append(10);
  (void)i8_builder.Append(-20);
  (void)i8_builder.Append(127);

  std::shared_ptr<arrow::Array> i8_arr;
  (void)i8_builder.Finish(&i8_arr);

  auto schema = arrow::schema({arrow::field("val", arrow::int8())});
  auto batch = arrow::RecordBatch::Make(schema, 3, {i8_arr});

  auto [type_tree, mappings] = *schema_from_arrow(*batch->schema());
  auto sid = *writer.register_schema("i8_schema", type_tree);
  TopicDescriptor desc;
  desc.name = "i8_topic";
  desc.schema_id = sid;
  auto tid = *writer.register_topic(*ds_or, desc);

  auto status = import_record_batch(writer, tid, *batch, mappings);
  ASSERT_TRUE(status.ok()) << status;

  auto flushed = writer.flush_all();
  engine.commit_chunks(std::move(flushed));

  DataReader reader = engine.create_reader();
  std::vector<double> values;
  auto cursor_or = reader.range_query(
      QueryRange{.topic_id = tid, .t_min = 0, .t_max = 10});
  ASSERT_TRUE(cursor_or.ok());
  cursor_or->for_each([&](const SampleRow& row) {
    values.push_back(row.chunk->read_numeric_as_double(0, row.row_index));
  });
  ASSERT_EQ(values.size(), 3u);
  EXPECT_DOUBLE_EQ(values[0], 10.0);
  EXPECT_DOUBLE_EQ(values[1], -20.0);
  EXPECT_DOUBLE_EQ(values[2], 127.0);
}

// ===========================================================================
// Test: import_table
// ===========================================================================

TEST(ArrowImportTest, ImportTable) {
  DataEngine engine;
  auto ds_or = engine.create_dataset(
      DatasetDescriptor{.source_name = "test", .time_domain_id = 0});
  ASSERT_TRUE(ds_or.ok());

  DataWriter writer = engine.create_writer();

  // Build an Arrow Table with float64 column
  constexpr int64_t N = 500;
  arrow::DoubleBuilder val_builder;
  for (int64_t i = 0; i < N; ++i) {
    (void)val_builder.Append(static_cast<double>(i));
  }
  std::shared_ptr<arrow::Array> val_arr;
  (void)val_builder.Finish(&val_arr);

  auto schema = arrow::schema({arrow::field("value", arrow::float64())});
  auto table = arrow::Table::Make(schema, {val_arr});

  auto [type_tree, mappings] = *schema_from_arrow(*schema);
  auto sid = *writer.register_schema("tbl_schema", type_tree);
  TopicDescriptor desc;
  desc.name = "tbl_topic";
  desc.schema_id = sid;
  desc.max_chunk_rows = 128;
  auto tid = *writer.register_topic(*ds_or, desc);

  auto status = import_table(writer, tid, *table, mappings);
  ASSERT_TRUE(status.ok()) << status;

  auto flushed = writer.flush_all();
  engine.commit_chunks(std::move(flushed));

  DataReader reader = engine.create_reader();
  std::size_t count = 0;
  auto cursor_or = reader.range_query(
      QueryRange{.topic_id = tid, .t_min = 0, .t_max = N - 1});
  ASSERT_TRUE(cursor_or.ok());
  cursor_or->for_each([&](const SampleRow&) { ++count; });
  EXPECT_EQ(count, static_cast<std::size_t>(N));
}

// ===========================================================================
// Test: import with nulls
// ===========================================================================

TEST(ArrowImportTest, ImportWithNulls) {
  DataEngine engine;
  auto ds_or = engine.create_dataset(
      DatasetDescriptor{.source_name = "test", .time_domain_id = 0});
  ASSERT_TRUE(ds_or.ok());

  DataWriter writer = engine.create_writer();

  // Build batch with nulls
  arrow::FloatBuilder builder;
  (void)builder.Append(1.0F);
  (void)builder.AppendNull();
  (void)builder.Append(3.0F);
  (void)builder.AppendNull();

  std::shared_ptr<arrow::Array> arr;
  (void)builder.Finish(&arr);

  auto schema = arrow::schema({arrow::field("val", arrow::float32())});
  auto batch = arrow::RecordBatch::Make(schema, 4, {arr});

  auto [type_tree, mappings] = *schema_from_arrow(*batch->schema());
  auto sid = *writer.register_schema("null_schema", type_tree);
  TopicDescriptor desc;
  desc.name = "null_topic";
  desc.schema_id = sid;
  auto tid = *writer.register_topic(*ds_or, desc);

  auto status = import_record_batch(writer, tid, *batch, mappings);
  ASSERT_TRUE(status.ok()) << status;

  auto flushed = writer.flush_all();
  engine.commit_chunks(std::move(flushed));

  DataReader reader = engine.create_reader();
  auto cursor_or = reader.range_query(
      QueryRange{.topic_id = tid, .t_min = 0, .t_max = 10});
  ASSERT_TRUE(cursor_or.ok());
  std::size_t row = 0;
  cursor_or->for_each([&](const SampleRow& r) {
    if (row == 0) {
      EXPECT_FALSE(r.chunk->is_null(0, r.row_index));
      EXPECT_FLOAT_EQ(
          static_cast<float>(r.chunk->read_numeric_as_double(0, r.row_index)),
          1.0F);
    } else if (row == 1) {
      EXPECT_TRUE(r.chunk->is_null(0, r.row_index));
    } else if (row == 2) {
      EXPECT_FALSE(r.chunk->is_null(0, r.row_index));
      EXPECT_FLOAT_EQ(
          static_cast<float>(r.chunk->read_numeric_as_double(0, r.row_index)),
          3.0F);
    } else if (row == 3) {
      EXPECT_TRUE(r.chunk->is_null(0, r.row_index));
    }
    ++row;
  });
  EXPECT_EQ(row, 4u);
}

}  // namespace
}  // namespace pj::engine::arrow_import
