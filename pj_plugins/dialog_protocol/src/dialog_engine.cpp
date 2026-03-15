#include <QBuffer>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QSettings>
#include <QTimer>
#include <QUiLoader>
#include <QVBoxLayout>
#include <pj_plugins/host/widget_data_view.hpp>
#include <pj_plugins/host/widget_event_builder.hpp>
#include <pj_plugins/host_qt/dialog_engine.hpp>
#include <pj_plugins/host_qt/widget_binding.hpp>

namespace PJ {

DialogEngine::DialogEngine(PJ::DialogHandle handle, DialogEngineConfig config)
    : handle_(std::move(handle)), config_(config) {}

// ---------------------------------------------------------------------------
// JSON diff: compute which widget keys changed between old and new data
// ---------------------------------------------------------------------------

static nlohmann::json compute_diff(const nlohmann::json& old_data, const nlohmann::json& new_data) {
  nlohmann::json diff = nlohmann::json::object();
  for (const auto& [key, val] : new_data.items()) {
    if (!old_data.contains(key) || old_data[key] != val) {
      diff[key] = val;
    }
  }
  return diff;
}

// ---------------------------------------------------------------------------
// apply_and_diff: re-read widget data, apply (diffed or full), update prev
// ---------------------------------------------------------------------------

/// Holds the result of applying widget data: whether accept was requested and
/// whether a sub-dialog was requested (with its UI XML).
struct ApplyResult {
  bool wants_accept = false;
  std::optional<std::string> sub_dialog_ui;
};

static ApplyResult apply_and_diff(
    QWidget* root, PJ::DialogHandle& handle, nlohmann::json& prev_data, bool enable_diff, int& diff_apply_count) {
  std::string raw = handle.widget_data();
  nlohmann::json new_data = nlohmann::json::parse(raw, nullptr, false);
  if (new_data.is_discarded()) {
    return {};
  }

  PJ::WidgetDataView full_view(raw);
  ApplyResult result;
  result.wants_accept = full_view.requestAccept();
  result.sub_dialog_ui = full_view.subDialogUi();

  // Strip commands before diffing (they're one-shot)
  new_data.erase("__request_accept");
  new_data.erase("__request_sub_dialog");

  if (enable_diff) {
    nlohmann::json diff = compute_diff(prev_data, new_data);
    if (!diff.empty()) {
      PJ::WidgetDataView view(diff.dump());
      applyWidgetData(root, view);
      ++diff_apply_count;
    }
  } else {
    applyWidgetData(root, full_view);
  }
  prev_data = std::move(new_data);
  return result;
}

// ---------------------------------------------------------------------------
// show_dialog
// ---------------------------------------------------------------------------

DialogResult DialogEngine::showDialog(QWidget* parent) {
  stats_ = {};

  // 1. Load .ui
  std::string ui = handle_.ui_content();
  QByteArray data(ui.data(), static_cast<int>(ui.size()));
  QBuffer buffer(&data);
  buffer.open(QIODevice::ReadOnly);

  QUiLoader loader;
  QWidget* loaded = loader.load(&buffer, parent);
  if (!loaded) {
    return DialogResult::kRejected;
  }

  // 2. Wrap in QDialog if needed
  QDialog* dialog = qobject_cast<QDialog*>(loaded);
  if (!dialog) {
    dialog = new QDialog(parent);
    dialog->setWindowTitle(loaded->windowTitle());
    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(loaded);

    auto* button_box = loaded->findChild<QDialogButtonBox*>("buttonBox");
    if (button_box) {
      QObject::connect(button_box, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
      QObject::connect(button_box, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    } else {
      qWarning(
          "DialogEngine: no QDialogButtonBox named 'buttonBox' found in UI. "
          "OK/Cancel buttons will not work. Ensure your .ui XML has: "
          "<widget class=\"QDialogButtonBox\" name=\"buttonBox\">");
    }
  }

  // Restore saved dialog geometry (keyed by plugin manifest name)
  auto manifest_json = nlohmann::json::parse(handle_.manifest(), nullptr, false);
  std::string plugin_name = manifest_json.is_object() ? manifest_json.value("name", "") : "";
  QString geometry_key = QString("DialogGeometry/%1").arg(QString::fromStdString(plugin_name));
  if (!plugin_name.empty()) {
    QSettings settings;
    auto saved = settings.value(geometry_key).toByteArray();
    if (!saved.isEmpty()) {
      dialog->restoreGeometry(saved);
    }
  }

  // Task 7: detect parser slot widget
  stats_.has_parser_slot = loaded->findChild<QWidget*>("pj_parser_slot") != nullptr;

  QWidget* binding_root = loaded;

  // 3. Apply initial widget data
  std::string initial_raw = handle_.widget_data();
  nlohmann::json prev_data = nlohmann::json::parse(initial_raw, nullptr, false);
  if (prev_data.is_discarded()) {
    prev_data = nlohmann::json::object();
  }
  {
    PJ::WidgetDataView view(initial_raw);
    applyWidgetData(binding_root, view);
  }

  // Helper: open a sub-dialog from UI XML (nested modal inside parent)
  auto maybe_open_sub_dialog = [&](const ApplyResult& ar) {
    if (!ar.sub_dialog_ui) {
      return;
    }

    QByteArray sub_data(ar.sub_dialog_ui->data(), static_cast<int>(ar.sub_dialog_ui->size()));
    QBuffer sub_buffer(&sub_data);
    sub_buffer.open(QIODevice::ReadOnly);

    QUiLoader sub_loader;
    QWidget* sub_loaded = sub_loader.load(&sub_buffer, dialog);
    if (!sub_loaded) {
      return;
    }

    QDialog* sub_dialog = qobject_cast<QDialog*>(sub_loaded);
    if (!sub_dialog) {
      sub_dialog = new QDialog(dialog);
      sub_dialog->setWindowTitle(sub_loaded->windowTitle());
      auto* sub_layout = new QVBoxLayout(sub_dialog);
      sub_layout->setContentsMargins(0, 0, 0, 0);
      sub_layout->addWidget(sub_loaded);

      auto* sub_bb = sub_loaded->findChild<QDialogButtonBox*>("buttonBox");
      if (sub_bb) {
        QObject::connect(sub_bb, &QDialogButtonBox::accepted, sub_dialog, &QDialog::accept);
        QObject::connect(sub_bb, &QDialogButtonBox::rejected, sub_dialog, &QDialog::reject);
      }
    }

    sub_dialog->exec();
    delete sub_dialog;
  };

  // 4. Handle file picker actions
  auto maybe_show_file_picker = [&](const std::string& widget_name) {
    if (!config_.enable_file_picker) {
      return;
    }
    PJ::WidgetDataView view(handle_.widget_data());
    if (!view.isFilePicker(widget_name)) {
      return;
    }
    auto filter = view.filePickerFilter(widget_name).value_or("");
    auto title = view.filePickerTitle(widget_name).value_or("Select File");
    QString path =
        QFileDialog::getOpenFileName(dialog, QString::fromStdString(title), QString(), QString::fromStdString(filter));
    if (!path.isEmpty()) {
      if (handle_.sendEvent(widget_name, PJ::WidgetEventBuilder::fileSelected(path.toStdString()))) {
        auto ar = apply_and_diff(binding_root, handle_, prev_data, config_.enable_diff, stats_.diff_apply_count);
        maybe_open_sub_dialog(ar);
      }
    }
  };

  // 5. Wire signals
  connectWidgetSignals(binding_root, [&](const std::string& name, const std::string& event_json) {
    stats_.event_count++;
    if (handle_.sendEvent(name, event_json)) {
      auto ar = apply_and_diff(binding_root, handle_, prev_data, config_.enable_diff, stats_.diff_apply_count);
      if (ar.wants_accept) {
        dialog->accept();
        return;
      }
      maybe_open_sub_dialog(ar);
    }
    maybe_show_file_picker(name);
  });

  // 6. Start tick timer
  QTimer tick_timer;
  tick_timer.setInterval(config_.tick_interval_ms);
  QObject::connect(&tick_timer, &QTimer::timeout, [&]() {
    stats_.tick_count++;
    if (handle_.tick()) {
      auto ar = apply_and_diff(binding_root, handle_, prev_data, config_.enable_diff, stats_.diff_apply_count);
      maybe_open_sub_dialog(ar);
    }
  });
  tick_timer.start();

  // 7. Run dialog
  int result = dialog->exec();
  tick_timer.stop();

  // 8. Notify plugin and clean up
  DialogResult dr;
  if (result == QDialog::Accepted) {
    handle_.accept(handle_.save_config());
    dr = DialogResult::kAccepted;
  } else {
    handle_.reject();
    dr = DialogResult::kRejected;
  }
  // Save dialog geometry for next time
  if (!plugin_name.empty()) {
    QSettings settings;
    settings.setValue(geometry_key, dialog->saveGeometry());
  }

  delete dialog;
  return dr;
}

// ---------------------------------------------------------------------------
// run_headless
// ---------------------------------------------------------------------------

std::string DialogEngine::savedConfig() const {
  return handle_.save_config();
}

std::string DialogEngine::runHeadless(int max_ticks) {
  for (int i = 0; i < max_ticks; ++i) {
    (void)handle_.tick();
  }
  return handle_.widget_data();
}

}  // namespace PJ
