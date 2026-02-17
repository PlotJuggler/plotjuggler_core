#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "pj/base/type_tree.hpp"
#include "pj/base/types.hpp"
#include "pj/engine/writer.hpp"

namespace pj::engine::arrow_import {

/// Describes how an Arrow IPC column maps to a PJ topic column.
struct ArrowColumnMapping {
  /// Source column index in Arrow batch/table schema.
  int arrow_column_index;
  /// Destination column index in PJ flattened schema.
  std::size_t pj_column_index;
  /// Target primitive type used by PJ storage.
  PrimitiveType pj_type;
  /// Source field name.
  std::string field_name;
};

/// Parse schema from Arrow IPC stream bytes (reads first message).
/// Returns a TypeTreeNode and column mappings for supported types.
/// Unsupported Arrow types are skipped.
[[nodiscard]] absl::StatusOr<std::pair<std::shared_ptr<pj::TypeTreeNode>, std::vector<ArrowColumnMapping>>>
schema_from_ipc(absl::Span<const uint8_t> ipc_stream);

/// Import all record batches from Arrow IPC stream bytes into a DataWriter topic.
///
/// timestamp_column: which Arrow column contains timestamps (as int64).
///   If -1, row indices (0, 1, 2, ...) are used as timestamps.
[[nodiscard]] absl::Status import_ipc_stream(
    DataWriter& writer, TopicId topic_id, absl::Span<const uint8_t> ipc_stream,
    const std::vector<ArrowColumnMapping>& mappings, int timestamp_column = -1);

}  // namespace pj::engine::arrow_import
