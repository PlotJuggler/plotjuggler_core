/**
 * @file BuiltinObject.h
 * @brief Type-erased holder for any builtin object a MessageParser may produce.
 *
 * BuiltinObject is `std::any`. A producer constructs it by passing a
 * concrete builtin value (`sdk::Image`, `sdk::PointCloud`, `sdk::DepthImage`,
 * `sdk::ImageAnnotations`, `sdk::FrameTransforms`, ...); a consumer recovers
 * the concrete type via `std::any_cast<T>(&obj)` and obtains the type tag via
 * `typeOf(obj)`.
 *
 * The type erasure is deliberate: choosing `std::any` over `std::variant`
 * keeps the SDK forward-compatible. Plugins built against an older SDK can
 * keep producing the alternatives they know without any TU referencing the
 * (later-extended) full alternative list; hosts built against an older SDK
 * that receive an unknown type simply see `BuiltinObjectType::kNone` from
 * `typeOf` and reject the message. No protocol bump required when a new
 * builtin type is appended to BuiltinObjectType.
 */
#pragma once

#include <any>
#include <cstdint>
#include <magic_enum/magic_enum.hpp>
#include <optional>
#include <string_view>

#include "pj_base/builtin/DepthImage.h"
#include "pj_base/builtin/FrameTransforms.h"
#include "pj_base/builtin/Image.h"
#include "pj_base/builtin/ImageAnnotations.h"
#include "pj_base/builtin/PointCloud.h"

namespace PJ {
namespace sdk {

enum class BuiltinObjectType : uint16_t {
  kNone = 0,
  kImage = 1,             ///< sdk::Image — raw or compressed, distinguished by encoding string.
  kPointCloud = 3,        ///< sdk::PointCloud — packed points + per-channel field layout.
  kDepthImage = 4,        ///< sdk::DepthImage — depth pixels + camera intrinsics.
  kImageAnnotations = 5,  ///< sdk::ImageAnnotations — 2D overlays (points, lines, text).
  kFrameTransforms = 6,   ///< sdk::FrameTransforms — named 3D frame relationships.
  // Reserved for future types; keep numeric values stable across releases.
  // kOccupancyGrid  = 7,
};

/// A-priori classification of a schema. Currently carries only the type;
/// the struct leaves room to attach declarative metadata later without
/// breaking the API.
struct SchemaClassification {
  BuiltinObjectType object_type = BuiltinObjectType::kNone;
};

/// Canonical string for a type value. Uses magic_enum for reflection.
/// e.g. name(kImage) == "kImage".
[[nodiscard]] inline constexpr std::string_view name(BuiltinObjectType type) noexcept {
  return magic_enum::enum_name(type);
}

/// Parse a type name into the enum. Accepts the same strings name()
/// emits (e.g. "kImage"). Returns nullopt for unknown names.
[[nodiscard]] inline constexpr std::optional<BuiltinObjectType> parseBuiltinObjectType(std::string_view s) noexcept {
  return magic_enum::enum_cast<BuiltinObjectType>(s);
}

using BuiltinObject = std::any;

/// Get the type tag for a BuiltinObject without copying it.
/// Returns kNone for an empty BuiltinObject or one that wraps a type
/// unknown to this SDK build.
[[nodiscard]] inline BuiltinObjectType typeOf(const BuiltinObject& obj) noexcept {
  if (!obj.has_value()) {
    return BuiltinObjectType::kNone;
  }
  const auto& t = obj.type();
  if (t == typeid(Image)) {
    return BuiltinObjectType::kImage;
  }
  if (t == typeid(PointCloud)) {
    return BuiltinObjectType::kPointCloud;
  }
  if (t == typeid(DepthImage)) {
    return BuiltinObjectType::kDepthImage;
  }
  if (t == typeid(ImageAnnotations)) {
    return BuiltinObjectType::kImageAnnotations;
  }
  if (t == typeid(FrameTransforms)) {
    return BuiltinObjectType::kFrameTransforms;
  }
  return BuiltinObjectType::kNone;
}

}  // namespace sdk
}  // namespace PJ
