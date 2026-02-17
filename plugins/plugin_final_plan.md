# PlotJuggler Portable Plugin System — Final Unified Plan (Data-Engine Aligned)

This document unifies:

- `plugins/plugin_plan_codex.md`
- `plugins/plugin_plan_claude.md`

and updates the migration strategy to match the current direction of the new
`data/` engine implementation.

---

# Part I — Goals and Direction

## 1.1 Core Goals

The core goals remain unchanged:

- **G1 — Clean data boundary** between plugins and host.
- **G2 — Decoupled plugin families** (DataSource vs Parser vs Transform).
- **G3 — Unified source model** (replace DataLoader/DataStreamer split).
- **G4 — Portable compute plugins** via C ABI + Arrow.
- **G5 — Practical migration** with minimal user-visible breakage.

## 1.2 Data-Engine-Aligned Goal

Add explicit goal:

- **G6 — Engine-first ingestion**: the canonical storage target is the new
  `data/` engine (`DataEngine`, chunked storage, typed writer/reader), with
  legacy structures treated as transitional compatibility views.

## 1.3 Non-Goals (Early Phases)

- Immediate mandatory `.ui + JSON` dialog protocol for all DataSources.
- Full WASM rollout before native C ABI path is stable.
- All plugin families migrated in one technical phase.

---

# Part II — Current `data/` Engine Reality (What the plugin plan must respect)

This section binds plugin architecture to current engine implementation status.

## 2.1 Stable Engine Capabilities

- Chunked topic storage with shared timestamp column.
- Typed write/read flow (`DataWriter`, `DataReader`).
- Time-domain IDs and display offsets.
- Range query and latest-at query APIs.
- Additive schema evolution validation in `TypeRegistry`.

## 2.2 Current Constraints and Gaps

- Writer API is row-builder based (`begin_row/set_*/finish_row`), not
  `MessageView` append APIs.
- Commit path is synchronous (`flush_all()` -> `commit_chunks()`); staged
  multi-thread queue model is deferred.
- Derived DAG engine is not implemented yet.
- Variable-length array expansion at ingest is not implemented yet.
- Timestamps are currently stored as raw `int64`, not delta encoded.

## 2.3 Integration Implications

- The plugin pipeline must target row-builder ingestion first.
- Transform portability and scheduling must proceed independently from the
  not-yet-implemented derived DAG internals.
- Time ordering semantics must be explicit at the ingestion boundary.

---

# Part III — Unified Plugin Architecture

## 3.1 Plugin Families

1. **Parser plugin**: C ABI + Arrow output.
2. **Transform plugin**: C ABI + Arrow input/output.
3. **DataSource plugin**: Qt/C++ source plugin with `Direct/Delegated/Both`.
4. **StatePublisher/Toolbox**: out of early migration scope.

## 3.2 Data Flow (Target)

```
DataSource (Direct or Delegated)
        |
        v
Host dispatch (parser selection, lifecycle, error policy)
        |
        v
Arrow batch normalization (wide/tall/scatter + metadata)
        |
        v
Arrow -> DataWriter adapter (row appends)
        |
        v
TopicChunkBuilder -> seal -> commit_chunks -> DataReader queries
```

## 3.3 Transitional Compatibility

- Host may maintain a compatibility adapter for legacy consumers.
- Source of truth for newly migrated paths is the `data/` engine.
- Compatibility outputs are generated from engine reads, not parallel writes.

---

# Part IV — C ABI Specification (Handle-Based, Versioned)

## 4.1 Versioning Model

Each ABI family has independent versioning:

- `pj_parser`: `abi_major`, `abi_minor`
- `pj_transform`: `abi_major`, `abi_minor`

Compatibility rules:

1. Host requires exact `abi_major` match.
2. Host accepts plugin `abi_minor <= host_abi_minor`.
3. Additive optional functionality is feature-probed (symbol lookup).

## 4.2 Parser ABI (required)

```c
typedef struct pj_parser_instance* pj_parser_handle;

typedef struct {
    uint32_t abi_major;
    uint32_t abi_minor;
    const char* plugin_name;
    const char* plugin_version;
    const char* encoding;
} pj_parser_info;

int32_t pj_parser_get_info(pj_parser_info* out_info);
int32_t pj_parser_create(const char* init_json, uint32_t init_len,
                         pj_parser_handle* out);
void    pj_parser_destroy(pj_parser_handle h);
void    pj_parser_free(void* ptr);

int32_t pj_parser_configure(pj_parser_handle h,
                            const char* topic_name,
                            const char* type_name,
                            const uint8_t* schema_data,
                            uint32_t schema_len,
                            const char* config_json,
                            uint32_t config_len);

int32_t pj_parser_parse_native(pj_parser_handle h,
                               const uint8_t* msg_data,
                               uint32_t msg_len,
                               double host_timestamp,
                               struct ArrowArray* out_array,
                               struct ArrowSchema* out_schema);
```

## 4.3 Parser ABI (optional)

```c
int32_t pj_parser_parse_ipc(pj_parser_handle h,
                            const uint8_t* msg_data,
                            uint32_t msg_len,
                            double host_timestamp,
                            uint8_t** out_ipc,
                            uint32_t* out_ipc_len);

const char* pj_parser_describe_parameters(pj_parser_handle h);
```

## 4.4 Transform ABI (same pattern)

- `get_info/create/destroy/free/configure/calculate/reset`
- Handle-based, no global state assumptions.

---

# Part V — Data Contract

## 5.1 Timestamp Contract (engine-aligned)

1. Parser output timestamp column is authoritative.
2. Host-provided timestamp is fallback input only.
3. Canonical ingestion timestamp type is **`int64` nanoseconds**.
4. If parser emits float timestamp, host performs one boundary conversion to
   `int64` ns before writer append.

## 5.2 Supported Arrow Shapes

### Wide timeseries

- Required: `_timestamp`
- Additional columns map to series fields.

### Tall timeseries

- Required: `_series_name`, `_timestamp`, value columns.

### Scatter XY

- Required: `_x`, `_y`
- Optional: `_series_name`
- No synthetic timestamp required.

### Metadata sidecar

- Stored in Arrow schema metadata key: `pj:metadata`.

## 5.3 Ordering Contract

The plugin boundary must define one of these host policies (configurable):

1. Reject out-of-order rows per topic.
2. Accept and preserve append order (document query semantics impact).
3. Buffer-sort before commit (bounded latency/memory).

The default policy for initial migration should be explicit and tested.

---

# Part VI — Discovery, Loading, Packaging

## 6.1 Manifest

Each C ABI plugin ships `plugin.json`:

```json
{
  "name": "my_parser",
  "type": "parser",
  "abi_family": "pj_parser",
  "abi_major": 1,
  "abi_minor": 0,
  "plugin_version": "1.2.0",
  "encoding": "protobuf",
  "format": "native",
  "library": "libmy_parser.so",
  "min_host_version": "4.0.0"
}
```

## 6.2 Search Paths

1. User marketplace cache.
2. User local plugin directory.
3. System install plugin directory.
4. Environment path override.

## 6.3 Loader Policy

1. Load with `RTLD_LOCAL`.
2. Resolve required symbols first; fail closed if missing.
3. Resolve optional symbols opportunistically.
4. Isolate instance state per plugin handle.

---

# Part VII — DataSource Strategy

## 7.1 Modes

- `Direct`: DataSource emits Arrow.
- `Delegated`: DataSource emits raw envelopes; host invokes parser.
- `Both`: supports both paths.

## 7.2 Native Dialogs First

- Keep native `QWidget*` dialogs in early phases.
- Host injects parser selector/config area for delegated sources.
- `.ui + JSON` host-rendered protocol remains optional later-phase work.

## 7.3 Config Precedence

1. Layout-restored config.
2. Global preferences.
3. Plugin defaults.

---

# Part VIII — Host Architecture (Engine-first details)

## 8.1 Parser Dispatch Model

- Cache parser instances per topic (or topic + schema key).
- Reconfigure parser on schema hash changes.
- User-selected parser acts as default; explicit per-message encoding overrides.

## 8.2 Arrow-to-Engine Adapter

Adapter responsibilities:

1. Validate required columns by shape.
2. Normalize timestamp to `int64` ns.
3. Resolve/register schema and topic descriptors.
4. Write rows via `begin_row/set_*/finish_row`.
5. Flush/commit chunks in deterministic order.

## 8.3 Error and Warning Semantics

- Plugins do not show blocking host-independent dialogs in the hot path.
- Warnings are surfaced as structured events.
- Host applies policy: continue/skip/abort and presents summary UX.

## 8.4 Legacy Bridge

- Temporary bridge can project engine data into old containers for unaffected
  UI/tooling modules.
- Bridge is explicitly transitional and removed after parity sign-off.

---

# Part IX — SDK Plan

## 9.1 SDK Contents

- `pj_plugin_api.h` (C ABI).
- nanoarrow headers.
- Optional C++ convenience wrappers/macros.
- Example parser/transform plugins.
- Contract test harness (symbol/lifecycle/ownership checks).

## 9.2 Reference Examples

- Minimal parser (wide output).
- Dynamic parser (tall output).
- Scatter parser.
- SISO transform.
- MIMO transform.

---

# Part X — Migration Inventory and Sequencing

## 10.1 First Wave (low risk, high leverage)

- DataStreamSample (direct).
- DataLoadParquet (direct).
- ParserDataTamer.
- ParserIDL.
- ParserLineInflux.

## 10.2 Second Wave

- DataLoadULog.
- UDP/WebSocket/ZMQ streamers (delegated).
- ParserROS variants.

## 10.3 Third Wave

- DataLoadMCAP.
- DataStreamMQTT.
- DataLoadCSV.
- ParserProtobuf split model.
- ZCM loaders/streamers.

## 10.4 Later

- Transform family migration.
- Optional dialog protocol modernization.
- WASM runtime and marketplace hardening.

---

# Part XI — Detailed Phase Plan with Gates

## Phase 0 — Foundation Hardening (Immediate)

Scope:

1. Resolve toolchain/build compatibility for `data/` integration path.
2. Tighten writer lifecycle and bounds validation behavior.
3. Lock and test timestamp ordering policy.
4. Add explicit ingestion-path diagnostics (not silent no-ops).

Exit gates:

1. CI build matrix green on supported toolchains.
2. Negative-path tests for invalid column/topic/row lifecycle.
3. Documented and tested ordering policy.

## Phase 1 — Host Dispatch Decoupling in Current System

Scope:

1. Move parser selection and instantiation to host.
2. Keep current plugin interfaces initially.
3. Remove duplicated source-side parser UI logic.

Exit gates:

1. Functional parity for migrated built-ins.
2. No parser-selection UX regression.

## Phase 2 — Parser C ABI + Arrow-to-Engine Ingestion

Scope:

1. Introduce parser C ABI v1.
2. Implement discovery + loader enforcement.
3. Ship Arrow-to-engine adapter.
4. Port easiest parsers to validate full path.

Exit gates:

1. ABI contract tests pass.
2. Engine parity tests pass against baseline fixtures.
3. Performance thresholds met (throughput, memory, latency).

## Phase 3 — Unified DataSource API

Scope:

1. Introduce `Direct/Delegated/Both` DataSource base class.
2. Port simple sources, then complex interactive sources.
3. Keep native dialogs and host parser injection.

Exit gates:

1. Layout save/restore parity.
2. Output parity across migrated plugins.

## Phase 4 — Transform C ABI and Runtime Integration

Scope:

1. Introduce transform C ABI v1.
2. Port SISO then MIMO transforms.
3. Add incremental/batched transform strategy.

Exit gates:

1. Numerical parity within tolerance.
2. No unacceptable live-latency regressions.

## Phase 5 — Optional Enhancements

Scope:

1. `.ui + JSON` host-rendered dialog protocol (opt-in).
2. WASM support (Arrow IPC adapter path).
3. Marketplace trust and distribution UX.

---

# Part XII — Test and Benchmark Strategy

## 12.1 C ABI Contract Tests

- Required symbol presence.
- Optional symbol probing behavior.
- Multi-instance isolation.
- Configure-before-parse error behavior.
- Ownership correctness (`release`, `pj_parser_free`).

## 12.2 Parity Tests

- Golden input sets through old and new paths.
- Compare series identity, row counts, values, timestamps, null semantics.
- Include mixed-encoding and schema-evolution scenarios.

## 12.3 Engine-specific Correctness

- Range query correctness on ingested plugin data.
- Latest-at correctness.
- Retention behavior with non-positive timestamps.
- Schema evolution additive-only correctness.

## 12.4 Performance Gates

For each phase with data-path changes, collect:

- Ingest throughput.
- Peak RSS.
- Poll-to-commit latency.
- Transform update latency on large datasets.

Use phase baselines, and fail CI for defined regression thresholds.

---

# Part XIII — Risks and Mitigations

## 13.1 Risks

1. ABI instability.
2. Engine integration regressions.
3. Ambiguous timestamp/order semantics.
4. Transform recompute cost at scale.
5. Ecosystem migration friction.

## 13.2 Mitigations

1. Strict ABI major/minor governance.
2. Adapter + parity tests at every phase gate.
3. Explicit ordering policy and enforcement.
4. Incremental transform strategy with benchmark gates.
5. SDK, examples, and beta compatibility sprint for external maintainers.

---

# Part XIV — Open Questions

1. Default ordering policy for out-of-order ingest at host boundary.
2. Batch sizing policy for delegated parse calls under live streaming.
3. Exact retention/eviction policy exposure in plugin-facing configuration.
4. Compatibility layer lifetime and cutover criteria.
5. WASM runtime target and security model for marketplace delivery.

---

# Part XV — Final Recommendation

Proceed with a **compute-first, engine-first** migration:

1. Stabilize `data/` integration preconditions (Phase 0).
2. Decouple host parser dispatch quickly (Phase 1).
3. Deliver parser C ABI and Arrow-to-engine path early (Phase 2).
4. Migrate DataSources progressively with native dialogs preserved (Phase 3).
5. Add transform ABI once ingest path is stable (Phase 4).

This sequencing preserves momentum, reduces migration risk, and keeps all new
plugin work aligned with the long-term `data/` engine architecture.
