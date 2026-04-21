/**
 * @file toolbox_protocol.h
 * @brief C ABI protocol for Toolbox plugins (version 3).
 *
 * v3 summary of changes vs v1:
 *   - Single `bind(ctx, registry, err)` replaces bind_toolbox_host +
 *     bind_runtime_host + bind_colormap_registry. Plugins acquire services
 *     from the registry under canonical names ("pj.toolbox_write.v1",
 *     "pj.toolbox_runtime.v1", and optional "pj.colormap.v1").
 *   - All fallible calls take a PJ_error_t* out-parameter. No more
 *     get_last_error slot on the plugin vtable.
 *   - get_dialog_context (void*) replaced by get_dialog returning a typed
 *     PJ_borrowed_dialog_t fat pointer.
 */
#ifndef PJ_TOOLBOX_PROTOCOL_H
#define PJ_TOOLBOX_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pj_base/plugin_data_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Protocol version. Host and plugin must agree on the same major version. */
#define PJ_TOOLBOX_PLUGIN_PROTOCOL_VERSION 3

/**
 * Minimum vtable size for v3.0 compatibility, pinned at v3.0 release.
 *
 * Loaders reject plugins whose `struct_size < PJ_TOOLBOX_MIN_VTABLE_SIZE`.
 * MUST NOT GROW when new tail slots are appended. See PJ_ABI_VERSION comment
 * in plugin_data_api.h for the rationale.
 *
 * Last v3.0 slot is `on_data_changed`.
 */
#define PJ_TOOLBOX_MIN_VTABLE_SIZE (offsetof(PJ_toolbox_vtable_t, on_data_changed) + sizeof(void (*)(void*)))

#if defined(_WIN32)
#define PJ_TOOLBOX_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define PJ_TOOLBOX_EXPORT __attribute__((visibility("default")))
#else
#define PJ_TOOLBOX_EXPORT
#endif

/** Severity level for plugin-to-host diagnostic messages. */
typedef enum {
  PJ_TOOLBOX_MESSAGE_INFO = 0,
  PJ_TOOLBOX_MESSAGE_WARNING = 1,
  PJ_TOOLBOX_MESSAGE_ERROR = 2,
} PJ_toolbox_message_level_t;

enum {
  PJ_TOOLBOX_CAPABILITY_HAS_DIALOG = 1ull << 0,
  PJ_TOOLBOX_CAPABILITY_NON_MODAL_DIALOG = 1ull << 1,
};

/**
 * Toolbox runtime host vtable — control-plane callbacks, delivered as the
 * "pj.toolbox_runtime.v1" service.
 */
typedef struct PJ_toolbox_runtime_host_vtable_t {
  uint32_t protocol_version;
  uint32_t struct_size;

  void (*report_message)(void* ctx, PJ_toolbox_message_level_t level, PJ_string_view_t message);

  /** Notify the host that data has been modified; host refreshes UI. */
  void (*notify_data_changed)(void* ctx);
} PJ_toolbox_runtime_host_vtable_t;

typedef struct {
  void* ctx;
  const PJ_toolbox_runtime_host_vtable_t* vtable;
} PJ_toolbox_runtime_host_t;

/**
 * Toolbox plugin vtable (v3).
 *
 * Typical lifecycle: create -> bind(registry) -> load_config (optional)
 *                    -> [user interacts] -> save_config -> destroy.
 */
typedef struct PJ_toolbox_vtable_t {
  uint32_t protocol_version;
  uint32_t struct_size;

  void* (*create)(void);
  void (*destroy)(void* ctx);

  const char* manifest_json;
  uint64_t (*capabilities)(void* ctx);

  /**
   * Bind host services. The host registers at least "pj.toolbox_write.v1"
   * and "pj.toolbox_runtime.v1"; optional services such as "pj.colormap.v1"
   * may also be present.
   */
  bool (*bind)(void* ctx, PJ_service_registry_t registry, PJ_error_t* out_error);

  bool (*save_config)(void* ctx, PJ_string_view_t* out_json, PJ_error_t* out_error);
  bool (*load_config)(void* ctx, PJ_string_view_t config_json, PJ_error_t* out_error);

  /**
   * Return a typed borrowed reference to this toolbox's dialog. The host
   * must NOT call the dialog vtable's create() or destroy() on a borrowed
   * handle. Returns {NULL, NULL} if this toolbox has no dialog.
   */
  PJ_borrowed_dialog_t (*get_dialog)(void* ctx);

  /** Notify the plugin that new records have been appended to the datastore. */
  void (*on_data_changed)(void* ctx);

  /* ====================================================================
   * Tail slots beyond here are OPTIONAL. Host reads MUST check both
   * struct_size and slot-nullability via PJ_HAS_TAIL_SLOT.
   * ==================================================================== */

  /** Query a plugin-exposed extension by reverse-DNS id. See
   *  PJ_data_source_vtable_t::get_plugin_extension for the full contract. */
  const void* (*get_plugin_extension)(void* ctx, PJ_string_view_t id);
} PJ_toolbox_vtable_t;
/* The vtable above is ABI-APPENDABLE: new slots may be added at the tail;
 * host reads guard with PJ_HAS_TAIL_SLOT. See PJ_TOOLBOX_MIN_VTABLE_SIZE. */

typedef const PJ_toolbox_vtable_t* (*PJ_get_toolbox_vtable_fn)(void);

#ifdef __cplusplus
}
#endif

#endif  // PJ_TOOLBOX_PROTOCOL_H
