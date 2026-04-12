# pj_media Architecture

The HOW document for the pj_media module. `REQUIREMENTS.md` defines WHAT
pj_media does; this document defines HOW it is built. Read `REQUIREMENTS.md`
first — this document assumes familiarity with every section.

Cross-references to REQUIREMENTS.md use `§R` notation (e.g., `§R4.3`).
Cross-references to `OBJECT_STORE_DESIGN.md` use `§OS` notation (e.g., `§OS3.4`).

---

## 1. Module Structure

pj_media ships as two CMake targets with a strict dependency direction:

```
pj_media_qt  ──►  pj_media_core  ──►  pj_base
     │                  │                  │
     │                  ├──►  pj_datastore │
     │                  ├──►  FFmpeg       │
     │                  ├──►  turbojpeg    │
     │                  └──►  libpng       │
     │                                     │
     ├──►  Qt 6.8+ (Widgets, Gui, Rhi)    │
     └──►  pj_media_core                  │
```

### pj_media_core (no Qt)

Pure C++ library. Contains everything that does not touch Qt:

| Component | Header(s) | Role |
|-----------|-----------|------|
| `FrameSlot` | `frame_slot.h` | Single-slot latest-wins mailbox (§3) |
| `PlaybackController` | `playback_controller.h` | Per-widget orchestrator: owns decoders, worker thread(s), FrameSlot (§5) |
| `VideoDecoder` | `video_decoder.h` | FFmpeg wrapper with HW-accel detection and software fallback (§4) |
| `ImageDecoder` | `image_decoder.h` | turbojpeg / libpng / raw-pixel dispatch (§4) |
| `SceneDecoder` | `scene_decoder.h` | CDR / Protobuf deserializer for annotations and 2D primitives (§4) |
| `MediaIndexRegistry` | `media_index_registry.h` | Per-topic keyframe timestamp index sidechannel (§6) |
| `Compositor` | `compositor.h` | Multi-layer decode orchestration and blending (§8) |
| `CancelToken` | `cancel_token.h` | Atomic flag polled by decoders between decode units |
| `DecodedFrame` | `decoded_frame.h` | RAII wrapper for decoded pixel data (YUV planes or RGB buffer) |

DataSource plugins do NOT link `pj_media_core` — plugins depend only
on `pj_base` (see `pj_plugins/docs/ARCHITECTURE.md`). Format-specific
helpers (NAL scanning, PTS lookup) belong inside the DataSource plugin
itself or in lightweight headers within `pj_base`.

### pj_media_qt (Qt widgets)

Qt widget library built on top of `pj_media_core`:

| Component | Header(s) | Role |
|-----------|-----------|------|
| `MediaViewerWidget` | `media_viewer_widget.h` | `QRhiWidget` subclass: GPU rendering, zoom/pan, shader pipeline (§7) |
| `MediaViewerFactory` | `media_viewer_factory.h` | Creates the right viewer type from topic `metadata_json` |
| YUV shaders | `shaders/yuv_to_rgb.{vert,frag}` | BT.601/BT.709 color conversion in fragment shader |

The Qt layer is thin — it owns the GPU surface and polls the
`PlaybackController`'s `FrameSlot` at render rate. All decode logic,
threading, and cancellation live in `pj_media_core`.

---

## 2. Data Flow

Two paths: write (ingest) and read (display). pj_media participates
only in the read path.

### Write path (ingest — pj_media is NOT involved)

```
DataSource plugin
  ├─► [direct]    ObjectStore::pushOwned / pushLazy       ◄── v1
  └─► [delegated] host.pushRawMessage ─► MessageParser::parse()
                                            ├─► scalar write host  (DataEngine)
                                            └─► object write host  (ObjectStore)
                                                                   ◄── requires parser ABI v2
```

**V1 supports direct ingest only.** DataSource plugins call
`pushOwned` / `pushLazy` directly. Delegated ingest (via
`MessageParser::parse()` with two hosts) requires parser ABI v2
(two-host `parse()` signature), which is an outstanding prerequisite
(see `REQUIREMENTS.md` Prerequisites). Delegated ingest is a
subsequent milestone.

### Read path (display — pj_media's domain)

```
TimelineCursor ──timestamp──► PlaybackController
                                   │
                    ┌──────────────┼──────────────┐
                    ▼              ▼               ▼
              ObjectStore    ObjectStore      ObjectStore
              latestAt()     latestAt()       latestAt()
              (layer 0)      (layer 1)        (layer N)
                    │              │               │
                    ▼              ▼               ▼
              VideoDecoder   SceneDecoder    ImageDecoder
                    │              │               │
                    └──────┬───────┘───────────────┘
                           ▼
                      Compositor
                           │
                           ▼
                      FrameSlot  ◄──── single slot per widget
                           │
                    ───poll at ~60 Hz───
                           │
                           ▼
                   MediaViewerWidget (QRhiWidget)
                           │
                           ▼
                      GPU display
```

On each render tick:

1. `PlaybackController` receives the current timestamp from
   `TimelineCursor`.
2. For each active layer: call `ObjectStore::latestAt(topic, ts)` to
   get an owning byte handle.
3. Each layer's decoder decodes its bytes independently.
4. `Compositor` combines per-layer outputs per the widget's layer
   configuration.
5. The composited frame is written to the widget's `FrameSlot`.
6. The UI thread's render timer polls `FrameSlot::take()` and uploads
   the frame to the GPU.

Steps 2-5 run on the `PlaybackController`'s worker thread(s).
Step 6 runs on the Qt main thread.

---

## 3. Scrub Architecture

This section ports the patterns proven in
`~/ws_plotjuggler/video_player_lab/ARCHITECTURE.md §3` into pj_media's
architecture. That document is the canonical reference for WHY these
patterns work and WHY alternatives fail — read it before modifying
anything in this section.

### 3.1 FrameSlot — the single-slot mailbox

The `FrameSlot` is the mechanism that eliminates stale-frame
interleaving during scrub. It is ~60 lines of code:

```cpp
struct CompositeFrame {
  // Blended pixel buffer from all active layers (RGB or YUV).
  // Exact representation is an implementation detail; may be a
  // shared_ptr to a pixel buffer, an AVFrame, or a QImage.
};

struct SlotResult {
  int64_t timestamp_ns;
  CompositeFrame frame;
};

class FrameSlot {
 public:
  void store(int64_t timestamp_ns, CompositeFrame frame);
  std::optional<SlotResult> take();

 private:
  std::mutex mutex_;
  CompositeFrame frame_;
  int64_t timestamp_ns_ = 0;
  bool has_new_ = false;
};
```

Properties:

- **Latest-wins**: `store()` physically overwrites the previous frame.
  A stale frame cannot reach the display because it is replaced before
  the UI polls.
- **At most two composite frames alive** (one in the slot, one being
  displayed). Memory usage is bounded.
- **No Qt event queue involvement**. No `QMetaCallEvent`, no
  `QueuedConnection`, no `invokeMethod`. The only synchronization
  primitive is `std::mutex`.
- **Pull timing**: the UI polls via `QTimer::timeout` at ~60 Hz. On
  each tick: `auto r = slot_.take(); if (r) uploadToGpu(r->frame);`.

The video_player_lab prototype uses frame indices as identifiers
because it is file-only (§5.1-5.2 of that document). pj_media uses
`int64_t` nanosecond timestamps instead — the universal coordinate
that works for both file and streaming sources.

**Qt signals are NOT used for frame delivery.** Adding a signal escape
hatch is explicitly forbidden. See
`~/ws_plotjuggler/video_player_lab/ARCHITECTURE.md §3.2-3.3` for the
structural proof that push-based delivery via Qt event queues cannot be
fixed by patching, and the enumeration of 5+ failed patches that must
not be retried.

### 3.2 Direction-aware cancel-store

When the user scrubs rapidly, in-flight decodes are cancelled via
`CancelToken` (an `atomic<bool>` polled between decode units). The
question is: should a partially-decoded frame be published to the
`FrameSlot`?

The rule, proven in
`~/ws_plotjuggler/video_player_lab/ARCHITECTURE.md §3.6`:

- **Forward scrub** (`current_request_ts > previous_request_ts`):
  publish partials. The decoder is decoding toward the target from a
  keyframe behind it — partial progress shows frames between the
  previous position and the target, which matches the user's drag
  direction.
- **Backward scrub** (`current_request_ts < previous_request_ts`):
  suppress partials. The decoder replays FORWARD from a keyframe
  while the user is dragging BACKWARD — publishing partials would
  show frames moving in the wrong direction.
- **Same position or first request**: suppress.

Implementation: `PlaybackController` tracks `last_request_ts_` and
sets a `suppress_partial_publication_` flag on the active decode
before each new request. The decoder checks this flag in its cancel
path.

### 3.3 CancelToken

Each decode request carries a `CancelToken` — a class wrapping an
`atomic<bool>`, shared between requester and decoder via
`shared_ptr<CancelToken>`:

```cpp
class CancelToken {
 public:
  bool isCancelled() const { return flag_.load(std::memory_order_relaxed); }
  void cancel() { flag_.store(true, std::memory_order_relaxed); }

 private:
  std::atomic<bool> flag_{false};
};
```

Decoders poll `token->isCancelled()` at natural check points:

- `VideoDecoder`: between NAL units (after each `avcodec_receive_frame`).
- `ImageDecoder`: between JPEG MCU rows (if turbojpeg supports
  progressive; otherwise after the single decode call).
- `SceneDecoder`: between primitives in a batch message.

A new request flips the previous token. The decoder returns early, and
the `PlaybackController` decides whether to publish the partial result
based on the direction-aware rule (§3.2).

---

## 4. Decoder Classes

Decoders are built-in C++ classes in `pj_media_core`. They are NOT
plugins — codec state is per-viewer, per-layer, and adding new codecs
requires a pj_media code change.

### 4.1 VideoDecoder

Stateful. One instance per video layer per widget. Wraps FFmpeg's
`AVCodecContext`.

**Initialization**: at construction, probes for HW-accel backends in
platform priority order (see `TECHNICAL_NOTES.md §3`). If none
succeeds, falls back to software decode. The backend choice is
logged but not exposed in the public API.

**Decode cycle** (called by `PlaybackController` per render tick):

1. Receive raw bytes (owning `shared_ptr`) from ObjectStore via
   `latestAt`.
2. If the requested timestamp requires a seek (the entry is not the
   next sequential frame): consult the keyframe index (§6), find the
   keyframe at-or-before the target, call `avcodec_flush_buffers`,
   then decode forward from the keyframe through intermediate P/B
   frames to the target.
3. Feed bytes via `avcodec_send_packet`. Retrieve decoded frame via
   `avcodec_receive_frame`.
4. Poll `CancelToken` between each `avcodec_receive_frame` call.
5. If the decode completes, return the `DecodedFrame` (YUV planes
   still in decoder-native format — YUV420P, NV12, or P010).

**ENOMEM recovery**: if `avcodec_send_packet` returns
`AVERROR(ENOMEM)` (HW surface pool exhaustion), call
`avcodec_flush_buffers` and retry once. If it fails again, propagate
the error to the widget's last-error state.

**EOF flush**: when the DataSource signals end-of-data, the
`PlaybackController` calls `VideoDecoder::flush()` — sends a NULL
packet to drain buffered frames from the codec's internal queue.

**Forward-decode threshold**: if the target timestamp is within
roughly one GOP duration of the last decoded timestamp, decode forward
without seeking. Beyond that threshold, seek to the nearest keyframe
first. The threshold is configurable; the default is approximately
one GOP (~5 seconds at 30 fps). This avoids unnecessary seeks during
sequential playback and small forward scrub steps.

### 4.2 ImageDecoder

Stateless. One instance per image layer. Multiple instances in one
widget are fine (they share no state).

Dispatches based on the image encoding (from topic `metadata_json`):

| Encoding | Library | Notes |
|----------|---------|-------|
| JPEG | turbojpeg | `tjDecompress2` → RGB888. < 10 ms for typical frames; caching decoded output is not worth the memory |
| PNG | libpng | Standard decode path |
| Raw pixels | memcpy | mono8, rgb8, rgba8, bgr8, bgra8 — copy into `DecodedFrame` with format tag |
| Depth (mono16) | custom | 16-bit depth → colormap in the compositor (§8) |

No decoded-frame cache. On-the-fly decode is fast enough for stills
that caching wastes more memory than it saves time (§R4.2).

### 4.3 SceneDecoder

Stateless. One instance per scene/annotation layer. Deserializes CDR
or Protobuf wire format into typed scene primitives and annotations
(see `datatypes_2D.md` for the type catalog).

Output is a `SceneFrame` — a collection of typed primitives ready for
the compositor to rasterize or overlay.

---

## 5. PlaybackController

The per-widget orchestrator. One `PlaybackController` per
`MediaViewerWidget`. Owns:

- One decoder instance per active layer (§R4.6).
- The widget's single `FrameSlot`.
- One or more worker threads for decode.
- The `Compositor` instance.
- `CancelToken` management.
- Scrub direction tracking (`last_request_ts_`).

### 5.1 Lifecycle

**Construction**: receives references to `ObjectStore`,
`TimelineCursor`, and the list of layer descriptors (topic +
decoder type per layer). Instantiates decoders, starts worker
thread(s).

**Destruction**: cancels in-flight decodes, joins worker threads,
releases ObjectStore handles. Must complete before `ObjectStore`
or `TimelineCursor` are destroyed.

### 5.2 Request processing

When `TimelineCursor` advances to a new timestamp:

1. `PlaybackController` compares `new_ts` to `last_request_ts_`
   to determine scrub direction.
2. Cancels the previous `CancelToken`.
3. Creates a new `CancelToken` for this request.
4. Sets `suppress_partial_publication_` based on direction (§3.2).
5. Posts the request to the worker thread(s).

The worker thread:

1. For each layer: calls `ObjectStore::latestAt(topic, ts)` to get
   the owning byte handle.
2. Passes each handle to the layer's decoder.
3. Collects decoded outputs; passes them to the `Compositor`.
4. Writes the composited result to the `FrameSlot`.

### 5.3 Worker thread model

**V1 architecture: single worker thread per widget.** One thread
drives all per-layer decoders sequentially per render tick, then
composites and writes to the `FrameSlot`. This is simple and adequate
for typical workloads (video + annotation overlay + optional depth).

The three contractual guarantees from §R4.6 hold trivially under this
model: decoders don't share state (sequential on the same thread),
one FrameSlot per widget, and stale frames cannot reach the display
(latest-wins slot semantics).

**Future possibility: per-layer worker threads.** If profiling shows
that a heavy video decode blocks lightweight overlay decodes, per-layer
parallelism could improve responsiveness. This is NOT part of the v1
architecture because it requires designing a compositor barrier
(ensuring all layers are decoded for the same timestamp before
compositing) and a generation model (preventing stale per-layer
results from leaking into the composite). These mechanisms are not
yet specified.

---

## 6. MediaIndexRegistry

Keyframe tracking lives in pj_media, not in ObjectStore (§R4.2,
§R4.4). The `MediaIndexRegistry` is the sidechannel that holds
per-topic keyframe timestamp lists.

```cpp
class MediaIndexRegistry {
 public:
  void registerIndex(ObjectTopicId topic,
                     std::vector<int64_t> keyframe_timestamps);

  void appendKeyframe(ObjectTopicId topic, int64_t timestamp);

  std::optional<int64_t> keyframeBefore(ObjectTopicId topic,
                                         int64_t timestamp) const;

  void removeIndex(ObjectTopicId topic);
};
```

**Keying**: entries are keyed by `ObjectTopicId`, not topic name
strings — consistent with the rest of the system.

**Lifecycle**: entries are cleared when the corresponding topic is
removed from ObjectStore (`removeTopic`, `clear`). The application
is responsible for calling `removeIndex()` as part of topic teardown.
Widgets observing a removed topic will find no keyframe index and
fall back to sequential decode (no seeking).

### Two population modes

**File-backed sources**: the DataSource plugin pre-computes the
keyframe list at open time — by scanning NAL start codes (H.264/H.265),
reading the MP4 `stss` (sync sample) atom, or equivalent. It publishes
the sorted timestamp array via the C ABI slot
`object_write_host.publish_keyframe_index(topic, timestamps, count)`.
The host receives the array and calls `registerIndex()` on the
application-owned `MediaIndexRegistry`. The DataSource plugin never
links `pj_media_core` — communication is through the C ABI write host
only. This is a one-time cost at file open, amortized over all
subsequent seeks.

**Streaming sources**: the `VideoDecoder` builds the index
incrementally. On each new entry it NAL-parses the first few bytes to
detect IDR frames and calls `appendKeyframe()`. This is per-entry
overhead but is amortized over live playback — at 30 fps, one
NAL-header check per frame is negligible.

### Usage by VideoDecoder

When `VideoDecoder` needs to seek to timestamp `T` (using the
`ObjectTopicId` it was constructed with):

1. `registry.keyframeBefore(topic_id, T)` → returns `kf_ts`.
2. `ObjectStore::indexAt(topic_id, kf_ts)` → gets the keyframe's index
   `i` in the entry sequence.
3. `ObjectStore::at(topic, i)` → gets the keyframe bytes. Decode it
   after calling `avcodec_flush_buffers` to reset decoder state.
4. Iterate `ObjectStore::at(topic, i+1)`, `at(topic, i+2)`, ...
   decoding each P/B frame until reaching the entry at timestamp `T`.

Because live and scrub modes are mutually exclusive (§R4.3), the buffer
is frozen during this seek — no entry can be evicted between steps 2
and 4.

### Thread safety

`MediaIndexRegistry` is protected by its own `shared_mutex`.
`registerIndex` and `appendKeyframe` take exclusive locks;
`keyframeBefore` takes a shared lock. The registry is independent of
`ObjectStore`'s locking.

---

## 7. Rendering Pipeline

### 7.1 QRhiWidget

`MediaViewerWidget` subclasses `QRhiWidget` (Qt 6.8+), which abstracts
over Vulkan, Metal, D3D11, and OpenGL at runtime. The widget:

1. Creates GPU textures for YUV planes (Y, U, V for YUV420P; Y+UV for
   NV12; Y+UV for P010 with 10-bit depth).
2. On each render tick, polls `FrameSlot::take()`.
3. If a new frame arrived: uploads plane data to GPU textures via
   `QRhiResourceUpdateBatch`.
4. Draws a full-screen quad with the YUV-to-RGB fragment shader.

### 7.2 YUV-to-RGB shaders

Fragment shader performs BT.601 or BT.709 color conversion:

```glsl
// Simplified — actual shader handles NV12/P010/YUV420P variants
vec3 yuv = vec3(
    texture(y_plane, uv).r,
    texture(u_plane, uv).r - 0.5,
    texture(v_plane, uv).r - 0.5
);
fragColor = vec4(bt709_matrix * yuv, 1.0);
```

The color matrix is selected based on the video's color space metadata
(from FFmpeg's `AVFrame::colorspace`). Default is BT.709 for HD
content, BT.601 for SD.

### 7.3 Zoom and pan

A 3x3 view transform matrix in the vertex shader handles zoom
(mouse-wheel, cursor-anchored) and pan (mouse drag):

```glsl
gl_Position = vec4(view_matrix * vec3(in_position, 1.0), 1.0);
```

The transform is updated on mouse events and stored as widget state.
No pixel reprocessing — transformation is free on the GPU.

### 7.4 Software fallback

When `QRhi` reports no suitable GPU backend, or when the platform
lacks GPU support:

1. Decoder falls back to software decode (guaranteed by
   `VideoDecoder`'s fallback logic).
2. CPU-side YUV→RGB conversion via sws_scale or manual matrix
   multiply.
3. Upload RGB pixels to a `QImage` and render via `QPainter`.

Acceptable degradation; the UX remains functional.

---

## 8. Multi-Layer Compositor

A viewer widget may composite multiple ObjectStore topics at the same
display time (§R4.8). The `Compositor` class orchestrates this.

### 8.1 Layer model

Each layer is a `(topic, decoder, blend_mode, z_order)` tuple
configured at widget construction time. Layer types:

| Layer type | Decoder | Output |
|------------|---------|--------|
| Base image/video | `VideoDecoder` or `ImageDecoder` | RGB/YUV pixel buffer |
| Annotation overlay | `SceneDecoder` | List of 2D primitives (rasterized onto the base) |
| Depth colormap | `ImageDecoder` (mono16) | False-color pixel buffer (colormap applied by compositor) |
| Segmentation mask | `ImageDecoder` | Indexed-color pixel buffer (alpha-blended) |

### 8.2 Compositing pipeline

On each render tick, the `PlaybackController`'s worker thread:

1. Queries `ObjectStore::latestAt(topic, ts)` for each layer.
2. Decodes each layer independently via its decoder.
3. Passes all decoded outputs to the `Compositor`.
4. The `Compositor` applies layer ordering and blending:
   - Base layer rendered first.
   - Overlays rasterized on top (annotations as vector primitives,
     depth/segmentation as alpha-blended pixel buffers).
5. The composited `CompositeFrame` is written to the `FrameSlot`.

### 8.3 At-or-before semantics

Every layer query uses `latestAt` — strict at-or-before, never a
future entry (§R4.8). A 10 Hz detection overlay composited on a
60 Hz video is correct: the same bounding box persists across
multiple video frames until replaced by a newer annotation.

No automatic skew rejection. Multi-rate is the norm — the compositor
trusts that an entry exists in the store until explicitly evicted.

### 8.4 frame_id correlation (optional)

When an overlay's metadata carries a `frame_id` matching a specific
source image, the compositor may prefer exact pairing over
timestamp-based pairing. Layers without `frame_id` fall back to
timestamp matching.

---

## 9. Clock Integration

### 9.1 TimelineCursor

`TimelineCursor` is a small read-only interface declared in `pj_base`.
The application owns it; widgets subscribe to it via a callback or
observer pattern.

```cpp
// In pj_base — exact signatures TBD
class TimelineCursor {
 public:
  using TimestampCallback = std::function<void(int64_t ns)>;

  void subscribe(TimestampCallback cb);
  void unsubscribe(/* handle */);

  int64_t currentTimestamp() const;
};
```

Widgets only subscribe, never drive. The application advances the
cursor — from a slider, from a playback timer, from an external
sync source.

### 9.2 Rate hints

DataSource plugins publish optional hints at startup:

- `preferred_fps`: natural frame rate of the source (e.g., 30.0 for
  a 30 fps video).
- `natural_range_ns`: total duration of the dataset.

The application's clock aggregates hints across sources and picks a
default playback pace. The user may override.

### 9.3 Live vs scrub mode

The application manages the live/scrub mode transition (§R4.3).
From pj_media's perspective:

- **Live mode**: `TimelineCursor` advances at the live edge. The
  `PlaybackController` requests the newest timestamp on each tick.
  Each `ObjectStore::latestAt` returns the most recent entry.

- **Scrub mode**: `TimelineCursor` is driven by user interaction
  (slider drag). The `PlaybackController` requests the user-selected
  timestamp. The buffer is frozen — no pushes, no eviction.

The `PlaybackController` does not know or care which mode is active.
It reacts identically: receive timestamp → query store → decode →
composite → slot. The mode distinction is entirely in whether the
cursor advances automatically or manually, and whether the DataSource
is actively pushing.

---

## 10. Threading Model

### 10.1 Thread roles

| Thread | Responsibilities | Lock discipline |
|--------|-----------------|-----------------|
| **Qt main thread** | UI events, `QTimer` render tick, `FrameSlot::take()`, GPU upload, widget lifecycle | Never blocks on decode. Never holds ObjectStore locks longer than a `latestAt` call |
| **PlaybackController worker** (1 per widget) | `ObjectStore::latestAt` queries, decoder invocation, compositing, `FrameSlot::store()` | Acquires ObjectStore shared locks (released immediately after handle copy). Holds decoder-internal state exclusively |
| **DataSource poll thread** (1 per app, existing) | `DataSource::poll()` → `ObjectStore::pushOwned/pushLazy` | Acquires ObjectStore exclusive locks per push. Never touches decoders |

### 10.2 Lock inventory

| Lock | Type | Protects | Held by |
|------|------|----------|---------|
| `ObjectSeries::mutex` (§OS3.4) | `shared_mutex` | Per-topic entry storage | Shared: worker threads via `latestAt`/`at`/`indexAt`. Exclusive: poll thread via `pushOwned`/`pushLazy`/eviction |
| `FrameSlot::mutex_` | `mutex` | Single-frame mailbox | Worker: `store()`. Main: `take()`. Never held concurrently — always < 1 us |
| `MediaIndexRegistry::mutex_` | `shared_mutex` | Keyframe index | Shared: worker via `keyframeBefore`. Exclusive: DataSource via `registerIndex`, worker via `appendKeyframe` |

### 10.3 Lock ordering

No nested locking across the three lock families. Each lock is
acquired and released independently — never held simultaneously:

1. Worker acquires `ObjectSeries::mutex` (shared) → copies
   `shared_ptr` handle → releases lock.
2. Worker decodes (no locks held — decoder operates on owned data).
   For streaming video, if the decoder detects a keyframe via NAL
   inspection, it calls `MediaIndexRegistry::appendKeyframe()` which
   acquires `MediaIndexRegistry::mutex_` (exclusive) → appends →
   releases. This happens after the ObjectStore lock is released.
3. Worker acquires `FrameSlot::mutex_` → stores frame → releases.

The main thread only ever acquires `FrameSlot::mutex_` (via `take()`).
It never touches ObjectStore or MediaIndexRegistry directly.

### 10.4 Contention analysis

- **ObjectStore shared_mutex**: at typical media frame rates (30-60
  fps), the push thread holds an exclusive lock for ~1 us per entry
  (deque append + timestamp vector append + optional eviction).
  Worker threads hold shared locks for ~1 us per query (binary search
  + shared_ptr copy). Contention is effectively zero.
- **FrameSlot mutex**: held for < 1 us on both sides (store is a
  move; take is a move). Zero contention in practice.
- **MediaIndexRegistry shared_mutex**: reads (binary search) are
  O(log k) where k is the number of keyframes. Writes
  (`appendKeyframe`) are O(1) amortized. Negligible contention.

### 10.5 Error propagation across threads

Errors from decoder workers must not propagate as C++ exceptions
across the worker→UI thread boundary (§R5). The error flow:

1. **Worker thread**: decoder methods return `PJ::Expected<DecodedFrame>`
   or similar. If a decoder fails (corrupt data, unsupported codec,
   HW-accel error), the worker catches the failure at the thread
   boundary.
2. **Last-error state**: the `PlaybackController` stores the error in
   a thread-safe last-error field (atomic or mutex-protected). The
   `FrameSlot` is NOT written to on error — the widget continues
   displaying the last good frame.
3. **UI thread**: on its next poll, the widget checks the last-error
   state. If set, it renders a visible error indicator (e.g., "decode
   failed" overlay or a no-signal background) on the affected layer.
4. **Recovery**: the error state is cleared when the next successful
   decode completes.

**Missing data is not an error**: when `ObjectStore::latestAt` returns
`nullopt` on a valid topic (e.g., the cursor is before the first
entry), the widget renders a "no data" indicator without raising,
logging, or surfacing an error condition.

### 10.6 EntryTimestampsView and extended locks

`ObjectStore::entryTimestamps()` returns an `EntryTimestampsView` that
holds a `shared_lock` for its lifetime (§OS4). pj_media's
`VideoDecoder` may use it during seek planning (batch timestamp
access). The lock is shared (readers-only), so it does not block other
decoders, but it blocks the push thread for the view's lifetime.

Rule: any code path that acquires an `EntryTimestampsView` must drop
it promptly — copy timestamps into a local vector if further
processing is needed beyond the search. Never hold a view across a
decode call.

---

## 11. Port Map

What to take from each reference prototype and what to leave behind.

### From `video_player_lab/`

| Component | Action | Target in pj_media | Notes |
|-----------|--------|-------------------|-------|
| `FrameSlot` | **PORT** | `pj_media_core/frame_slot.h` | Copy the ~60-line implementation. Change identity from `size_t index` to `int64_t timestamp_ns`. The mechanism is identical |
| Direction-aware cancel-store | **PORT** | `PlaybackController` | Port the request-level direction tracking from `FrameProvider::bg_loop`. Replace frame-index comparison with timestamp comparison. See `~/ws_plotjuggler/video_player_lab/ARCHITECTURE.md §3.6` for the exact rule |
| `VideoDecoder::flush()` at EOF | **PORT** | `VideoDecoder` | 3 lines: send NULL packet, drain buffered frames. Currently missing in the parallel pj_media effort |
| `ENOMEM` recovery | **PORT** | `VideoDecoder` | On `AVERROR(ENOMEM)`: `avcodec_flush_buffers` + retry once. Hit during scrub testing |
| `FrameCache` (JPEG cache) | **DO NOT PORT** | — | pj_media's image subsystem doesn't need it (on-the-fly JPEG decode is fast enough). Video decoded-frame caching, if needed, will use a proximity-based strategy specific to pj_media's `VideoDecoder` |
| `FrameConverter` | **DO NOT PORT** | — | Equivalent HW→SW transfer exists in pj_media's `VideoDecoder` |
| `Mp4DataSource` | **DO NOT PORT** | — | pj_media handles MP4 via `FFmpegVideoSource`. The prototype's demuxer is narrower |
| `PlaybackClock` | **DO NOT PORT** | — | Replaced by pj_media's `TimelineCursor` subscription model |
| `VideoWidget` (QRhiWidget) | **DO NOT PORT** | — | pj_media already has `MediaViewerWidget` with the same QRhi + shader approach plus additional features (pixel inspector) |
| Keyframe pre-decode at open | **CONDITIONAL** | — | Only if a product decision requires a scrub-preview thumbnail strip. Port as opt-in `KeyframeThumbnails`, not baked into open(). See `~/ws_plotjuggler/video_player_lab/ARCHITECTURE.md §3.7` |

### From `~/ws_plotjuggler/pj_media/` (parallel effort)

| Component | Action | Target in pj_media | Notes |
|-----------|--------|-------------------|-------|
| QRhiWidget + YUV shaders | **CHERRY-PICK** | `MediaViewerWidget` | The shader code and QRhi setup are production-ready. Adapt to pj_media_core's frame types |
| FFmpegVideoSource / VideoDecoder | **CHERRY-PICK** | `VideoDecoder` | HW-accel probing, codec open/close, sws_scale paths. Strip the push-based delivery and replace with FrameSlot |
| `FrameSlot` (already ported from video_player_lab) | **USE AS-IS** | — | Already present in the parallel effort |
| ImageSource + BufferStrategy | **ADAPT** | `ImageDecoder` | The per-topic buffer strategy is more complex than needed for pj_media_core's stateless `ImageDecoder`. Take the turbojpeg/libpng dispatch; leave the caching strategy |
| PayloadDescriptor bytecode VM | **EVALUATE** | — | Clever but complex. Evaluate whether the simpler approach (metadata_json + decoder dispatch) suffices before porting |
| TimelineBridge | **DO NOT PORT** | — | Replaced by `TimelineCursor` in pj_base |
| Timestamp µs vs ns dichotomy | **FIX** | — | pj_media uses ns everywhere. The parallel effort's video engine used µs internally. All internal timestamps must be int64_t nanoseconds |

### From `plotjuggler_core/pj_media/mcap_player/`

| Component | Action | Notes |
|-----------|--------|-------|
| `LazyMediaSeries<T>` callback model | **VALIDATED** | The pattern (timestamps + resolve closures capturing shared_ptr) is sound and maps directly to ObjectStore's `pushLazy` with fetch callbacks. The mcap_player sandbox validated the approach. No code to port — ObjectStore implements the pattern natively |
| `CompressedImageParser` (CDR → turbojpeg) | **REFERENCE** | Demonstrates the parser → decoder split. In pj_media proper, the CDR envelope peeling is a `MessageParser` plugin; the turbojpeg decode is `ImageDecoder` |

---

## Appendix: Key Invariants

These invariants are load-bearing. Violating any of them reintroduces
bugs that were already proven unfixable by patching.

1. **No Qt signals for frame delivery.** The FrameSlot mailbox is the
   only path from decoder to display. Adding a signal escape hatch
   reintroduces stale-frame interleaving. (Proof:
   `video_player_lab/ARCHITECTURE.md §3.2-3.3`)

2. **One FrameSlot per widget, never per layer.** Compositing happens
   before the slot, not after. The UI thread sees exactly one
   composited frame per poll.

3. **Backward scrub suppresses partial publications.** Forward partials
   are fine; backward partials show frames moving in the wrong
   direction. (Proof: `video_player_lab/ARCHITECTURE.md §3.6`)

4. **ObjectStore handles are owning.** A decoder can hold a byte handle
   while the store pushes and evicts. No use-after-free is possible.
   (Guarantee: §OS3.4, §OS4)

5. **Live and scrub are mutually exclusive.** During scrub, the buffer
   is frozen — no pushes, no eviction. Seek sequences (keyframe →
   decode forward) cannot race with eviction. (Requirement: §R4.3)

6. **Keyframe tracking is pj_media's concern, not ObjectStore's.**
   ObjectStore is codec-agnostic (§OS3.6). `MediaIndexRegistry` owns
   keyframe indices. (Decision: §R4.2, §R4.4)

7. **Parsers are codec-agnostic envelope peelers.** They never inspect
   NAL types, keyframe flags, or GOP structure. All codec knowledge
   lives in pj_media's decoder classes. (Decision: §R4.4)
