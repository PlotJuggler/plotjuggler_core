# Writing a DataSource Plugin

## What is a DataSource?

A DataSource plugin is a shared library (`.so` / `.dylib` / `.dll`) that
acquires data — from files, network streams, hardware, etc. — and feeds it
into PlotJuggler. Plugins link only against `pj_base` (no Qt, no host
internals) and communicate through a stable C ABI.

## Quick Start

1. Subclass `PJ::DataSourcePluginBase`
2. Override five methods: `manifest`, `capabilities`, `start`, `stop`, `currentState`
3. Export with `PJ_DATA_SOURCE_PLUGIN(YourClass)`
4. Build as a shared library linking `pj_base`

A complete example lives at `pj_plugins/examples/mock_data_source.cpp`.

## Step by Step

### 1. Declare your class

```cpp
#include <pj_base/sdk/data_source_plugin_base.hpp>

class MySource : public PJ::DataSourcePluginBase {
 public:
  std::string manifest() const override {
    return R"({"name": "My Source", "version": "1.0.0"})";
  }

  uint64_t capabilities() const override {
    return PJ::kCapabilityContinuousStream | PJ::kCapabilityDirectIngest;
  }

  bool start() override;
  void stop() override;
  PJ::DataSourceState currentState() const override { return state_; }

 private:
  PJ::DataSourceState state_ = PJ::DataSourceState::kIdle;
};
```

### 2. Implement start() and stop()

When `start()` is called, both host bindings are already available via the
protected accessors `writeHost()` and `runtimeHost()`.

```cpp
bool MySource::start() {
  state_ = PJ::DataSourceState::kStarting;
  runtimeHost().notifyState(state_);

  // Create a topic and write data
  auto topic = writeHost().ensureTopic("my/topic");
  if (!topic) {
    setLastError(topic.error());
    state_ = PJ::DataSourceState::kFailed;
    return false;
  }

  const PJ::sdk::NamedFieldValue fields[] = {
      {.name = "temperature", .is_null = false,
       .value = PJ::sdk::ValueRef{double(23.5)}}};
  auto status = writeHost().appendRecord(
      *topic, PJ::Timestamp{1000}, PJ::Span(fields));
  if (!status) {
    setLastError(status.error());
    state_ = PJ::DataSourceState::kFailed;
    return false;
  }

  state_ = PJ::DataSourceState::kRunning;
  runtimeHost().notifyState(state_);
  return true;
}

void MySource::stop() {
  state_ = PJ::DataSourceState::kStopped;
  runtimeHost().notifyState(state_);
}
```

### 3. Export the plugin

At file scope, after the class definition:

```cpp
PJ_DATA_SOURCE_PLUGIN(MySource)
```

This generates the `extern "C"` entry point that the host resolves via dlsym.

### 4. Build

```cmake
add_library(my_source_plugin SHARED my_source.cpp)
target_link_libraries(my_source_plugin PRIVATE pj_base)
```

No other dependencies are needed.

## Host Services Available to Plugins

Two host bindings are provided before `start()` is called:

### Write host — data plane

Access via `writeHost()`. Use this to write decoded data into the storage
engine.

| Method | Purpose |
|---|---|
| `ensureTopic(name)` | Create or look up a topic. Returns a handle. |
| `ensureField(topic, name, type)` | Pre-register a field for fast writes. |
| `appendRecord(topic, timestamp, fields)` | Write a row of named field values. |
| `appendRecordFast(topic, timestamp, fields)` | Write using pre-resolved field handles. |

### Runtime host — control plane

Access via `runtimeHost()`. Use this for lifecycle coordination and diagnostics.

| Method | Purpose |
|---|---|
| `reportMessage(level, text)` | Send info/warning/error to the host UI log. |
| `progressStart(label, total, cancellable)` | Begin a progress bar. |
| `progressUpdate(step)` | Advance progress. Returns false if cancelled. |
| `progressFinish()` | End the progress sequence. |
| `notifyState(state)` | Tell the host your state changed. |
| `requestStop(terminal_state, reason)` | Ask the host to stop you (self-terminate). |
| `isStopRequested()` | Check if the host wants you to stop. |
| `ensureParserBinding(request)` | Bind a parser for delegated ingest (see below). |
| `pushRawMessage(handle, timestamp, payload)` | Push raw bytes through a parser binding. |

## Optional Features

### Pause and resume

Override `pause()` and `resume()`, and declare `kCapabilitySupportsPause` in
your capabilities:

```cpp
uint64_t capabilities() const override {
  return PJ::kCapabilityContinuousStream | PJ::kCapabilityDirectIngest
       | PJ::kCapabilitySupportsPause;
}

bool pause() override {
  state_ = PJ::DataSourceState::kPaused;
  runtimeHost().notifyState(state_);
  return true;
}

bool resume() override {
  state_ = PJ::DataSourceState::kRunning;
  runtimeHost().notifyState(state_);
  return true;
}
```

### Periodic polling

Override `poll()` for streaming sources. The host calls it periodically while
the plugin is running. Return `false` to signal an error.

### Configuration persistence

Override `saveConfig()` / `loadConfig()` to support layout save/restore:

```cpp
std::string saveConfig() const override { return my_config_json_; }
bool loadConfig(std::string_view json) override {
  my_config_json_ = std::string(json);
  return true;
}
```

### Progress reporting

Report progress during long operations (e.g. file imports):

```cpp
runtimeHost().progressStart("Importing CSV", total_rows, /*cancellable=*/true);
for (uint64_t i = 0; i < total_rows; ++i) {
  if (!runtimeHost().progressUpdate(i)) {
    // User cancelled
    runtimeHost().progressFinish();
    return false;
  }
  // ... process row ...
}
runtimeHost().progressFinish();
```

### Delegated parsing

If your source is a transport or container (MQTT, ZMQ, MCAP) where the payload
encoding varies, use delegated ingest instead of writing decoded data directly.
Declare `kCapabilityDelegatedIngest` and use the runtime host:

```cpp
// 1. Bind a parser for a topic
auto binding = runtimeHost().ensureParserBinding({
    .topic_name = "sensor/imu",
    .parser_encoding = "protobuf",
    .type_name = "imu_sample",
    .schema = schema_bytes,
});
if (!binding) { setLastError(binding.error()); return false; }

// 2. Push raw payloads — the host parses and stores them
auto status = runtimeHost().pushRawMessage(*binding, timestamp_ns, payload);
```

The host manages parser instances, caches bindings, and handles schema
evolution automatically.

## State Machine

```
idle --> configuring --> starting --> running --> stopping --> stopped
                                       |  ^
                                 pause |  | resume
                                       v  |
                                     paused

Any state --> failed
```

- **stopped** and **failed** are terminal — create a new instance to restart.
- Always call `runtimeHost().notifyState()` when you transition.
- Use `runtimeHost().requestStop(kStopped, reason)` to self-terminate.
- Check `runtimeHost().isStopRequested()` during long operations.

## Capability Flags Reference

| Flag | Value | When to use |
|---|---|---|
| `kCapabilityFiniteImport` | `1 << 0` | File importers that load all data at once |
| `kCapabilityContinuousStream` | `1 << 1` | Live streaming sources |
| `kCapabilityDirectIngest` | `1 << 2` | Plugin decodes data and writes via write host |
| `kCapabilityDelegatedIngest` | `1 << 3` | Plugin pushes raw bytes for host-side parsing |
| `kCapabilitySupportsPause` | `1 << 4` | pause()/resume() are implemented |
| `kCapabilityHasDialog` | `1 << 5` | Plugin provides a configuration dialog |

Combine with bitwise OR.

## Error Handling

- Call `setLastError(msg)` before returning `false` from any method.
- The base class catches all exceptions thrown from virtual methods and stores
  them via `setLastError()` automatically — you never need to worry about
  exceptions crossing the C ABI boundary.
- Check host operations: `writeHost().appendRecord()` and
  `runtimeHost().ensureParserBinding()` return `Expected<T>` / `Status` —
  always check before proceeding.

## Example

`pj_plugins/examples/mock_data_source.cpp` is a complete reference that
demonstrates: manifest, capabilities, direct ingest, delegated ingest, progress
reporting, pause/resume, and config persistence.
