/**
 * @file canonical_object_abi.h
 * @brief C ABI representation of canonical objects produced by parsers.
 *
 * The C++ vocabulary lives in pj_base/sdk/canonical_object.hpp
 * (sdk::CanonicalObject = std::variant<Image, CompressedImage, PointCloud>).
 * This file defines the wire format used to cross the plugin C ABI boundary
 * for that variant: parser plugins produce a flat byte blob with a small
 * header describing the kind, and the host deserializes it back to the
 * C++ type.
 *
 * The blob layout is little-endian, packed, with no implementation-defined
 * padding. Trampolines and host loader use it directly.
 */
#ifndef PJ_CANONICAL_OBJECT_ABI_H
#define PJ_CANONICAL_OBJECT_ABI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pj_base/plugin_data_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Owned buffer of named field values produced by the parse_scalars slot.
 * The plugin owns the @p fields array; the host calls @p release(alloc_handle)
 * when done. release MAY be NULL if the plugin manages the buffer in a way
 * that does not require explicit release between calls.
 */
typedef struct PJ_named_field_value_buffer_t {
  const PJ_named_field_value_t* fields;
  size_t count;
  void* alloc_handle;
  void (*release)(void* alloc_handle);
} PJ_named_field_value_buffer_t;

/**
 * Canonical object kinds. Numeric values are stable across releases — never
 * renumber. Mirror of PJ::sdk::CanonicalObjectKind for use across the C ABI.
 */
typedef enum PJ_canonical_object_kind_t {
  PJ_CANONICAL_OBJECT_KIND_NONE = 0,
  PJ_CANONICAL_OBJECT_KIND_IMAGE = 1,
  PJ_CANONICAL_OBJECT_KIND_COMPRESSED_IMAGE = 2,
  PJ_CANONICAL_OBJECT_KIND_POINTCLOUD = 3,
  /* Reserve future kinds; appended at the tail. */
  /* PJ_CANONICAL_OBJECT_KIND_MARKERS         = 4, */
  /* PJ_CANONICAL_OBJECT_KIND_OCCUPANCY_GRID  = 5, */
} PJ_canonical_object_kind_t;

/**
 * Schema classification — what kind a parser declares for a given schema.
 * Returned a priori (without parsing payload) by the classify_schema slot.
 *
 * Currently a single field plus reserved padding to keep the struct size
 * stable across future minor extensions (declarative metadata can attach
 * via additional structs returned by other slots, not by growing this one).
 */
typedef struct PJ_schema_classification_t {
  uint16_t object_kind; /**< PJ_canonical_object_kind_t. */
  uint16_t reserved;    /**< Must be zero. */
} PJ_schema_classification_t;

/**
 * Canonical object as a flat byte blob produced by the parse_object slot.
 *
 * Layout of @p data:
 *
 *   header (12 bytes, little-endian):
 *     uint16_t kind                   // PJ_canonical_object_kind_t
 *     uint16_t reserved
 *     int64_t  timestamp_ns
 *
 *   body (varies by kind, immediately follows the header):
 *
 *     KIND_IMAGE:
 *       uint32_t width
 *       uint32_t height
 *       uint16_t pixel_format
 *       uint16_t reserved
 *       uint32_t pixels_size
 *       uint8_t  pixels[pixels_size]   // tightly packed, no row stride
 *
 *     KIND_COMPRESSED_IMAGE:
 *       uint8_t  format                // 0=unknown, 1=JPEG, 2=PNG, 3=QOI
 *       uint8_t  has_depth_min
 *       uint8_t  has_depth_max
 *       uint8_t  reserved
 *       float    depth_min             // valid iff has_depth_min
 *       float    depth_max             // valid iff has_depth_max
 *       uint32_t bytes_size
 *       uint8_t  bytes[bytes_size]
 *
 *     KIND_POINTCLOUD:
 *       uint32_t width
 *       uint32_t height
 *       uint32_t point_step
 *       uint32_t row_step
 *       uint8_t  is_bigendian
 *       uint8_t  is_dense
 *       uint16_t fields_count
 *       fields[fields_count]:
 *         uint32_t name_size
 *         char     name[name_size]
 *         uint32_t offset
 *         uint8_t  datatype            // 0=unknown,1=i8,2=u8,3=i16,4=u16,
 *                                      // 5=i32,6=u32,7=f32,8=f64
 *         uint8_t  reserved[3]
 *         uint32_t count
 *       uint32_t data_size
 *       uint8_t  data[data_size]
 *
 * Memory ownership:
 *   The blob's @p data is owned by the parser plugin. The plugin allocates
 *   it during parse_object and the host calls @p release(ctx, data) when it
 *   is done with the bytes. release MAY be NULL if data points into a
 *   plugin-internal buffer that the plugin manages itself across calls.
 */
typedef struct PJ_canonical_object_blob_t {
  const uint8_t* data;
  uint64_t size;
  /** Opaque handle the plugin uses to identify the allocation. */
  void* alloc_handle;
  /** Release callback invoked by the host. NULL means no release needed. */
  void (*release)(void* alloc_handle);
} PJ_canonical_object_blob_t;

#ifdef __cplusplus
}
#endif

#endif /* PJ_CANONICAL_OBJECT_ABI_H */
