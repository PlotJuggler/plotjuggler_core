#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

#include "pj/engine/buffer.hpp"
#include "pj/engine/column_buffer.hpp"
#include "pj/engine/encoding.hpp"
#include "pj/engine/types.hpp"

namespace pj::engine {

struct ColumnStats {
  uint32_t null_count = 0;
  uint32_t run_count = 0;
  bool is_constant = true;
  std::optional<double> min_value;
  std::optional<double> max_value;
};

struct ChunkStats {
  Timestamp t_min = std::numeric_limits<Timestamp>::max();
  Timestamp t_max = std::numeric_limits<Timestamp>::min();
  uint32_t row_count = 0;
  std::vector<ColumnStats> column_stats;
};

// Immutable sealed chunk - the result of sealing a TopicChunkBuilder
struct TopicChunk {
  ChunkId id = 0;
  TopicId topic_id = 0;
  SchemaId schema_version = 0;
  ChunkStats stats;

  // Encoded timestamp column (always delta-encoded)
  encoding::DeltaEncoded encoded_timestamps;

  // Per-column encoded data
  std::vector<RawBuffer> encoded_columns;          // Raw typed data for numeric cols
  std::vector<EncodingType> column_encodings;       // What encoding each column uses
  std::vector<RawBuffer> validity_bitmaps;          // Per-column (empty if no nulls)
  std::vector<ColumnDescriptor> column_descriptors; // Metadata about each column

  // For dictionary-encoded string columns (indexed by column index, nullopt for non-string)
  std::vector<std::optional<encoding::DictionaryEncoded>> dictionary_data;
  // For packed bool columns (indexed by column index, nullopt for non-bool)
  std::vector<std::optional<encoding::PackedBools>> packed_bool_data;

  // Decode helpers
  [[nodiscard]] Timestamp read_timestamp(std::size_t row) const;
  [[nodiscard]] double read_numeric_as_double(std::size_t col_index,
                                              std::size_t row) const;
  [[nodiscard]] std::string_view read_string(std::size_t col_index,
                                             std::size_t row) const;
  [[nodiscard]] bool read_bool(std::size_t col_index, std::size_t row) const;
  [[nodiscard]] bool is_null(std::size_t col_index, std::size_t row) const;
};

// Builder for constructing a chunk row by row
class TopicChunkBuilder {
 public:
  TopicChunkBuilder(TopicId topic_id, SchemaId schema_id,
                    std::vector<ColumnDescriptor> columns,
                    uint32_t max_rows);

  // Start a new row with the given timestamp
  void begin_row(Timestamp timestamp);

  // Set values for the current row (by column index)
  void set_float32(std::size_t col_index, float value);
  void set_float64(std::size_t col_index, double value);
  void set_int8(std::size_t col_index, int8_t value);
  void set_int16(std::size_t col_index, int16_t value);
  void set_int32(std::size_t col_index, int32_t value);
  void set_int64(std::size_t col_index, int64_t value);
  void set_uint8(std::size_t col_index, uint8_t value);
  void set_uint16(std::size_t col_index, uint16_t value);
  void set_uint32(std::size_t col_index, uint32_t value);
  void set_uint64(std::size_t col_index, uint64_t value);
  void set_bool(std::size_t col_index, bool value);
  void set_string(std::size_t col_index, std::string_view value);
  void set_null(std::size_t col_index);

  // Finalize the current row (append all columns)
  void finish_row();

  [[nodiscard]] bool is_full() const noexcept;
  [[nodiscard]] uint32_t row_count() const noexcept;
  [[nodiscard]] const ChunkStats& stats() const noexcept;

  // Seal: finalize stats, apply encodings, produce immutable TopicChunk
  [[nodiscard]] TopicChunk seal();

 private:
  TopicId topic_id_;
  SchemaId schema_id_;
  uint32_t max_rows_;
  static inline ChunkId next_chunk_id_ = 1;  // monotonic counter

  std::vector<Timestamp> timestamps_;
  std::vector<TypedColumnBuffer> columns_;
  std::vector<ColumnDescriptor> column_descriptors_;
  ChunkStats stats_;

  // Track per-row state during begin_row/finish_row
  bool row_in_progress_ = false;
  Timestamp current_timestamp_ = 0;

  void update_column_stats(std::size_t col_index, double value);
};

}  // namespace pj::engine
