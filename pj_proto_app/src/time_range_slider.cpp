#include "time_range_slider.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

namespace proto {

TimeRangeSlider::TimeRangeSlider(QWidget* parent) : QWidget(parent) {
  setMinimumHeight(30);
  setMaximumHeight(40);
  setMouseTracking(true);
}

void TimeRangeSlider::setGlobalRange(PJ::Timestamp global_min, PJ::Timestamp global_max) {
  if (global_max <= global_min) {
    return;
  }
  bool first_time = (global_min_ == 0 && global_max_ == 1);
  global_min_ = global_min;
  global_max_ = global_max;
  if (first_time) {
    begin_ = global_min;
    end_ = global_max;
  } else {
    begin_ = std::max(begin_, global_min);
    end_ = std::min(end_, global_max);
    if (begin_ >= end_) {
      begin_ = global_min;
      end_ = global_max;
    }
  }
  update();
}

int TimeRangeSlider::timestampToPixel(PJ::Timestamp t) const {
  if (global_max_ == global_min_) {
    return kTrackMargin;
  }
  double fraction = static_cast<double>(t - global_min_) / static_cast<double>(global_max_ - global_min_);
  int track_width = width() - 2 * kTrackMargin;
  return kTrackMargin + static_cast<int>(fraction * track_width);
}

PJ::Timestamp TimeRangeSlider::pixelToTimestamp(int x) const {
  int track_width = width() - 2 * kTrackMargin;
  if (track_width <= 0) {
    return global_min_;
  }
  double fraction = static_cast<double>(x - kTrackMargin) / static_cast<double>(track_width);
  fraction = std::clamp(fraction, 0.0, 1.0);
  return global_min_ + static_cast<PJ::Timestamp>(fraction * static_cast<double>(global_max_ - global_min_));
}

void TimeRangeSlider::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  int y_center = height() / 2;
  int track_y = y_center - 2;

  // Track background
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(200, 200, 200));
  p.drawRect(kTrackMargin, track_y, width() - 2 * kTrackMargin, 4);

  // Selection region
  int x_begin = timestampToPixel(begin_);
  int x_end = timestampToPixel(end_);
  p.setBrush(QColor(80, 120, 200, 120));
  p.drawRect(x_begin, track_y - 4, x_end - x_begin, 12);

  // Handles
  p.setBrush(QColor(60, 100, 180));
  p.drawRoundedRect(x_begin - kHandleWidth / 2, y_center - 10, kHandleWidth, 20, 2, 2);
  p.drawRoundedRect(x_end - kHandleWidth / 2, y_center - 10, kHandleWidth, 20, 2, 2);
}

void TimeRangeSlider::mousePressEvent(QMouseEvent* event) {
  int x = static_cast<int>(event->position().x());
  int x_begin = timestampToPixel(begin_);
  int x_end = timestampToPixel(end_);

  if (std::abs(x - x_begin) <= kHandleWidth) {
    drag_target_ = DragTarget::kBegin;
  } else if (std::abs(x - x_end) <= kHandleWidth) {
    drag_target_ = DragTarget::kEnd;
  } else if (x > x_begin && x < x_end) {
    drag_target_ = DragTarget::kMiddle;
    drag_start_x_ = x;
    drag_begin_start_ = begin_;
    drag_end_start_ = end_;
  }
}

void TimeRangeSlider::mouseMoveEvent(QMouseEvent* event) {
  if (drag_target_ == DragTarget::kNone) {
    return;
  }

  int x = static_cast<int>(event->position().x());
  PJ::Timestamp t = pixelToTimestamp(x);

  if (drag_target_ == DragTarget::kBegin) {
    begin_ = std::clamp(t, global_min_, end_ - 1);
  } else if (drag_target_ == DragTarget::kEnd) {
    end_ = std::clamp(t, begin_ + 1, global_max_);
  } else if (drag_target_ == DragTarget::kMiddle) {
    int dx = x - drag_start_x_;
    PJ::Timestamp dt = pixelToTimestamp(kTrackMargin + dx) - global_min_;
    PJ::Timestamp new_begin = drag_begin_start_ + dt;
    PJ::Timestamp new_end = drag_end_start_ + dt;
    if (new_begin >= global_min_ && new_end <= global_max_) {
      begin_ = new_begin;
      end_ = new_end;
    }
  }

  update();
  emit rangeChanged(begin_, end_);
}

void TimeRangeSlider::mouseReleaseEvent(QMouseEvent*) {
  drag_target_ = DragTarget::kNone;
}

}  // namespace proto
