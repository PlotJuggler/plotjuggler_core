/**
 * @file message_parser_protocol.h
 * @brief C ABI protocol for MessageParser plugins (version 3).
 *
 * v3 summary of changes vs v1:
 *   - Single `bind(ctx, registry, err)` replaces `bind_write_host`. Plugins
 *     acquire services (including "pj.parser_write.v1") from the registry.
 *   - All fallible calls take a `PJ_error_t*` out-parameter. The
 *     plugin-level `get_last_error` slot is gone.
 *
 * The host obtains the plugin's vtable via `PJ_get_message_parser_vtable()`
 * and drives the plugin through: create -> bind(registry) ->
 * (bind_schema) -> parse* -> destroy.
 */
#ifndef PJ_MESSAGE_PARSER_PROTOCOL_H
#define PJ_MESSAGE_PARSER_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pj_base/plugin_data_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Protocol version. Host and plugin must agree on the same major version. */
#define PJ_MESSAGE_PARSER_PROTOCOL_VERSION 3

/**
 * Minimum vtable size for v3.0 compatibility, pinned at v3.0 release.
 *
 * Loaders reject plugins whose `struct_size < PJ_MESSAGE_PARSER_MIN_VTABLE_SIZE`.
 * MUST NOT GROW when new tail slots are appended. See PJ_ABI_VERSION comment
 * in plugin_data_api.h for the rationale.
 *
 * Last v3.0 slot is `parse`.
 */
#define PJ_MESSAGE_PARSER_MIN_VTABLE_SIZE \
  (offsetof(PJ_message_parser_vtable_t, parse) + sizeof(bool (*)(void*, int64_t, PJ_bytes_view_t, PJ_error_t*)))

#if defined(_WIN32)
#define PJ_MESSAGE_PARSER_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define PJ_MESSAGE_PARSER_EXPORT __attribute__((visibility("default")))
#else
#define PJ_MESSAGE_PARSER_EXPORT
#endif

/**
 * MessageParser plugin vtable (v3).
 *
 * Fallible slots take a `PJ_error_t* out_error`; callers may pass NULL
 * to discard error detail.
 */
typedef struct PJ_message_parser_vtable_t {
  uint32_t protocol_version; /**< Must equal PJ_MESSAGE_PARSER_PROTOCOL_VERSION. */
  uint32_t struct_size;      /**< sizeof(PJ_message_parser_vtable_t). */

  void* (*create)(void);
  void (*destroy)(void* ctx);

  /**
   * Static JSON manifest. Compile-time constant.
   *
   * Required keys:
   *   "name"     — human-readable plugin name (string).
   *   "version"  — semver version string (string).
   *   "encoding" — encoding this parser handles (string). The host uses
   *                this to match binding requests to parsers.
   */
  const char* manifest_json;

  /**
   * Bind host services. The host registers at least "pj.parser_write.v1".
   * Plugins that need extra services can query additional names.
   */
  bool (*bind)(void* ctx, PJ_service_registry_t registry, PJ_error_t* out_error);

  /**
   * Bind a message schema. Optional — parsers that don't require schema
   * (e.g. JSON) may accept and ignore this.
   */
  bool (*bind_schema)(void* ctx, PJ_string_view_t type_name, PJ_bytes_view_t schema, PJ_error_t* out_error);

  bool (*save_config)(void* ctx, PJ_string_view_t* out_json, PJ_error_t* out_error);
  bool (*load_config)(void* ctx, PJ_string_view_t config_json, PJ_error_t* out_error);

  /**
   * Parse one raw message into writes via the bound write host.
   * @p timestamp_ns is nanoseconds since the Unix epoch.
   */
  bool (*parse)(void* ctx, int64_t timestamp_ns, PJ_bytes_view_t payload, PJ_error_t* out_error);

  /* ====================================================================
   * Tail slots beyond here are OPTIONAL. Host reads MUST check both
   * struct_size and slot-nullability via PJ_HAS_TAIL_SLOT.
   * ==================================================================== */

  /** Query a plugin-exposed extension by reverse-DNS id. See
   *  PJ_data_source_vtable_t::get_plugin_extension for the full contract. */
  const void* (*get_plugin_extension)(void* ctx, PJ_string_view_t id);
} PJ_message_parser_vtable_t;
/* The vtable above is ABI-APPENDABLE: new slots may be added at the tail;
 * host reads guard with PJ_HAS_TAIL_SLOT. See PJ_MESSAGE_PARSER_MIN_VTABLE_SIZE. */

/** Signature of the exported entry point: `PJ_get_message_parser_vtable`. */
typedef const PJ_message_parser_vtable_t* (*PJ_get_message_parser_vtable_fn)(void);

#ifdef __cplusplus
}
#endif

#endif  // PJ_MESSAGE_PARSER_PROTOCOL_H
