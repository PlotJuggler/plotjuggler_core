#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <arrow/api.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "pj/base/type_tree.hpp"
#include "pj/base/types.hpp"
#include "pj/engine/writer.hpp"

namespace pj::engine::arrow_import {

/// Describes how an Arrow schema column maps to a PJ topic column.
struct ArrowColumnMapping {
  int arrow_column_index;
  std::size_t pj_column_index;
  PrimitiveType pj_type;
  std::string field_name;
};

/// Map a single Arrow DataType to PrimitiveType.
/// Returns nullopt for unsupported types (structs, lists, etc.).
[[nodiscard]] std::optional<PrimitiveType> arrow_type_to_primitive(
    const std::shared_ptr<arrow::DataType>& type);

/// Convert an Arrow schema to a PJ TypeTreeNode and column mappings.
/// Unsupported Arrow types are skipped.
[[nodiscard]] absl::StatusOr<std::pair<
    std::shared_ptr<pj::TypeTreeNode>,
    std::vector<ArrowColumnMapping>>>
schema_from_arrow(const arrow::Schema& schema);

/// Import an Arrow RecordBatch into a DataWriter topic.
///
/// timestamp_column: which Arrow column contains timestamps (as int64).
///   If -1, row indices (0, 1, 2, ...) are used as timestamps.
[[nodiscard]] absl::Status import_record_batch(
    DataWriter& writer,
    TopicId topic_id,
    const arrow::RecordBatch& batch,
    const std::vector<ArrowColumnMapping>& mappings,
    int timestamp_column = -1);

/// Import an Arrow Table (potentially chunked) into a DataWriter topic.
/// Combines chunks first if combine_chunks is true.
[[nodiscard]] absl::Status import_table(
    DataWriter& writer,
    TopicId topic_id,
    const arrow::Table& table,
    const std::vector<ArrowColumnMapping>& mappings,
    int timestamp_column = -1,
    bool combine_chunks = true);

}  // namespace pj::engine::arrow_import
