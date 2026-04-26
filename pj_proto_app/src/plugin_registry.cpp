#include "plugin_registry.hpp"

#include <algorithm>
#include <utility>

#include "pj_marketplace/platform_utils.hpp"
#include <QFileInfo>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace proto {

namespace {

bool appendManifestEncodings(const nlohmann::json& value, std::vector<std::string>& encodings, std::string& error) {
  auto append_string = [&](const nlohmann::json& item) -> bool {
    if (!item.is_string() || item.get<std::string>().empty()) {
      error = "'encoding' must contain non-empty strings";
      return false;
    }
    encodings.push_back(item.get<std::string>());
    return true;
  };

  if (value.is_string()) {
    return append_string(value);
  }
  if (value.is_array()) {
    for (const auto& item : value) {
      if (!append_string(item)) {
        return false;
      }
    }
    if (encodings.empty()) {
      error = "'encoding' array must not be empty";
      return false;
    }
    return true;
  }

  error = "'encoding' must be a string or an array of strings";
  return false;
}

}  // namespace

PluginRegistry::PluginRegistry(std::string_view plugin_dir, PJ::DiagnosticSink sink)
    : plugin_dir_(plugin_dir), sink_(std::move(sink)) {}

void PluginRegistry::report(PJ::DiagnosticLevel level, const std::string& id, std::string message) const {
  if (!sink_) {
    return;
  }
  sink_(PJ::Diagnostic{level, "PluginRegistry", id, std::move(message), std::chrono::system_clock::now()});
}

bool PluginRegistry::loadAndRegisterDataSource(const std::filesystem::path& so_path) {
  auto result = PJ::DataSourceLibrary::load(so_path.string());
  if (!result) {
    report(PJ::DiagnosticLevel::kError, /*id*/ {},
         "DataSourceLibrary::load failed for " + so_path.filename().string() + ": " + result.error());
    return false;
  }
  LoadedDataSource loaded;
  loaded.library = std::move(*result);
  loaded.path = so_path.string();
  loaded.loaded_mtime = std::filesystem::last_write_time(so_path);

  auto handle = loaded.library.createHandle();
  loaded.capabilities = handle.capabilities();
  try {
    auto manifest = nlohmann::json::parse(handle.manifest());
    if (!manifest.contains("id") || !manifest["id"].is_string() || manifest["id"].get<std::string>().empty()
        || !manifest.contains("version") || !manifest["version"].is_string()
        || manifest["version"].get<std::string>().empty()) {
      report(PJ::DiagnosticLevel::kError, /*id*/ {},
           "DataSource " + so_path.string() + ": embedded manifest missing required string fields 'id' and/or 'version'");
      return false;
    }
    loaded.id = manifest["id"].get<std::string>();
    loaded.name = manifest.value("name", loaded.id);
    loaded.version = manifest["version"].get<std::string>();
    if (manifest.contains("file_extensions")) {
      for (const auto& ext : manifest["file_extensions"]) {
        loaded.file_extensions.push_back(ext.get<std::string>());
      }
    }
  } catch (const nlohmann::json::exception& e) {
    report(PJ::DiagnosticLevel::kError, /*id*/ {},
         std::string("DataSource ") + so_path.string() + ": manifest parse failed: " + e.what());
    return false;
  }
  report(PJ::DiagnosticLevel::kInfo, loaded.id, "Loaded DataSource " + loaded.name + " from " + loaded.path);
  data_sources_.push_back(std::move(loaded));
  return true;
}

bool PluginRegistry::loadAndRegisterMessageParser(const std::filesystem::path& so_path) {
  auto result = PJ::MessageParserLibrary::load(so_path.string());
  if (!result) {
    report(PJ::DiagnosticLevel::kError, /*id*/ {},
         "MessageParserLibrary::load failed for " + so_path.filename().string() + ": " + result.error());
    return false;
  }
  LoadedMessageParser loaded;
  loaded.library = std::move(*result);
  loaded.path = so_path.string();
  loaded.loaded_mtime = std::filesystem::last_write_time(so_path);

  auto handle = loaded.library.createHandle();
  try {
    auto manifest = nlohmann::json::parse(handle.manifest());
    if (!manifest.contains("id") || !manifest["id"].is_string() || manifest["id"].get<std::string>().empty()
        || !manifest.contains("version") || !manifest["version"].is_string()
        || manifest["version"].get<std::string>().empty()) {
      report(PJ::DiagnosticLevel::kError, /*id*/ {},
           "MessageParser " + so_path.string() +
               ": embedded manifest missing required string fields 'id' and/or 'version'");
      return false;
    }
    loaded.id = manifest["id"].get<std::string>();
    loaded.name = manifest.value("name", loaded.id);
    loaded.version = manifest["version"].get<std::string>();
    if (!manifest.contains("encoding")) {
      report(PJ::DiagnosticLevel::kError, loaded.id,
           "MessageParser " + so_path.string() + ": embedded manifest missing required key 'encoding'");
      return false;
    }
    std::string encoding_error;
    if (!appendManifestEncodings(manifest["encoding"], loaded.encodings, encoding_error)) {
      report(PJ::DiagnosticLevel::kError, loaded.id, "MessageParser " + so_path.string() + ": " + encoding_error);
      return false;
    }
  } catch (const nlohmann::json::exception& e) {
    report(PJ::DiagnosticLevel::kError, /*id*/ {},
         std::string("MessageParser ") + so_path.string() + ": manifest parse failed: " + e.what());
    return false;
  }
  report(PJ::DiagnosticLevel::kInfo, loaded.id, "Loaded MessageParser " + loaded.name + " from " + loaded.path);
  message_parsers_.push_back(std::move(loaded));
  return true;
}

bool PluginRegistry::loadAndRegisterToolbox(const std::filesystem::path& so_path) {
  auto result = PJ::ToolboxLibrary::load(so_path.string());
  if (!result) {
    report(PJ::DiagnosticLevel::kError, /*id*/ {},
         "ToolboxLibrary::load failed for " + so_path.filename().string() + ": " + result.error());
    return false;
  }
  LoadedToolbox loaded;
  loaded.library = std::move(*result);
  loaded.path = so_path.string();
  loaded.loaded_mtime = std::filesystem::last_write_time(so_path);

  auto handle = loaded.library.createHandle();
  loaded.capabilities = handle.capabilities();
  try {
    auto manifest = nlohmann::json::parse(handle.manifest());
    if (!manifest.contains("id") || !manifest["id"].is_string() || manifest["id"].get<std::string>().empty()
        || !manifest.contains("version") || !manifest["version"].is_string()
        || manifest["version"].get<std::string>().empty()) {
      report(PJ::DiagnosticLevel::kError, /*id*/ {},
           "Toolbox " + so_path.string() + ": embedded manifest missing required string fields 'id' and/or 'version'");
      return false;
    }
    loaded.id = manifest["id"].get<std::string>();
    loaded.name = manifest.value("name", loaded.id);
    loaded.version = manifest["version"].get<std::string>();
  } catch (const nlohmann::json::exception& e) {
    report(PJ::DiagnosticLevel::kError, /*id*/ {},
         std::string("Toolbox ") + so_path.string() + ": manifest parse failed: " + e.what());
    return false;
  }
  report(PJ::DiagnosticLevel::kInfo, loaded.id, "Loaded Toolbox " + loaded.name + " from " + loaded.path);
  toolbox_plugins_.push_back(std::move(loaded));
  return true;
}

void PluginRegistry::scanDirectory() {
  namespace fs = std::filesystem;

  std::error_code ec;
  if (!fs::is_directory(plugin_dir_, ec)) {
    report(PJ::DiagnosticLevel::kError, /*id*/ {},
         "Plugin directory not found: " + plugin_dir_ + " (" + ec.message() + ")");
    return;
  }

  const std::string expected_ext = PJ::PlatformUtils::pluginExtension();

  std::size_t walked = 0;
  std::size_t matched = 0;
  for (const auto& entry : fs::recursive_directory_iterator(plugin_dir_, ec)) {
    ++walked;
    if (!entry.is_regular_file() || entry.path().extension().string() != expected_ext) {
      continue;
    }
    ++matched;
    const bool ok_ds = loadAndRegisterDataSource(entry.path());
    const bool ok_mp = !ok_ds && loadAndRegisterMessageParser(entry.path());
    const bool ok_tb = !ok_ds && !ok_mp && loadAndRegisterToolbox(entry.path());
    if (!ok_ds && !ok_mp && !ok_tb) {
      report(PJ::DiagnosticLevel::kError, /*id*/ {}, "Failed to load plugin: " + entry.path().string());
    }
  }
  report(PJ::DiagnosticLevel::kInfo, /*id*/ {},
       "Plugin scan done: walked=" + std::to_string(walked) + " matched=" + std::to_string(matched)
           + (ec ? std::string(" (iter ec=") + ec.message() + ")" : std::string{}));
}

void PluginRegistry::reload() {
  namespace fs = std::filesystem;

  if (!fs::is_directory(plugin_dir_)) {
    report(PJ::DiagnosticLevel::kError, /*id*/ {}, "Plugin directory not found: " + plugin_dir_);
    return;
  }

  // Collect all plugin files currently on disk
  std::vector<fs::path> on_disk;
  for (const auto& entry : fs::recursive_directory_iterator(plugin_dir_)) {
    if (entry.is_regular_file() &&
        entry.path().extension() == PJ::PlatformUtils::pluginExtension()) {
      on_disk.push_back(entry.path());
    }
  }

  // Remove entries whose .so no longer exists on disk
  auto is_gone = [&](const std::string& p) {
    return std::none_of(on_disk.begin(), on_disk.end(),
                        [&](const fs::path& dp) { return dp.string() == p; });
  };
  std::erase_if(data_sources_, [&](const LoadedDataSource& ds) {
    if (is_gone(ds.path)) {
      report(PJ::DiagnosticLevel::kInfo, ds.id, "Unloaded DataSource (removed from disk): " + ds.path);
      return true;
    }
    return false;
  });
  std::erase_if(message_parsers_, [&](const LoadedMessageParser& mp) {
    if (is_gone(mp.path)) {
      report(PJ::DiagnosticLevel::kInfo, mp.id, "Unloaded MessageParser (removed from disk): " + mp.path);
      return true;
    }
    return false;
  });
  std::erase_if(toolbox_plugins_, [&](const LoadedToolbox& tb) {
    if (is_gone(tb.path)) {
      report(PJ::DiagnosticLevel::kInfo, tb.id, "Unloaded Toolbox (removed from disk): " + tb.path);
      return true;
    }
    return false;
  });

  // Load new .so files; reload modified ones
  for (const auto& so_path : on_disk) {
    const std::string path_str = so_path.string();
    const auto disk_mtime = fs::last_write_time(so_path);

    auto ds_it = std::find_if(data_sources_.begin(), data_sources_.end(),
                              [&](const LoadedDataSource& ds) { return ds.path == path_str; });
    if (ds_it != data_sources_.end()) {
      if (disk_mtime <= ds_it->loaded_mtime) {
        continue;
      }
      report(PJ::DiagnosticLevel::kInfo, ds_it->id, "Reloading updated DataSource: " + path_str);
      data_sources_.erase(ds_it);
    } else {
      auto mp_it = std::find_if(message_parsers_.begin(), message_parsers_.end(),
                                [&](const LoadedMessageParser& mp) { return mp.path == path_str; });
      if (mp_it != message_parsers_.end()) {
        if (disk_mtime <= mp_it->loaded_mtime) {
          continue;
        }
        report(PJ::DiagnosticLevel::kInfo, mp_it->id, "Reloading updated MessageParser: " + path_str);
        message_parsers_.erase(mp_it);
      } else {
        auto tb_it = std::find_if(toolbox_plugins_.begin(), toolbox_plugins_.end(),
                                  [&](const LoadedToolbox& tb) { return tb.path == path_str; });
        if (tb_it != toolbox_plugins_.end()) {
          if (disk_mtime <= tb_it->loaded_mtime) {
            continue;
          }
          report(PJ::DiagnosticLevel::kInfo, tb_it->id, "Reloading updated Toolbox: " + path_str);
          toolbox_plugins_.erase(tb_it);
        }
      }
    }

    if (!loadAndRegisterDataSource(so_path) &&
        !loadAndRegisterMessageParser(so_path) &&
        !loadAndRegisterToolbox(so_path)) {
      report(PJ::DiagnosticLevel::kError, /*id*/ {}, "Failed to load plugin: " + path_str);
    }
  }
}


std::vector<LoadedDataSource*> PluginRegistry::fileImportSources() {
  std::vector<LoadedDataSource*> result;
  for (auto& ds : data_sources_) {
    if (ds.capabilities & PJ_DATA_SOURCE_CAPABILITY_FINITE_IMPORT) {
      result.push_back(&ds);
    }
  }
  return result;
}

std::vector<LoadedDataSource*> PluginRegistry::streamSources() {
  std::vector<LoadedDataSource*> result;
  for (auto& ds : data_sources_) {
    if (ds.capabilities & PJ_DATA_SOURCE_CAPABILITY_CONTINUOUS_STREAM) {
      result.push_back(&ds);
    }
  }
  return result;
}

std::string PluginRegistry::buildFileFilter() const {
  // Collect all extensions and per-plugin filters
  std::string all_exts;
  std::string per_plugin;
  for (const auto& ds : data_sources_) {
    if (!(ds.capabilities & PJ_DATA_SOURCE_CAPABILITY_FINITE_IMPORT)) {
      continue;
    }
    if (ds.file_extensions.empty()) {
      continue;
    }

    if (!per_plugin.empty()) {
      per_plugin += ";;";
    }
    per_plugin += ds.name + " (";
    for (size_t i = 0; i < ds.file_extensions.size(); ++i) {
      if (i > 0) {
        per_plugin += " ";
      }
      per_plugin += "*" + ds.file_extensions[i];
      // Collect for "All supported" entry
      if (!all_exts.empty()) {
        all_exts += " ";
      }
      all_exts += "*" + ds.file_extensions[i];
    }
    per_plugin += ")";
  }

  std::string filter;
  if (!all_exts.empty()) {
    filter = "All supported files (" + all_exts + ")";
    if (!per_plugin.empty()) {
      filter += ";;" + per_plugin;
    }
  } else {
    filter = per_plugin;
  }
  if (!filter.empty()) {
    filter += ";;";
  }
  filter += "All files (*)";
  return filter;
}

std::vector<LoadedDataSource*> PluginRegistry::findSourcesForExtension(std::string_view ext) {
  std::vector<LoadedDataSource*> result;
  for (auto& ds : data_sources_) {
    if (!(ds.capabilities & PJ_DATA_SOURCE_CAPABILITY_FINITE_IMPORT)) {
      continue;
    }
    for (const auto& supported_ext : ds.file_extensions) {
      if (supported_ext == ext) {
        result.push_back(&ds);
        break;
      }
    }
  }
  return result;
}

LoadedMessageParser* PluginRegistry::findParserByEncoding(std::string_view encoding) {
  for (auto& mp : message_parsers_) {
    for (const auto& enc : mp.encodings) {
      if (enc == encoding) {
        return &mp;
      }
    }
  }
  return nullptr;
}

std::string PluginRegistry::listAvailableEncodings() const {
  std::vector<std::string> unique_encodings;
  for (const auto& parser : message_parsers_) {
    for (const auto& enc : parser.encodings) {
      if (std::find(unique_encodings.begin(), unique_encodings.end(), enc) == unique_encodings.end()) {
        unique_encodings.push_back(enc);
      }
    }
  }

  // Build JSON array
  std::string json = "[";
  for (size_t i = 0; i < unique_encodings.size(); ++i) {
    if (i > 0) {
      json += ",";
    }
    json += "\"" + unique_encodings[i] + "\"";
  }
  json += "]";
  return json;
}

QMap<QString, PJ::InstalledExtension> PluginRegistry::loadedExtensionsSnapshot() const {
  QMap<QString, PJ::InstalledExtension> snapshot;

  auto add_loaded = [&](const std::string& id, const std::string& version, const std::string& path) {
    if (id.empty()) {
      return;
    }
    const QString qid = QString::fromStdString(id);
    if (snapshot.contains(qid)) {
      return;
    }

    PJ::InstalledExtension record;
    record.id = qid;
    record.version = QString::fromStdString(version);
    record.path = QString::fromStdString(path);
    record.install_date = QFileInfo(record.path).lastModified();
    record.enabled = true;
    snapshot.insert(qid, record);
  };

  for (const auto& ds : data_sources_) {
    add_loaded(ds.id, ds.version, ds.path);
  }
  for (const auto& parser : message_parsers_) {
    add_loaded(parser.id, parser.version, parser.path);
  }
  for (const auto& toolbox : toolbox_plugins_) {
    add_loaded(toolbox.id, toolbox.version, toolbox.path);
  }

  return snapshot;
}

}  // namespace proto
