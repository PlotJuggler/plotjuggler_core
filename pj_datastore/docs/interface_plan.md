# PlotJuggler Host Data Interface Plan

## 1. Purpose

This document defines the first concrete implementation target for the new plugin system: the host-side read/write interfaces that plugins need from `pj_datastore`.

It is intentionally written from the plugin-facing point of view and then mapped back onto the current datastore implementation.

This is **not**:

- a plugin discovery/loading plan
- a UI/dialog protocol plan
- a save/load protocol plan
- a DAG/derived-function plan
- a full `pj_datamodel` specification

Those topics are intentionally deferred.

## 2. Scope of This Document

This document defines only:

- the shared host write interface used by `DataSource`, `MessageParser`, and `Toolbox`
- the logical host read interface used by `Toolbox`
- the identity handles needed by those interfaces
- ownership, lifetime, and error rules for those interfaces
- the gap between the desired interface and the current `pj_datastore` APIs

This document does **not** define:

- runtime event transport
- save/load wire shape
- destructive update API shape
- exact `data_source/topic/field` semantics beyond what is needed for the interface
- detailed timestamp/null/schema rules beyond what must be enforced by the interface

## 3. Decisions Locked by Requirements

The following are already considered fixed inputs:

- `DataSource`, `MessageParser`, and `Toolbox` share one logical write contract.
- The write contract must support:
  - incremental logical writes
  - bulk writes via serialized Arrow IPC
- `Toolbox` is the only plugin family that needs read access.
- Bulk Arrow read/export is not required for plugins in v1.
- `DataSource` is write-only and maps 1:1 to one application-level `data_source`.
- `MessageParser` is write-only and maps 1:1 to one `(data_source, topic)` pair.
- `Toolbox` can read existing data, write into existing data sources, and create new data sources.
- Plugins must not touch `PlotDataMapRef` or concrete datastore classes directly.
- No exceptions cross the plugin boundary.

## 4. Public Host Data API to Build

The new plugin-facing host data API should be introduced as a separate high-level surface on top of `pj_datastore`, not by exposing the existing `DataWriter` / `DataReader` objects directly.

Recommended public header split:

- `pj_datastore/include/pj_datastore/plugin_host_types.hpp`
- `pj_datastore/include/pj_datastore/plugin_host_write.hpp`
- `pj_datastore/include/pj_datastore/plugin_host_read.hpp`

These headers define the stable host service layer that the future plugin runtime will call.

Important scope note:

- This document defines the datastore-facing host services, not the family-specific plugin runtime bindings.
- `createDataSource()` is a capability of the host write service, but it is not meant to be exposed equally to every plugin family.
- The plugin runtime must inject the initial binding handles:
  - `DataSource` starts already bound to one `DataSourceHandle`
  - `MessageParser` starts already bound to one `(DataSourceHandle, TopicHandle)` pair
  - `Toolbox` receives the full read/write capability set, including `createDataSource()`
- The restricted family-specific surfaces belong to the plugin runtime plan, not this document.

## 5. Core Handle Model

### 5.1 Handle Identity

Use explicit host-managed handles:

```cpp
struct DataSourceHandle {
  PJ::DatasetId id = 0;
};

struct TopicHandle {
  PJ::TopicId id = 0;
};

struct FieldHandle {
  TopicHandle topic;
  PJ::FieldId id = 0;
};
```

### 5.2 Mapping to Current `pj_datastore`

- `DataSourceHandle.id` maps to the current `DatasetId`
- `TopicHandle.id` maps to the current `TopicId`
- `FieldHandle.id` maps to the current field/column index within one topic

`FieldHandle` must carry `TopicHandle` because the current field identifier is not globally unique by itself.

### 5.3 Handle Rules

- Handles are created and owned by the host.
- A successful handle remains valid for the lifetime of the underlying object.
- Handles are cheap value types and may be cached by the caller.
- Passing a handle of the wrong kind or from the wrong topic/source is a runtime error.

## 6. Value and Type Model at the Interface Boundary

This interface plan needs a concrete value/type set even though the broader data model is specified elsewhere.

Use the following field types at this layer:

```cpp
enum class FieldType : uint8_t {
  kFloat32,
  kFloat64,
  kInt32,
  kInt64,
  kUint64,
  kBool,
  kString
};
```

Use one tagged value reference type:

```cpp
using ValueRef = std::variant<
    float,
    double,
    int32_t,
    int64_t,
    uint64_t,
    bool,
    std::string_view>;
```

Interface rules:

- Field type is fixed when the field is first created.
- Reusing the same field name with a different type is an error.
- Null is supported for every field type.
- Missing fields in a logical record are treated as absent/null by host semantics.

## 7. Write API

## 7.1 Structural Operations

The write service must provide idempotent structure creation helpers:

```cpp
class IPluginHostWrite {
public:
  [[nodiscard]] PJ::Expected<DataSourceHandle> createDataSource(std::string_view name);

  [[nodiscard]] PJ::Expected<TopicHandle> ensureTopic(
      DataSourceHandle source, std::string_view topic_name);

  [[nodiscard]] PJ::Expected<FieldHandle> ensureField(
      TopicHandle topic, std::string_view field_name, FieldType type);
};
```

Rules:

- `createDataSource()` creates a new writable source owned by the caller.
- `ensureTopic()` is idempotent within one `DataSourceHandle`.
- `ensureField()` is idempotent within one `TopicHandle`.
- `ensureField()` returns the existing field on same-type re-call.
- `ensureField()` fails on type mismatch.

## 7.2 Incremental Logical Writes

Use a record-oriented API, not direct access to row builders:

```cpp
struct NamedFieldValue {
  std::string_view name;
  FieldType type;
  bool is_null = false;
  ValueRef value;
};

struct BoundFieldValue {
  FieldHandle field;
  bool is_null = false;
  ValueRef value;
};

class IPluginHostWrite {
public:
  [[nodiscard]] PJ::Status appendRecord(
      TopicHandle topic, PJ::Timestamp timestamp, PJ::Span<const NamedFieldValue> fields);

  [[nodiscard]] PJ::Status appendRecordFast(
      TopicHandle topic, PJ::Timestamp timestamp, PJ::Span<const BoundFieldValue> fields);
};
```

Decision:

- `appendRecord()` is the canonical simple incremental write API.
- `appendRecordFast()` is the canonical high-rate incremental write API.

Rules:

- `appendRecord()` resolves or creates fields by name+type on demand.
- `appendRecordFast()` requires all fields to be pre-resolved.
- Both APIs write exactly one logical topic record at one timestamp.
- Both APIs allow sparse input: only present fields are passed in.
- `appendRecord()` is intentionally permissive and may create new fields on first sight. Callers that need strict schema control should pre-resolve fields and use `appendRecordFast()`.
- Duplicate field names/handles within one call are invalid.
- A field handle belonging to another topic is invalid.
- Callers do not manage chunking, flushing, or commit steps.

Implementation note:

- `appendRecord()` should be implemented over current `DataWriter` row append APIs.
- The service may buffer or normalize one logical record before calling the lower-level writer.
- Current `DataWriter::ensureColumn()` and `resolveField()` are the main low-level building blocks.

## 7.3 Bulk Arrow IPC Writes

Bulk writes are a separate first-class mode:

```cpp
class IPluginHostWrite {
public:
  [[nodiscard]] PJ::Status appendArrowIpc(
      TopicHandle topic,
      PJ::Span<const uint8_t> ipc_stream,
      std::string_view timestamp_column = "_timestamp");
};
```

Decision:

- One Arrow IPC write call targets exactly one logical topic.
- The timestamp column is required and is identified by name.
- All non-timestamp columns map to field names by exact column name.

Rules:

- Missing timestamp column is an error.
- Multiple columns with the same field name are invalid.
- Unsupported Arrow types are an error.
- Existing fields must match type.
- Missing fields in a batch are allowed.
- New fields may be created from Arrow column names on demand.

Implementation note:

- Current `arrow_import.cpp` already contains most of the decoding machinery.
- It needs a higher-level wrapper that:
  - looks up the timestamp column by name
  - resolves or creates fields by column name
  - hides the current schema/mapping preparation details from the caller

## 7.4 Visibility and Commit Rule

The plugin-facing write service must hide the current explicit `flush()` / `commitChunks()` sequence.

Decision:

- Successful write calls transfer ownership of the data to the host service.
- Plugins do not call `flush()`, `flushAll()`, or `commitChunks()` directly.
- The host service is responsible for batching/flush/commit internally.

This is necessary to prevent the current datastore commit mechanics from leaking into the plugin boundary.

## 8. Read API

## 8.1 Catalog Enumeration

The read API needs one lightweight enumeration surface over the whole source/topic/field tree.

Use an index-based view:

```cpp
struct DataSourceInfoView {
  DataSourceHandle handle;
  std::string_view name;
  uint32_t first_topic = 0;
  uint32_t topic_count = 0;
};

struct TopicInfoView {
  TopicHandle handle;
  DataSourceHandle source;
  std::string_view name;
  uint32_t first_field = 0;
  uint32_t field_count = 0;
};

struct FieldInfoView {
  FieldHandle handle;
  std::string_view name;
  FieldType type;
};

struct CatalogView {
  PJ::Span<const DataSourceInfoView> data_sources;
  PJ::Span<const TopicInfoView> topics;
  PJ::Span<const FieldInfoView> fields;
};

class IPluginHostRead {
public:
  [[nodiscard]] CatalogView catalogView() const;
};
```

Rules:

- `catalogView()` is a borrowed, read-only view owned by the host.
- It is valid until the next structural catalog mutation or service destruction.
- A toolbox that performs writes which may create or delete data sources, topics, or fields must reacquire the catalog view before continuing to use it.
- Appending sample data to already-existing fields does not by itself invalidate the catalog view.
- No sample data is materialized by this call.

## 8.2 Materialized Field Read

Toolboxes need one-field-at-a-time logical reads.

Use:

```cpp
struct BoolSeriesValues {
  std::vector<uint8_t> values;
};

struct StringSeriesValues {
  std::vector<uint32_t> offsets;
  std::vector<char> bytes;
};

struct MaterializedSeries {
  DataSourceHandle source;
  TopicHandle topic;
  FieldHandle field;
  FieldType type;

  std::vector<PJ::Timestamp> timestamps;
  std::variant<
      std::vector<float>,
      std::vector<double>,
      std::vector<int32_t>,
      std::vector<int64_t>,
      std::vector<uint64_t>,
      BoolSeriesValues,
      StringSeriesValues> values;
  std::vector<uint8_t> validity_bits;
};

class IPluginHostRead {
public:
  [[nodiscard]] PJ::Expected<MaterializedSeries> readSeries(FieldHandle field) const;
};
```

Decisions:

- `readSeries()` returns the full retained history of one field.
- Returned sample data is materialized and caller-owned.
- `FieldType::kBool` materializes as `BoolSeriesValues`.
- `FieldType::kString` materializes as `StringSeriesValues`, not as `std::vector<std::string>`.

Rules:

- `readSeries()` is valid only for `Toolbox`.
- Requesting an unknown field is an error.
- Requesting a field handle with the wrong topic is an error.

Implementation note:

- This should be implemented on top of `DataReader`, `RangeCursor`, and `TopicChunk` decode helpers.
- The first version does not need range-bounded reads in this interface.

## 9. Error Model

Use the existing `PJ::Status` / `PJ::Expected<T>` style at this host interface layer.

Required error cases:

- unknown data source/topic/field handle
- type mismatch on field re-creation
- invalid handle/topic combinations
- duplicate field entries in one logical record
- unsupported Arrow type in bulk write
- missing timestamp column in Arrow IPC write
- malformed Arrow IPC stream
- read call on unknown field

No exceptions are allowed to escape this layer.

## 10. What Changes in `pj_datastore`

The desired host data API does not exist yet. The current datastore already provides useful lower-level pieces, but not the plugin-facing service layer.

### 10.1 Existing pieces that can be reused

- `DataEngine::createDataset()` for `createDataSource()`
- `DataWriter::registerTopic()` as the low-level topic creation primitive
- `DataWriter::ensureColumn()` / `resolveField()` as low-level field helpers
- `DataWriter` row append methods as the low-level engine write path
- `arrow_import.cpp` as the low-level Arrow IPC decoding path
- `DataReader::listDatasets()` / `listTopics()` / `getMetadata()` as catalog inputs
- `DataReader` + query/chunk decode logic as the basis for `readSeries()`

### 10.2 Missing pieces to add

- idempotent `ensureTopic()` by `(data_source, topic_name)`
- high-level `appendRecord()` and `appendRecordFast()`
- host-owned hidden commit/flush behavior
- field-handle-aware write wrappers
- host-facing `appendArrowIpc()` with timestamp-column-by-name behavior
- a catalog view builder spanning sources, topics, and fields
- full-history `readSeries()` materialization for one field

### 10.3 Current API shape to hide from plugins

The following current concepts must stay below the plugin-facing boundary:

- explicit `flush()` / `flushAll()`
- explicit `commitChunks()`
- chunk sizes and sealing rules
- `TopicChunkBuilder`
- `ColumnData`
- `arrow_import` schema/mapping preparation details
- physical column layout and encoding details

## 11. Recommended Implementation Sequence

1. Add the new public host-facing types and service interfaces under `pj_datastore/include/pj_datastore/`.
2. Implement a write-service adapter over the current `DataEngine` + `DataWriter`.
3. Implement a read-service adapter over the current `DataReader` + query/chunk decode layer.
4. Add tests for the new high-level services before any plugin runtime work starts.
5. Only after these services exist, plan the plugin runtime layer that will call them.

## 12. Acceptance Criteria

This interface plan is implemented correctly when:

- a `DataSource` can create one host `data_source`, ensure topics/fields, and append sparse logical records without touching `DataWriter` directly
- a `MessageParser` can append records into its bound topic using only the high-level write service
- a caller can append one topic batch via Arrow IPC without manually building column mappings
- a `Toolbox` can enumerate the source/topic/field tree and materialize one field timeseries
- plugin code never needs direct access to `DataWriter`, `DataReader`, `PlotDataMapRef`, or chunk internals
