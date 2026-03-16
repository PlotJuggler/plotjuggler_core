#pragma once

#include <QWidget>

#include "pj_base/types.hpp"

namespace proto {

class TimeRangeSlider : public QWidget {
  Q_OBJECT

 public:
  explicit TimeRangeSlider(QWidget* parent = nullptr);

  void setGlobalRange(PJ::Timestamp global_min, PJ::Timestamp global_max);
  [[nodiscard]] PJ::Timestamp begin() const {
    return begin_;
  }
  [[nodiscard]] PJ::Timestamp end() const {
    return end_;
  }

 signals:
  void rangeChanged(PJ::Timestamp begin, PJ::Timestamp end);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

 private:
  [[nodiscard]] int timestampToPixel(PJ::Timestamp t) const;
  [[nodiscard]] PJ::Timestamp pixelToTimestamp(int x) const;

  PJ::Timestamp global_min_ = 0;
  PJ::Timestamp global_max_ = 1;
  PJ::Timestamp begin_ = 0;
  PJ::Timestamp end_ = 1;

  enum class DragTarget { kNone, kBegin, kEnd, kMiddle };
  DragTarget drag_target_ = DragTarget::kNone;
  int drag_start_x_ = 0;
  PJ::Timestamp drag_begin_start_ = 0;
  PJ::Timestamp drag_end_start_ = 0;

  static constexpr int kHandleWidth = 8;
  static constexpr int kTrackMargin = 10;
};

}  // namespace proto
