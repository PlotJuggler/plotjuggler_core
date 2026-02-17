# Data Engine Phase 1 Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement the core data engine with typed columnar storage, chunk lifecycle, type tree registry, baseline encodings, and query APIs.

**Architecture:** Layered design separating logical (type tree, metadata, IDs) from physical (column buffers, encodings, chunks). Data flows: Plugin Writer -> Building Chunk -> seal -> TopicStorage (committed) -> Reader -> Query cursors. All types follow the C++20 API Design Recommendations with Abseil.

**Tech Stack:** C++20, Abseil (flat_hash_map, StatusOr, StrCat, Duration/Time), GoogleTest, CMake + Conan 2

**Coding Standard:** `docs/cpp_design_recommendations.md` + `.clang-tidy` config. snake_case functions/variables, CamelCase types, `_` member suffix, `k` prefix for constants/enum values. `[[nodiscard]]` on fallible returns. `const` by default. `absl::StatusOr<T>` for recoverable errors. `absl::flat_hash_map` over `std::unordered_map`.

---

## Directory Layout

```
data/
├── conanfile.txt
├── CMakeLists.txt
├── include/
│   └── pj/
│       └── engine/
│           ├── types.hpp           # Core IDs, Timestamp, NumericType, NumericValue
│           ├── type_tree.hpp       # TypeKind, PrimitiveType, TypeTreeNode
│           ├── type_registry.hpp   # TypeRegistry (register/lookup schemas)
│           ├── buffer.hpp          # RawBuffer, validity bitmap ops
│           ├── column_buffer.hpp   # TypedColumnBuffer (append/read per type)
│           ├── encoding.hpp        # Delta, dictionary, packed bitfield encoders
│           ├── chunk.hpp           # ChunkStats, TopicChunk, build/seal
│           ├── topic_storage.hpp   # TopicStorage (sealed chunks, eviction)
│           ├── dataset.hpp         # DatasetDescriptor, TimeDomain
│           ├── query.hpp           # RangeCursor, LatestAtResult, QueryRange, QueryPoint
│           ├── writer.hpp          # IDataWriter, TopicWriteHandle, ScalarSeriesHandle
│           ├── reader.hpp          # IDataReader
│           └── engine.hpp          # DataEngine coordinator
├── src/
│   ├── type_registry.cpp
│   ├── buffer.cpp
│   ├── column_buffer.cpp
│   ├── encoding.cpp
│   ├── chunk.cpp
│   ├── topic_storage.cpp
│   ├── query.cpp
│   ├── writer.cpp
│   ├── reader.cpp
│   └── engine.cpp
└── tests/
    ├── types_test.cpp
    ├── type_tree_test.cpp
    ├── type_registry_test.cpp
    ├── buffer_test.cpp
    ├── column_buffer_test.cpp
    ├── encoding_test.cpp
    ├── chunk_test.cpp
    ├── topic_storage_test.cpp
    ├── query_test.cpp
    └── engine_integration_test.cpp
```

---

### Task 1: Project Scaffold + Build System

**Files:**
- Create: `data/conanfile.txt`
- Create: `data/CMakeLists.txt`

**Step 1: Create conanfile.txt**

```ini
[requires]
abseil/20240722.0
gtest/1.15.0

[generators]
CMakeDeps
CMakeToolchain

[layout]
cmake_layout
```

**Step 2: Create CMakeLists.txt**

CMake project that:
- Requires C++20
- Uses Conan-generated find_package for Abseil and GoogleTest
- Declares a `pj_engine` static library (all `src/*.cpp`)
- Declares test executables (all `tests/*_test.cpp`)
- Sets strict warning flags from the design guide

```cmake
cmake_minimum_required(VERSION 3.24)
project(pj_engine LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(absl REQUIRED)
find_package(GTest REQUIRED)

add_library(pj_engine STATIC
  src/type_registry.cpp
  src/buffer.cpp
  src/column_buffer.cpp
  src/encoding.cpp
  src/chunk.cpp
  src/topic_storage.cpp
  src/query.cpp
  src/writer.cpp
  src/reader.cpp
  src/engine.cpp
)
target_include_directories(pj_engine PUBLIC include)
target_compile_features(pj_engine PUBLIC cxx_std_20)
target_compile_options(pj_engine PRIVATE
  -Wall -Wextra -Wshadow -Wnon-virtual-dtor -Wold-style-cast
  -Wcast-qual -Wconversion -Woverloaded-virtual -Wpedantic
)
target_link_libraries(pj_engine PUBLIC
  absl::flat_hash_map
  absl::flat_hash_set
  absl::btree
  absl::status
  absl::statusor
  absl::strings
  absl::str_format
  absl::hash
  absl::inlined_vector
  absl::span
  absl::time
)

enable_testing()

set(TEST_SOURCES
  tests/types_test.cpp
  tests/type_tree_test.cpp
  tests/type_registry_test.cpp
  tests/buffer_test.cpp
  tests/column_buffer_test.cpp
  tests/encoding_test.cpp
  tests/chunk_test.cpp
  tests/topic_storage_test.cpp
  tests/query_test.cpp
  tests/engine_integration_test.cpp
)

foreach(test_src ${TEST_SOURCES})
  get_filename_component(test_name ${test_src} NAME_WE)
  add_executable(${test_name} ${test_src})
  target_link_libraries(${test_name} PRIVATE pj_engine GTest::gtest_main)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
```

**Step 2: Create stub source files**

Create empty/minimal `.cpp` stubs for all source files so the build can succeed immediately. Each stub just includes its header.

**Step 3: Install dependencies and build**

```bash
cd data && conan install . --output-folder=build --build=missing -s build_type=Debug
cmake -B data/build -S data -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=data/build/conan_toolchain.cmake
cmake --build data/build -j$(nproc)
```

Expected: Build succeeds (empty library, no tests yet).

**Step 4: Commit**

```bash
git add data/conanfile.txt data/CMakeLists.txt data/include/ data/src/ data/tests/
git commit -m "feat(engine): scaffold Phase 1 project structure with CMake + Conan"
```

---

### Task 2: Core Types

**Files:**
- Create: `data/include/pj/engine/types.hpp`
- Create: `data/tests/types_test.cpp`

Core ID aliases, Timestamp, NumericType enum, and NumericValue variant.

```cpp
// types.hpp
#pragma once
#include <cstdint>
#include <variant>

namespace pj::engine {

using DatasetId    = uint32_t;
using TopicId      = uint32_t;
using FieldId      = uint32_t;
using ChunkId      = uint64_t;
using TimeDomainId = uint32_t;
using SchemaId     = uint32_t;
using PluginId     = uint32_t;

using Timestamp = int64_t;  // nanoseconds since epoch

enum class NumericType : uint8_t {
  kFloat32,
  kFloat64,
  kInt8,
  kInt16,
  kInt32,
  kInt64,
  kUint8,
  kUint16,
  kUint32,
  kUint64,
};

using NumericValue = std::variant<
    float, double,
    int8_t, int16_t, int32_t, int64_t,
    uint8_t, uint16_t, uint32_t, uint64_t>;

// Size in bytes of each numeric type
[[nodiscard]] constexpr size_t numeric_type_size(NumericType type) noexcept;

// Map a NumericValue variant index to NumericType
[[nodiscard]] constexpr NumericType numeric_value_type(const NumericValue& v) noexcept;

// Convert any NumericValue to double (for stats, display)
[[nodiscard]] constexpr double numeric_value_to_double(const NumericValue& v) noexcept;

constexpr ChunkId kInvalidChunkId = 0;

}  // namespace pj::engine
```

**Tests:**
- `numeric_type_size` returns correct sizes for all types
- `numeric_value_type` correctly identifies variant alternatives
- `numeric_value_to_double` converts all types accurately
- ID types are distinct aliases (compile-time check not needed, just document)

**Commit:** `feat(engine): add core type aliases, NumericType, NumericValue`

---

### Task 3: Type Tree Nodes

**Files:**
- Create: `data/include/pj/engine/type_tree.hpp`
- Create: `data/tests/type_tree_test.cpp`

Defines the recursive type tree structure. Represents schemas hierarchically.

```cpp
// type_tree.hpp (key types)
#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "pj/engine/types.hpp"

namespace pj::engine {

enum class PrimitiveType : uint8_t {
  kFloat32, kFloat64,
  kInt8, kInt16, kInt32, kInt64,
  kUint8, kUint16, kUint32, kUint64,
  kBool, kString,
};

enum class TypeKind : uint8_t {
  kPrimitive,
  kStruct,
  kArray,
  kEnum,
};

struct EnumMapping {
  absl::flat_hash_map<int64_t, std::string> value_to_name;
  absl::flat_hash_map<std::string, int64_t> name_to_value;
};

struct TypeTreeNode {
  std::string name;
  TypeKind kind;
  absl::flat_hash_set<std::string> semantic_tags;  // e.g., "quaternion", "pose"

  // kPrimitive
  std::optional<PrimitiveType> primitive_type;

  // kStruct: ordered child fields
  std::vector<std::shared_ptr<TypeTreeNode>> children;

  // kArray: element type + optional fixed size
  std::shared_ptr<TypeTreeNode> element_type;
  std::optional<uint32_t> fixed_array_size;

  // kEnum: wire-value <-> name mapping
  std::optional<EnumMapping> enum_mapping;
};

// Factory functions (prefer over direct construction)
[[nodiscard]] std::shared_ptr<TypeTreeNode> make_primitive(
    std::string name, PrimitiveType type);

[[nodiscard]] std::shared_ptr<TypeTreeNode> make_struct(
    std::string name, std::vector<std::shared_ptr<TypeTreeNode>> children);

[[nodiscard]] std::shared_ptr<TypeTreeNode> make_array(
    std::string name, std::shared_ptr<TypeTreeNode> element_type,
    std::optional<uint32_t> fixed_size = std::nullopt);

[[nodiscard]] std::shared_ptr<TypeTreeNode> make_enum(
    std::string name, PrimitiveType underlying_type, EnumMapping mapping);

// Flatten a type tree into an ordered list of leaf field paths
// e.g., Pose -> ["frame_name", "position.x", "position.y", "position.z",
//                "rotation.w", "rotation.x", "rotation.y", "rotation.z"]
[[nodiscard]] std::vector<std::string> flatten_field_paths(
    const TypeTreeNode& root);

// Count leaf (primitive/enum) fields in a type tree
[[nodiscard]] size_t count_leaf_fields(const TypeTreeNode& root);

}  // namespace pj::engine
```

**Tests:**
- Build a `robot_pose` type tree (position.xyz + rotation.wxyz + frame_name)
- `flatten_field_paths` returns correct 8 leaf paths in order
- `count_leaf_fields` returns 8
- Factory functions set correct TypeKind
- Semantic tags are preserved
- Array type tree with fixed size

**Commit:** `feat(engine): add TypeTreeNode, factory functions, flattening`

---

### Task 4: Type Registry

**Files:**
- Create: `data/include/pj/engine/type_registry.hpp`
- Create: `data/src/type_registry.cpp`
- Create: `data/tests/type_registry_test.cpp`

Central schema registry with late discovery support.

```cpp
// type_registry.hpp (key interface)
class TypeRegistry {
public:
  // Register a known schema (from Protobuf, ROS, etc.)
  [[nodiscard]] absl::StatusOr<SchemaId> register_schema(
      std::string schema_name, std::shared_ptr<TypeTreeNode> type_tree);

  // Late discovery: register from first message (JSON, etc.)
  // Returns existing schema if name already registered.
  [[nodiscard]] absl::StatusOr<SchemaId> register_or_get(
      std::string schema_name, std::shared_ptr<TypeTreeNode> type_tree);

  // Lookup
  [[nodiscard]] const TypeTreeNode* lookup(SchemaId id) const;
  [[nodiscard]] std::optional<SchemaId> find_by_name(std::string_view name) const;

  // Schema evolution: add fields to an existing schema (additive only)
  [[nodiscard]] absl::Status evolve_schema(
      SchemaId id, std::shared_ptr<TypeTreeNode> updated_tree);

private:
  SchemaId next_id_ = 1;
  absl::flat_hash_map<SchemaId, std::shared_ptr<TypeTreeNode>> schemas_;
  absl::flat_hash_map<std::string, SchemaId> name_to_id_;
};
```

**Tests:**
- Register a schema, lookup by ID and name
- `register_or_get` returns existing ID for duplicate name
- `register_schema` fails for duplicate name (returns error)
- `evolve_schema` accepts additive changes (new field added)
- `evolve_schema` rejects type changes on existing fields (returns error)
- `lookup` returns nullptr for unknown ID

**Commit:** `feat(engine): add TypeRegistry with late discovery and evolution`

---

### Task 5: Raw Buffer + Validity Bitmap

**Files:**
- Create: `data/include/pj/engine/buffer.hpp`
- Create: `data/src/buffer.cpp`
- Create: `data/tests/buffer_test.cpp`

Raw byte buffer (owns memory, supports append and random access) and validity bitmap operations.

```cpp
// buffer.hpp (key types)
class RawBuffer {
public:
  RawBuffer() = default;
  explicit RawBuffer(size_t initial_capacity);

  void reserve(size_t capacity);
  void append(const void* data, size_t size);
  void resize(size_t new_size);
  void clear();

  [[nodiscard]] const uint8_t* data() const noexcept;
  [[nodiscard]] uint8_t* mutable_data() noexcept;
  [[nodiscard]] size_t size() const noexcept;
  [[nodiscard]] size_t capacity() const noexcept;
  [[nodiscard]] bool empty() const noexcept;

private:
  std::vector<uint8_t> data_;
};

// Validity bitmap operations (Arrow-compatible: bit index = row index)
namespace validity_bitmap {

void init(RawBuffer& buf, size_t num_bits);
void set_valid(RawBuffer& buf, size_t bit_index);
void set_null(RawBuffer& buf, size_t bit_index);
[[nodiscard]] bool is_valid(const RawBuffer& buf, size_t bit_index);
[[nodiscard]] size_t count_nulls(const RawBuffer& buf, size_t num_bits);

// Required bytes for `num_bits` bits (ceil division)
[[nodiscard]] constexpr size_t bytes_for_bits(size_t num_bits) noexcept;

}  // namespace validity_bitmap
```

**Tests:**
- RawBuffer: append data, read back, reserve/resize
- Validity bitmap: set/clear/query individual bits
- Validity bitmap: count nulls
- Boundary: bit index at byte boundaries (7, 8, 15, 16)
- `bytes_for_bits(0) == 0`, `bytes_for_bits(1) == 1`, `bytes_for_bits(8) == 1`, `bytes_for_bits(9) == 2`

**Commit:** `feat(engine): add RawBuffer and validity bitmap operations`

---

### Task 6: Typed Column Buffer

**Files:**
- Create: `data/include/pj/engine/column_buffer.hpp`
- Create: `data/src/column_buffer.cpp`
- Create: `data/tests/column_buffer_test.cpp`

Typed append/read for each primitive type. Wraps RawBuffer with type awareness.

```cpp
// column_buffer.hpp (key interface)
enum class EncodingType : uint8_t {
  kRaw,          // Unencoded typed storage
  kDelta,        // Delta encoding (timestamps)
  kDictionary,   // Dictionary encoding (strings)
  kPackedBool,   // Packed bitfield (bools)
};

struct ColumnDescriptor {
  FieldId field_id;
  PrimitiveType logical_type;
  std::string field_path;  // e.g., "position.x"
};

class TypedColumnBuffer {
public:
  explicit TypedColumnBuffer(ColumnDescriptor descriptor);

  [[nodiscard]] const ColumnDescriptor& descriptor() const noexcept;
  [[nodiscard]] size_t row_count() const noexcept;
  [[nodiscard]] bool has_nulls() const noexcept;

  // Append a typed value. Type must match logical_type.
  void append_float32(float value);
  void append_float64(double value);
  void append_int32(int32_t value);
  void append_int64(int64_t value);
  void append_uint8(uint8_t value);
  void append_uint16(uint16_t value);
  void append_uint32(uint32_t value);
  void append_uint64(uint64_t value);
  void append_int8(int8_t value);
  void append_int16(int16_t value);
  void append_bool(bool value);
  void append_string(std::string_view value);
  void append_null();

  // Read (raw unencoded access)
  [[nodiscard]] float read_float32(size_t row) const;
  [[nodiscard]] double read_float64(size_t row) const;
  [[nodiscard]] int32_t read_int32(size_t row) const;
  [[nodiscard]] int64_t read_int64(size_t row) const;
  // ... analogous for other types
  [[nodiscard]] std::string_view read_string(size_t row) const;
  [[nodiscard]] bool read_bool(size_t row) const;
  [[nodiscard]] bool is_null(size_t row) const;

  // Read as double (for numeric columns — stats, display)
  [[nodiscard]] double read_as_double(size_t row) const;

  // Access underlying buffers (for encoding at seal time)
  [[nodiscard]] const RawBuffer& value_buffer() const noexcept;
  [[nodiscard]] const RawBuffer& validity_buffer() const noexcept;
  [[nodiscard]] const RawBuffer& offsets_buffer() const noexcept;  // strings only

private:
  ColumnDescriptor descriptor_;
  RawBuffer values_;
  RawBuffer validity_;
  RawBuffer offsets_;     // For variable-length (string): offset array
  size_t row_count_ = 0;
  size_t null_count_ = 0;
};
```

String storage: offset buffer (uint32_t per string) + value buffer (concatenated raw bytes). `offsets_[i]` is the byte offset of string `i`, `offsets_[i+1] - offsets_[i]` is the length.

**Tests:**
- Append and read back float32, float64, int32, int64, bool, string
- Append null, verify `is_null` returns true, other rows return false
- `read_as_double` works for all numeric types
- String: multiple strings, read back matches
- `row_count` increments correctly
- `has_nulls` returns false when no nulls appended

**Commit:** `feat(engine): add TypedColumnBuffer with typed append/read`

---

### Task 7: Delta Encoding

**Files:**
- Create: `data/include/pj/engine/encoding.hpp`
- Create: `data/src/encoding.cpp`
- Create: `data/tests/encoding_test.cpp`

Delta encoding for timestamp columns. Also dictionary encoding for strings and packed bitfield for bools.

```cpp
// encoding.hpp
namespace pj::engine::encoding {

// Delta encoding: stores first value + deltas as int64_t
struct DeltaEncoded {
  int64_t base_value;
  RawBuffer deltas;  // int64_t deltas
  size_t count;
};

[[nodiscard]] DeltaEncoded delta_encode(
    const int64_t* values, size_t count);

void delta_decode(
    const DeltaEncoded& encoded, int64_t* output, size_t count);

// Dictionary encoding for strings
struct DictionaryEncoded {
  std::vector<std::string> dictionary;  // unique values
  RawBuffer indices;                     // uint32_t indices into dictionary
  size_t count;
};

[[nodiscard]] DictionaryEncoded dictionary_encode(
    const TypedColumnBuffer& string_column);

[[nodiscard]] std::string_view dictionary_lookup(
    const DictionaryEncoded& encoded, size_t row);

// Packed bitfield for bools (1 bit per value)
struct PackedBools {
  RawBuffer bits;
  size_t count;
};

[[nodiscard]] PackedBools pack_bools(const uint8_t* values, size_t count);

[[nodiscard]] bool unpack_bool(const PackedBools& packed, size_t index);

}  // namespace pj::engine::encoding
```

**Tests (encoding_test.cpp):**
- Delta encode monotonic timestamps, decode, verify round-trip exact
- Delta encode with negative deltas (non-monotonic), round-trip
- Delta encode single value, empty
- Dictionary encode repeated strings ("base", "world", "base", "base"), verify dictionary has 2 entries
- Dictionary lookup returns correct strings
- Pack bools: 16 values, verify each unpacks correctly
- Boundary: bool pack at byte boundary (8, 9 values)

**Commit:** `feat(engine): add delta, dictionary, and packed bool encodings`

---

### Task 8: Chunk Stats + TopicChunk

**Files:**
- Create: `data/include/pj/engine/chunk.hpp`
- Create: `data/src/chunk.cpp`
- Create: `data/tests/chunk_test.cpp`

ChunkStats (incremental update during build), TopicChunk (build phase + seal).

```cpp
// chunk.hpp (key types)
struct ColumnStats {
  uint32_t null_count = 0;
  uint32_t run_count = 0;
  bool is_constant = true;
  std::optional<double> min_value;
  std::optional<double> max_value;
};

struct ChunkStats {
  Timestamp t_min = std::numeric_limits<Timestamp>::max();
  Timestamp t_max = std::numeric_limits<Timestamp>::min();
  uint32_t row_count = 0;
  std::vector<ColumnStats> column_stats;
};

class TopicChunkBuilder {
public:
  TopicChunkBuilder(TopicId topic_id, SchemaId schema_id,
                    std::vector<ColumnDescriptor> columns,
                    uint32_t max_rows);

  // Append a row: timestamp + values for all columns
  // Values provided via a callback or struct to avoid giant parameter list
  struct RowBuilder {
    void set_float32(FieldId field, float value);
    void set_float64(FieldId field, double value);
    void set_int32(FieldId field, int32_t value);
    void set_int64(FieldId field, int64_t value);
    void set_string(FieldId field, std::string_view value);
    void set_bool(FieldId field, bool value);
    void set_null(FieldId field);
    // ... other numeric types
  };

  [[nodiscard]] RowBuilder start_row(Timestamp timestamp);
  void finish_row(RowBuilder&& row);

  [[nodiscard]] bool is_full() const noexcept;
  [[nodiscard]] uint32_t row_count() const noexcept;
  [[nodiscard]] const ChunkStats& stats() const noexcept;

  // Seal: finalize stats, select encodings, produce immutable TopicChunk
  [[nodiscard]] TopicChunk seal();

private:
  TopicId topic_id_;
  SchemaId schema_id_;
  uint32_t max_rows_;
  ChunkId chunk_id_;

  RawBuffer timestamps_;                  // Raw int64_t timestamps
  std::vector<TypedColumnBuffer> columns_;
  ChunkStats stats_;
};

struct TopicChunk {
  ChunkId id;
  TopicId topic_id;
  SchemaId schema_version;
  ChunkStats stats;

  // Encoded data (immutable after seal)
  encoding::DeltaEncoded encoded_timestamps;
  std::vector<RawBuffer> encoded_columns;       // Per-column encoded data
  std::vector<EncodingType> column_encodings;   // What encoding each column uses
  std::vector<RawBuffer> validity_bitmaps;      // Per-column (empty if no nulls)

  // For dictionary-encoded string columns
  std::vector<std::optional<encoding::DictionaryEncoded>> dictionary_columns;

  // For packed bool columns
  std::vector<std::optional<encoding::PackedBools>> packed_bool_columns;

  // Decode helpers
  [[nodiscard]] Timestamp read_timestamp(size_t row) const;
  [[nodiscard]] double read_numeric_as_double(FieldId field, size_t row) const;
  [[nodiscard]] std::string_view read_string(FieldId field, size_t row) const;
  [[nodiscard]] bool read_bool(FieldId field, size_t row) const;
  [[nodiscard]] bool is_null(FieldId field, size_t row) const;
};
```

Seal logic:
1. Delta-encode the timestamp column
2. For each column based on type:
   - Numeric (float/int/uint): copy raw buffer as-is (kRaw encoding)
   - String: dictionary encode
   - Bool: pack to bitfield
3. Finalize stats
4. Generate monotonic ChunkId

**Tests:**
- Build a chunk with 3 rows of float32 data, seal it
- Verify `stats.t_min`, `stats.t_max`, `stats.row_count`
- Verify `stats.column_stats[i].min_value`, `max_value`
- Read back timestamps and values from sealed chunk
- Verify `is_full` after `max_rows` reached
- Build chunk with string column, seal, verify dictionary encoding applied
- Build chunk with bool column, seal, verify packed bitfield
- Build chunk with nulls, verify validity bitmap and null_count

**Commit:** `feat(engine): add TopicChunkBuilder, seal with encoding selection`

---

### Task 9: Topic Storage + Eviction

**Files:**
- Create: `data/include/pj/engine/topic_storage.hpp`
- Create: `data/src/topic_storage.cpp`
- Create: `data/tests/topic_storage_test.cpp`

Manages the committed chunk deque and time-based eviction.

```cpp
// topic_storage.hpp
struct TopicDescriptor {
  std::string name;
  SchemaId schema_id;
  DatasetId dataset_id;
  uint32_t max_chunk_rows = 1024;  // Default chunk size
};

struct TopicMetadata {
  TopicId topic_id;
  std::string name;
  SchemaId current_schema;
  DatasetId dataset_id;
  Timestamp time_range_min;
  Timestamp time_range_max;
  uint64_t total_row_count;
  uint64_t total_byte_size;  // approximate
};

class TopicStorage {
public:
  TopicStorage(TopicId topic_id, TopicDescriptor descriptor);

  void append_sealed_chunk(TopicChunk chunk);
  void evict_before(Timestamp t_keep_min);

  [[nodiscard]] const std::deque<TopicChunk>& sealed_chunks() const noexcept;
  [[nodiscard]] TopicMetadata metadata() const;
  [[nodiscard]] const TopicDescriptor& descriptor() const noexcept;
  [[nodiscard]] TopicId topic_id() const noexcept;
  [[nodiscard]] bool empty() const noexcept;

  // Time range of all committed data
  [[nodiscard]] Timestamp time_min() const noexcept;
  [[nodiscard]] Timestamp time_max() const noexcept;

  // Schema evolution
  void update_schema(SchemaId new_schema);

private:
  TopicId topic_id_;
  TopicDescriptor descriptor_;
  std::deque<TopicChunk> sealed_chunks_;
};
```

**Tests:**
- Append 3 sealed chunks, verify `sealed_chunks().size() == 3`
- `time_min()` / `time_max()` reflect overall range
- `evict_before(t)` removes chunks with `t_max < t`, keeps the rest
- Eviction at various points: evict none, evict some, evict all
- `metadata()` returns correct aggregate stats
- `empty()` returns true initially, false after append

**Commit:** `feat(engine): add TopicStorage with eviction`

---

### Task 10: Dataset

**Files:**
- Create: `data/include/pj/engine/dataset.hpp`
- Create: No separate `.cpp` needed (header-only data structs + simple methods)

```cpp
// dataset.hpp
struct TimeDomain {
  TimeDomainId id;
  std::string name;
  Timestamp display_offset = 0;  // display_time = raw_time - display_offset
};

struct DatasetDescriptor {
  std::string source_name;
  TimeDomainId time_domain_id;
};

struct DatasetInfo {
  DatasetId id;
  std::string source_name;
  TimeDomain time_domain;
  std::vector<TopicId> topic_ids;
};
```

No separate test file needed — tested through engine integration tests.

**Commit:** `feat(engine): add Dataset and TimeDomain types`

---

### Task 11: Query System

**Files:**
- Create: `data/include/pj/engine/query.hpp`
- Create: `data/src/query.cpp`
- Create: `data/tests/query_test.cpp`

Range query (binary search on chunk time bounds) and latest-at query.

```cpp
// query.hpp
struct QueryRange {
  TopicId topic_id;
  Timestamp t_min;
  Timestamp t_max;
  std::vector<FieldId> fields;  // empty = all fields
};

struct QueryPoint {
  TopicId topic_id;
  Timestamp t;
  std::vector<FieldId> fields;
};

// A sample row from a query result
struct SampleRow {
  Timestamp timestamp;
  // Field values accessed via the chunk they came from
  const TopicChunk* chunk;
  size_t row_index;
};

// Cursor for iterating range query results
class RangeCursor {
public:
  RangeCursor(const std::deque<TopicChunk>& chunks,
              Timestamp t_min, Timestamp t_max);

  [[nodiscard]] bool valid() const noexcept;
  void advance();
  [[nodiscard]] SampleRow current() const;

  // Iterate all results
  void for_each(absl::FunctionRef<void(const SampleRow&)> callback) const;

private:
  const std::deque<TopicChunk>* chunks_;
  Timestamp t_min_;
  Timestamp t_max_;
  size_t chunk_index_;
  size_t row_index_;
};

struct LatestAtResult {
  bool found = false;
  Timestamp timestamp = 0;
  const TopicChunk* chunk = nullptr;
  size_t row_index = 0;
};

// Find the most recent sample at or before time t
[[nodiscard]] LatestAtResult latest_at(
    const std::deque<TopicChunk>& chunks, Timestamp t);

// Create a range cursor
[[nodiscard]] RangeCursor range_query(
    const std::deque<TopicChunk>& chunks,
    Timestamp t_min, Timestamp t_max);
```

Query implementation:
- Binary search on chunk `t_min`/`t_max` to find intersecting chunks
- Within each chunk, binary search on decoded timestamps for exact row range
- `RangeCursor` iterates across chunk boundaries seamlessly

**Tests:**
- 5 chunks with non-overlapping time ranges
- Range query spanning 2 chunks: verify all samples returned
- Range query hitting no chunks: cursor immediately invalid
- Range query exactly matching one chunk boundary
- `latest_at` at various times: before all data, between chunks, at exact sample, after all data
- `latest_at` returns the correct last sample when t falls between chunks
- Empty chunk deque: both queries handle gracefully

**Commit:** `feat(engine): add RangeCursor and latest_at query`

---

### Task 12: Writer Implementation

**Files:**
- Create: `data/include/pj/engine/writer.hpp`
- Create: `data/src/writer.cpp`
- No separate test — tested via engine integration tests

```cpp
// writer.hpp
struct TopicWriteHandle {
  TopicId topic_id;
  std::vector<FieldId> field_ids;
};

struct ScalarSeriesHandle {
  TopicId topic_id;
  FieldId value_field;
};

class DataWriter {
public:
  explicit DataWriter(class DataEngine& engine);

  // Schema registration
  [[nodiscard]] absl::StatusOr<SchemaId> register_schema(
      std::string schema_name, std::shared_ptr<TypeTreeNode> type_tree);

  // Topic registration
  [[nodiscard]] absl::StatusOr<TopicId> register_topic(
      DatasetId dataset_id, TopicDescriptor descriptor);

  // Bind for fast path
  [[nodiscard]] absl::StatusOr<TopicWriteHandle> bind_topic_writer(TopicId topic_id);
  [[nodiscard]] absl::StatusOr<FieldId> resolve_field(
      TopicId topic_id, std::string_view field_path);

  // Row-at-a-time append (builds into current chunk, seals when full)
  [[nodiscard]] TopicChunkBuilder::RowBuilder start_row(TopicId topic_id, Timestamp t);
  void finish_row(TopicId topic_id, TopicChunkBuilder::RowBuilder&& row);

  // Scalar convenience API
  [[nodiscard]] absl::StatusOr<ScalarSeriesHandle> register_scalar_series(
      DatasetId dataset_id, std::string_view topic_name, NumericType value_type);
  void append_scalar(const ScalarSeriesHandle& handle, Timestamp t, NumericValue value);

  // Flush: seal current building chunk if non-empty, return sealed chunks
  [[nodiscard]] std::vector<TopicChunk> flush(TopicId topic_id);
  [[nodiscard]] std::vector<std::pair<TopicId, TopicChunk>> flush_all();

private:
  DataEngine& engine_;
  absl::flat_hash_map<TopicId, TopicChunkBuilder> builders_;
};
```

**Commit:** `feat(engine): add DataWriter with scalar and row-based append`

---

### Task 13: Reader Implementation

**Files:**
- Create: `data/include/pj/engine/reader.hpp`
- Create: `data/src/reader.cpp`

```cpp
// reader.hpp
class DataReader {
public:
  explicit DataReader(const class DataEngine& engine);

  [[nodiscard]] std::vector<DatasetId> list_datasets() const;
  [[nodiscard]] std::vector<TopicId> list_topics(DatasetId dataset_id) const;
  [[nodiscard]] const TypeTreeNode* get_type_tree(TopicId topic_id) const;
  [[nodiscard]] std::optional<TopicMetadata> get_metadata(TopicId topic_id) const;

  [[nodiscard]] RangeCursor range_query(const QueryRange& range) const;
  [[nodiscard]] LatestAtResult latest_at(const QueryPoint& point) const;

private:
  const DataEngine& engine_;
};
```

**Commit:** `feat(engine): add DataReader with range and latest-at queries`

---

### Task 14: DataEngine Coordinator + Integration Tests

**Files:**
- Create: `data/include/pj/engine/engine.hpp`
- Create: `data/src/engine.cpp`
- Create: `data/tests/engine_integration_test.cpp`

The central coordinator that owns all state and provides Writer/Reader access.

```cpp
// engine.hpp
class DataEngine {
public:
  DataEngine();

  // Dataset management
  [[nodiscard]] absl::StatusOr<DatasetId> create_dataset(DatasetDescriptor descriptor);
  [[nodiscard]] const DatasetInfo* get_dataset(DatasetId id) const;

  // Topic management (called by DataWriter)
  [[nodiscard]] absl::StatusOr<TopicId> create_topic(
      DatasetId dataset_id, TopicDescriptor descriptor);
  [[nodiscard]] TopicStorage* get_topic_storage(TopicId id);
  [[nodiscard]] const TopicStorage* get_topic_storage(TopicId id) const;

  // Schema registry access
  [[nodiscard]] TypeRegistry& type_registry();
  [[nodiscard]] const TypeRegistry& type_registry() const;

  // Time domains
  [[nodiscard]] absl::StatusOr<TimeDomainId> create_time_domain(std::string name);
  [[nodiscard]] const TimeDomain* get_time_domain(TimeDomainId id) const;
  void set_display_offset(TimeDomainId id, Timestamp offset);

  // Commit cycle: commit sealed chunks, enforce retention
  void commit_chunks(std::vector<std::pair<TopicId, TopicChunk>> chunks);
  void enforce_retention(Timestamp retention_window_ns);

  // Writer/Reader factories
  [[nodiscard]] DataWriter create_writer();
  [[nodiscard]] DataReader create_reader() const;

private:
  TypeRegistry type_registry_;
  DatasetId next_dataset_id_ = 1;
  TopicId next_topic_id_ = 1;
  TimeDomainId next_time_domain_id_ = 1;

  absl::flat_hash_map<DatasetId, DatasetInfo> datasets_;
  absl::flat_hash_map<TopicId, TopicStorage> topics_;
  absl::flat_hash_map<TimeDomainId, TimeDomain> time_domains_;
};
```

**Integration Tests (engine_integration_test.cpp):**

1. **End-to-end scalar write + read:**
   - Create engine, dataset, register scalar series
   - Append 5000 scalar values (enough to span multiple chunks)
   - Flush + commit
   - Range query: verify all values returned in order
   - Latest-at at midpoint: verify correct value

2. **End-to-end structured write + read:**
   - Register a `robot_pose` schema (float32 x7 + string x1)
   - Register topic, bind writer
   - Append 200 rows with row builder
   - Flush + commit
   - Range query: verify field values round-trip
   - Verify string column is dictionary-encoded in sealed chunk
   - Verify bool column (if added) is packed

3. **Retention eviction:**
   - Write data spanning 10 seconds
   - Enforce retention of 5 seconds
   - Verify old chunks evicted, recent data intact
   - Range query on evicted range returns empty

4. **Schema evolution:**
   - Register topic with schema v1 (3 fields)
   - Write 100 rows, flush + commit
   - Evolve schema to v2 (add 1 field)
   - Write 100 more rows with 4 fields, flush + commit
   - Range query spanning both versions: old rows return null for new field

5. **Time domain offset:**
   - Create 2 datasets with different time domains
   - Set offsets
   - Verify `display_time = raw_time - offset`

**Commit:** `feat(engine): add DataEngine coordinator and integration tests`

---

## Build & Test Commands

```bash
# Install dependencies with Conan (first time / after conanfile.txt changes)
cd data && conan install . --output-folder=build --build=missing -s build_type=Debug

# Configure (using Conan-generated toolchain)
cmake -B data/build -S data -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=data/build/conan_toolchain.cmake

# Build
cmake --build data/build -j$(nproc)

# Run all tests
ctest --test-dir data/build --output-on-failure

# Run specific test
./data/build/types_test
./data/build/engine_integration_test
```

---

## Task Dependency Graph

```
Task 1 (scaffold)
  └─> Task 2 (types)
       ├─> Task 3 (type tree)
       │    └─> Task 4 (type registry)
       │         └─> Task 12 (writer) ─────────────┐
       │         └─> Task 13 (reader) ─────────────┤
       └─> Task 5 (buffer)                         │
            └─> Task 6 (column buffer)              │
                 ├─> Task 7 (encodings)             │
                 │    └─> Task 8 (chunk) ───────────┤
                 │         └─> Task 9 (storage) ────┤
                 └─────────────────────────────────>│
       Task 10 (dataset) ─────────────────────────>│
       Task 11 (query) ───────────────────────────>│
                                                    v
                                         Task 14 (engine + integration)
```

Tasks 10, 11 can proceed in parallel with Tasks 6-9 since they don't depend on each other.

---

Plan complete and saved to `docs/plans/2026-02-16-data-engine-phase1.md`. Two execution options:

**1. Subagent-Driven (this session)** - I dispatch fresh subagent per task, review between tasks, fast iteration

**2. Parallel Session (separate)** - Open new session with executing-plans, batch execution with checkpoints

Which approach?
