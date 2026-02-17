#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

#include "pj/base/types.hpp"
#include "pj/engine/buffer.hpp"
#include "pj/engine/column_buffer.hpp"
#include "pj/engine/encoding.hpp"

namespace pj::engine {

// Import base types into engine namespace
using pj::ChunkId;
using pj::kInvalidChunkId;
using pj::SchemaId;
using pj::Timestamp;
using pj::TopicId;

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

  // Raw timestamp column (one int64 per row)
  std::vector<Timestamp> timestamps;

  // Per-column encoded data
  std::vector<RawBuffer> encoded_columns;          // Raw typed data for numeric cols
  std::vector<EncodingType> column_encodings;       // What encoding each column uses
  std::vector<RawBuffer> validity_bitmaps;          // Per-column (empty if no nulls)
  std::vector<ColumnDescriptor> column_descriptors; // Metadata about each column

  // Per-column encoding data (variant: monostate for kRaw, or specific encoding)
  std::vector<encoding::ColumnEncodingData> encoding_data;

  // Decode helpers
  [[nodiscard]] Timestamp read_timestamp(std::size_t row) const;
  [[nodiscard]] double read_numeric_as_double(std::size_t col_index,
                                              std::size_t row) const;
  [[nodiscard]] std::string_view read_string(std::size_t col_index,
                                             std::size_t row) const;
  [[nodiscard]] bool read_bool(std::size_t col_index, std::size_t row) const;
  [[nodiscard]] bool is_null(std::size_t col_index, std::size_t row) const;

  // Bulk read: switch on type once, then tight inner loop.
  // For kBool/kString columns, fills NaN.
  void read_column_as_doubles(std::size_t col_index, double* out,
                              std::size_t row_start, std::size_t count) const;
};

// Builder for constructing a chunk row by row
class TopicChunkBuilder {
 public:
  TopicChunkBuilder(TopicId topic_id, SchemaId schema_id,
                    std::vector<ColumnDescriptor> columns,
                    uint32_t max_rows);

  // Start a new row with the given timestamp
  void begin_row(Timestamp timestamp);

  // Set values for the current row (by column index) — 7 storage types
  void set_float32(std::size_t col_index, float value);
  void set_float64(std::size_t col_index, double value);
  void set_int32(std::size_t col_index, int32_t value);
  void set_int64(std::size_t col_index, int64_t value);
  void set_uint64(std::size_t col_index, uint64_t value);
  void set_bool(std::size_t col_index, bool value);
  void set_string(std::size_t col_index, std::string_view value);
  void set_null(std::size_t col_index);

  // Finalize the current row (append all columns)
  void finish_row();

  // ---- Bulk column append ----
  // Call append_timestamps first, then append_column_* for each column,
  // then append_column_validity for columns with nulls, then finish_bulk_append.
  // Stats are computed in finish_bulk_append using the column's validity bitmap.
  void append_timestamps(const Timestamp* timestamps, std::size_t count);
  void append_column_float32(std::size_t col_index, const float* data,
                             std::size_t count);
  void append_column_float64(std::size_t col_index, const double* data,
                             std::size_t count);
  void append_column_int32(std::size_t col_index, const int32_t* data,
                           std::size_t count);
  void append_column_int64(std::size_t col_index, const int64_t* data,
                           std::size_t count);
  void append_column_uint64(std::size_t col_index, const uint64_t* data,
                            std::size_t count);
  void append_column_bool(std::size_t col_index, const uint8_t* data,
                          std::size_t count);
  void append_column_strings(std::size_t col_index, const uint32_t* offsets,
                             const char* data, std::size_t count);
  void append_column_validity(std::size_t col_index, const uint8_t* bitmap,
                              std::size_t bit_offset, std::size_t count);
  void finish_bulk_append();

  [[nodiscard]] uint32_t remaining_capacity() const noexcept;
  [[nodiscard]] bool is_full() const noexcept;
  [[nodiscard]] uint32_t row_count() const noexcept;
  [[nodiscard]] const ChunkStats& stats() const noexcept;
  [[nodiscard]] Timestamp last_timestamp() const noexcept;

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

  Timestamp last_timestamp_ = std::numeric_limits<Timestamp>::min();
  std::vector<double> last_column_values_;
  std::size_t bulk_pending_rows_ = 0;  // rows added via bulk but not yet finished

  void update_column_stats(std::size_t col_index, double value);

  // Bulk stats computation: single pass over column buffer data.
  // Called by finish_bulk_append() after both data and validity are set.
  // Reads from column buffer and skips null positions via validity bitmap.
  void compute_bulk_numeric_stats(std::size_t col_index, StorageKind kind,
                                  std::size_t first_row, std::size_t count);
  void compute_bulk_string_stats(std::size_t col_index,
                                 std::size_t first_row, std::size_t count);
};

}  // namespace pj::engine
