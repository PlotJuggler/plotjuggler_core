#include <pj_base/sdk/data_source_plugin_base.hpp>

#include <string>

namespace {

class MockDataSource : public PJ::DataSourcePluginBase {
 public:
  std::string manifest() const override {
    return R"({
      "name": "Mock DataSource",
      "version": "1.0.0",
      "description": "Test data source for protocol and host integration"
    })";
  }

  uint64_t capabilities() const override {
    return PJ::kCapabilityContinuousStream | PJ::kCapabilityDirectIngest |
           PJ::kCapabilityDelegatedIngest | PJ::kCapabilitySupportsPause;
  }

  std::string saveConfig() const override { return config_; }

  bool loadConfig(std::string_view config_json) override {
    config_ = std::string(config_json);
    return true;
  }

  bool start() override {
    if (!writeHostBound()) {
      setLastError("write host not bound");
      state_ = PJ::DataSourceState::kFailed;
      return false;
    }
    if (!runtimeHostBound()) {
      setLastError("runtime host not bound");
      state_ = PJ::DataSourceState::kFailed;
      return false;
    }

    state_ = PJ::DataSourceState::kStarting;
    runtimeHost().notifyState(state_);
    runtimeHost().reportMessage(PJ::DataSourceMessageLevel::kInfo, "mock start");

    if (config_.find("progress") != std::string::npos) {
      if (runtimeHost().progressStart("Mock Import", 3, true)) {
        for (uint64_t step = 1; step <= 3; ++step) {
          if (!runtimeHost().progressUpdate(step)) {
            runtimeHost().progressFinish();
            setLastError("progress canceled");
            state_ = PJ::DataSourceState::kFailed;
            runtimeHost().notifyState(state_);
            return false;
          }
        }
        runtimeHost().progressFinish();
      }
    }

    auto topic = writeHost().ensureTopic("mock/topic");
    if (!topic) {
      setLastError(topic.error());
      state_ = PJ::DataSourceState::kFailed;
      runtimeHost().notifyState(state_);
      return false;
    }

    const PJ::sdk::NamedFieldValue fields[] = {
        {.name = "value", .is_null = false, .value = PJ::sdk::ValueRef{double(42.0)}}};
    auto write_status = writeHost().appendRecord(
        *topic, PJ::Timestamp{123}, PJ::Span<const PJ::sdk::NamedFieldValue>(fields, 1));
    if (!write_status) {
      setLastError(write_status.error());
      state_ = PJ::DataSourceState::kFailed;
      runtimeHost().notifyState(state_);
      return false;
    }

    if (config_.find("delegated") != std::string::npos) {
      const uint8_t schema[] = {'s', 'c', 'h'};
      auto binding = runtimeHost().ensureParserBinding(PJ::ParserBindingRequest{
          .topic_name = "mock/topic",
          .parser_encoding = "json",
          .type_name = "mock_type",
          .schema = PJ::Span<const uint8_t>(schema, sizeof(schema)),
          .parser_config_json = R"({"mode":"test"})",
      });
      if (!binding) {
        setLastError(binding.error());
        state_ = PJ::DataSourceState::kFailed;
        runtimeHost().notifyState(state_);
        return false;
      }

      const uint8_t payload[] = {'{', '}'};
      auto push_status = runtimeHost().pushRawMessage(
          *binding, PJ::Timestamp{456}, PJ::Span<const uint8_t>(payload, sizeof(payload)));
      if (!push_status) {
        setLastError(push_status.error());
        state_ = PJ::DataSourceState::kFailed;
        runtimeHost().notifyState(state_);
        return false;
      }
    }

    state_ = PJ::DataSourceState::kRunning;
    runtimeHost().notifyState(state_);
    return true;
  }

  void stop() override {
    state_ = PJ::DataSourceState::kStopped;
    runtimeHost().notifyState(state_);
  }

  bool pause() override {
    if (state_ != PJ::DataSourceState::kRunning) {
      setLastError("pause requires running state");
      return false;
    }
    state_ = PJ::DataSourceState::kPaused;
    runtimeHost().notifyState(state_);
    return true;
  }

  bool resume() override {
    if (state_ != PJ::DataSourceState::kPaused) {
      setLastError("resume requires paused state");
      return false;
    }
    state_ = PJ::DataSourceState::kRunning;
    runtimeHost().notifyState(state_);
    return true;
  }

  bool poll() override {
    ++poll_count_;
    return true;
  }

  PJ::DataSourceState currentState() const override { return state_; }

 private:
  std::string config_ = "{}";
  PJ::DataSourceState state_ = PJ::DataSourceState::kIdle;
  int poll_count_ = 0;
};

}  // namespace

PJ_DATA_SOURCE_PLUGIN(MockDataSource)
