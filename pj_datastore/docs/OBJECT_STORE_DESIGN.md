# ObjectStore Design

## 1. Purpose

A message-oriented storage engine that sits alongside the existing columnar
`DataEngine`. While `DataEngine` stores scalar time-series in typed columns
with adaptive encoding, `ObjectStore` stores timestamped opaque payloads
(images, point clouds, scene primitives, transforms, etc.) with lazy
on-demand resolution.

The two stores share the same `DatasetId` identity from `pj_base` but use
separate handle types (`TopicId` vs `ObjectTopicId`). They are independent
peer classes — the application owns both.

## 2. Motivation

PlotJuggler needs to handle data types that don't fit the columnar model:

- **Large blobs** (images, point clouds, grids) — variable-length,
  megabyte-sized payloads that would destroy columnar encoding assumptions.
- **Structured messages** (scene primitives, transforms, annotations) —
  small but semantically atomic; the message is the unit of meaning, not
  individual scalar fields.
- **Lazy decode** — for file-backed sources, storing a seek callback instead
  of copying raw bytes keeps memory flat regardless of dataset size.
- **Streaming capture** — for live sources, raw compressed bytes must be
  buffered in memory since the source data is ephemeral.

Some fields from these messages (e.g., a transform's translation.x) may
also be duplicated into the columnar `DataEngine` for plotting. This
duplication is managed by the DataSource plugin — the ObjectStore does not
coordinate with the DataEngine.

## 3. Design Decisions

### 3.1 Separate from DataEngine (Option B)

The ObjectStore is a standalone class in `pj_datastore`, not embedded inside
`DataEngine`. Rationale:

- Single responsibility — ObjectStore has no encoding, chunking, column
  buffers, or derived transforms.
- Independent development and testing.
- Separate handle types (`ObjectTopicId` vs `TopicId`) prevent misuse at
  compile time.
- Zero risk of regressions on the columnar path.

### 3.2 Storage Model: raw bytes, not decoded objects

Each entry stores raw compressed/serialized bytes — not decoded results.
A JPEG image stays as JPEG bytes (~100-200KB), not decoded RGB pixels (~6MB).
Decoding is always deferred to the consumer (viewer or transformer plugin).

For file-backed sources, the entry stores a fetch callback instead of owned
bytes. The callback captures a shared_ptr to the file reader and seeks back
to the message on demand. For streaming sources, the entry stores owned
copies of the raw bytes.

The ObjectStore exposes a **uniform interface** — consumers always receive
raw bytes via a resolve call. Whether the bytes are owned or fetched from a
callback is an internal storage detail, invisible to consumers.

### 3.3 Homogeneous series

Each object series (topic) carries one message type, declared at
registration time. There is no per-entry type discrimination. The series
metadata tells consumers what the bytes represent and which parser to use.

### 3.4 Threading model

Same as DataEngine: effectively single-threaded. Streaming sources queue
messages internally and push during `poll()` on the main thread. No internal
locks.

### 3.5 Eviction

No automatic retention policy. The application calls `evictBefore(timestamp)`
explicitly. Entries are stored in a deque; eviction removes from the front.

### 3.6 Stateless entries

Every entry in the ObjectStore is independently resolvable. No entry depends
on prior entries for decoding. Stateful decoding (e.g., H.264 P-frames
requiring a preceding keyframe) is a viewer/consumer concern — the
ObjectStore stores raw NAL units; the video decoder widget maintains codec
state internally.

## 4. Data Model

### ObjectTopicId

Separate handle type from `TopicId`. Prevents accidental cross-use with
columnar APIs at compile time.

```cpp
struct ObjectTopicId {
  uint32_t id;
};
```

### Series metadata

Set once at registration time. Stored per series.

```cpp
struct ObjectTopicDescriptor {
  DatasetId dataset_id;
  std::string topic_name;
  std::string metadata_json;  // e.g. {"media_class":"image","encoding":"cdr",
                               //       "schema":"sensor_msgs/CompressedImage"}
};
```

The `metadata_json` is opaque to the ObjectStore — it stores and returns it
without interpretation. Consumers and the GUI use it to select viewers and
parsers.

### Entry

```cpp
struct ObjectEntry {
  int64_t timestamp;
  std::variant<
    std::vector<uint8_t>,                       // owned raw bytes (streaming)
    std::function<std::vector<uint8_t>()>        // lazy fetch (file-backed)
  > payload;
};
```

Consumers never see this variant directly. The public API resolves it
internally and returns raw bytes.

### Resolved entry (returned to consumers)

```cpp
struct ResolvedObjectEntry {
  int64_t timestamp;
  Span<const uint8_t> data;  // valid until next call or eviction
};
```

## 5. Public API

```cpp
namespace PJ {

class ObjectStore {
 public:
  // --- Registration ---

  /// Register a new object topic. Returns a unique handle.
  Expected<ObjectTopicId> registerTopic(const ObjectTopicDescriptor& descriptor);

  /// Get metadata for a registered topic.
  const ObjectTopicDescriptor& descriptor(ObjectTopicId id) const;

  /// List all registered topics, optionally filtered by dataset.
  std::vector<ObjectTopicId> listTopics() const;
  std::vector<ObjectTopicId> listTopics(DatasetId dataset_id) const;

  // --- Write ---

  /// Append an entry with owned raw bytes (streaming sources).
  Status pushOwned(ObjectTopicId id, int64_t timestamp,
                   std::vector<uint8_t> payload);

  /// Append an entry with a lazy fetch callback (file-backed sources).
  Status pushLazy(ObjectTopicId id, int64_t timestamp,
                  std::function<std::vector<uint8_t>()> fetch);

  // --- Read ---

  /// Find the entry at or nearest-before the given timestamp.
  /// Returns nullopt if no entry exists at or before the timestamp.
  std::optional<ResolvedObjectEntry> latestAt(ObjectTopicId id,
                                               int64_t timestamp) const;

  /// Get the entry at a specific index (for sequential playback / slider).
  std::optional<ResolvedObjectEntry> at(ObjectTopicId id, size_t index) const;

  /// Number of entries in a series.
  size_t entryCount(ObjectTopicId id) const;

  /// Timestamp range of a series.
  std::pair<int64_t, int64_t> timeRange(ObjectTopicId id) const;

  // --- Eviction ---

  /// Remove all entries with timestamp < threshold for a specific topic.
  void evictBefore(ObjectTopicId id, int64_t threshold);

  /// Remove all entries for all topics with timestamp < threshold.
  void evictAllBefore(int64_t threshold);

  // --- Lifecycle ---

  /// Remove a topic and all its entries.
  void removeTopic(ObjectTopicId id);

  /// Clear all topics and entries.
  void clear();
};

}  // namespace PJ
```

## 6. C ABI Surface (Plugin Access)

Toolbox and future transformer plugins access the ObjectStore through host
vtable extensions. The host resolves the variant internally; plugins only
see raw bytes.

```c
/// Read the latest object entry at or before the given timestamp.
/// On success, writes to *out_data and *out_timestamp.
/// The returned data pointer is valid until the next call to this function
/// on the same context, or until the topic is evicted.
bool (*read_object_at)(void* ctx,
                       PJ_object_topic_handle_t topic,
                       int64_t timestamp_ns,
                       PJ_bytes_view_t* out_data,
                       int64_t* out_timestamp);

/// Write an object entry with owned bytes.
bool (*write_object)(void* ctx,
                     PJ_object_topic_handle_t topic,
                     int64_t timestamp_ns,
                     PJ_bytes_view_t payload);

/// List available object topics.
/// Returns JSON array: [{"topic":"...","metadata":{...}}, ...]
const char* (*list_object_topics)(void* ctx);
```

These are appended to `PJ_toolbox_host_vtable_t` (guarded by `struct_size`
for backward compatibility) and will form the basis of a future transformer
plugin vtable.

## 7. Integration Points

### DataSource plugins (write path)

A DataSource plugin pushes messages through delegated ingest. The host
examines the parser's manifest:

- If `"media_class"` is present: host calls `parser->indexMedia()` for
  metadata, then pushes to the ObjectStore via `pushOwned()` (streaming) or
  `pushLazy()` (file-backed, with a fetch callback capturing the reader).
- If absent: host calls `parser->parse()` and writes to the columnar
  DataEngine (existing path).
- A DataSource may also duplicate selected scalar fields into the DataEngine
  via the existing write host.

### GUI / application (read path)

The application holds both `DataEngine` and `ObjectStore`. The catalog
merges topics from both. When the user opens a viewer:

- The GUI reads `metadata_json` to determine the viewer type (image viewer,
  3D viewport, etc.) and the parser needed for decoding.
- On each timestamp update: `objectStore.latestAt(topic, timestamp)` returns
  raw bytes, the parser decodes them, the viewer displays the result.

### Transformer plugins (future)

A transformer plugin reads object entries from one topic (e.g., image),
processes them (e.g., object detection), and writes object entries to
another topic (e.g., annotation). Both reads and writes go through the
C ABI host vtable.

## 8. Internal Storage

```cpp
struct ObjectSeries {
  ObjectTopicDescriptor descriptor;
  std::deque<ObjectEntry> entries;  // sorted by timestamp, front-evictable

  // Cached resolved bytes for the last read (avoids re-fetching
  // when latestAt returns the same entry on consecutive calls).
  mutable int64_t last_resolved_ts_ = INT64_MIN;
  mutable std::vector<uint8_t> last_resolved_bytes_;
};
```

`latestAt()` uses binary search on the deque (timestamps are monotonically
non-decreasing). If the result is the same timestamp as the last resolved
entry, the cached bytes are returned directly — this handles the common
case where the GUI polls the same frame repeatedly (e.g., paused playback).

Topic lookup uses `tsl::robin_map<ObjectTopicId, ObjectSeries>`.

## 9. Relationship to datatypes_2D.md

The ObjectStore stores raw serialized messages. It does not define or
enforce any specific message schema. The types defined in `datatypes_2D.md`
(Image, PointCloud, ScenePrimitive, FrameTransform, etc.) are the domain
types that parsers decode INTO and viewers consume. The ObjectStore sits
between them:

```
DataSource → ObjectStore (raw bytes) → Parser → Typed result → Viewer
```

The ObjectStore is agnostic to the domain type. It stores whatever bytes the
DataSource provides, tagged with metadata that tells downstream consumers
which parser to apply.

## 10. What This Design Does NOT Cover

- Derived object series (transform DAG for objects) — deferred.
- Disk persistence / sqlite caching — deferred. The interface
  (`pushLazy` with a fetch callback) does not prevent adding disk-backed
  storage later; the callback can read from any source.
- Video GOP-aware eviction — deferred. VideoFrame entries are stored
  individually; the viewer handles GOP dependencies.
- Compression of owned bytes — deferred. Raw bytes are stored as-is.
  In-memory compression (e.g., LZ4) can be added transparently later
  inside `pushOwned` / `latestAt` without API changes.
- Multi-threaded access — not planned. Same single-threaded model as
  DataEngine.
