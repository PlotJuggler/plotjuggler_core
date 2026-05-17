#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "pj_base/builtin/ImageAnnotations.h"
#include "pj_base/expected.hpp"

namespace PJ {

/// Wire-format identifier for canonical image annotations.
inline constexpr std::string_view kSchemaImageAnnotations = "PJ.ImageAnnotations";

/// Serializes a sdk::ImageAnnotations to canonical PJ.ImageAnnotations wire bytes.
///
/// `timestamp` and `image_topic` are not serialized; callers that need them
/// must preserve them outside this payload.
[[nodiscard]] std::vector<uint8_t> serializeImageAnnotations(const sdk::ImageAnnotations& ia);

/// Decodes canonical PJ.ImageAnnotations wire bytes into sdk::ImageAnnotations.
///
/// Returns an error for null, empty, truncated, or malformed payloads.
[[nodiscard]] Expected<sdk::ImageAnnotations> deserializeImageAnnotations(const uint8_t* data, size_t size);

}  // namespace PJ
