#pragma once

// QtDiagnosticBridge — adapter that exposes a thread-safe PJ::DiagnosticSink
// returning a Qt signal callers can connect to. Lets non-Qt modules surface
// diagnostics to a Qt GUI without depending on Qt themselves.

#include <QObject>
#include <QString>

#include "pj_base/diagnostic_sink.hpp"

namespace proto {

class QtDiagnosticBridge : public QObject {
  Q_OBJECT

 public:
  explicit QtDiagnosticBridge(QObject* parent = nullptr);

  /// Returns a sink that marshals every event onto this object's Qt thread via
  /// a queued connection, then emits diagnosticReported. Safe to call from any
  /// thread; safe to outlive this bridge (the lambda holds a QPointer).
  PJ::DiagnosticSink sink();

 signals:
  void diagnosticReported(int level, QString source, QString id, QString message);
};

}  // namespace proto
