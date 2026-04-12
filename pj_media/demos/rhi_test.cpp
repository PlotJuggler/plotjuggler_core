/// Systematic test to isolate QRhiWidget multi-instance failure.
/// Each experiment changes ONE variable from the known-working baseline.
///
/// Usage: rhi_test <experiment>
///   1 = baseline: two plain QRhiWidgets in constructor (works)
///   2 = two plain QRhiWidgets created dynamically after show()
///   3 = two MediaViewerWidgets in constructor (before show)
///   4 = two MediaViewerWidgets created dynamically after show()
///   5 = two MediaViewerWidgets dynamic + setFrame called
///   6 = two MediaViewerWidgets dynamic + setFrame + play timer

#include <rhi/qrhi.h>

#include <QApplication>
#include <QHBoxLayout>
#include <QRhiWidget>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <cstdio>

#include "pj_media_qt/media_viewer_widget.h"

class PlainRhiWidget : public QRhiWidget {
 public:
  PlainRhiWidget(QWidget* parent, float r, float g, float b) : QRhiWidget(parent), r_(r), g_(g), b_(b) {
    setApi(Api::OpenGL);
  }

 protected:
  void initialize(QRhiCommandBuffer*) override {}
  void render(QRhiCommandBuffer* cb) override {
    auto* r = rhi();
    if (!r) {
      return;
    }
    auto* u = r->nextResourceUpdateBatch();
    cb->beginPass(renderTarget(), QColor::fromRgbF(r_, g_, b_, 1.0f), {1.0f, 0}, u);
    cb->endPass();
  }
  float r_;
  float g_;
  float b_;
};

int main(int argc, char* argv[]) {
  int experiment = argc > 1 ? std::atoi(argv[1]) : 0;
  if (experiment < 1 || experiment > 6) {
    std::printf(
        "Usage: rhi_test <1-6>\n"
        "  1 = two PlainRhiWidgets in constructor\n"
        "  2 = two PlainRhiWidgets created dynamically after show()\n"
        "  3 = two MediaViewerWidgets in constructor\n"
        "  4 = two MediaViewerWidgets created dynamically after show()\n"
        "  5 = experiment 4 + setFrame called\n"
        "  6 = experiment 5 + play timer calling setFrame repeatedly\n");
    return 1;
  }

  QApplication app(argc, argv);
  QWidget window;
  auto* layout = new QVBoxLayout(&window);
  auto* splitter = new QSplitter(Qt::Horizontal);
  layout->addWidget(splitter);
  window.resize(800, 400);

  QImage red_img(100, 100, QImage::Format_RGB888);
  red_img.fill(QColor(255, 0, 0));
  QImage blue_img(100, 100, QImage::Format_RGB888);
  blue_img.fill(QColor(0, 0, 255));

  PJ::MediaViewerWidget* mv1 = nullptr;
  PJ::MediaViewerWidget* mv2 = nullptr;

  if (experiment == 1) {
    std::printf("Exp 1: two PlainRhiWidgets in constructor\n");
    auto* w1 = new PlainRhiWidget(splitter, 1, 0, 0);
    auto* w2 = new PlainRhiWidget(splitter, 0, 0, 1);
    w1->setMinimumSize(200, 150);
    w2->setMinimumSize(200, 150);
    splitter->addWidget(w1);
    splitter->addWidget(w2);
  } else if (experiment == 2) {
    std::printf("Exp 2: two PlainRhiWidgets created after show()\n");
    window.show();
    auto* w1 = new PlainRhiWidget(splitter, 1, 0, 0);
    auto* w2 = new PlainRhiWidget(splitter, 0, 0, 1);
    w1->setMinimumSize(200, 150);
    w2->setMinimumSize(200, 150);
    splitter->addWidget(w1);
    splitter->addWidget(w2);
    return app.exec();
  } else if (experiment == 3) {
    std::printf("Exp 3: two MediaViewerWidgets in constructor\n");
    mv1 = new PJ::MediaViewerWidget(splitter);
    mv2 = new PJ::MediaViewerWidget(splitter);
    mv1->setMinimumSize(200, 150);
    mv2->setMinimumSize(200, 150);
    splitter->addWidget(mv1);
    splitter->addWidget(mv2);
  } else if (experiment == 4) {
    std::printf("Exp 4: two MediaViewerWidgets created after show()\n");
    window.show();
    mv1 = new PJ::MediaViewerWidget(splitter);
    mv2 = new PJ::MediaViewerWidget(splitter);
    mv1->setMinimumSize(200, 150);
    mv2->setMinimumSize(200, 150);
    splitter->addWidget(mv1);
    splitter->addWidget(mv2);
    return app.exec();
  } else if (experiment == 5) {
    std::printf("Exp 5: two MediaViewerWidgets dynamic + setFrame once\n");
    window.show();
    mv1 = new PJ::MediaViewerWidget(splitter);
    mv2 = new PJ::MediaViewerWidget(splitter);
    mv1->setMinimumSize(200, 150);
    mv2->setMinimumSize(200, 150);
    splitter->addWidget(mv1);
    splitter->addWidget(mv2);
    QTimer::singleShot(500, [&]() {
      mv1->setFrame(red_img);
      mv2->setFrame(blue_img);
    });
    return app.exec();
  } else if (experiment == 6) {
    std::printf("Exp 6: two MediaViewerWidgets dynamic + timer setFrame\n");
    window.show();
    mv1 = new PJ::MediaViewerWidget(splitter);
    mv2 = new PJ::MediaViewerWidget(splitter);
    mv1->setMinimumSize(200, 150);
    mv2->setMinimumSize(200, 150);
    splitter->addWidget(mv1);
    splitter->addWidget(mv2);
    auto* timer = new QTimer(&window);
    int frame = 0;
    QObject::connect(timer, &QTimer::timeout, [&]() {
      red_img.fill(QColor(frame % 256, 0, 0));
      blue_img.fill(QColor(0, 0, frame % 256));
      mv1->setFrame(red_img);
      mv2->setFrame(blue_img);
      ++frame;
    });
    timer->start(33);
    return app.exec();
  }

  window.show();
  return app.exec();
}
