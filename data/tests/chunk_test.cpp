#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include "pj/engine/chunk.hpp"

namespace pj::engine {
namespace {

// ---------------------------------------------------------------------------
// Helper: create a vector of ColumnDescriptors
// ---------------------------------------------------------------------------

ColumnDescriptor make_col(FieldId id, PrimitiveType type,
                          std::string path) {
  return ColumnDescriptor{id, type, std::move(path)};
}

// ===========================================================================
// Test 1: Build and seal float32 chunk
// ===========================================================================

TEST(ChunkTest, BuildAndSealFloat32Chunk) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat32, "x"),
      make_col(2, PrimitiveType::kFloat32, "y"),
      make_col(3, PrimitiveType::kFloat32, "z"),
  };
  TopicChunkBuilder builder(/*topic_id=*/10, /*schema_id=*/1,
                            std::move(cols), /*max_rows=*/100);

  // Add 5 rows
  for (uint32_t i = 0; i < 5; ++i) {
    Timestamp ts = 1000 + static_cast<Timestamp>(i) * 100;
    builder.begin_row(ts);
    builder.set_float32(0, static_cast<float>(i) * 1.0F);
    builder.set_float32(1, static_cast<float>(i) * 2.0F);
    builder.set_float32(2, static_cast<float>(i) * 3.0F);
    builder.finish_row();
  }

  EXPECT_EQ(builder.row_count(), 5U);
  EXPECT_FALSE(builder.is_full());

  const auto& stats = builder.stats();
  EXPECT_EQ(stats.t_min, 1000);
  EXPECT_EQ(stats.t_max, 1400);
  EXPECT_EQ(stats.row_count, 5U);

  // Column 0 (x): values 0, 1, 2, 3, 4
  EXPECT_DOUBLE_EQ(*stats.column_stats[0].min_value, 0.0);
  EXPECT_DOUBLE_EQ(*stats.column_stats[0].max_value, 4.0);

  // Column 1 (y): values 0, 2, 4, 6, 8
  EXPECT_DOUBLE_EQ(*stats.column_stats[1].min_value, 0.0);
  EXPECT_DOUBLE_EQ(*stats.column_stats[1].max_value, 8.0);

  // Column 2 (z): values 0, 3, 6, 9, 12
  EXPECT_DOUBLE_EQ(*stats.column_stats[2].min_value, 0.0);
  EXPECT_DOUBLE_EQ(*stats.column_stats[2].max_value, 12.0);

  TopicChunk chunk = builder.seal();
  EXPECT_NE(chunk.id, 0U);
  EXPECT_EQ(chunk.topic_id, 10U);
  EXPECT_EQ(chunk.schema_version, 1U);
  EXPECT_EQ(chunk.stats.row_count, 5U);
  EXPECT_EQ(chunk.column_encodings.size(), 3U);
  for (std::size_t c = 0; c < 3; ++c) {
    EXPECT_EQ(chunk.column_encodings[c], EncodingType::kRaw);
  }
}

// ===========================================================================
// Test 2: Read back sealed values
// ===========================================================================

TEST(ChunkTest, ReadBackSealedValues) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat32, "x"),
      make_col(2, PrimitiveType::kFloat64, "y"),
      make_col(3, PrimitiveType::kInt32, "z"),
  };
  TopicChunkBuilder builder(/*topic_id=*/20, /*schema_id=*/2,
                            std::move(cols), /*max_rows=*/100);

  Timestamp timestamps[] = {1000, 1100, 1200, 1300, 1400};
  float x_vals[] = {1.5F, 2.5F, 3.5F, 4.5F, 5.5F};
  double y_vals[] = {10.0, 20.0, 30.0, 40.0, 50.0};
  int32_t z_vals[] = {-1, 0, 1, 2, 3};

  for (int i = 0; i < 5; ++i) {
    builder.begin_row(timestamps[i]);
    builder.set_float32(0, x_vals[i]);
    builder.set_float64(1, y_vals[i]);
    builder.set_int32(2, z_vals[i]);
    builder.finish_row();
  }

  TopicChunk chunk = builder.seal();

  // Read back timestamps
  for (std::size_t i = 0; i < 5; ++i) {
    EXPECT_EQ(chunk.read_timestamp(i), timestamps[i]) << "row " << i;
  }

  // Read back float32 column as double
  for (std::size_t i = 0; i < 5; ++i) {
    EXPECT_FLOAT_EQ(static_cast<float>(chunk.read_numeric_as_double(0, i)),
                    x_vals[i])
        << "row " << i;
  }

  // Read back float64 column
  for (std::size_t i = 0; i < 5; ++i) {
    EXPECT_DOUBLE_EQ(chunk.read_numeric_as_double(1, i), y_vals[i])
        << "row " << i;
  }

  // Read back int32 column as double
  for (std::size_t i = 0; i < 5; ++i) {
    EXPECT_DOUBLE_EQ(chunk.read_numeric_as_double(2, i),
                     static_cast<double>(z_vals[i]))
        << "row " << i;
  }
}

// ===========================================================================
// Test 3: is_full
// ===========================================================================

TEST(ChunkTest, IsFull) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat32, "val"),
  };
  TopicChunkBuilder builder(/*topic_id=*/30, /*schema_id=*/1,
                            std::move(cols), /*max_rows=*/3);

  EXPECT_FALSE(builder.is_full());
  EXPECT_EQ(builder.row_count(), 0U);

  for (uint32_t i = 0; i < 3; ++i) {
    builder.begin_row(static_cast<Timestamp>(i));
    builder.set_float32(0, static_cast<float>(i));
    builder.finish_row();
  }

  EXPECT_TRUE(builder.is_full());
  EXPECT_EQ(builder.row_count(), 3U);
}

// ===========================================================================
// Test 4: String column
// ===========================================================================

TEST(ChunkTest, StringColumn) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kString, "label"),
  };
  TopicChunkBuilder builder(/*topic_id=*/40, /*schema_id=*/1,
                            std::move(cols), /*max_rows=*/100);

  std::string_view strings[] = {"hello", "world", "hello", "world"};
  for (int i = 0; i < 4; ++i) {
    builder.begin_row(static_cast<Timestamp>(i * 100));
    builder.set_string(0, strings[i]);
    builder.finish_row();
  }

  TopicChunk chunk = builder.seal();

  EXPECT_EQ(chunk.column_encodings[0], EncodingType::kDictionary);
  ASSERT_TRUE(chunk.dictionary_data[0].has_value());
  // 2 unique strings: "hello" and "world"
  EXPECT_EQ(chunk.dictionary_data[0]->dictionary.size(), 2U);

  // Read back all strings
  for (std::size_t i = 0; i < 4; ++i) {
    EXPECT_EQ(chunk.read_string(0, i), strings[i]) << "row " << i;
  }
}

// ===========================================================================
// Test 5: Bool column
// ===========================================================================

TEST(ChunkTest, BoolColumn) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kBool, "flag"),
  };
  TopicChunkBuilder builder(/*topic_id=*/50, /*schema_id=*/1,
                            std::move(cols), /*max_rows=*/100);

  bool bools[] = {true, false, true, true, false};
  for (int i = 0; i < 5; ++i) {
    builder.begin_row(static_cast<Timestamp>(i));
    builder.set_bool(0, bools[i]);
    builder.finish_row();
  }

  TopicChunk chunk = builder.seal();

  EXPECT_EQ(chunk.column_encodings[0], EncodingType::kPackedBool);
  ASSERT_TRUE(chunk.packed_bool_data[0].has_value());

  for (std::size_t i = 0; i < 5; ++i) {
    EXPECT_EQ(chunk.read_bool(0, i), bools[i]) << "row " << i;
  }
}

// ===========================================================================
// Test 6: Null handling
// ===========================================================================

TEST(ChunkTest, NullHandling) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat64, "val"),
  };
  TopicChunkBuilder builder(/*topic_id=*/60, /*schema_id=*/1,
                            std::move(cols), /*max_rows=*/100);

  // Row 0: 10.0, Row 1: null, Row 2: 30.0, Row 3: null, Row 4: 50.0
  builder.begin_row(100);
  builder.set_float64(0, 10.0);
  builder.finish_row();

  builder.begin_row(200);
  builder.set_null(0);
  builder.finish_row();

  builder.begin_row(300);
  builder.set_float64(0, 30.0);
  builder.finish_row();

  builder.begin_row(400);
  builder.set_null(0);
  builder.finish_row();

  builder.begin_row(500);
  builder.set_float64(0, 50.0);
  builder.finish_row();

  const auto& stats = builder.stats();
  EXPECT_EQ(stats.column_stats[0].null_count, 2U);

  TopicChunk chunk = builder.seal();

  EXPECT_FALSE(chunk.is_null(0, 0));
  EXPECT_TRUE(chunk.is_null(0, 1));
  EXPECT_FALSE(chunk.is_null(0, 2));
  EXPECT_TRUE(chunk.is_null(0, 3));
  EXPECT_FALSE(chunk.is_null(0, 4));

  // Non-null values should read back correctly
  EXPECT_DOUBLE_EQ(chunk.read_numeric_as_double(0, 0), 10.0);
  EXPECT_DOUBLE_EQ(chunk.read_numeric_as_double(0, 2), 30.0);
  EXPECT_DOUBLE_EQ(chunk.read_numeric_as_double(0, 4), 50.0);
}

// ===========================================================================
// Test 7: Mixed types
// ===========================================================================

TEST(ChunkTest, MixedTypes) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat32, "position"),
      make_col(2, PrimitiveType::kString, "label"),
      make_col(3, PrimitiveType::kBool, "active"),
  };
  TopicChunkBuilder builder(/*topic_id=*/70, /*schema_id=*/1,
                            std::move(cols), /*max_rows=*/100);

  builder.begin_row(1000);
  builder.set_float32(0, 1.5F);
  builder.set_string(1, "alpha");
  builder.set_bool(2, true);
  builder.finish_row();

  builder.begin_row(2000);
  builder.set_float32(0, 2.5F);
  builder.set_string(1, "beta");
  builder.set_bool(2, false);
  builder.finish_row();

  builder.begin_row(3000);
  builder.set_float32(0, 3.5F);
  builder.set_string(1, "alpha");
  builder.set_bool(2, true);
  builder.finish_row();

  TopicChunk chunk = builder.seal();

  // Check encodings
  EXPECT_EQ(chunk.column_encodings[0], EncodingType::kRaw);
  EXPECT_EQ(chunk.column_encodings[1], EncodingType::kDictionary);
  EXPECT_EQ(chunk.column_encodings[2], EncodingType::kPackedBool);

  // Read back all values
  EXPECT_FLOAT_EQ(static_cast<float>(chunk.read_numeric_as_double(0, 0)),
                  1.5F);
  EXPECT_FLOAT_EQ(static_cast<float>(chunk.read_numeric_as_double(0, 1)),
                  2.5F);
  EXPECT_FLOAT_EQ(static_cast<float>(chunk.read_numeric_as_double(0, 2)),
                  3.5F);

  EXPECT_EQ(chunk.read_string(1, 0), "alpha");
  EXPECT_EQ(chunk.read_string(1, 1), "beta");
  EXPECT_EQ(chunk.read_string(1, 2), "alpha");

  EXPECT_TRUE(chunk.read_bool(2, 0));
  EXPECT_FALSE(chunk.read_bool(2, 1));
  EXPECT_TRUE(chunk.read_bool(2, 2));

  // Timestamps
  EXPECT_EQ(chunk.read_timestamp(0), 1000);
  EXPECT_EQ(chunk.read_timestamp(1), 2000);
  EXPECT_EQ(chunk.read_timestamp(2), 3000);
}

// ===========================================================================
// Test 8: Column stats (min/max, is_constant, run_count)
// ===========================================================================

TEST(ChunkTest, ColumnStatsNumeric) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat64, "varying"),
      make_col(2, PrimitiveType::kFloat64, "constant"),
  };
  TopicChunkBuilder builder(/*topic_id=*/80, /*schema_id=*/1,
                            std::move(cols), /*max_rows=*/100);

  // varying: -5, 0, 10, 3, 10
  // constant: 42, 42, 42, 42, 42
  double varying[] = {-5.0, 0.0, 10.0, 3.0, 10.0};
  for (int i = 0; i < 5; ++i) {
    builder.begin_row(static_cast<Timestamp>(i));
    builder.set_float64(0, varying[i]);
    builder.set_float64(1, 42.0);
    builder.finish_row();
  }

  const auto& stats = builder.stats();

  // Varying column
  EXPECT_DOUBLE_EQ(*stats.column_stats[0].min_value, -5.0);
  EXPECT_DOUBLE_EQ(*stats.column_stats[0].max_value, 10.0);
  EXPECT_FALSE(stats.column_stats[0].is_constant);
  // run_count: -5->0 (change), 0->10 (change), 10->3 (change), 3->10 (change) = 1 + 4 = 5
  EXPECT_EQ(stats.column_stats[0].run_count, 5U);

  // Constant column
  EXPECT_DOUBLE_EQ(*stats.column_stats[1].min_value, 42.0);
  EXPECT_DOUBLE_EQ(*stats.column_stats[1].max_value, 42.0);
  EXPECT_TRUE(stats.column_stats[1].is_constant);
  EXPECT_EQ(stats.column_stats[1].run_count, 1U);
}

// ===========================================================================
// Test: Unique chunk IDs
// ===========================================================================

TEST(ChunkTest, UniqueChunkIds) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat32, "val"),
  };

  TopicChunkBuilder builder1(1, 1, cols, 10);
  builder1.begin_row(100);
  builder1.set_float32(0, 1.0F);
  builder1.finish_row();
  TopicChunk c1 = builder1.seal();

  TopicChunkBuilder builder2(1, 1, cols, 10);
  builder2.begin_row(200);
  builder2.set_float32(0, 2.0F);
  builder2.finish_row();
  TopicChunk c2 = builder2.seal();

  EXPECT_NE(c1.id, c2.id);
  EXPECT_NE(c1.id, kInvalidChunkId);
  EXPECT_NE(c2.id, kInvalidChunkId);
}

// ===========================================================================
// Test: Integer types round-trip
// ===========================================================================

TEST(ChunkTest, IntegerTypesRoundTrip) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kInt8, "i8"),
      make_col(2, PrimitiveType::kInt16, "i16"),
      make_col(3, PrimitiveType::kInt64, "i64"),
      make_col(4, PrimitiveType::kUint8, "u8"),
      make_col(5, PrimitiveType::kUint16, "u16"),
      make_col(6, PrimitiveType::kUint32, "u32"),
      make_col(7, PrimitiveType::kUint64, "u64"),
  };
  TopicChunkBuilder builder(/*topic_id=*/90, /*schema_id=*/1,
                            std::move(cols), /*max_rows=*/100);

  builder.begin_row(1000);
  builder.set_int8(0, -42);
  builder.set_int16(1, -1000);
  builder.set_int64(2, 123456789012345LL);
  builder.set_uint8(3, 255);
  builder.set_uint16(4, 65535);
  builder.set_uint32(5, 4000000000U);
  builder.set_uint64(6, 18000000000000000000ULL);
  builder.finish_row();

  TopicChunk chunk = builder.seal();

  EXPECT_DOUBLE_EQ(chunk.read_numeric_as_double(0, 0), -42.0);
  EXPECT_DOUBLE_EQ(chunk.read_numeric_as_double(1, 0), -1000.0);
  EXPECT_DOUBLE_EQ(chunk.read_numeric_as_double(2, 0), 123456789012345.0);
  EXPECT_DOUBLE_EQ(chunk.read_numeric_as_double(3, 0), 255.0);
  EXPECT_DOUBLE_EQ(chunk.read_numeric_as_double(4, 0), 65535.0);
  EXPECT_DOUBLE_EQ(chunk.read_numeric_as_double(5, 0), 4000000000.0);
  // uint64 large values may lose precision in double, so just check close
  EXPECT_NEAR(chunk.read_numeric_as_double(6, 0), 1.8e19, 1e4);
}

// ===========================================================================
// Test: No nulls means is_null always returns false
// ===========================================================================

TEST(ChunkTest, NoNullsIsNullReturnsFalse) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat32, "val"),
  };
  TopicChunkBuilder builder(/*topic_id=*/100, /*schema_id=*/1,
                            std::move(cols), /*max_rows=*/100);

  for (int i = 0; i < 3; ++i) {
    builder.begin_row(static_cast<Timestamp>(i));
    builder.set_float32(0, static_cast<float>(i));
    builder.finish_row();
  }

  TopicChunk chunk = builder.seal();

  for (std::size_t i = 0; i < 3; ++i) {
    EXPECT_FALSE(chunk.is_null(0, i)) << "row " << i;
  }
}

// ===========================================================================
// Test: String column is_constant and run_count
// ===========================================================================

TEST(ChunkTest, StringColumnStats) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kString, "tag"),
  };
  TopicChunkBuilder builder(/*topic_id=*/110, /*schema_id=*/1,
                            std::move(cols), /*max_rows=*/100);

  // All same string -> is_constant = true, run_count = 1
  for (int i = 0; i < 4; ++i) {
    builder.begin_row(static_cast<Timestamp>(i));
    builder.set_string(0, "same");
    builder.finish_row();
  }

  const auto& stats = builder.stats();
  EXPECT_TRUE(stats.column_stats[0].is_constant);
  EXPECT_EQ(stats.column_stats[0].run_count, 1U);
  // String columns should not have numeric min/max
  EXPECT_FALSE(stats.column_stats[0].min_value.has_value());
  EXPECT_FALSE(stats.column_stats[0].max_value.has_value());
}

// ===========================================================================
// Test: Empty chunk (0 rows)
// ===========================================================================

TEST(ChunkTest, EmptyChunk) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat32, "val"),
  };
  TopicChunkBuilder builder(/*topic_id=*/120, /*schema_id=*/1,
                            std::move(cols), /*max_rows=*/100);

  EXPECT_EQ(builder.row_count(), 0U);
  EXPECT_FALSE(builder.is_full());

  TopicChunk chunk = builder.seal();
  EXPECT_EQ(chunk.stats.row_count, 0U);
  EXPECT_EQ(chunk.encoded_timestamps.count, 0U);
}

}  // namespace
}  // namespace pj::engine
