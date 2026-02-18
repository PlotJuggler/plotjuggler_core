#pragma once
#include <memory>
#include <optional>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "pj/base/expected.hpp"
#include "pj/base/type_tree.hpp"
#include "pj/base/types.hpp"

namespace pj::engine {

class TypeRegistry {
 public:
  // Register a known schema (from Protobuf, ROS, etc.)
  // Fails if schema_name already exists.
  [[nodiscard]] pj::Expected<pj::SchemaId> register_schema(
      std::string schema_name, std::shared_ptr<pj::TypeTreeNode> type_tree);

  // Late discovery: register from first message (JSON, etc.)
  // Returns existing schema ID if name already registered.
  [[nodiscard]] pj::Expected<pj::SchemaId> register_or_get(
      std::string schema_name, std::shared_ptr<pj::TypeTreeNode> type_tree);

  // Lookup by ID — returns nullptr if not found
  [[nodiscard]] const pj::TypeTreeNode* lookup(pj::SchemaId id) const;

  // Lookup by name — returns nullopt if not found
  [[nodiscard]] std::optional<pj::SchemaId> find_by_name(std::string_view name) const;

  // Schema evolution: add fields to existing schema (additive only).
  // Fails if: ID not found, existing fields changed type, fields removed.
  [[nodiscard]] pj::Status evolve_schema(pj::SchemaId id, std::shared_ptr<pj::TypeTreeNode> updated_tree);

 private:
  /// Next schema id to assign.
  pj::SchemaId next_id_ = 1;
  /// Schema storage by id.
  absl::flat_hash_map<pj::SchemaId, std::shared_ptr<pj::TypeTreeNode>> schemas_;
  /// Name -> schema id index.
  absl::flat_hash_map<std::string, pj::SchemaId> name_to_id_;
};

}  // namespace pj::engine
