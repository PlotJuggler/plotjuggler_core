# DataSource Execution Plan

## Summary

The `pj_base` data plane is now ready. The rest of the DataSource work is a
control-plane project:

- define the portable `DataSource` family ABI and C++ SDK
- define the host runtime that binds a source instance to one `data_source`
- integrate delegated parsing through a host-owned parser-binding surface
- reuse the existing dialog protocol for configuration, including integrated
  host-owned parser controls
- migrate legacy sources in a staged order from simplest to most demanding

This plan covers `DataSource` only. It does not include Toolbox work, parser
implementation work, plugin discovery, or deployment.

## 1. Runtime Architecture

### 1.1 New subsystem to add

Add a new `pj_plugins/data_source_protocol` subtree mirroring the structure used
for `dialog_protocol`:

- raw C ABI header for portable DataSource plugins
- header-only C++ SDK for plugin authors
- host-side C++ wrappers for loading and driving DataSource plugins
- tests for raw ABI, wrapper API, host runtime behavior, and migration examples

The split of responsibilities stays explicit:

- `pj_base`: data read/write surfaces and shared vocabulary
- `pj_plugins/dialog_protocol`: configuration UI protocol
- `pj_plugins/data_source_protocol`: DataSource lifecycle, delegated parsing, and
  host runtime control plane

### 1.2 DataSource library shape

A DataSource shared library may export:

- `PJ_get_data_source_vtable()`
- optionally `PJ_get_dialog_vtable()`

The DataSource runtime instance and the dialog instance are separate contexts.
They are synchronized only through serialized config JSON. They do not share live
mutable state across ABI boundaries.

### 1.3 One-instance model

- One DataSource runtime instance is pre-bound by the host to one application
  `data_source`.
- The DataSource never creates its own `data_source`.
- A DataSource may create topics lazily inside that bound `data_source`.
- Starting or stopping a DataSource never mutates some shared host container
  directly and never implicitly clears already-ingested data.

For v1, the host owns reset semantics:

- if a restart should append, the host reuses the same bound `data_source`
- if a restart should begin empty, the host creates a fresh DataSource instance
  bound to a fresh `data_source`

## 2. Public Interfaces to Build

### 2.1 DataSource plugin ABI

Define one raw ABI plus one C++ SDK wrapper for the `DataSource` family.

The exported DataSource instance surface must include:

- `create` / `destroy`
- `bind_write_host`
- `bind_runtime_host`
- `load_config`
- `save_config`
- `start`
- `stop`
- `pause`
- `resume`
- `poll`
- `current_state`
- `capabilities`
- `get_last_error`

`pause` and `resume` are always present in the ABI but may return
`not_supported`.

`poll` is always present and may be a no-op. It exists so sources that prefer a
cooperative model can make progress without owning their own worker thread.

### 2.2 Capability flags

Lock the v1 capability model to this set:

- `finite_import`
- `continuous_stream`
- `direct_ingest`
- `delegated_ingest`
- `supports_pause`
- `has_dialog`

Do not add arbitrary plugin-defined actions in v1.

If the host wants a running source to be reconfigured, it stops it, opens the
dialog again with the saved config, then starts a new or restarted instance.

### 2.3 Runtime states

Lock the DataSource state machine to:

- `idle`
- `configuring`
- `starting`
- `running`
- `paused`
- `stopping`
- `stopped`
- `failed`

Transitions:

- instance creation starts in `idle`
- dialog display is a host concern, but while the host is driving dialog-based
  setup the source is treated as `configuring`
- `start` transitions to `starting`, then either `running` or `failed`
- `pause` transitions `running -> paused`
- `resume` transitions `paused -> running`
- `stop` transitions to `stopping`, then `stopped`
- asynchronous runtime failures transition to `failed`, then the host stops and
  disposes the instance

### 2.4 Host runtime surface

Define one host runtime vtable provided to each DataSource instance.

It must provide:

- message reporting with levels `info`, `warning`, `error`
- progress begin / update / end
- cancel check for long-running finite imports
- state-change notification
- source-initiated close / failure notification
- delegated parser binding

The runtime host API must be safe to call from plugin-owned worker threads.

### 2.5 Delegated parser binding surface

Delegated parsing is not implemented by the DataSource plugin directly.
Instead, the host provides a parser-binding surface through the runtime host.

Define one opaque parser-binding handle and two operations:

- `ensure_parser_binding(binding_request) -> parser_binding_handle`
- `push_raw_message(binding_handle, host_timestamp_ns, payload_bytes)`

`binding_request` must contain:

- topic name
- parser encoding key
- optional type name
- optional schema bytes
- host-owned parser config blob

Rules:

- bindings are scoped to the bound `data_source`
- the host creates the actual `MessageParser` instance for the corresponding
  `(data_source, topic)` pair
- the DataSource never instantiates parser plugins directly
- repeated binding requests for the same topic and same parser metadata are
  idempotent
- if parser metadata changes incompatibly for an already-bound topic, binding is
  an error

### 2.6 Threading rule

For v1, the contract is:

- the write host from `pj_base`
- the DataSource runtime host
- the delegated parser-binding surface

must all be safe to call from source-owned worker threads.

This is required to migrate MQTT, ZMQ, UDP, WebSocket, Foxglove, and the
PlotJuggler bridge without inventing fake polling-only wrappers around their
current behavior.

## 3. Config and Dialog Model

### 3.1 Persisted state envelope

Persist one host-owned JSON envelope per DataSource instance:

```json
{
  "version": 1,
  "source_config": { "... plugin-owned ..." },
  "parser_binding": { "... host-owned ..." }
}
```

Rules:

- `source_config` is produced and consumed only by the DataSource plugin through
  `save_config` / `load_config`
- `parser_binding` is produced and consumed only by the host runtime
- the DataSource plugin never interprets host-owned parser state
- parser configuration remains logically independent even when shown inside the
  same dialog as source-specific controls

### 3.2 Dialog ownership model

When a DataSource has configuration UI:

- the plugin supplies its source-specific dialog through the existing dialog
  protocol
- the host may enrich that dialog with host-owned parser controls for delegated
  sources

Do not create a second modal step just for parser configuration.

The required UX for delegated sources is one integrated dialog.

### 3.3 Parser UI injection

Standardize one reserved placeholder widget name:

- `pj_parser_slot`

If the source dialog contains a widget with that object name, the host renders
the parser selector/config panel there.

If the placeholder is absent, the host appends a default parser group at the end
of the dialog.

This keeps source dialogs simple while still supporting:

- `UDP`
- `Websocket`
- `MQTT`
- `ZMQ`
- `MCAP`
- `Foxglove`
- `PlotJuggler Bridge`

### 3.4 Supported delegated parser UI patterns in v1

Lock v1 to three patterns only:

- `global_selectable_parser`
  - one user-selected parser encoding for the whole source
  - examples: `UDP`, `Websocket`, `MQTT`, `ZMQ`
- `global_fixed_parser`
  - parser encoding fixed by the source
  - examples: `Foxglove`, `PlotJuggler Bridge`
- `per_topic_fixed_parser`
  - parser encoding determined per topic/channel from source metadata
  - example: `MCAP`

Do not support arbitrary per-topic user-selected parser encodings in v1.

### 3.5 Dialog-less start from restored config

The host start flow is:

1. create the DataSource instance
2. bind hosts
3. load `source_config`
4. load the host-owned `parser_binding` state for delegated sources
5. if there is no UI or the host chooses a headless start path, call `start`
6. otherwise open the dialog, merge parser UI if needed, save the envelope, then
   call `start`

The runtime may skip the dialog when restored config is known-valid or when the
caller explicitly requests a headless restart.

## 4. Implementation Phases

### Phase 1: Protocol and wrappers

Build `pj_plugins/data_source_protocol` with:

- raw C ABI header
- C++ SDK wrapper
- host-side RAII loader and instance handle
- unit tests for lifecycle, config, state, and error handling

Acceptance:

- a mock DataSource plugin can be loaded and driven without Qt widgets or parser
  integration
- no exceptions cross the ABI
- error handling is explicit and testable

### Phase 2: Host runtime on top of `pj_base`

Implement the host runtime that composes:

- `PJ_source_write_host_t` from `pj_base`
- message / progress / state host callbacks
- delegated parser binding over the MessageParser family

Acceptance:

- direct sources can write topics and fields through the bound source host
- delegated sources can bind parsers and push raw payloads
- host-mediated stop / failed transitions work for background-thread sources

### Phase 3: Dialog integration and parser UI injection

Extend the Qt dialog engine or add a thin DataSource-specific host layer to:

- show the plugin-owned dialog
- render host-owned parser controls into `pj_parser_slot`
- persist the config envelope
- support dynamic dialog updates driven by `on_tick`

Acceptance:

- one integrated dialog works for both direct and delegated sources
- parser configuration is visibly host-owned but feels native in the same dialog
- restored config round-trips through the envelope without `QSettings`

### Phase 4: First migrations

Migration order is fixed:

1. `DataLoadCSV` or `DataLoadParquet`
2. `DataLoadMCAP`
3. `DataStreamSample`
4. `DataStreamUDP` or `DataStreamWebsocket`
5. `DataStreamMQTT` or `DataStreamZMQ`
6. `DataStreamPlotJugglerBridge`
7. `DataStreamFoxgloveBridge`
8. `DataLoadZcm` and `DataStreamZcm`
9. `DataLoadULog`

Selection rule:

- choose the smaller plugin when two options satisfy the same phase goal

### Phase 5: Legacy compatibility cleanup

After at least one direct and one delegated source are stable:

- remove remaining assumptions that a source mutates shared host containers
- replace old signal-driven host integration with the new runtime host surface
- move all persisted source state into the common save/load path

## 5. Migration Mapping

### Direct finite sources

- `CSV`
- `Parquet`
- `ULog`
- `ZCM log`

Required features:

- bound write host
- dialog protocol
- progress and cancel for large imports
- direct field creation and direct writes

### Delegated finite source

- `MCAP`

Required features:

- bound write host
- integrated host-owned parser UI
- delegated parser binding per topic
- progress and cancel

### Direct continuous sources

- `Sample`
- `ZCM stream`

Required features:

- bound write host
- start / stop lifecycle
- runtime message reporting

### Delegated continuous sources

- `UDP`
- `Websocket`
- `MQTT`
- `ZMQ`
- `Foxglove`
- `PlotJuggler Bridge`

Required features:

- integrated source + parser dialog
- delegated parser binding
- lazy topic creation
- runtime state transitions
- runtime messages
- optional pause / resume

## 6. Test Plan

### Protocol and wrapper tests

- load a mock DataSource plugin
- save / load config round-trip
- state-machine transition coverage
- unsupported `pause` / `resume` returns a structured error, not a crash
- background-thread calls into the runtime host are safe

### Direct-source runtime tests

- one source instance writes multiple topics into one bound `data_source`
- lazy topic creation works after `start`
- stopping a source does not clear already-written data
- headless start from restored config works without opening a dialog

### Delegated-source runtime tests

- `ensure_parser_binding` is idempotent for same topic and metadata
- incompatible rebinding for an existing topic fails cleanly
- pushing raw payloads routes through host-created parser instances
- one DataSource can feed multiple delegated topics concurrently

### Dialog integration tests

- direct source dialog works unchanged
- delegated source dialog renders parser controls in `pj_parser_slot`
- missing `pj_parser_slot` falls back to appended parser group
- dialog-driven topic discovery works with repeated `on_tick`
- source config and parser config persist independently inside the host envelope

### Import / stream behavior tests

- finite import progress updates and cancel path
- continuous source start / stop / failed transitions
- optional pause / resume path
- runtime failure reported by plugin triggers host stop
- status and warning reporting replaces legacy `QMessageBox` coupling

### Migration acceptance tests

- `CSV` or `Parquet` migrated source reproduces old user-visible behavior
- `MCAP` migrated source supports topic selection plus delegated parsing
- `Sample` migrated source proves continuous direct runtime
- one simple delegated continuous source proves end-to-end raw payload routing

## Assumptions and Defaults

- `pj_base` data interfaces are fixed input.
- `MessageParser` remains headless in v1.
- Parser configuration is host-owned and persisted outside plugin-owned
  `source_config`.
- The existing dialog protocol is reused rather than replaced.
- One integrated dialog is required for delegated sources.
- Reconfiguration while running is implemented as stop -> edit -> restart in v1.
- Stopping a DataSource does not implicitly delete or clear the bound
  `data_source`.
- The ULog post-load auxiliary window is not treated as a required v1
  DataSource capability.
