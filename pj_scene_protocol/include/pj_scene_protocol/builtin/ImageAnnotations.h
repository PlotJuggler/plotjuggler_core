/**
 * @file ImageAnnotations.h
 * @brief Vector primitives (points, lines, circles, text) overlaid on a
 *        specific image at a specific timestamp.
 *
 * ImageAnnotations is the 2D overlay builtin. Unlike Image / DepthImage /
 * PointCloud — which carry potentially-megabyte buffers and use the
 * BufferAnchor zero-copy pattern — annotation data is small (a few
 * hundred bytes typically) so the type owns its contents via std::vector
 * outright. Eager ingestion is the natural default; no anchor lifetime
 * concerns to worry about.
 *
 * The concrete type lives in pj_scene_protocol/image_annotation.h
 * (PJ::ImageAnnotation) for historical reasons. This header re-exposes it
 * as PJ::sdk::ImageAnnotations so it sits next to the other builtin
 * objects in the same namespace.
 */
#pragma once

#include "pj_scene_protocol/image_annotation.h"

namespace PJ {
namespace sdk {

using ImageAnnotations = ::PJ::ImageAnnotation;

}  // namespace sdk
}  // namespace PJ
