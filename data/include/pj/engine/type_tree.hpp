#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

#include "pj/engine/types.hpp"

namespace pj::engine {

enum class PrimitiveType : uint8_t {
  kFloat32,
  kFloat64,
  kInt8,
  kInt16,
  kInt32,
  kInt64,
  kUint8,
  kUint16,
  kUint32,
  kUint64,
  kBool,
  kString,
};

enum class TypeKind : uint8_t {
  kPrimitive,
  kStruct,
  kArray,
  kEnum,
};

struct EnumMapping {
  absl::flat_hash_map<int64_t, std::string> value_to_name;
  absl::flat_hash_map<std::string, int64_t> name_to_value;
};

struct TypeTreeNode {
  std::string name;
  TypeKind kind;
  absl::flat_hash_set<std::string> semantic_tags;  // e.g., "quaternion", "pose"

  // kPrimitive
  std::optional<PrimitiveType> primitive_type;

  // kStruct: ordered child fields
  std::vector<std::shared_ptr<TypeTreeNode>> children;

  // kArray: element type + optional fixed size
  std::shared_ptr<TypeTreeNode> element_type;
  std::optional<uint32_t> fixed_array_size;

  // kEnum: wire-value <-> name mapping
  std::optional<EnumMapping> enum_mapping;
};

// Factory functions
[[nodiscard]] std::shared_ptr<TypeTreeNode> make_primitive(
    std::string name, PrimitiveType type);

[[nodiscard]] std::shared_ptr<TypeTreeNode> make_struct(
    std::string name, std::vector<std::shared_ptr<TypeTreeNode>> children);

[[nodiscard]] std::shared_ptr<TypeTreeNode> make_array(
    std::string name, std::shared_ptr<TypeTreeNode> element_type,
    std::optional<uint32_t> fixed_size = std::nullopt);

[[nodiscard]] std::shared_ptr<TypeTreeNode> make_enum(
    std::string name, PrimitiveType underlying_type, EnumMapping mapping);

// Flatten a type tree into ordered list of leaf field paths
// e.g., Pose -> ["frame_name", "position.x", "position.y", "position.z",
//                "rotation.w", "rotation.x", "rotation.y", "rotation.z"]
[[nodiscard]] std::vector<std::string> flatten_field_paths(
    const TypeTreeNode& root);

// Count leaf (primitive/enum) fields in a type tree
[[nodiscard]] std::size_t count_leaf_fields(const TypeTreeNode& root);

}  // namespace pj::engine
