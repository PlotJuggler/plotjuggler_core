#include <turbojpeg.h>

#include <QApplication>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "video_widget.hpp"

#define MCAP_IMPLEMENTATION
#include <mcap/reader.hpp>
#include <nanocdr/nanocdr.hpp>

// ---------------------------------------------------------------------------
// LazyMediaSeries — timestamps + resolve callbacks, no data stored in memory
// ---------------------------------------------------------------------------

template <typename T>
class LazyMediaSeries {
 public:
  struct Entry {
    int64_t timestamp;
    std::function<T()> resolve;
  };

  void addEntry(int64_t timestamp, std::function<T()> resolve_fn) {
    entries_.push_back({timestamp, std::move(resolve_fn)});
  }

  /// Find the entry at or before the given timestamp (nearest-before).
  const Entry* latestAt(int64_t ts) const {
    if (entries_.empty()) {
      return nullptr;
    }
    // entries_ is sorted by timestamp (insertion order from MCAP)
    auto it = std::upper_bound(
        entries_.begin(), entries_.end(), ts, [](int64_t t, const Entry& e) { return t < e.timestamp; });
    if (it == entries_.begin()) {
      return nullptr;
    }
    return &*(--it);
  }

  /// Direct index access (for slider / sequential playback).
  const Entry* at(size_t index) const {
    return index < entries_.size() ? &entries_[index] : nullptr;
  }

  size_t size() const {
    return entries_.size();
  }
  bool empty() const {
    return entries_.empty();
  }

  int64_t minTimestamp() const {
    return entries_.empty() ? 0 : entries_.front().timestamp;
  }
  int64_t maxTimestamp() const {
    return entries_.empty() ? 0 : entries_.back().timestamp;
  }

  void clear() {
    entries_.clear();
  }

 private:
  std::vector<Entry> entries_;
};

// ---------------------------------------------------------------------------
// JPEG decoder — shared across all resolve callbacks
// ---------------------------------------------------------------------------

class JpegDecoder {
 public:
  JpegDecoder() : tj_(tjInitDecompress()) {}
  ~JpegDecoder() {
    if (tj_) {
      tjDestroy(tj_);
    }
  }

  JpegDecoder(const JpegDecoder&) = delete;
  JpegDecoder& operator=(const JpegDecoder&) = delete;

  QImage decodeFromCdr(const uint8_t* raw, size_t size) const {
    if (size < 16 || !tj_) {
      return {};
    }

    nanocdr::Decoder dec(nanocdr::ConstBuffer(raw, size));
    dec.jump(8);         // skip stamp (sec + nsec)
    skipCdrString(dec);  // skip frame_id
    skipCdrString(dec);  // skip format

    uint32_t data_len;
    dec.decode(data_len);
    auto buf = dec.currentBuffer();
    if (buf.size() < data_len) {
      return {};
    }

    auto* jpeg_ptr = const_cast<uint8_t*>(buf.data());

    int width = 0, height = 0, subsamp = 0;
    if (tjDecompressHeader2(tj_, jpeg_ptr, data_len, &width, &height, &subsamp) != 0) {
      return {};
    }

    QImage img(width, height, QImage::Format_RGB888);
    if (tjDecompress2(
            tj_, jpeg_ptr, data_len, img.bits(), width, img.bytesPerLine(), height, TJPF_RGB,
            TJFLAG_FASTUPSAMPLE | TJFLAG_FASTDCT) != 0) {
      return {};
    }
    return img;
  }

 private:
  static void skipCdrString(nanocdr::Decoder& dec) {
    uint32_t len;
    dec.decode(len);
    dec.jump(len);
  }

  tjhandle tj_ = nullptr;
};

// ---------------------------------------------------------------------------
// buildImageSeries — opens an MCAP file and builds a LazyMediaSeries<QImage>
//
// This is what a DataSource + MessageParser would produce together:
//   DataSource: opens file, iterates messages, provides re-read capability
//   MessageParser: knows how to decode CompressedImage from CDR + JPEG
//
// The returned series holds only timestamps + closures. No image data in memory.
// Each closure captures shared_ptr<McapReader> and shared_ptr<JpegDecoder>.
// ---------------------------------------------------------------------------

struct McapImageSource {
  LazyMediaSeries<QImage> series;
  std::shared_ptr<mcap::McapReader> reader;
  std::shared_ptr<JpegDecoder> decoder;
};

McapImageSource buildImageSeries(const std::string& path) {
  static constexpr const char* kTargetTopic = "/camera/color/image_raw/compressed";

  auto reader = std::make_shared<mcap::McapReader>();
  auto status = reader->open(path);
  if (!status.ok()) {
    return {};
  }

  status = reader->readSummary(mcap::ReadSummaryMethod::AllowFallbackScan);
  if (!status.ok()) {
    reader->close();
    return {};
  }

  // Find the target channel
  uint16_t channel_id = 0;
  for (auto& [chan_id, chan_ptr] : reader->channels()) {
    if (chan_ptr && chan_ptr->topic == kTargetTopic) {
      channel_id = chan_id;
      break;
    }
  }
  if (channel_id == 0) {
    reader->close();
    return {};
  }

  auto decoder = std::make_shared<JpegDecoder>();

  // Build the lazy series: walk all messages, record timestamp + resolve callback
  McapImageSource source;
  source.reader = reader;
  source.decoder = decoder;

  auto topic_filter = [](std::string_view topic) { return topic == kTargetTopic; };
  mcap::ReadMessageOptions opts;
  opts.topicFilter = topic_filter;

  auto view = reader->readMessages([](const mcap::Status&) {}, opts);
  for (auto it = view.begin(); it != view.end(); ++it) {
    int64_t ts = static_cast<int64_t>(it->message.logTime);

    // Each callback captures only lightweight shared_ptrs + scalars.
    // No image data copied. The callback re-reads from the MCAP file on demand.
    source.series.addEntry(ts, [reader, decoder, ts, channel_id]() -> QImage {
      mcap::ReadMessageOptions seek_opts;
      seek_opts.startTime = static_cast<mcap::Timestamp>(ts);
      seek_opts.endTime = seek_opts.startTime + 1;
      seek_opts.topicFilter = [](std::string_view topic) { return topic == kTargetTopic; };

      auto seek_view = reader->readMessages([](const mcap::Status&) {}, seek_opts);
      for (auto seek_it = seek_view.begin(); seek_it != seek_view.end(); ++seek_it) {
        if (seek_it->message.channelId != channel_id) {
          continue;
        }
        return decoder->decodeFromCdr(
            reinterpret_cast<const uint8_t*>(seek_it->message.data), seek_it->message.dataSize);
      }
      return {};
    });
  }

  return source;
}

// ---------------------------------------------------------------------------
// McapPlayerWindow
// ---------------------------------------------------------------------------

class McapPlayerWindow : public QMainWindow {
  Q_OBJECT

 public:
  McapPlayerWindow() {
    setWindowTitle("MCAP Player");
    resize(800, 650);

    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* layout = new QVBoxLayout(central);

    load_button_ = new QPushButton("Load MCAP", this);
    layout->addWidget(load_button_);

    video_widget_ = new VideoWidget(this);
    video_widget_->setMinimumSize(320, 240);
    video_widget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(video_widget_, 1);

    auto* controls = new QHBoxLayout();
    play_button_ = new QPushButton("\u25B6", this);
    play_button_->setFixedWidth(40);
    play_button_->setEnabled(false);
    controls->addWidget(play_button_);

    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setEnabled(false);
    controls->addWidget(slider_);

    time_label_ = new QLabel("0 / 0", this);
    time_label_->setFixedWidth(120);
    controls->addWidget(time_label_);

    layout->addLayout(controls);

    play_timer_ = new QTimer(this);
    play_timer_->setInterval(33);

    connect(load_button_, &QPushButton::clicked, this, &McapPlayerWindow::onLoad);
    connect(play_button_, &QPushButton::clicked, this, &McapPlayerWindow::onPlayPause);
    connect(slider_, &QSlider::valueChanged, this, &McapPlayerWindow::onSliderChanged);
    connect(play_timer_, &QTimer::timeout, this, &McapPlayerWindow::onTimerTick);
  }

 private slots:
  void onLoad() {
    auto path = QFileDialog::getOpenFileName(this, "Open MCAP", QString(), "MCAP Files (*.mcap)");
    if (path.isEmpty()) {
      return;
    }

    setCursor(Qt::WaitCursor);
    source_ = buildImageSeries(path.toStdString());
    setCursor(Qt::ArrowCursor);

    if (source_.series.empty()) {
      return;
    }

    slider_->setRange(0, static_cast<int>(source_.series.size() - 1));
    slider_->setValue(0);
    slider_->setEnabled(true);
    play_button_->setEnabled(true);
    showFrame(0);
  }

  void onPlayPause() {
    if (play_timer_->isActive()) {
      stopPlayback();
    } else {
      play_timer_->start();
      play_button_->setText("\u23F8");
    }
  }

  void onSliderChanged(int value) {
    if (!play_timer_->isActive()) {
      showFrame(static_cast<size_t>(value));
    }
  }

  void onTimerTick() {
    size_t next = static_cast<size_t>(slider_->value()) + 1;
    if (next >= source_.series.size()) {
      stopPlayback();
      return;
    }
    slider_->blockSignals(true);
    slider_->setValue(static_cast<int>(next));
    slider_->blockSignals(false);
    showFrame(next);
  }

 private:
  void stopPlayback() {
    play_timer_->stop();
    play_button_->setText("\u25B6");
  }

  void showFrame(size_t index) {
    auto* entry = source_.series.at(index);
    if (!entry) {
      return;
    }

    QImage img = entry->resolve();
    if (!img.isNull()) {
      video_widget_->setFrame(img);
    }

    time_label_->setText(QString("%1 / %2").arg(index + 1).arg(source_.series.size()));
  }

  McapImageSource source_;
  VideoWidget* video_widget_ = nullptr;
  QSlider* slider_ = nullptr;
  QPushButton* load_button_ = nullptr;
  QPushButton* play_button_ = nullptr;
  QLabel* time_label_ = nullptr;
  QTimer* play_timer_ = nullptr;
};

// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  McapPlayerWindow window;
  window.show();
  return app.exec();
}

#include "main.moc"
