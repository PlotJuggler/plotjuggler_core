#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "pj_base/sdk/message_parser_plugin_base.hpp"

namespace {

/// Example parser that exercises the dual-emit path: a scalar `seq` field
/// alongside a raw blob written to the ObjectStore. Payload envelope:
///   bytes [0..8)  → little-endian uint64 seq
///   bytes [8..N)  → opaque blob (e.g. compressed frame, image, serialized msg)
///
/// The parser declares the `object` capability in its manifest, which signals
/// the host runtime to register the optional `pj.parser_object_write.v1`
/// service so `objectWriteHost()` is non-null inside parse().
class MockMediaParser : public PJ::MessageParserPluginBase {
 public:
  PJ::Status parse(PJ::Timestamp timestamp_ns, PJ::Span<const uint8_t> payload) override {
    if (!writeHostBound()) {
      return PJ::unexpected(std::string("write host not bound"));
    }
    if (payload.size() < sizeof(uint64_t)) {
      return PJ::unexpected(std::string("payload too small (need 8 bytes seq prefix)"));
    }

    uint64_t seq = 0;
    std::memcpy(&seq, payload.data(), sizeof(uint64_t));
    if (auto s = writeHost().appendRecord(
            timestamp_ns, {{.name = "seq", .value = static_cast<uint64_t>(seq)}});
        !s) {
      return s;
    }

    if (auto* obj = objectWriteHost()) {
      std::vector<uint8_t> body(payload.data() + sizeof(uint64_t), payload.data() + payload.size());
      if (auto s = obj->pushOwned(timestamp_ns, std::move(body)); !s) {
        return s;
      }
    }
    return PJ::okStatus();
  }
};

}  // namespace

PJ_MESSAGE_PARSER_PLUGIN(
    MockMediaParser,
    R"({"id":"mock-media-parser","name":"Mock Media Parser","version":"1.0.0","encoding":["mock-media"],"capabilities":["object"]})")
