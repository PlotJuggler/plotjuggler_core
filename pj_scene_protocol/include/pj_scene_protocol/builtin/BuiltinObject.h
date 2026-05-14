/**
 * @file BuiltinObject.h
 * @brief Sum type of all builtin objects a MessageParser may produce.
 *
 * New alternatives (kDepthImage, kMarkers, kOccupancyGrid, …) are appended
 * at the tail and announced via BuiltinObjectKind. Plugins built against
 * an older SDK keep producing the alternatives they know; hosts built
 * against an older SDK that receive an unknown kind reject the message
 * rather than crashing. Forward-compatible — no protocol bump required.
 */
#pragma once

#include <type_traits>
#include <variant>

#include "pj_scene_protocol/builtin/BuiltinObjectKind.h"
#include "pj_scene_protocol/builtin/DepthImage.h"
#include "pj_scene_protocol/builtin/Image.h"
#include "pj_scene_protocol/builtin/ImageAnnotations.h"
#include "pj_scene_protocol/builtin/PointCloud.h"

namespace PJ {
namespace sdk {

using BuiltinObject = std::variant<Image, PointCloud, DepthImage, ImageAnnotations>;

/// Get the kind tag for a BuiltinObject without unpacking it.
[[nodiscard]] inline BuiltinObjectKind kindOf(const BuiltinObject& obj) noexcept {
  return std::visit(
      [](const auto& concrete) -> BuiltinObjectKind {
        using T = std::decay_t<decltype(concrete)>;
        if constexpr (std::is_same_v<T, Image>) {
          return BuiltinObjectKind::kImage;
        } else if constexpr (std::is_same_v<T, PointCloud>) {
          return BuiltinObjectKind::kPointCloud;
        } else if constexpr (std::is_same_v<T, DepthImage>) {
          return BuiltinObjectKind::kDepthImage;
        } else if constexpr (std::is_same_v<T, ImageAnnotations>) {
          return BuiltinObjectKind::kImageAnnotations;
        } else {
          return BuiltinObjectKind::kNone;
        }
      },
      obj);
}

}  // namespace sdk
}  // namespace PJ
