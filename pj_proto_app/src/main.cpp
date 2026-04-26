#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QTimer>

#include <filesystem>

#include "main_window.hpp"

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  app.setApplicationName("pj_proto_app");

  QCommandLineParser parser;
  parser.setApplicationDescription("PlotJuggler Prototype");
  parser.addHelpOption();

  QCommandLineOption plugin_dir_opt(
      "plugin-dir", "Directory containing plugin .so files (default: ./plugins/)", "path", "./plugins/");
  QCommandLineOption load_opt("load", "Automatically load a file at startup", "filepath");
  QCommandLineOption screenshot_opt("screenshot", "Take a screenshot after loading and save to path", "path");
  QCommandLineOption plot_opt("plot", "Auto-plot first N fields after loading", "count", "3");
  QCommandLineOption dummy_stream_opt("dummy-stream", "Start the dummy streaming plugin automatically");
  parser.addOption(plugin_dir_opt);
  parser.addOption(load_opt);
  parser.addOption(screenshot_opt);
  parser.addOption(plot_opt);
  parser.addOption(dummy_stream_opt);
  parser.process(app);

  auto plugin_dir = parser.value(plugin_dir_opt).toStdString();

  qDebug() << "[plugin-dir] argv raw value :" << parser.value(plugin_dir_opt);
  qDebug() << "[plugin-dir] cwd            :" << QDir::currentPath();
  {
    std::error_code ec;
    auto abs = std::filesystem::absolute(plugin_dir, ec);
    qDebug() << "[plugin-dir] absolute       :" << QString::fromStdString(abs.string())
             << "(absolute err:" << QString::fromStdString(ec.message()) << ")";
    auto canon = std::filesystem::weakly_canonical(plugin_dir, ec);
    qDebug() << "[plugin-dir] weakly_canon   :" << QString::fromStdString(canon.string())
             << "(canon err:" << QString::fromStdString(ec.message()) << ")";
    qDebug() << "[plugin-dir] exists         :" << std::filesystem::exists(plugin_dir, ec)
             << " is_directory:" << std::filesystem::is_directory(plugin_dir, ec);
  }

  proto::MainWindow window(plugin_dir);
  window.resize(1200, 700);
  window.show();

  bool has_load = parser.isSet(load_opt);
  bool has_plot = parser.isSet(plot_opt);

  // Auto-load file if --load was specified
  if (has_load) {
    auto file_path = parser.value(load_opt);
    int plot_count = has_plot ? parser.value(plot_opt).toInt() : 0;
    QTimer::singleShot(100, [&window, file_path, plot_count]() {
      window.loadFile(file_path);
      if (plot_count > 0) {
        window.plotFirstFields(plot_count);
      }
    });
  }

  // Auto-start dummy stream if --dummy-stream was specified
  if (parser.isSet(dummy_stream_opt)) {
    int plot_count = has_plot ? parser.value(plot_opt).toInt() : 3;
    QTimer::singleShot(100, [&window, plot_count]() {
      window.startDummyStream();
      if (plot_count > 0) {
        window.plotFirstFields(plot_count);
      }
    });
  }

  // Take screenshot if --screenshot was specified
  if (parser.isSet(screenshot_opt)) {
    auto screenshot_path = parser.value(screenshot_opt);
    int delay = has_load ? 500 : (parser.isSet(dummy_stream_opt) ? 2000 : 200);
    QTimer::singleShot(delay, [&window, &app, screenshot_path]() {
      window.grab().save(screenshot_path);
      qDebug("Screenshot saved to %s", qPrintable(screenshot_path));
      app.quit();
    });
  }

  return app.exec();
}
