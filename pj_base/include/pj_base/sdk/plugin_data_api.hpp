#pragma once

#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "pj_base/expected.hpp"
#include "pj_base/plugin_data_api.h"
#include "pj_base/span.hpp"
#include "pj_base/type_tree.hpp"
#include "pj_base/types.hpp"

namespace PJ::sdk {

using DataSourceHandle = PJ_data_source_handle_t;
using TopicHandle = PJ_topic_handle_t;
using FieldHandle = PJ_field_handle_t;

[[nodiscard]] inline PJ_primitive_type_t toAbiType(PrimitiveType type) {
  return static_cast<PJ_primitive_type_t>(type);
}

[[nodiscard]] inline PrimitiveType fromAbiType(PJ_primitive_type_t type) {
  return static_cast<PrimitiveType>(type);
}

inline bool operator==(DataSourceHandle a, DataSourceHandle b) {
  return a.id == b.id;
}

inline bool operator==(TopicHandle a, TopicHandle b) {
  return a.id == b.id;
}

inline bool operator==(FieldHandle a, FieldHandle b) {
  return a.topic == b.topic && a.id == b.id;
}

inline bool operator!=(DataSourceHandle a, DataSourceHandle b) {
  return !(a == b);
}

inline bool operator!=(TopicHandle a, TopicHandle b) {
  return !(a == b);
}

inline bool operator!=(FieldHandle a, FieldHandle b) {
  return !(a == b);
}

/// Typed null — a null value with an explicit column type. Use this when the
/// schema defines a field's type but the value is absent (e.g., an optional
/// field in ROS/Protobuf that is not set in a particular message). Unlike
/// `kNull` (untyped), a `TypedNull` can create a new column even on first use.
struct TypedNull {
  PrimitiveType type;
};

using ValueRef = std::variant<
    NullValue, TypedNull, float, double, int8_t, int16_t, int32_t, int64_t, uint8_t, uint16_t, uint32_t, uint64_t, bool,
    std::string_view>;

/// Returns true if the value is null (either untyped kNull or TypedNull).
[[nodiscard]] inline bool isNull(const ValueRef& v) {
  return std::holds_alternative<NullValue>(v) || std::holds_alternative<TypedNull>(v);
}

struct NamedFieldValue {
  std::string name;  // owned — safe from dangling string_view references
  ValueRef value;
};

struct BoundFieldValue {
  FieldHandle field;
  ValueRef value;
};

class CatalogSnapshot {
 public:
  CatalogSnapshot() = default;
  explicit CatalogSnapshot(PJ_catalog_snapshot_t raw) : raw_(raw) {}
  ~CatalogSnapshot() {
    reset();
  }

  CatalogSnapshot(const CatalogSnapshot&) = delete;
  CatalogSnapshot& operator=(const CatalogSnapshot&) = delete;

  CatalogSnapshot(CatalogSnapshot&& other) noexcept : raw_(other.release()) {}

  CatalogSnapshot& operator=(CatalogSnapshot&& other) noexcept {
    if (this != &other) {
      reset();
      raw_ = other.release();
    }
    return *this;
  }

  [[nodiscard]] Span<const PJ_data_source_info_t> dataSources() const {
    return Span<const PJ_data_source_info_t>(raw_.data_sources, raw_.data_source_count);
  }

  [[nodiscard]] Span<const PJ_topic_info_t> topics() const {
    return Span<const PJ_topic_info_t>(raw_.topics, raw_.topic_count);
  }

  [[nodiscard]] Span<const PJ_field_info_t> fields() const {
    return Span<const PJ_field_info_t>(raw_.fields, raw_.field_count);
  }

 private:
  PJ_catalog_snapshot_t raw_{};

  [[nodiscard]] PJ_catalog_snapshot_t release() noexcept {
    auto raw = raw_;
    raw_ = {};
    return raw;
  }

  void reset() {
    if (raw_.release != nullptr) {
      raw_.release(raw_.release_ctx);
      raw_ = {};
    }
  }
};

[[nodiscard]] inline std::string_view toStringView(PJ_string_view_t view) {
  return std::string_view(view.data == nullptr ? "" : view.data, view.size);
}

[[nodiscard]] inline PJ_string_view_t toAbiString(std::string_view view) {
  return PJ_string_view_t{view.data(), view.size()};
}

[[nodiscard]] inline PJ_bytes_view_t toAbiBytes(Span<const uint8_t> bytes) {
  return PJ_bytes_view_t{bytes.data(), bytes.size()};
}

[[nodiscard]] inline PrimitiveType typeOf(const ValueRef& value) {
  return std::visit(
      [](auto&& v) -> PrimitiveType {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, NullValue>) {
          return PrimitiveType::kUnspecified;
        } else if constexpr (std::is_same_v<T, TypedNull>) {
          return v.type;
        } else if constexpr (std::is_same_v<T, float>) {
          return PrimitiveType::kFloat32;
        } else if constexpr (std::is_same_v<T, double>) {
          return PrimitiveType::kFloat64;
        } else if constexpr (std::is_same_v<T, int8_t>) {
          return PrimitiveType::kInt8;
        } else if constexpr (std::is_same_v<T, int16_t>) {
          return PrimitiveType::kInt16;
        } else if constexpr (std::is_same_v<T, int32_t>) {
          return PrimitiveType::kInt32;
        } else if constexpr (std::is_same_v<T, int64_t>) {
          return PrimitiveType::kInt64;
        } else if constexpr (std::is_same_v<T, uint8_t>) {
          return PrimitiveType::kUint8;
        } else if constexpr (std::is_same_v<T, uint16_t>) {
          return PrimitiveType::kUint16;
        } else if constexpr (std::is_same_v<T, uint32_t>) {
          return PrimitiveType::kUint32;
        } else if constexpr (std::is_same_v<T, uint64_t>) {
          return PrimitiveType::kUint64;
        } else if constexpr (std::is_same_v<T, bool>) {
          return PrimitiveType::kBool;
        } else if constexpr (std::is_same_v<T, std::string_view>) {
          return PrimitiveType::kString;
        }
      },
      value);
}

[[nodiscard]] inline PJ_scalar_value_t toAbiScalar(const ValueRef& value) {
  PJ_scalar_value_t out{};
  out.type = toAbiType(typeOf(value));
  std::visit(
      [&](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, NullValue>) {
          // zeroed scalar — type is a safe default, value is unused
        } else if constexpr (std::is_same_v<T, TypedNull>) {
          // zeroed scalar — type is set above via typeOf(), value is unused
        } else if constexpr (std::is_same_v<T, float>) {
          out.data.as_float32 = v;
        } else if constexpr (std::is_same_v<T, double>) {
          out.data.as_float64 = v;
        } else if constexpr (std::is_same_v<T, int8_t>) {
          out.data.as_int8 = v;
        } else if constexpr (std::is_same_v<T, int16_t>) {
          out.data.as_int16 = v;
        } else if constexpr (std::is_same_v<T, int32_t>) {
          out.data.as_int32 = v;
        } else if constexpr (std::is_same_v<T, int64_t>) {
          out.data.as_int64 = v;
        } else if constexpr (std::is_same_v<T, uint8_t>) {
          out.data.as_uint8 = v;
        } else if constexpr (std::is_same_v<T, uint16_t>) {
          out.data.as_uint16 = v;
        } else if constexpr (std::is_same_v<T, uint32_t>) {
          out.data.as_uint32 = v;
        } else if constexpr (std::is_same_v<T, uint64_t>) {
          out.data.as_uint64 = v;
        } else if constexpr (std::is_same_v<T, bool>) {
          out.data.as_bool = v ? 1 : 0;
        } else if constexpr (std::is_same_v<T, std::string_view>) {
          out.data.as_string = toAbiString(v);
        }
      },
      value);
  return out;
}

// ---------------------------------------------------------------------------
// Write-host views (protocol v4)
//
// Three distinct typed views, one per plugin family, each wrapping its own
// ABI fat pointer. The host-side impl may share one backend across all
// three services — but at the ABI layer the types are distinct so the
// compiler enforces scope.
//
// Arrow C Data Interface is the canonical bulk path (appendArrowStream).
// Per-record helpers remain for streaming producers and simple plugins.
// The parser write host is strictly per-record — host coalesces internally.
// ---------------------------------------------------------------------------

// --- PJ_error_t helpers ------------------------------------------------------

/// Copy a string_view into a fixed-size null-terminated char buffer, truncating.
inline void setErrorField(char* dest, std::size_t dest_size, std::string_view src) {
  if (dest == nullptr || dest_size == 0) {
    return;
  }
  std::size_t n = src.size() < dest_size - 1 ? src.size() : dest_size - 1;
  std::memcpy(dest, src.data(), n);
  dest[n] = '\0';
}

/// Populate a PJ_error_t with code + domain + message. Safe on NULL pointer.
/// Clears the `extended` escape-hatch slots to prevent stale-pointer reuse.
inline void fillError(PJ_error_t* err, int32_t code, std::string_view domain, std::string_view message) {
  if (err == nullptr) {
    return;
  }
  err->code = code;
  setErrorField(err->domain, sizeof(err->domain), domain);
  setErrorField(err->message, sizeof(err->message), message);
  err->extended = nullptr;
  err->extended_kind[0] = '\0';
}

/// Attach a typed payload to an already-populated error. @p kind is a
/// reverse-DNS ID ("pj.error.cause.v1" etc); @p payload is valid for the
/// lifetime of the current ABI call window. Safe on NULL.
inline void setExtended(PJ_error_t* err, std::string_view kind, const void* payload) {
  if (err == nullptr) {
    return;
  }
  err->extended = payload;
  setErrorField(err->extended_kind, sizeof(err->extended_kind), kind);
}

/// Returns true if the error carries a typed extended payload.
[[nodiscard]] inline bool hasExtended(const PJ_error_t& err) {
  return err.extended_kind[0] != '\0' && err.extended != nullptr;
}

/// Convert a PJ_error_t into a human-readable string. Safe on zero-initialized.
[[nodiscard]] inline std::string errorToString(const PJ_error_t& err) {
  std::string out;
  if (err.domain[0] != '\0') {
    out.append(err.domain);
    out.append(": ");
  }
  if (err.message[0] != '\0') {
    out.append(err.message);
  }
  if (out.empty()) {
    out = "unspecified error";
  }
  return out;
}

/// Builds a PJ_named_field_value_t span from a C++ NamedFieldValue span.
[[nodiscard]] inline std::vector<PJ_named_field_value_t> toAbiNamed(Span<const NamedFieldValue> fields) {
  std::vector<PJ_named_field_value_t> raw;
  raw.reserve(fields.size());
  for (const auto& field : fields) {
    raw.push_back(
        PJ_named_field_value_t{
            .name = toAbiString(field.name),
            .is_null = isNull(field.value),
            .value = toAbiScalar(field.value),
        });
  }
  return raw;
}

[[nodiscard]] inline std::vector<PJ_bound_field_value_t> toAbiBound(Span<const BoundFieldValue> fields) {
  std::vector<PJ_bound_field_value_t> raw;
  raw.reserve(fields.size());
  for (const auto& field : fields) {
    raw.push_back(
        PJ_bound_field_value_t{
            .field = field.field,
            .is_null = isNull(field.value),
            .value = toAbiScalar(field.value),
        });
  }
  return raw;
}

/// View over PJ_source_write_host_t. Exposes multi-topic writes rooted on
/// a single data source.
class SourceWriteHostView {
 public:
  SourceWriteHostView() = default;
  explicit SourceWriteHostView(PJ_source_write_host_t host) : host_(host) {}

  [[nodiscard]] bool valid() const {
    return host_.ctx != nullptr && host_.vtable != nullptr;
  }

  [[nodiscard]] Expected<TopicHandle> ensureTopic(std::string_view topic_name) const {
    if (!valid()) {
      return unexpected("source write host is not bound");
    }
    TopicHandle handle{};
    PJ_error_t err{};
    if (!host_.vtable->ensure_topic(host_.ctx, toAbiString(topic_name), &handle, &err)) {
      return unexpected(errorToString(err));
    }
    return handle;
  }

  [[nodiscard]] Expected<FieldHandle> ensureField(
      TopicHandle topic, std::string_view field_name, PrimitiveType type) const {
    if (!valid()) {
      return unexpected("source write host is not bound");
    }
    FieldHandle handle{};
    PJ_error_t err{};
    if (!host_.vtable->ensure_field(host_.ctx, topic, toAbiString(field_name), toAbiType(type), &handle, &err)) {
      return unexpected(errorToString(err));
    }
    return handle;
  }

  [[nodiscard]] Status appendRecord(TopicHandle topic, Timestamp timestamp, Span<const NamedFieldValue> fields) const {
    if (!valid()) {
      return unexpected("source write host is not bound");
    }
    auto raw = toAbiNamed(fields);
    PJ_error_t err{};
    if (!host_.vtable->append_record(host_.ctx, topic, timestamp, raw.data(), raw.size(), &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  [[nodiscard]] Status appendBoundRecord(
      TopicHandle topic, Timestamp timestamp, Span<const BoundFieldValue> fields) const {
    if (!valid()) {
      return unexpected("source write host is not bound");
    }
    auto raw = toAbiBound(fields);
    PJ_error_t err{};
    if (!host_.vtable->append_bound_record(host_.ctx, topic, timestamp, raw.data(), raw.size(), &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  [[nodiscard]] Status appendRecord(
      TopicHandle topic, Timestamp timestamp, std::initializer_list<NamedFieldValue> fields) const {
    return appendRecord(topic, timestamp, Span<const NamedFieldValue>(fields.begin(), fields.size()));
  }

  [[nodiscard]] Status appendBoundRecord(
      TopicHandle topic, Timestamp timestamp, std::initializer_list<BoundFieldValue> fields) const {
    return appendBoundRecord(topic, timestamp, Span<const BoundFieldValue>(fields.begin(), fields.size()));
  }

  /// Hand an Arrow C Data Interface stream to the host for bulk ingest.
  ///
  /// Ownership: on success, the host takes ownership of @p stream — it pulls
  /// all batches via get_next and calls stream->release before returning.
  /// The plugin must NOT call release itself after a successful call.
  /// On failure (returns error), ownership is NOT transferred — the plugin
  /// retains responsibility for calling stream->release itself.
  ///
  /// @param timestamp_column Name of the int64 column in the stream's schema
  ///        whose values are nanoseconds since Unix epoch. Empty means use
  ///        a synthetic monotonic timestamp.
  [[nodiscard]] Status appendArrowStream(
      TopicHandle topic, struct ArrowArrayStream* stream, std::string_view timestamp_column = "timestamp") const {
    if (!valid()) {
      return unexpected("source write host is not bound");
    }
    PJ_error_t err{};
    if (!host_.vtable->append_arrow_stream(host_.ctx, topic, stream, toAbiString(timestamp_column), &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  [[nodiscard]] const PJ_source_write_host_t& raw() const noexcept {
    return host_;
  }

 private:
  PJ_source_write_host_t host_{};
};

/// View over PJ_parser_write_host_t. Single-topic: the topic is bound at
/// service-creation time by the host; the plugin never names it.
class ParserWriteHostView {
 public:
  ParserWriteHostView() = default;
  explicit ParserWriteHostView(PJ_parser_write_host_t host) : host_(host) {}

  [[nodiscard]] bool valid() const {
    return host_.ctx != nullptr && host_.vtable != nullptr;
  }

  [[nodiscard]] Expected<FieldHandle> ensureField(std::string_view field_name, PrimitiveType type) const {
    if (!valid()) {
      return unexpected("parser write host is not bound");
    }
    FieldHandle handle{};
    PJ_error_t err{};
    if (!host_.vtable->ensure_field(host_.ctx, toAbiString(field_name), toAbiType(type), &handle, &err)) {
      return unexpected(errorToString(err));
    }
    return handle;
  }

  [[nodiscard]] Status appendRecord(Timestamp timestamp, Span<const NamedFieldValue> fields) const {
    if (!valid()) {
      return unexpected("parser write host is not bound");
    }
    auto raw = toAbiNamed(fields);
    PJ_error_t err{};
    if (!host_.vtable->append_record(host_.ctx, timestamp, raw.data(), raw.size(), &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  [[nodiscard]] Status appendBoundRecord(Timestamp timestamp, Span<const BoundFieldValue> fields) const {
    if (!valid()) {
      return unexpected("parser write host is not bound");
    }
    auto raw = toAbiBound(fields);
    PJ_error_t err{};
    if (!host_.vtable->append_bound_record(host_.ctx, timestamp, raw.data(), raw.size(), &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  [[nodiscard]] Status appendRecord(Timestamp timestamp, std::initializer_list<NamedFieldValue> fields) const {
    return appendRecord(timestamp, Span<const NamedFieldValue>(fields.begin(), fields.size()));
  }

  [[nodiscard]] Status appendBoundRecord(Timestamp timestamp, std::initializer_list<BoundFieldValue> fields) const {
    return appendBoundRecord(timestamp, Span<const BoundFieldValue>(fields.begin(), fields.size()));
  }

  [[nodiscard]] const PJ_parser_write_host_t& raw() const noexcept {
    return host_;
  }

 private:
  PJ_parser_write_host_t host_{};
};

/// View over PJ_toolbox_host_t. Multi-source read+write + catalog.
class ToolboxHostView {
 public:
  ToolboxHostView() = default;
  explicit ToolboxHostView(PJ_toolbox_host_t host) : host_(host) {}

  [[nodiscard]] bool valid() const {
    return host_.ctx != nullptr && host_.vtable != nullptr;
  }

  [[nodiscard]] Expected<DataSourceHandle> createDataSource(std::string_view name) const {
    if (!valid()) {
      return unexpected("toolbox host is not bound");
    }
    DataSourceHandle handle{};
    PJ_error_t err{};
    if (!host_.vtable->create_data_source(host_.ctx, toAbiString(name), &handle, &err)) {
      return unexpected(errorToString(err));
    }
    return handle;
  }

  [[nodiscard]] Expected<TopicHandle> ensureTopic(DataSourceHandle source, std::string_view topic_name) const {
    if (!valid()) {
      return unexpected("toolbox host is not bound");
    }
    TopicHandle handle{};
    PJ_error_t err{};
    if (!host_.vtable->ensure_topic(host_.ctx, source, toAbiString(topic_name), &handle, &err)) {
      return unexpected(errorToString(err));
    }
    return handle;
  }

  [[nodiscard]] Expected<FieldHandle> ensureField(
      TopicHandle topic, std::string_view field_name, PrimitiveType type) const {
    if (!valid()) {
      return unexpected("toolbox host is not bound");
    }
    FieldHandle handle{};
    PJ_error_t err{};
    if (!host_.vtable->ensure_field(host_.ctx, topic, toAbiString(field_name), toAbiType(type), &handle, &err)) {
      return unexpected(errorToString(err));
    }
    return handle;
  }

  [[nodiscard]] Status appendRecord(TopicHandle topic, Timestamp timestamp, Span<const NamedFieldValue> fields) const {
    if (!valid()) {
      return unexpected("toolbox host is not bound");
    }
    auto raw = toAbiNamed(fields);
    PJ_error_t err{};
    if (!host_.vtable->append_record(host_.ctx, topic, timestamp, raw.data(), raw.size(), &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  [[nodiscard]] Status appendBoundRecord(
      TopicHandle topic, Timestamp timestamp, Span<const BoundFieldValue> fields) const {
    if (!valid()) {
      return unexpected("toolbox host is not bound");
    }
    auto raw = toAbiBound(fields);
    PJ_error_t err{};
    if (!host_.vtable->append_bound_record(host_.ctx, topic, timestamp, raw.data(), raw.size(), &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  [[nodiscard]] Status appendRecord(
      TopicHandle topic, Timestamp timestamp, std::initializer_list<NamedFieldValue> fields) const {
    return appendRecord(topic, timestamp, Span<const NamedFieldValue>(fields.begin(), fields.size()));
  }

  [[nodiscard]] Status appendBoundRecord(
      TopicHandle topic, Timestamp timestamp, std::initializer_list<BoundFieldValue> fields) const {
    return appendBoundRecord(topic, timestamp, Span<const BoundFieldValue>(fields.begin(), fields.size()));
  }

  /// Bulk-write via Arrow C Data Interface. Same ownership rule as
  /// SourceWriteHostView::appendArrowStream: success transfers ownership,
  /// failure retains it.
  [[nodiscard]] Status appendArrowStream(
      TopicHandle topic, struct ArrowArrayStream* stream, std::string_view timestamp_column = "timestamp") const {
    if (!valid()) {
      return unexpected("toolbox host is not bound");
    }
    PJ_error_t err{};
    if (!host_.vtable->append_arrow_stream(host_.ctx, topic, stream, toAbiString(timestamp_column), &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  [[nodiscard]] Expected<CatalogSnapshot> catalogSnapshot() const {
    if (!valid()) {
      return unexpected("toolbox host is not bound");
    }
    PJ_catalog_snapshot_t raw{};
    PJ_error_t err{};
    if (!host_.vtable->acquire_catalog_snapshot(host_.ctx, &raw, &err)) {
      return unexpected(errorToString(err));
    }
    return CatalogSnapshot(raw);
  }

  /// Read one field's time series into host-owned Arrow structs.
  ///
  /// The caller passes in zero-initialised @p out_schema and @p out_array;
  /// the host populates them (allocates buffers, sets release callbacks).
  /// On success the caller MUST invoke out_schema->release and
  /// out_array->release when done. The array has two columns:
  /// ["timestamp" (int64), <field_name> (typed)].
  [[nodiscard]] Status readSeriesArrow(
      FieldHandle field, struct ArrowSchema* out_schema, struct ArrowArray* out_array) const {
    if (!valid()) {
      return unexpected("toolbox host is not bound");
    }
    if (out_schema == nullptr || out_array == nullptr) {
      return unexpected("readSeriesArrow: out_schema and out_array must not be null");
    }
    PJ_error_t err{};
    if (!host_.vtable->read_series_arrow(host_.ctx, field, out_schema, out_array, &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  [[nodiscard]] const PJ_toolbox_host_t& raw() const noexcept {
    return host_;
  }

 private:
  PJ_toolbox_host_t host_{};
};

// ---------------------------------------------------------------------------
// ColorMapRegistryView — typed C++ view over PJ_colormap_registry_t
// ---------------------------------------------------------------------------

/// Signature of a color evaluation callback — mirrors the C ABI.
using ColorMapEvalFn = const char* (*)(double value, void* user_ctx);

/// C++ wrapper around PJ_colormap_registry_t for plugins that publish
/// colormaps. Constructed from the fat pointer delivered via
/// `bind_colormap_registry`. Empty-constructible; `valid()` tells whether
/// the host exposed a registry.
class ColorMapRegistryView {
 public:
  ColorMapRegistryView() = default;
  explicit ColorMapRegistryView(PJ_colormap_registry_t registry) : registry_(registry) {}

  [[nodiscard]] bool valid() const noexcept {
    return registry_.vtable != nullptr && registry_.ctx != nullptr;
  }

  /// Register (or replace) a named colormap. The new entry becomes active.
  [[nodiscard]] Status registerMap(std::string_view name, ColorMapEvalFn eval_fn, void* user_ctx) const {
    if (!valid() || registry_.vtable->register_map == nullptr) {
      return unexpected("colormap registry is not bound");
    }
    PJ_error_t err{};
    if (!registry_.vtable->register_map(registry_.ctx, toAbiString(name), eval_fn, user_ctx, &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  /// Unregister a colormap by name. Clears the active selection if it matched.
  [[nodiscard]] Status unregisterMap(std::string_view name) const {
    if (!valid() || registry_.vtable->unregister_map == nullptr) {
      return unexpected("colormap registry is not bound");
    }
    PJ_error_t err{};
    if (!registry_.vtable->unregister_map(registry_.ctx, toAbiString(name), &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

 private:
  PJ_colormap_registry_t registry_{};
};

}  // namespace PJ::sdk
