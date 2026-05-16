/**
 * @file BuiltinObjectKind.h
 * @brief A-priori tag identifying which builtin object a parser produces
 *        for a given schema.
 *
 * The kind is returned by MessageParser::classifySchema() without parsing
 * any payload — the host uses it to decide whether to register an
 * ObjectTopic and how to route the message.
 *
 * Numeric values are stable across releases; mirror of the C ABI enum
 * PJ_builtin_object_kind_t in pj_base/builtin_object_abi.h.
 */
#pragma once

#include <cstdint>
#include <magic_enum/magic_enum.hpp>
#include <optional>
#include <string_view>

namespace PJ {
namespace sdk {

enum class BuiltinObjectKind : uint16_t {
  kNone = 0,
  kImage = 1,             ///< sdk::Image — raw or compressed, distinguished by encoding string.
  kPointCloud = 3,        ///< sdk::PointCloud — packed points + per-channel field layout.
  kDepthImage = 4,        ///< sdk::DepthImage — depth pixels + camera intrinsics.
  kImageAnnotations = 5,  ///< sdk::ImageAnnotations — 2D overlays (points, lines, text).
  // Reserved for future kinds; keep numeric values stable across releases.
  // kOccupancyGrid  = 6,
};

/// A-priori classification of a schema. Currently carries only the kind;
/// the struct (instead of a raw enum) leaves room to attach declarative
/// metadata later without breaking the API.
struct SchemaClassification {
  BuiltinObjectKind object_kind = BuiltinObjectKind::kNone;
};

/// Canonical string for a kind value (without the leading `k`). Uses
/// magic_enum for reflection. e.g. name(kImage) == "kImage" — callers
/// that want the bare token strip the prefix.
[[nodiscard]] inline constexpr std::string_view name(BuiltinObjectKind kind) noexcept {
  return magic_enum::enum_name(kind);
}

/// Parse a kind name into the enum. Accepts the same strings name()
/// emits (e.g. "kImage"). Returns nullopt for unknown names.
[[nodiscard]] inline constexpr std::optional<BuiltinObjectKind> parseBuiltinObjectKind(std::string_view s) noexcept {
  return magic_enum::enum_cast<BuiltinObjectKind>(s);
}

}  // namespace sdk
}  // namespace PJ
