#pragma once

#include <QObject>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "pj_base/data_source_protocol.h"
#include "pj_base/plugin_data_api.h"
#include "pj_datastore/engine.hpp"
#include "pj_datastore/plugin_data_host.hpp"
#include "pj_plugins/host/data_source_handle.hpp"
#include "pj_plugins/host/data_source_library.hpp"

namespace proto {

struct RuntimeHostState {
  std::vector<PJ_data_source_state_t> state_transitions;
  int progress_starts = 0;
  int progress_updates = 0;
  int progress_finishes = 0;
  std::atomic<bool> stop_requested{false};
  std::vector<std::string> messages;
};

class DataSourceSession : public QObject {
  Q_OBJECT

 public:
  DataSourceSession(PJ::DataEngine& engine, PJ::DataSourceLibrary& library, PJ::TimeDomainId td_id,
                    std::string source_name, QObject* parent = nullptr);

  bool startFileImport(const std::string& config_json);
  bool startStream(const std::string& config_json);
  void stopStream();
  void poll();
  void requestStop();

  [[nodiscard]] PJ::DataSourceHandle& handle() { return handle_; }
  [[nodiscard]] PJ::DataSourceLibrary& library() { return library_; }

 signals:
  void importComplete();
  void streamDataReady();

 private:
  static PJ_data_source_runtime_host_t makeRuntimeHost(RuntimeHostState* state);

  bool setupAndStart(const std::string& config_json);

  PJ::DataEngine& engine_;
  PJ::DataSourceLibrary& library_;
  PJ::TimeDomainId td_id_;
  std::string source_name_;
  PJ::DataSourceHandle handle_;
  std::unique_ptr<PJ::DatastoreSourceWriteHost> write_host_;
  RuntimeHostState runtime_state_;
};

}  // namespace proto
