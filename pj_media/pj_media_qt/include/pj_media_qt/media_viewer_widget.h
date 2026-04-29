#pragma once

#include <rhi/qrhi.h>

#include <QFile>
#include <QImage>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QRhiWidget>
#include <QWheelEvent>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "pj_media_core/decoded_frame.h"
#include "pj_media_core/scene_frame.h"

namespace PJ {

class MediaSource;

/// GPU-accelerated image/video viewer using QRhiWidget.
///
/// Two usage modes:
///
/// 1. **MediaSource mode** (preferred): call setMediaSource() once, then
///    setTimestamp() on each application tick. The widget polls the source
///    in render() via takeFrame().
///
/// 2. **Manual mode** (backward compat): call setFrame() directly with
///    decoded pixels. Used when no MediaSource is attached.
///
/// Supports YUV420P (3-plane BT.709 shader), RGB888/RGBA8888 DecodedFrame,
/// and QImage inputs.
///
/// Zoom (mouse wheel, cursor-anchored) and pan (mouse drag) via a view
/// transform matrix in the vertex shader. See REQUIREMENTS.md §4.7.
///
/// Thread-safe: setFrame() may be called from any thread.
class MediaViewerWidget : public QRhiWidget {
  Q_OBJECT

 public:
  explicit MediaViewerWidget(QWidget* parent = nullptr);

  /// Attach a MediaSource. The widget does NOT take ownership.
  /// Call setTimestamp() to drive the source; render() polls takeFrame().
  void setMediaSource(MediaSource* source);

  /// Forward a timestamp to the attached MediaSource.
  /// No-op if no source is attached.
  void setTimestamp(int64_t ts_ns);

  /// Set a decoded video frame (YUV420P or RGB). Thread-safe.
  void setFrame(const DecodedFrame& frame);

  /// Set an RGB image (backward compat for image viewers). Thread-safe.
  void setFrame(const QImage& img);

  /// Reset zoom to 1x and pan to origin.
  void resetView();

 signals:
  void zoomChanged(float zoom);

 protected:
  void initialize(QRhiCommandBuffer* cb) override;
  void render(QRhiCommandBuffer* cb) override;
  void releaseResources() override;

  void wheelEvent(QWheelEvent* e) override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void mouseDoubleClickEvent(QMouseEvent* e) override;

 private:
  [[nodiscard]] QMatrix4x4 buildViewTransform(QSize output_size) const;
  static QShader loadShader(const QString& path);
  // Get-or-create the glyph mask texture for a given (text, font_size). Renders
  // via QPainter on first miss and uploads as an R8 QRhiTexture. The texture
  // pointer is owned by text_cache_; never delete the returned pointer.
  struct TextEntry;
  TextEntry* getOrCreateTextTexture(const std::string& text, double font_size,
                                    QRhiResourceUpdateBatch* updates);

  // Pipeline for YUV→RGB shader (video frames)
  QRhi* rhi_cached_ = nullptr;
  QRhiGraphicsPipeline* pipeline_ = nullptr;
  QRhiBuffer* uniform_buf_ = nullptr;
  QRhiSampler* sampler_ = nullptr;
  QRhiShaderResourceBindings* srb_ = nullptr;

  // YUV420P: 3 separate R8 textures. RGBA: tex_y_ used as RGBA8.
  QRhiTexture* tex_y_ = nullptr;
  QRhiTexture* tex_u_ = nullptr;
  QRhiTexture* tex_v_ = nullptr;

  // MediaSource (not owned)
  MediaSource* media_source_ = nullptr;

  // Pending frame (set from any thread, uploaded on render tick)
  std::mutex frame_mutex_;
  DecodedFrame pending_decoded_;  // YUV420P or RGB frame
  QImage pending_qimage_;         // QImage fallback
  bool has_pending_ = false;
  bool pending_is_yuv_ = false;

  int tex_width_ = 0;
  int tex_height_ = 0;
  float frame_aspect_ = 0.0f;
  int current_pixel_format_ = 2;  // 0=YUV420P, 1=NV12, 2=RGBA

  float zoom_ = 1.0f;
  float pan_x_ = 0.0f;
  float pan_y_ = 0.0f;
  QPointF last_mouse_pos_;

  // Uniform buffer layout (std140):
  // mat4 viewTransform  (64 bytes, offset 0)
  // mat4 colorMatrix    (64 bytes, offset 64)
  // int  pixelFormat    (4 bytes, offset 128)
  // padding             (12 bytes)
  static constexpr int kUniformBufSize = 144;

  // ----- Vector overlay pipeline (markers / annotations) -----
  // Second QRhi pipeline that draws line primitives on top of the image
  // pass, sharing the viewTransform so markers track pan/zoom/letterbox.
  QRhiGraphicsPipeline* marker_pipeline_ = nullptr;
  QRhiBuffer* marker_uniform_buf_ = nullptr;
  QRhiBuffer* marker_vbo_ = nullptr;
  QRhiShaderResourceBindings* marker_srb_ = nullptr;
  size_t marker_vbo_capacity_ = 0;        ///< current VBO byte capacity
  std::vector<float> marker_vertex_data_; ///< CPU-side scratch (pos.xy + color.rgba)
  std::vector<SceneFrame> last_overlays_; ///< persisted across renders
  bool overlays_dirty_ = false;           ///< rebuild VBO on next render

  // ----- kPoints quad pipeline (solid filled squares for kPoints topology) -----
  // Third QRhi pipeline (Triangles topology) sharing marker_uniform_buf_ but
  // with its own SRB and VBO. Each kPoints point becomes 2 triangles centred
  // on the point with side = thickness.
  QRhiGraphicsPipeline* points_pipeline_ = nullptr;
  QRhiBuffer* points_vbo_ = nullptr;
  QRhiShaderResourceBindings* points_srb_ = nullptr;
  size_t points_vbo_capacity_ = 0;
  std::vector<float> points_vertex_data_;

  // ----- Thick lines pipeline (Triangles topology, perpendicular expansion) -----
  // Fourth QRhi pipeline. Used when PointsAnnotation.thickness > 1.5 (line
  // primitives) or when CircleAnnotation.thickness > 1.5. Each segment expands
  // CPU-side to 2 triangles forming a rectangle of width = thickness.
  QRhiGraphicsPipeline* thick_pipeline_ = nullptr;
  QRhiBuffer* thick_vbo_ = nullptr;
  QRhiShaderResourceBindings* thick_srb_ = nullptr;
  size_t thick_vbo_capacity_ = 0;
  std::vector<float> thick_vertex_data_;

  // ----- Text pipeline (Triangles, textured quads with QPainter masks) -----
  // Fifth QRhi pipeline. One textured quad per TextAnnotation; texture is an
  // R8 alpha mask painted by QPainter, the per-vertex color provides the tint.
  // Cache key = (text, font_size_q): two labels with same text+size but different
  // colors share the same texture (color applied at fragment time).
  QRhiGraphicsPipeline* text_pipeline_ = nullptr;
  QRhiBuffer* text_vbo_ = nullptr;
  QRhiShaderResourceBindings* text_srb_ = nullptr;  // pipeline layout SRB (placeholder texture)
  QRhiTexture* text_placeholder_tex_ = nullptr;     // owned, lives until releaseResources
  QRhiSampler* text_sampler_ = nullptr;
  size_t text_vbo_capacity_ = 0;
  std::vector<float> text_vertex_data_;  ///< stride 32: pos.xy + uv.xy + color.rgba

  struct TextKey {
    std::string text;
    uint32_t font_size_q = 0;
    bool operator==(const TextKey& o) const noexcept {
      return font_size_q == o.font_size_q && text == o.text;
    }
  };
  struct TextKeyHash {
    size_t operator()(const TextKey& k) const noexcept {
      return std::hash<std::string>{}(k.text) ^ (static_cast<size_t>(k.font_size_q) * 0x9e3779b9u);
    }
  };
  struct TextEntry {
    QRhiTexture* tex = nullptr;
    QRhiShaderResourceBindings* srb = nullptr;  // Owns its own binding to `tex`.
    int width = 0;
    int height = 0;
  };
  std::unordered_map<TextKey, TextEntry, TextKeyHash> text_cache_;
  // Per-text quad metadata captured at rebuild time, consumed at draw time.
  struct TextDrawItem {
    QRhiShaderResourceBindings* srb;  // pointer borrowed from text_cache_
    size_t vbo_offset_bytes;
  };
  std::vector<TextDrawItem> text_draw_items_;

  // Uniform layout for marker pipeline (std140). frameSize is vec4 (only .xy
  // used) instead of vec2 to dodge a std140 alignment quirk in the OpenGL
  // backend; see scene_lines.vert for context.
  struct alignas(16) MarkerUbo {
    float view[16];
    float frame_size[4];
  };
  static constexpr int kMarkerUniformBufSize = sizeof(MarkerUbo);
};

}  // namespace PJ
