/// Visual sanity check for the full markers feature in pj_media.
///
/// Renders an 800x600 dark-grey image with one of every supported primitive
/// on top, so a human can confirm rendering at a glance:
///
///   • 3 kPoints (regression of the kPoints quad pipeline)
///   • 1 thick LineLoop bbox  → exercises the thick-line pipeline
///   • 1 LineStrip with per-vertex colour gradient
///   • 1 LineList (3 disjoint thin segments)
///   • 1 LineLoop triangle with translucent fill (LoopFill triangulation)
///   • 1 Circle outline only (cyan ring)
///   • 1 Circle filled (magenta disc with translucent interior + outline)
///   • 2 TextAnnotations (white "person 0.92" and cyan "circle")
///
/// Use the mouse wheel to zoom and drag to pan; everything must stay anchored
/// to image-pixel coordinates and translate together with the background.

#include <turbojpeg.h>

#include <QApplication>
#include <QMainWindow>
#include <QVBoxLayout>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "pj_base/expected.hpp"
#include "pj_datastore/object_store.hpp"
#include "pj_media_core/codecs.h"
#include "pj_media_core/composite_media_source.h"
#include "pj_media_core/image_pipeline_source.h"
#include "pj_media_core/scene_decoder.h"
#include "pj_media_core/scene_pipeline_source.h"
#include "pj_marker_protocol/image_annotation.h"
#include "pj_media_qt/media_viewer_widget.h"

namespace {

constexpr int kImgWidth = 800;
constexpr int kImgHeight = 600;

std::vector<uint8_t> makeBackgroundJpeg() {
  std::vector<uint8_t> rgb(static_cast<size_t>(kImgWidth) * static_cast<size_t>(kImgHeight) * 3, 64);

  tjhandle compressor = tjInitCompress();
  unsigned char* jpeg_buf = nullptr;
  unsigned long jpeg_size = 0;  // NOLINT(google-runtime-int)
  tjCompress2(
      compressor, rgb.data(), kImgWidth, kImgWidth * 3, kImgHeight, TJPF_RGB, &jpeg_buf, &jpeg_size, TJSAMP_420, 80,
      TJFLAG_FASTUPSAMPLE);
  std::vector<uint8_t> result(jpeg_buf, jpeg_buf + jpeg_size);
  tjFree(jpeg_buf);
  tjDestroy(compressor);
  return result;
}

PJ::SceneFrame makeDemoSceneFrame() {
  PJ::SceneFrame sf;
  sf.timestamp = 0;
  PJ::ImageAnnotation ia;
  ia.timestamp = 0;
  ia.image_topic = "demo/image";

  // 1. kPoints — three solid coloured squares (regression).
  {
    PJ::PointsAnnotation pa;
    pa.topology = PJ::AnnotationTopology::kPoints;
    pa.points = {{100.0, 100.0}};
    pa.thickness = 4.0;
    pa.color = {255, 0, 0, 255};
    ia.points.push_back(pa);
  }
  {
    PJ::PointsAnnotation pa;
    pa.topology = PJ::AnnotationTopology::kPoints;
    pa.points = {{300.0, 200.0}};
    pa.thickness = 16.0;
    pa.color = {0, 255, 0, 255};
    ia.points.push_back(pa);
  }
  {
    PJ::PointsAnnotation pa;
    pa.topology = PJ::AnnotationTopology::kPoints;
    pa.points = {{500.0, 400.0}};
    pa.thickness = 32.0;
    pa.color = {0, 128, 255, 255};
    ia.points.push_back(pa);
  }

  // 2. Thick bbox — kLineLoop, thickness 4 → routes to thick pipeline.
  {
    PJ::PointsAnnotation pa;
    pa.topology = PJ::AnnotationTopology::kLineLoop;
    pa.points = {{60.0, 60.0}, {260.0, 60.0}, {260.0, 200.0}, {60.0, 200.0}};
    pa.thickness = 4.0;
    pa.color = {255, 80, 80, 255};
    ia.points.push_back(pa);
  }

  // 3. kLineStrip with per-vertex gradient (red → yellow → green → blue).
  {
    PJ::PointsAnnotation pa;
    pa.topology = PJ::AnnotationTopology::kLineStrip;
    pa.points = {{350.0, 80.0}, {420.0, 130.0}, {490.0, 180.0}, {560.0, 230.0}};
    pa.thickness = 3.0;  // also thick → exercises thick pipeline + gradient
    pa.colors = {
        {255, 0, 0, 255},
        {255, 255, 0, 255},
        {0, 255, 0, 255},
        {0, 0, 255, 255},
    };
    pa.color = {255, 255, 255, 255};  // fallback; ignored when colors matches
    ia.points.push_back(pa);
  }

  // 4. kLineList — 3 disjoint thin grey segments (regression).
  {
    PJ::PointsAnnotation pa;
    pa.topology = PJ::AnnotationTopology::kLineList;
    pa.points = {
        {90.0, 380.0}, {180.0, 410.0},   // segment A
        {200.0, 420.0}, {290.0, 380.0},  // segment B
        {310.0, 400.0}, {400.0, 430.0},  // segment C
    };
    pa.thickness = 1.0;  // sub-1.5 → 1px Lines pipeline
    pa.color = {200, 200, 200, 255};
    ia.points.push_back(pa);
  }

  // 5. LineLoop triangle with translucent fill — exercises LoopFill triangulation.
  {
    PJ::PointsAnnotation pa;
    pa.topology = PJ::AnnotationTopology::kLineLoop;
    pa.points = {{120.0, 470.0}, {230.0, 470.0}, {175.0, 560.0}};
    pa.thickness = 2.0;
    pa.color = {0, 220, 220, 255};
    pa.fill_color = {0, 220, 220, 80};  // translucent cyan fill
    ia.points.push_back(pa);
  }

  // 6. Circle outline only — cyan ring, no fill.
  {
    PJ::CircleAnnotation ca;
    ca.center = {650.0, 150.0};
    ca.radius = 50.0;
    ca.thickness = 3.0;  // thick pipeline
    ca.color = {0, 220, 220, 255};
    ca.fill_color = {0, 0, 0, 0};
    ia.circles.push_back(ca);
  }

  // 7. Circle filled — magenta disc with translucent interior + opaque outline.
  {
    PJ::CircleAnnotation ca;
    ca.center = {650.0, 400.0};
    ca.radius = 60.0;
    ca.thickness = 2.0;  // also thick
    ca.color = {255, 0, 220, 255};
    ca.fill_color = {255, 0, 220, 80};
    ia.circles.push_back(ca);
  }

  // 8 + 9. TextAnnotations — labels above the bbox and near the circle.
  {
    PJ::TextAnnotation ta;
    ta.position = {60.0, 30.0};
    ta.font_size = 18.0;
    ta.color = {255, 255, 255, 255};
    ta.text = "person 0.92";
    ia.texts.push_back(ta);
  }
  {
    PJ::TextAnnotation ta;
    ta.position = {610.0, 215.0};
    ta.font_size = 14.0;
    ta.color = {0, 220, 220, 255};
    ta.text = "circle";
    ia.texts.push_back(ta);
  }

  sf.annotations.push_back(std::move(ia));
  return sf;
}

class FixedSceneDecoder final : public PJ::ISceneDecoder {
 public:
  PJ::Expected<PJ::SceneFrame> decode(const uint8_t* /*data*/, size_t /*size*/) override {
    return makeDemoSceneFrame();
  }
};

}  // namespace

class QuadOverlayWindow : public QMainWindow {
 public:
  QuadOverlayWindow() {
    setWindowTitle("Markers Full Demo");
    resize(880, 700);

    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* layout = new QVBoxLayout(central);

    auto* bootstrap = new PJ::MediaViewerWidget(this);
    bootstrap->setMaximumSize(0, 0);
    layout->addWidget(bootstrap);

    viewer_ = new PJ::MediaViewerWidget(this);
    viewer_->setMinimumSize(kImgWidth, kImgHeight);
    viewer_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(viewer_, 1);

    store_ = std::make_unique<PJ::ObjectStore>();

    auto image_id =
        store_->registerTopic({.dataset_id = 1, .topic_name = "demo/image", .metadata_json = "{}"});
    image_topic_ = *image_id;
    store_->pushOwned(image_topic_, /*ts=*/0, makeBackgroundJpeg());

    auto annot_id =
        store_->registerTopic({.dataset_id = 1, .topic_name = "demo/annotations", .metadata_json = "{}"});
    annot_topic_ = *annot_id;
    store_->pushOwned(annot_topic_, /*ts=*/0, std::vector<uint8_t>{0x00});

    auto image_src =
        std::make_unique<PJ::ImagePipelineSource>(store_.get(), image_topic_, PJ::makeJpegPipeline());
    auto scene_src = std::make_unique<PJ::ScenePipelineSource>(store_.get(), annot_topic_,
                                                               std::make_unique<FixedSceneDecoder>());

    auto composite = std::make_unique<PJ::CompositeMediaSource>();
    composite->addLayer(std::move(image_src));
    composite->addLayer(std::move(scene_src));
    source_ = std::move(composite);

    viewer_->setMediaSource(source_.get());
    viewer_->setTimestamp(0);
    viewer_->update();
  }

 private:
  std::unique_ptr<PJ::ObjectStore> store_;
  PJ::ObjectTopicId image_topic_{};
  PJ::ObjectTopicId annot_topic_{};
  std::unique_ptr<PJ::MediaSource> source_;
  PJ::MediaViewerWidget* viewer_ = nullptr;
};

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QuadOverlayWindow window;
  window.show();
  return app.exec();
}
