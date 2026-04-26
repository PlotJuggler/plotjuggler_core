#include "qt_diagnostic_bridge.hpp"

#include <QMetaObject>
#include <QPointer>
#include <Qt>

namespace proto {

QtDiagnosticBridge::QtDiagnosticBridge(QObject* parent) : QObject(parent) {}

PJ::DiagnosticSink QtDiagnosticBridge::sink() {
  // Capture a QPointer so a queued event firing after `this` is destroyed
  // becomes a no-op instead of a use-after-free.
  QPointer<QtDiagnosticBridge> guard(this);
  return [guard](const PJ::Diagnostic& d) {
    if (!guard) {
      return;
    }
    QMetaObject::invokeMethod(
        guard.data(),
        [guard, d]() {
          if (!guard) {
            return;
          }
          emit guard->diagnosticReported(
              static_cast<int>(d.level), QString::fromStdString(d.source), QString::fromStdString(d.id),
              QString::fromStdString(d.message));
        },
        Qt::QueuedConnection);
  };
}

}  // namespace proto
