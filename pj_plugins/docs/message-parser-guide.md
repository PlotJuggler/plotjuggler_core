# Writing a MessageParser Plugin

## What is a MessageParser?

A MessageParser plugin is a shared library (`.so` / `.dylib` / `.dll`) that
decodes raw byte payloads — JSON, Protobuf, ROS messages, Influx line protocol,
etc. — into named numeric fields that PlotJuggler can plot. Plugins link only
against `pj_base` (no Qt, no host internals) and communicate through a stable
C ABI.

MessageParsers are typically used via **delegated ingest**: a DataSource plugin
acquires raw data from files or network streams and pushes raw payloads through
the host, which routes them to the appropriate parser based on encoding name.

## Quick Start

1. Subclass `PJ::MessageParserPluginBase`
2. Override `parse()` (required) and optionally `bindSchema()`, `saveConfig()`,
   `loadConfig()`
3. Export with `PJ_MESSAGE_PARSER_PLUGIN(YourClass, R"({"name":"...","version":"...","encoding":"..."})")`
4. Build as a shared library linking `pj_base`

A complete example lives at `pj_plugins/examples/mock_json_parser.cpp`.

## Step by Step

### 1. Declare your class

```cpp
#include <pj_base/sdk/message_parser_plugin_base.hpp>

class MyJsonParser : public PJ::MessageParserPluginBase {
 public:
  PJ::Status parse(PJ::Timestamp timestamp_ns,
                    PJ::Span<const uint8_t> payload) override;
};
```

### 2. Implement parse()

When `parse()` is called, the write host is already bound via
`bindWriteHost()`. Use `writeHost()` (protected) to write decoded fields.
Return `okStatus()` on success, or `unexpected("reason")` on failure.

```cpp
PJ::Status MyJsonParser::parse(PJ::Timestamp timestamp_ns,
                                PJ::Span<const uint8_t> payload) {
  if (!writeHostBound()) {
    return PJ::unexpected(std::string("write host not bound"));
  }

  // Decode payload bytes into field values.
  // Use whatever parsing library your plugin links.
  std::string text(reinterpret_cast<const char*>(payload.data()),
                   payload.size());
  double value = std::strtod(text.c_str(), nullptr);

  const PJ::sdk::NamedFieldValue fields[] = {{.name = "value", .value = value}};
  return writeHost().appendRecord(
      timestamp_ns, PJ::Span<const PJ::sdk::NamedFieldValue>(fields, 1));
}
```

The write host is **topic-scoped** — the host binds it to a specific topic
before calling `parse()`. Fields written via `writeHost().appendRecord()` are
automatically namespaced under that topic. The parser does not need to know or
manage topic names.

### 3. Export the plugin

At file scope, after the class definition. The second argument is a JSON
manifest string literal (see Manifest Schema below):

```cpp
PJ_MESSAGE_PARSER_PLUGIN(MyJsonParser,
    R"({"name":"JSON Parser","version":"1.0.0","encoding":"json"})")
```

This generates the `extern "C"` entry point that the host resolves via dlsym.
The manifest is embedded as a compile-time constant in the vtable, so the host
can read it without creating an instance.

### 4. Build

```cmake
add_library(my_parser_plugin SHARED my_parser.cpp)
target_link_libraries(my_parser_plugin PRIVATE pj_base)
```

No other dependencies are needed.

## Lifecycle

The host drives the parser through these phases:

```
create() → bind_write_host() → [bind_schema()] → parse()* → destroy()
```

1. **create** — the host calls `create()` to allocate a new parser instance.
2. **bind_write_host** — the host provides the data-plane write host. Must be
   called before `parse()`.
3. **bind_schema** (optional) — for parsers that need schema (Protobuf, ROS,
   IDL), the host provides the schema bytes and type name. Parsers that don't
   need schema (JSON, Influx) can ignore this call.
4. **parse** — called once per message. The parser decodes the payload and
   writes fields via `writeHost()`.
5. **destroy** — the host destroys the instance.

## Write Host API

Access via `writeHost()` (protected). The write host is topic-scoped — every
field and record you write is automatically placed under the parser's assigned
topic.

| Method | Purpose |
|---|---|
| `ensureField(name, type)` | Pre-register a field for fast writes. Returns a `FieldHandle`. |
| `appendRecord(timestamp, fields)` | Write a row of named field values. |
| `appendBoundRecord(timestamp, fields)` | Write using pre-resolved field handles (faster). |
| `appendArrowIpc(ipc_stream, timestamp_col)` | Write an Arrow IPC stream directly. |

### Named vs bound writes

For simple parsers, use `appendRecord()` with `NamedFieldValue` — field names
are resolved on each call:

```cpp
const PJ::sdk::NamedFieldValue fields[] = {
    {.name = "temperature", .value = 23.5},
    {.name = "humidity", .value = 61.0}};
writeHost().appendRecord(timestamp_ns, PJ::Span(fields));
```

For high-throughput parsers, pre-register fields with `ensureField()` and use
`appendBoundRecord()` with `BoundFieldValue` — field handles are resolved once:

```cpp
// During bind_schema or first parse:
auto temp_field = writeHost().ensureField("temperature",
                                          PJ::PrimitiveType::kFloat64);
auto hum_field = writeHost().ensureField("humidity",
                                          PJ::PrimitiveType::kFloat64);

// During each parse:
const PJ::sdk::BoundFieldValue fields[] = {
    {.field = *temp_field, .value = 23.5},
    {.field = *hum_field, .value = 61.0}};
writeHost().appendBoundRecord(timestamp_ns, PJ::Span(fields));
```

## Optional Features

### Schema binding

Override `bindSchema()` to receive schema data before parsing begins. The
default implementation is a no-op.

```cpp
PJ::Status bindSchema(std::string_view type_name,
                       PJ::Span<const uint8_t> schema) override {
  // Store schema for use during parse().
  type_name_ = std::string(type_name);
  schema_.assign(schema.begin(), schema.end());
  // Build lookup tables, compile descriptors, etc.
  return PJ::okStatus();
}
```

The `type_name` is the encoding-specific message type (e.g.
`"sensor_msgs/Imu"` for ROS, `"my.package.ImuSample"` for Protobuf). The
`schema` bytes are encoding-specific (e.g. ROS `.msg` definition text,
Protobuf `FileDescriptorSet` binary).

### Configuration persistence

Override `saveConfig()` / `loadConfig()` to support layout save/restore:

```cpp
std::string saveConfig() const override { return config_json_; }

PJ::Status loadConfig(std::string_view json) override {
  config_json_ = std::string(json);
  // Parse JSON and apply settings.
  // e.g. max_array_size_, use_embedded_timestamp_, etc.
  return PJ::okStatus();
}
```

Common configuration patterns:
- **Array clamping**: `{"max_array_size": 100}` — limits how many array
  elements are expanded into individual series.
- **Embedded timestamps**: `{"use_embedded_timestamp": true}` — the parser
  extracts timestamp from the payload (e.g. ROS `Header.stamp`) instead of
  using the host-provided `timestamp_ns`.

### Embedded timestamp extraction

The `parse()` method receives a host-provided `timestamp_ns`. If the message
payload contains its own timestamp (e.g. a ROS Header or protobuf timestamp
field), the parser is free to ignore the host timestamp and write records with
the extracted timestamp instead:

```cpp
PJ::Status parse(PJ::Timestamp timestamp_ns,
                  PJ::Span<const uint8_t> payload) override {
  // Extract embedded timestamp from payload.
  PJ::Timestamp ts = use_embedded_timestamp_
      ? extractTimestamp(payload)
      : timestamp_ns;

  const PJ::sdk::NamedFieldValue fields[] = { /* ... */ };
  return writeHost().appendRecord(ts, PJ::Span(fields));
}
```

This is a parser-internal decision, controlled via `loadConfig()`.

### Dialog integration

A MessageParser can provide a configuration dialog by exporting a dialog vtable
from the same `.so` (same pattern as DataSource+Dialog). This is useful for
parsers that need GUI-based schema selection (e.g. Protobuf `.proto` file
loading).

The host resolves the dialog via `MessageParserLibrary::resolveDialogVtable()`.
See `pj_plugins/docs/data-source-guide.md` for the dialog pattern — it works
identically for parsers.

## Manifest Schema

The manifest is a JSON string literal embedded in the vtable. The host reads
it without instantiating the plugin.

| Key | Type | Required | Description |
|-----|------|----------|-------------|
| `name` | string | yes | Human-readable plugin name. |
| `version` | string | yes | Semver version string. |
| `encoding` | string | yes | Encoding this parser handles, e.g. `"json"`, `"protobuf"`, `"ros1msg"`. The host uses this to match binding requests to parsers. |

Example:
```json
{
  "name": "Protobuf Parser",
  "version": "1.0.0",
  "encoding": "protobuf"
}
```

## Error Handling

- All fallible SDK virtuals (`parse`, `bindSchema`, `bindWriteHost`,
  `loadConfig`) return `PJ::Status`. Return `okStatus()` on success,
  `unexpected("reason")` on failure.
- `setLastError(msg)` is available for custom error reporting.
- The base class catches all exceptions thrown from virtual methods and stores
  them automatically — you never need to worry about exceptions crossing the
  C ABI boundary.
- Check host operations: `writeHost().appendRecord()` and
  `writeHost().ensureField()` return `Expected<T>` / `Status` — always check
  before proceeding.

## How Parsers Are Used (Host Perspective)

DataSource plugins that act as transports (MQTT, ZMQ, MCAP, ROS bag files)
don't decode payloads themselves. Instead they declare `kCapabilityDelegatedIngest`
and push raw bytes through the host:

```
DataSource                        Host                         MessageParser
    │                               │                               │
    │  ensureParserBinding(         │                               │
    │    topic="sensor/imu",        │                               │
    │    encoding="protobuf",       │──→ load parser .so            │
    │    type_name="ImuSample",     │──→ create()                   │
    │    schema=descriptor_bytes)   │──→ bind_write_host()          │
    │                               │──→ bind_schema("ImuSample",   │
    │                               │       descriptor_bytes)       │
    │  ←── binding handle           │                               │
    │                               │                               │
    │  pushRawMessage(handle,       │                               │
    │    timestamp, payload)        │──→ parse(timestamp, payload)  │
    │                               │       │                       │
    │                               │       │ writeHost().append... │
    │                               │       ▼                       │
    │                               │    data stored                │
```

The parser is topic-scoped — the host binds a separate write host per topic,
so `ensureField("x")` in the parser creates `"sensor/imu/x"` in the datastore.

## Examples

- `pj_plugins/examples/mock_json_parser.cpp` — minimal parser that treats
  payloads as text-encoded doubles and writes one "value" field per message.
- `pj_base/tests/message_parser_plugin_base_test.cpp` — comprehensive test
  fixture exercising the full SDK surface: vtable generation, bind/parse
  round-trip, schema binding, config persistence, and exception safety.
