/**
 * @file data_source_plugin_base.hpp
 * @brief C++ SDK for implementing DataSource plugins.
 *
 * Plugin authors subclass DataSourcePluginBase, override the required virtuals,
 * and export with the PJ_DATA_SOURCE_PLUGIN(ClassName) macro. The SDK handles
 * C ABI trampoline generation and exception safety.
 *
 * See pj_plugins/examples/mock_data_source.cpp for a complete example.
 */
#pragma once

#include <exception>
#include <string>
#include <string_view>

#include "pj_base/data_source_protocol.h"
#include "pj_base/expected.hpp"
#include "pj_base/sdk/plugin_data_api.hpp"

namespace PJ {

/// C++ mirror of PJ_data_source_state_t.
enum class DataSourceState : uint32_t {
  kIdle = PJ_DATA_SOURCE_STATE_IDLE,
  kConfiguring = PJ_DATA_SOURCE_STATE_CONFIGURING,
  kStarting = PJ_DATA_SOURCE_STATE_STARTING,
  kRunning = PJ_DATA_SOURCE_STATE_RUNNING,
  kPaused = PJ_DATA_SOURCE_STATE_PAUSED,
  kStopping = PJ_DATA_SOURCE_STATE_STOPPING,
  kStopped = PJ_DATA_SOURCE_STATE_STOPPED,
  kFailed = PJ_DATA_SOURCE_STATE_FAILED,
};

/// Severity level for plugin-to-host diagnostic messages.
enum class DataSourceMessageLevel : uint32_t {
  kInfo = PJ_DATA_SOURCE_MESSAGE_INFO,
  kWarning = PJ_DATA_SOURCE_MESSAGE_WARNING,
  kError = PJ_DATA_SOURCE_MESSAGE_ERROR,
};

/// @name Capability flag constants
/// @{
constexpr uint64_t kCapabilityFiniteImport = PJ_DATA_SOURCE_CAPABILITY_FINITE_IMPORT;
constexpr uint64_t kCapabilityContinuousStream = PJ_DATA_SOURCE_CAPABILITY_CONTINUOUS_STREAM;
constexpr uint64_t kCapabilityDirectIngest = PJ_DATA_SOURCE_CAPABILITY_DIRECT_INGEST;
constexpr uint64_t kCapabilityDelegatedIngest = PJ_DATA_SOURCE_CAPABILITY_DELEGATED_INGEST;
constexpr uint64_t kCapabilitySupportsPause = PJ_DATA_SOURCE_CAPABILITY_SUPPORTS_PAUSE;
constexpr uint64_t kCapabilityHasDialog = PJ_DATA_SOURCE_CAPABILITY_HAS_DIALOG;
/// @}

using ParserBindingHandle = PJ_parser_binding_handle_t;

/// C++ mirror of PJ_parser_binding_request_t for delegated-ingest parser lookup.
struct ParserBindingRequest {
  std::string_view topic_name;
  std::string_view parser_encoding;
  std::string_view type_name;
  Span<const uint8_t> schema;
  std::string_view parser_config_json;
};

/**
 * Type-safe C++ view over the runtime host vtable.
 *
 * Plugins access this via DataSourcePluginBase::runtimeHost(). Each method
 * is a null-safe wrapper: calls on an unbound host are no-ops or return
 * safe defaults. This is the control-plane counterpart to
 * sdk::SourceWriteHostView (data plane).
 */
class DataSourceRuntimeHostView {
 public:
  explicit DataSourceRuntimeHostView(PJ_data_source_runtime_host_t host = {}) : host_(host) {}

  /// Returns true if both context and vtable pointers are set.
  [[nodiscard]] bool valid() const {
    return host_.ctx != nullptr && host_.vtable != nullptr;
  }

  /// Returns the last host-side error, or empty if none.
  [[nodiscard]] std::string_view lastError() const {
    if (!valid() || host_.vtable->get_last_error == nullptr) {
      return {};
    }
    const char* err = host_.vtable->get_last_error(host_.ctx);
    return err == nullptr ? std::string_view{} : std::string_view(err);
  }

  /// Send a diagnostic message to the host UI log.
  void reportMessage(DataSourceMessageLevel level, std::string_view message) const {
    if (valid() && host_.vtable->report_message != nullptr) {
      host_.vtable->report_message(
          host_.ctx,
          static_cast<PJ_data_source_message_level_t>(level),
          sdk::toAbiString(message));
    }
  }

  /// Begin a progress bar with @p total_steps. Set @p cancellable to allow user abort.
  [[nodiscard]] bool progressStart(
      std::string_view label, uint64_t total_steps, bool cancellable) const {
    if (!valid() || host_.vtable->progress_start == nullptr) {
      return false;
    }
    return host_.vtable->progress_start(
        host_.ctx, sdk::toAbiString(label), total_steps, cancellable);
  }

  /// Advance progress. Returns false if the user cancelled.
  [[nodiscard]] bool progressUpdate(uint64_t current_step) const {
    if (!valid() || host_.vtable->progress_update == nullptr) {
      return false;
    }
    return host_.vtable->progress_update(host_.ctx, current_step);
  }

  /// End the current progress sequence.
  void progressFinish() const {
    if (valid() && host_.vtable->progress_finish != nullptr) {
      host_.vtable->progress_finish(host_.ctx);
    }
  }

  /// Check whether the host has requested the plugin to stop.
  [[nodiscard]] bool isStopRequested() const {
    if (!valid() || host_.vtable->is_stop_requested == nullptr) {
      return false;
    }
    return host_.vtable->is_stop_requested(host_.ctx);
  }

  /// Inform the host that the plugin has transitioned to @p state.
  void notifyState(DataSourceState state) const {
    if (valid() && host_.vtable->notify_state != nullptr) {
      host_.vtable->notify_state(host_.ctx, static_cast<PJ_data_source_state_t>(state));
    }
  }

  /// Plugin-initiated stop. @p terminal_state should be kStopped or kFailed.
  void requestStop(DataSourceState terminal_state, std::string_view reason) const {
    if (valid() && host_.vtable->request_stop != nullptr) {
      host_.vtable->request_stop(
          host_.ctx, static_cast<PJ_data_source_state_t>(terminal_state), sdk::toAbiString(reason));
    }
  }

  /// Bind (or look up) a parser for delegated ingest. Returns the handle on success.
  [[nodiscard]] Expected<ParserBindingHandle> ensureParserBinding(
      const ParserBindingRequest& request) const {
    if (!valid() || host_.vtable->ensure_parser_binding == nullptr) {
      return unexpected(std::string("runtime host is not bound"));
    }

    PJ_parser_binding_request_t raw{
        .topic_name = sdk::toAbiString(request.topic_name),
        .parser_encoding = sdk::toAbiString(request.parser_encoding),
        .type_name = sdk::toAbiString(request.type_name),
        .schema = sdk::toAbiBytes(request.schema),
        .parser_config_json = sdk::toAbiString(request.parser_config_json),
    };

    ParserBindingHandle handle{};
    if (!host_.vtable->ensure_parser_binding(host_.ctx, &raw, &handle)) {
      return unexpected(std::string(lastError()));
    }
    return handle;
  }

  /// Push a raw message for host-side parsing via a previously obtained binding handle.
  [[nodiscard]] Status pushRawMessage(
      ParserBindingHandle handle, Timestamp host_timestamp_ns, Span<const uint8_t> payload) const {
    if (!valid() || host_.vtable->push_raw_message == nullptr) {
      return unexpected(std::string("runtime host is not bound"));
    }
    if (!host_.vtable->push_raw_message(
            host_.ctx, handle, host_timestamp_ns, sdk::toAbiBytes(payload))) {
      return unexpected(std::string(lastError()));
    }
    return okStatus();
  }

  /// Access the underlying C ABI struct.
  [[nodiscard]] const PJ_data_source_runtime_host_t& raw() const {
    return host_;
  }

 private:
  PJ_data_source_runtime_host_t host_{};
};

/**
 * Base class for DataSource plugins.
 *
 * Subclass and override the pure-virtual methods: manifest(), capabilities(),
 * start(), stop(), and currentState(). Optionally override pause/resume, poll,
 * saveConfig/loadConfig for richer behaviour.
 *
 * Use writeHost() and runtimeHost() (protected) to interact with the host
 * during start() and poll(). Export with PJ_DATA_SOURCE_PLUGIN(YourClass).
 *
 * The base class generates C ABI trampolines with full exception safety —
 * any exception thrown from a virtual is caught, stored via setLastError(),
 * and converted to a false/null return across the ABI boundary.
 */
class DataSourcePluginBase {
 public:
  virtual ~DataSourcePluginBase() = default;

  /// Return a JSON manifest with at least "name" and "version" keys.
  virtual std::string manifest() const = 0;

  /// Return a bitmask of kCapability* flags describing this source's features.
  virtual uint64_t capabilities() const = 0;

  /// Bind the data-plane write host. Override only if you need custom validation.
  virtual bool bindWriteHost(PJ_source_write_host_t write_host) {
    if (write_host.ctx == nullptr || write_host.vtable == nullptr) {
      setLastError("write host is not bound");
      return false;
    }
    write_host_ = write_host;
    return true;
  }

  /// Bind the control-plane runtime host. Override only if you need custom validation.
  virtual bool bindRuntimeHost(PJ_data_source_runtime_host_t runtime_host) {
    if (runtime_host.ctx == nullptr || runtime_host.vtable == nullptr) {
      setLastError("runtime host is not bound");
      return false;
    }
    runtime_host_ = runtime_host;
    return true;
  }

  /// Serialize plugin configuration to JSON. Default returns "{}".
  virtual std::string saveConfig() const {
    return "{}";
  }

  /// Restore plugin configuration from JSON. Default accepts any input.
  virtual bool loadConfig(std::string_view config_json) {
    (void)config_json;
    return true;
  }

  /// Begin data acquisition. Hosts are already bound when this is called.
  virtual bool start() = 0;

  /// Stop data acquisition. Must be idempotent.
  virtual void stop() = 0;

  /// Pause a running source. Default returns false (unsupported).
  virtual bool pause() {
    setLastError("pause is not supported");
    return false;
  }

  /// Resume a paused source. Default returns false (unsupported).
  virtual bool resume() {
    setLastError("resume is not supported");
    return false;
  }

  /// Called periodically while running. Override for streaming sources. Default is no-op.
  virtual bool poll() {
    return true;
  }

  /// Return the current lifecycle state.
  virtual DataSourceState currentState() const = 0;

  /// Return the last error message. Override for custom error reporting.
  virtual std::string lastError() const {
    return last_error_;
  }

  template <typename CreateFn>
  static const PJ_data_source_vtable_t* vtableWithCreate(CreateFn create_fn) {
    static const PJ_data_source_vtable_t vt = {
        PJ_DATA_SOURCE_PROTOCOL_VERSION,
        sizeof(PJ_data_source_vtable_t),
        create_fn,
        trampoline_destroy,
        trampoline_get_manifest,
        trampoline_capabilities,
        trampoline_bind_write_host,
        trampoline_bind_runtime_host,
        trampoline_save_config,
        trampoline_load_config,
        trampoline_start,
        trampoline_stop,
        trampoline_pause,
        trampoline_resume,
        trampoline_poll,
        trampoline_current_state,
        trampoline_get_last_error,
    };
    return &vt;
  }

 protected:
  [[nodiscard]] bool writeHostBound() const {
    return write_host_.ctx != nullptr && write_host_.vtable != nullptr;
  }

  [[nodiscard]] bool runtimeHostBound() const {
    return runtime_host_.ctx != nullptr && runtime_host_.vtable != nullptr;
  }

  [[nodiscard]] sdk::SourceWriteHostView writeHost() const {
    return sdk::SourceWriteHostView(write_host_);
  }

  [[nodiscard]] DataSourceRuntimeHostView runtimeHost() const {
    return DataSourceRuntimeHostView(runtime_host_);
  }

  void setLastError(std::string error) {
    last_error_ = std::move(error);
  }

 private:
  PJ_source_write_host_t write_host_{};
  PJ_data_source_runtime_host_t runtime_host_{};
  std::string manifest_buf_;
  std::string config_buf_;
  mutable std::string last_error_;

  static void trampoline_destroy(void* ctx) {
    try {
      delete static_cast<DataSourcePluginBase*>(ctx);
    } catch (...) {
    }
  }

  static const char* trampoline_get_manifest(void* ctx) {
    auto* self = static_cast<DataSourcePluginBase*>(ctx);
    try {
      self->manifest_buf_ = self->manifest();
      return self->manifest_buf_.c_str();
    } catch (const std::exception& e) {
      self->last_error_ = e.what();
      return "{}";
    } catch (...) {
      self->last_error_ = "Unknown exception in get_manifest";
      return "{}";
    }
  }

  static uint64_t trampoline_capabilities(void* ctx) {
    auto* self = static_cast<DataSourcePluginBase*>(ctx);
    try {
      return self->capabilities();
    } catch (const std::exception& e) {
      self->last_error_ = e.what();
      return 0;
    } catch (...) {
      self->last_error_ = "Unknown exception in capabilities";
      return 0;
    }
  }

  static bool trampoline_bind_write_host(void* ctx, PJ_source_write_host_t write_host) {
    auto* self = static_cast<DataSourcePluginBase*>(ctx);
    try {
      return self->bindWriteHost(write_host);
    } catch (const std::exception& e) {
      self->last_error_ = e.what();
      return false;
    } catch (...) {
      self->last_error_ = "Unknown exception in bind_write_host";
      return false;
    }
  }

  static bool trampoline_bind_runtime_host(
      void* ctx, PJ_data_source_runtime_host_t runtime_host) {
    auto* self = static_cast<DataSourcePluginBase*>(ctx);
    try {
      return self->bindRuntimeHost(runtime_host);
    } catch (const std::exception& e) {
      self->last_error_ = e.what();
      return false;
    } catch (...) {
      self->last_error_ = "Unknown exception in bind_runtime_host";
      return false;
    }
  }

  static const char* trampoline_save_config(void* ctx) {
    auto* self = static_cast<DataSourcePluginBase*>(ctx);
    try {
      self->config_buf_ = self->saveConfig();
      return self->config_buf_.c_str();
    } catch (const std::exception& e) {
      self->last_error_ = e.what();
      return "{}";
    } catch (...) {
      self->last_error_ = "Unknown exception in save_config";
      return "{}";
    }
  }

  static bool trampoline_load_config(void* ctx, const char* config_json) {
    auto* self = static_cast<DataSourcePluginBase*>(ctx);
    try {
      return self->loadConfig(
          config_json == nullptr ? std::string_view{} : std::string_view(config_json));
    } catch (const std::exception& e) {
      self->last_error_ = e.what();
      return false;
    } catch (...) {
      self->last_error_ = "Unknown exception in load_config";
      return false;
    }
  }

  static bool trampoline_start(void* ctx) {
    auto* self = static_cast<DataSourcePluginBase*>(ctx);
    try {
      return self->start();
    } catch (const std::exception& e) {
      self->last_error_ = e.what();
      return false;
    } catch (...) {
      self->last_error_ = "Unknown exception in start";
      return false;
    }
  }

  static void trampoline_stop(void* ctx) {
    auto* self = static_cast<DataSourcePluginBase*>(ctx);
    try {
      self->stop();
    } catch (const std::exception& e) {
      self->last_error_ = e.what();
    } catch (...) {
      self->last_error_ = "Unknown exception in stop";
    }
  }

  static bool trampoline_pause(void* ctx) {
    auto* self = static_cast<DataSourcePluginBase*>(ctx);
    try {
      return self->pause();
    } catch (const std::exception& e) {
      self->last_error_ = e.what();
      return false;
    } catch (...) {
      self->last_error_ = "Unknown exception in pause";
      return false;
    }
  }

  static bool trampoline_resume(void* ctx) {
    auto* self = static_cast<DataSourcePluginBase*>(ctx);
    try {
      return self->resume();
    } catch (const std::exception& e) {
      self->last_error_ = e.what();
      return false;
    } catch (...) {
      self->last_error_ = "Unknown exception in resume";
      return false;
    }
  }

  static bool trampoline_poll(void* ctx) {
    auto* self = static_cast<DataSourcePluginBase*>(ctx);
    try {
      return self->poll();
    } catch (const std::exception& e) {
      self->last_error_ = e.what();
      return false;
    } catch (...) {
      self->last_error_ = "Unknown exception in poll";
      return false;
    }
  }

  static PJ_data_source_state_t trampoline_current_state(void* ctx) {
    auto* self = static_cast<DataSourcePluginBase*>(ctx);
    try {
      return static_cast<PJ_data_source_state_t>(self->currentState());
    } catch (const std::exception& e) {
      self->last_error_ = e.what();
      return PJ_DATA_SOURCE_STATE_FAILED;
    } catch (...) {
      self->last_error_ = "Unknown exception in current_state";
      return PJ_DATA_SOURCE_STATE_FAILED;
    }
  }

  static const char* trampoline_get_last_error(void* ctx) {
    auto* self = static_cast<DataSourcePluginBase*>(ctx);
    try {
      self->last_error_ = self->lastError();
    } catch (const std::exception& e) {
      self->last_error_ = e.what();
    } catch (...) {
      self->last_error_ = "Unknown exception in get_last_error";
    }
    return self->last_error_.empty() ? nullptr : self->last_error_.c_str();
  }
};

}  // namespace PJ

/**
 * Export a DataSourcePluginBase subclass as a shared-library plugin.
 *
 * Place at file scope (after the class definition). Generates the extern "C"
 * entry point `PJ_get_data_source_vtable` that the host resolves via dlsym.
 *
 * Usage:
 * @code
 *   PJ_DATA_SOURCE_PLUGIN(MyDataSource)
 * @endcode
 */
#define PJ_DATA_SOURCE_PLUGIN(ClassName)                                                         \
  extern "C" PJ_DATA_SOURCE_EXPORT const PJ_data_source_vtable_t* PJ_get_data_source_vtable() { \
    static const PJ_data_source_vtable_t* vt =                                                  \
        PJ::DataSourcePluginBase::vtableWithCreate([]() -> void* { return new ClassName(); });    \
    return vt;                                                                                   \
  }
