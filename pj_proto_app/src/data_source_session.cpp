#include "data_source_session.hpp"

#include <iostream>

namespace proto {

// --- Runtime host callbacks (static, C-compatible) ---

namespace {

const char* rhGetLastError(void*) { return nullptr; }

void rhReportMessage(void* ctx, PJ_data_source_message_level_t, PJ_string_view_t msg) {
  auto* s = static_cast<RuntimeHostState*>(ctx);
  std::string m(msg.data, msg.size);
  s->messages.push_back(m);
  std::cerr << "[plugin] " << m << "\n";
}

bool rhProgressStart(void* ctx, PJ_string_view_t label, uint64_t, bool) {
  static_cast<RuntimeHostState*>(ctx)->progress_starts++;
  std::cerr << "[progress] start: " << std::string(label.data, label.size) << "\n";
  return true;
}

bool rhProgressUpdate(void* ctx, uint64_t) {
  static_cast<RuntimeHostState*>(ctx)->progress_updates++;
  return !static_cast<RuntimeHostState*>(ctx)->stop_requested.load();
}

void rhProgressFinish(void* ctx) { static_cast<RuntimeHostState*>(ctx)->progress_finishes++; }

bool rhIsStopRequested(void* ctx) {
  return static_cast<RuntimeHostState*>(ctx)->stop_requested.load();
}

void rhNotifyState(void* ctx, PJ_data_source_state_t state) {
  static_cast<RuntimeHostState*>(ctx)->state_transitions.push_back(state);
}

void rhRequestStop(void*, PJ_data_source_state_t, PJ_string_view_t reason) {
  std::cerr << "[plugin] requestStop: " << std::string(reason.data, reason.size) << "\n";
}

bool rhEnsureParserBinding(void*, const PJ_parser_binding_request_t*, PJ_parser_binding_handle_t* out) {
  *out = PJ_parser_binding_handle_t{1};
  return true;
}

bool rhPushRawMessage(void*, PJ_parser_binding_handle_t, int64_t, PJ_bytes_view_t) { return true; }

}  // namespace

PJ_data_source_runtime_host_t DataSourceSession::makeRuntimeHost(RuntimeHostState* state) {
  static const PJ_data_source_runtime_host_vtable_t vtable = {
      .protocol_version = PJ_DATA_SOURCE_PROTOCOL_VERSION,
      .struct_size = sizeof(PJ_data_source_runtime_host_vtable_t),
      .get_last_error = rhGetLastError,
      .report_message = rhReportMessage,
      .progress_start = rhProgressStart,
      .progress_update = rhProgressUpdate,
      .progress_finish = rhProgressFinish,
      .is_stop_requested = rhIsStopRequested,
      .notify_state = rhNotifyState,
      .request_stop = rhRequestStop,
      .ensure_parser_binding = rhEnsureParserBinding,
      .push_raw_message = rhPushRawMessage,
  };
  return PJ_data_source_runtime_host_t{.ctx = state, .vtable = &vtable};
}

DataSourceSession::DataSourceSession(PJ::DataEngine& engine, PJ::DataSourceLibrary& library, PJ::TimeDomainId td_id,
                                     std::string source_name, QObject* parent)
    : QObject(parent),
      engine_(engine),
      library_(library),
      td_id_(td_id),
      source_name_(std::move(source_name)),
      handle_(library.createHandle()) {}

bool DataSourceSession::setupAndStart(const std::string& config_json) {
  auto ds_result =
      engine_.createDataset(PJ::DatasetDescriptor{.source_name = source_name_, .time_domain_id = td_id_});
  if (!ds_result) {
    std::cerr << "Failed to create dataset: " << ds_result.error() << "\n";
    return false;
  }

  // Create write host
  PJ_data_source_handle_t source_handle{static_cast<uint32_t>(*ds_result)};
  write_host_ = std::make_unique<PJ::DatastoreSourceWriteHost>(engine_, source_handle);

  // Bind hosts
  (void)handle_.bindWriteHost(write_host_->raw());
  (void)handle_.bindRuntimeHost(makeRuntimeHost(&runtime_state_));

  // Load config if provided
  if (!config_json.empty()) {
    (void)handle_.loadConfig(config_json);
  }

  return true;
}

bool DataSourceSession::startFileImport(const std::string& config_json) {
  if (!setupAndStart(config_json)) return false;

  bool ok = handle_.start();
  write_host_->flushPending();
  emit importComplete();
  return ok;
}

bool DataSourceSession::startStream(const std::string& config_json) {
  if (!setupAndStart(config_json)) return false;
  return handle_.start();
}

void DataSourceSession::stopStream() {
  runtime_state_.stop_requested.store(true);
  handle_.stop();
}

void DataSourceSession::poll() {
  (void)handle_.poll();
  if (write_host_) {
    write_host_->flushPending();
  }
}

void DataSourceSession::requestStop() { runtime_state_.stop_requested.store(true); }

}  // namespace proto
