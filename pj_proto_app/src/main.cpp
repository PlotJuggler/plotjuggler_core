#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

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
  parser.addOption(plugin_dir_opt);
  parser.addOption(load_opt);
  parser.addOption(screenshot_opt);
  parser.addOption(plot_opt);
  parser.process(app);

  auto plugin_dir = parser.value(plugin_dir_opt).toStdString();

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

  // Take screenshot if --screenshot was specified
  if (parser.isSet(screenshot_opt)) {
    auto screenshot_path = parser.value(screenshot_opt);
    int delay = has_load ? 500 : 200;
    QTimer::singleShot(delay, [&window, &app, screenshot_path]() {
      window.grab().save(screenshot_path);
      qDebug("Screenshot saved to %s", qPrintable(screenshot_path));
      app.quit();
    });
  }

  return app.exec();
}
