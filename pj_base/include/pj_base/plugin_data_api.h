#ifndef PJ_PLUGIN_DATA_API_H
#define PJ_PLUGIN_DATA_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PJ_PLUGIN_DATA_API_VERSION 1

/**
 * Boot-level ABI version, exported by every plugin .so as a separate C symbol
 * independent of any vtable. Loaders dlsym this BEFORE fetching the family
 * vtable, because struct_size — the next level of compatibility check — lives
 * INSIDE the struct being validated, creating a bootstrap problem. An
 * incompatible or missing `pj_plugin_abi_version` is a fail-fast rejection
 * with a specific error.
 *
 * The integer value is PJ_ABI_VERSION below. The symbol name the loader looks
 * up is `pj_plugin_abi_version` (a regular C identifier, not a preprocessor
 * token).
 *
 * Contract for plugin authors: every plugin SDK macro (PJ_DATA_SOURCE_PLUGIN,
 * PJ_MESSAGE_PARSER_PLUGIN, etc.) emits `pj_plugin_abi_version` automatically.
 * Do not redefine it.
 *
 * v3 plugins advertise version 3.
 */
#define PJ_ABI_VERSION 3

/**
 * Convention for plugin-loaders:
 *
 *   1. `dlsym("pj_plugin_abi_version")` — reject if missing or not equal to 3.
 *   2. `dlsym("PJ_get_<family>_vtable")` — reject if missing.
 *   3. Check `vtable->protocol_version == PJ_<FAMILY>_PROTOCOL_VERSION`.
 *   4. Check `vtable->struct_size >= PJ_<FAMILY>_MIN_VTABLE_SIZE`
 *      (NOT `sizeof(...)` — that grows per host release and would reject
 *       plugins compiled against older headers).
 *   5. Every tail slot read must be guarded by
 *      `PJ_HAS_TAIL_SLOT(vtable_type, vtable_ptr, field)` below, which
 *      checks both struct_size and field non-null.
 */
#define PJ_HAS_TAIL_SLOT(vtable_type, vtable_ptr, field)                                        \
  ((vtable_ptr)->struct_size >= (offsetof(vtable_type, field) + sizeof((vtable_ptr)->field)) && \
   (vtable_ptr)->field != NULL)

typedef enum {
  PJ_PRIMITIVE_TYPE_FLOAT32 = 0,
  PJ_PRIMITIVE_TYPE_FLOAT64 = 1,
  PJ_PRIMITIVE_TYPE_INT8 = 2,
  PJ_PRIMITIVE_TYPE_INT16 = 3,
  PJ_PRIMITIVE_TYPE_INT32 = 4,
  PJ_PRIMITIVE_TYPE_INT64 = 5,
  PJ_PRIMITIVE_TYPE_UINT8 = 6,
  PJ_PRIMITIVE_TYPE_UINT16 = 7,
  PJ_PRIMITIVE_TYPE_UINT32 = 8,
  PJ_PRIMITIVE_TYPE_UINT64 = 9,
  PJ_PRIMITIVE_TYPE_BOOL = 10,
  PJ_PRIMITIVE_TYPE_STRING = 11,
  /** Sentinel: null value with no type information. Used when is_null=true
   *  and the plugin provides no type hint (untyped kNull). */
  PJ_PRIMITIVE_TYPE_UNSPECIFIED = 0xFF,
} PJ_primitive_type_t;

/* ABI-FROZEN: layout permanent; changes = v4 break. */
typedef struct {
  const char* data;
  size_t size;
} PJ_string_view_t;

/* ABI-FROZEN: layout permanent; changes = v4 break. */
typedef struct {
  const uint8_t* data;
  size_t size;
} PJ_bytes_view_t;

/* ABI-FROZEN: layout permanent; changes = v4 break. */
typedef struct {
  uint32_t id;
} PJ_data_source_handle_t;

/* ABI-FROZEN: layout permanent; changes = v4 break. */
typedef struct {
  uint32_t id;
} PJ_topic_handle_t;

/* ABI-FROZEN: layout permanent; changes = v4 break. */
typedef struct {
  PJ_topic_handle_t topic;
  uint32_t id;
} PJ_field_handle_t;

/* ==========================================================================
 * Protocol v3 core types
 *
 * PJ_error_t carries its message/domain INLINE (fixed-size null-terminated
 * buffers) so callers can copy it freely and its lifetime is trivial.
 * There is no dangling view into plugin-owned storage.
 * ========================================================================== */

#define PJ_ERROR_DOMAIN_MAX 32
#define PJ_ERROR_MESSAGE_MAX 224
#define PJ_ERROR_KIND_MAX 32

/*
 * ABI-FROZEN (with growth escape hatch).
 *
 * The inline layout is permanent for v3.x — existing fields never move or
 * change type. The `extended` + `extended_kind` slots are the designated
 * growth path for richer payloads (cause chains, stack traces, structured
 * field lists); never add further top-level fields.
 *
 * Lifetime of `extended`: valid until the next ABI call through the same
 * plugin instance's vtable. Callers that want to retain the payload past
 * that window must deep-copy. `extended_kind` is a reverse-DNS ID of the
 * payload type (e.g. "pj.error.cause.v1"); when `extended_kind[0]=='\0'`
 * the `extended` pointer is ignored regardless of its value.
 *
 * Every populator (see sdk::fillError) MUST clear both new slots when
 * writing to avoid stale pointers in reused error structs.
 */
typedef struct {
  int32_t code;                          /* 0 = success; otherwise domain-specific */
  char domain[PJ_ERROR_DOMAIN_MAX];      /* null-terminated; truncated if too long */
  char message[PJ_ERROR_MESSAGE_MAX];    /* null-terminated; truncated if too long */
  const void* extended;                  /* nullable typed payload */
  char extended_kind[PJ_ERROR_KIND_MAX]; /* reverse-DNS ID; "" if no payload */
} PJ_error_t;

/* ABI-FROZEN: fat pointer layout permanent. The `vtable` is const void* by
 * design — consumers cast to the appropriate typed service vtable based on
 * the service name they requested. */
typedef struct {
  void* ctx;
  const void* vtable;
} PJ_service_t;

/* ABI-APPENDABLE: new slots may be added at the tail; struct_size gates read. */
typedef struct PJ_service_registry_vtable_t {
  uint32_t protocol_version;
  uint32_t struct_size;
  bool (*get_service)(
      void* ctx, PJ_string_view_t name, uint32_t min_version, PJ_service_t* out_service, PJ_error_t* out_error);
} PJ_service_registry_vtable_t;

/* ABI-FROZEN: fat pointer layout permanent. */
typedef struct {
  void* ctx;
  const PJ_service_registry_vtable_t* vtable;
} PJ_service_registry_t;

struct PJ_dialog_vtable_t;

/* ABI-FROZEN: fat pointer layout permanent. */
typedef struct {
  void* ctx;
  const struct PJ_dialog_vtable_t* vtable;
} PJ_borrowed_dialog_t;

typedef union {
  float as_float32;
  double as_float64;
  int8_t as_int8;
  int16_t as_int16;
  int32_t as_int32;
  int64_t as_int64;
  uint8_t as_uint8;
  uint16_t as_uint16;
  uint32_t as_uint32;
  uint64_t as_uint64;
  uint8_t as_bool;
  PJ_string_view_t as_string;
} PJ_scalar_value_data_t;

typedef struct {
  PJ_primitive_type_t type;
  PJ_scalar_value_data_t data;
} PJ_scalar_value_t;

typedef struct {
  PJ_string_view_t name;
  bool is_null;
  PJ_scalar_value_t value;
} PJ_named_field_value_t;

typedef struct {
  PJ_field_handle_t field;
  bool is_null;
  PJ_scalar_value_t value;
} PJ_bound_field_value_t;

typedef struct {
  PJ_data_source_handle_t handle;
  PJ_string_view_t name;
  uint32_t first_topic;
  uint32_t topic_count;
} PJ_data_source_info_t;

typedef struct {
  PJ_topic_handle_t handle;
  PJ_data_source_handle_t source;
  PJ_string_view_t name;
  uint32_t first_field;
  uint32_t field_count;
} PJ_topic_info_t;

typedef struct {
  PJ_field_handle_t handle;
  PJ_string_view_t name;
  PJ_primitive_type_t type;
} PJ_field_info_t;

typedef struct {
  const uint32_t* offsets;
  size_t offset_count;
  const char* bytes;
  size_t byte_count;
} PJ_string_series_values_t;

typedef union {
  const float* as_float32;
  const double* as_float64;
  const int8_t* as_int8;
  const int16_t* as_int16;
  const int32_t* as_int32;
  const int64_t* as_int64;
  const uint8_t* as_uint8;
  const uint16_t* as_uint16;
  const uint32_t* as_uint32;
  const uint64_t* as_uint64;
  const uint8_t* as_bool;
  PJ_string_series_values_t as_string;
} PJ_series_values_t;

typedef struct {
  const PJ_data_source_info_t* data_sources;
  size_t data_source_count;
  const PJ_topic_info_t* topics;
  size_t topic_count;
  const PJ_field_info_t* fields;
  size_t field_count;
  void* release_ctx;
  void (*release)(void* release_ctx);
} PJ_catalog_snapshot_t;

typedef struct {
  PJ_data_source_handle_t source;
  PJ_topic_handle_t topic;
  PJ_field_handle_t field;
  PJ_primitive_type_t type;
  const int64_t* timestamps; /**< Nanoseconds since Unix epoch (1970-01-01T00:00:00Z). */
  size_t row_count;
  const uint8_t* validity_bits;
  size_t validity_size;
  PJ_series_values_t values;
  void* release_ctx;
  void (*release)(void* release_ctx);
} PJ_materialized_series_t;

/* ==========================================================================
 * Three distinct write-host vtables (protocol v3).
 *
 * Each plugin family binds to its own type so the compiler enforces scope:
 * a DataSource plugin cannot accidentally call Toolbox-only ops, a Parser
 * plugin cannot name topics, etc. The host-side implementation can (and
 * does) share one backend across all three — but at the ABI layer the
 * types are distinct.
 *
 * All fallible slots take a PJ_error_t* out-parameter. Callers may pass
 * NULL to discard detail.
 * ========================================================================== */

/* ABI-APPENDABLE: new slots may be added at the tail; struct_size gates read.
 *
 * Source write host: multi-topic writes bound to one data source. */
typedef struct PJ_source_write_host_vtable_t {
  uint32_t abi_version;
  uint32_t struct_size;

  bool (*ensure_topic)(void* ctx, PJ_string_view_t topic_name, PJ_topic_handle_t* out_topic, PJ_error_t* out_error);

  bool (*ensure_field)(
      void* ctx, PJ_topic_handle_t topic, PJ_string_view_t field_name, PJ_primitive_type_t type,
      PJ_field_handle_t* out_field, PJ_error_t* out_error);

  bool (*append_record)(
      void* ctx, PJ_topic_handle_t topic, int64_t timestamp, const PJ_named_field_value_t* fields, size_t field_count,
      PJ_error_t* out_error);

  bool (*append_bound_record)(
      void* ctx, PJ_topic_handle_t topic, int64_t timestamp, const PJ_bound_field_value_t* fields, size_t field_count,
      PJ_error_t* out_error);

  bool (*append_arrow_ipc)(
      void* ctx, PJ_topic_handle_t topic, PJ_bytes_view_t ipc_stream, PJ_string_view_t timestamp_column,
      PJ_error_t* out_error);
} PJ_source_write_host_vtable_t;

typedef struct {
  void* ctx;
  const PJ_source_write_host_vtable_t* vtable;
} PJ_source_write_host_t;

/* ABI-APPENDABLE: new slots may be added at the tail; struct_size gates read.
 *
 * Parser write host: single-topic writes. The bound topic is set at
 * service-creation time; the parser plugin never names it. */
typedef struct PJ_parser_write_host_vtable_t {
  uint32_t abi_version;
  uint32_t struct_size;

  bool (*ensure_field)(
      void* ctx, PJ_string_view_t field_name, PJ_primitive_type_t type, PJ_field_handle_t* out_field,
      PJ_error_t* out_error);

  bool (*append_record)(
      void* ctx, int64_t timestamp, const PJ_named_field_value_t* fields, size_t field_count, PJ_error_t* out_error);

  bool (*append_bound_record)(
      void* ctx, int64_t timestamp, const PJ_bound_field_value_t* fields, size_t field_count, PJ_error_t* out_error);

  bool (*append_arrow_ipc)(
      void* ctx, PJ_bytes_view_t ipc_stream, PJ_string_view_t timestamp_column, PJ_error_t* out_error);
} PJ_parser_write_host_vtable_t;

typedef struct {
  void* ctx;
  const PJ_parser_write_host_vtable_t* vtable;
} PJ_parser_write_host_t;

/* ABI-APPENDABLE: new slots may be added at the tail; struct_size gates read.
 *
 * Toolbox host: multi-source read+write. */
typedef struct PJ_toolbox_host_vtable_t {
  uint32_t abi_version;
  uint32_t struct_size;

  bool (*create_data_source)(
      void* ctx, PJ_string_view_t name, PJ_data_source_handle_t* out_source, PJ_error_t* out_error);

  bool (*ensure_topic)(
      void* ctx, PJ_data_source_handle_t source, PJ_string_view_t topic_name, PJ_topic_handle_t* out_topic,
      PJ_error_t* out_error);

  bool (*ensure_field)(
      void* ctx, PJ_topic_handle_t topic, PJ_string_view_t field_name, PJ_primitive_type_t type,
      PJ_field_handle_t* out_field, PJ_error_t* out_error);

  bool (*append_record)(
      void* ctx, PJ_topic_handle_t topic, int64_t timestamp, const PJ_named_field_value_t* fields, size_t field_count,
      PJ_error_t* out_error);

  bool (*append_bound_record)(
      void* ctx, PJ_topic_handle_t topic, int64_t timestamp, const PJ_bound_field_value_t* fields, size_t field_count,
      PJ_error_t* out_error);

  bool (*append_arrow_ipc)(
      void* ctx, PJ_topic_handle_t topic, PJ_bytes_view_t ipc_stream, PJ_string_view_t timestamp_column,
      PJ_error_t* out_error);

  bool (*acquire_catalog_snapshot)(void* ctx, PJ_catalog_snapshot_t* out_snapshot, PJ_error_t* out_error);

  bool (*read_series)(void* ctx, PJ_field_handle_t field, PJ_materialized_series_t* out_series, PJ_error_t* out_error);
} PJ_toolbox_host_vtable_t;

typedef struct {
  void* ctx;
  const PJ_toolbox_host_vtable_t* vtable;
} PJ_toolbox_host_t;

/**
 * Colormap registry service (v3).
 *
 * Independent host-provided service for toolbox plugins that want to
 * publish named colormap callbacks.
 */
typedef struct PJ_colormap_registry_vtable_t {
  uint32_t protocol_version;
  uint32_t struct_size;

  bool (*register_map)(
      void* ctx, PJ_string_view_t name, const char* (*eval_fn)(double value, void* user_ctx), void* user_ctx,
      PJ_error_t* out_error);

  bool (*unregister_map)(void* ctx, PJ_string_view_t name, PJ_error_t* out_error);
} PJ_colormap_registry_vtable_t;

typedef struct {
  void* ctx;
  const PJ_colormap_registry_vtable_t* vtable;
} PJ_colormap_registry_t;

#ifdef __cplusplus
}
#endif

#endif
