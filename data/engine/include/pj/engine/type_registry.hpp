#pragma once
#include <memory>
#include <optional>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "pj/base/type_tree.hpp"
#include "pj/base/types.hpp"

namespace pj::engine {

// Import base types into engine namespace
using pj::SchemaId;
using pj::PrimitiveType;
using pj::TypeKind;
using pj::TypeTreeNode;
using pj::EnumMapping;
using pj::make_primitive;
using pj::make_struct;
using pj::make_array;
using pj::make_enum;
using pj::flatten_field_paths;
using pj::count_leaf_fields;

class TypeRegistry {
public:
  // Register a known schema (from Protobuf, ROS, etc.)
  // Fails if schema_name already exists.
  [[nodiscard]] absl::StatusOr<SchemaId> register_schema(
      std::string schema_name, std::shared_ptr<TypeTreeNode> type_tree);

  // Late discovery: register from first message (JSON, etc.)
  // Returns existing schema ID if name already registered.
  [[nodiscard]] absl::StatusOr<SchemaId> register_or_get(
      std::string schema_name, std::shared_ptr<TypeTreeNode> type_tree);

  // Lookup by ID — returns nullptr if not found
  [[nodiscard]] const TypeTreeNode* lookup(SchemaId id) const;

  // Lookup by name — returns nullopt if not found
  [[nodiscard]] std::optional<SchemaId> find_by_name(
      std::string_view name) const;

  // Schema evolution: add fields to existing schema (additive only).
  // Fails if: ID not found, existing fields changed type, fields removed.
  [[nodiscard]] absl::Status evolve_schema(
      SchemaId id, std::shared_ptr<TypeTreeNode> updated_tree);

private:
  SchemaId next_id_ = 1;
  absl::flat_hash_map<SchemaId, std::shared_ptr<TypeTreeNode>> schemas_;
  absl::flat_hash_map<std::string, SchemaId> name_to_id_;
};

}  // namespace pj::engine
