#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "pj_base/builtin/ImageAnnotations.h"
#include "pj_base/expected.hpp"

namespace PJ {

/// Wire-format identifier for canonical image annotations.
inline constexpr std::string_view kSchemaImageAnnotations = "foxglove.ImageAnnotations";

/// Serializes a sdk::ImageAnnotations to canonical foxglove.ImageAnnotations Protobuf bytes.
///
/// `timestamp` and `image_topic` are not serialized; callers that need them
/// must preserve them outside this payload.
[[nodiscard]] std::vector<uint8_t> serializeImageAnnotations(const sdk::ImageAnnotations& ia);

/// Decodes canonical foxglove.ImageAnnotations Protobuf bytes into sdk::ImageAnnotations.
///
/// Returns an error for null, empty, truncated, or malformed payloads.
[[nodiscard]] Expected<sdk::ImageAnnotations> deserializeImageAnnotations(const uint8_t* data, size_t size);

}  // namespace PJ
