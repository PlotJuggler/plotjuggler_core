#include "marker_palette.h"

#include <cstdio>

namespace pj_demos {
namespace {

constexpr PJ::ColorRGBA kClassPalette[] = {
    {  0, 255,   0, 255},  // green
    {255,  64,  64, 255},  // red
    { 64, 128, 255, 255},  // blue
    {255, 192,   0, 255},  // amber
    {255,   0, 255, 255},  // magenta
    {  0, 255, 255, 255},  // cyan
    {255, 128,   0, 255},  // orange
    {128, 255,   0, 255},  // lime
    {200, 100, 255, 255},  // violet
    {255, 200, 200, 255},  // pink
};
constexpr size_t kPaletteSize = sizeof(kClassPalette) / sizeof(kClassPalette[0]);

}  // namespace

PJ::ColorRGBA colorForClass(int32_t class_id) {
  // Negative ids fold into the palette via unsigned wrap.
  auto idx = static_cast<uint32_t>(class_id) % static_cast<uint32_t>(kPaletteSize);
  return kClassPalette[idx];
}

uint32_t fnv1a32(std::string_view s) {
  uint32_t h = 0x811c9dc5u;
  for (unsigned char c : s) {
    h ^= c;
    h *= 0x01000193u;
  }
  return h;
}

std::string formatLabel(const std::string& label, double score) {
  if (label.empty()) return {};
  char buf[64] = {0};
  if (score == score && score > -1e9 && score < 1e9) {
    std::snprintf(buf, sizeof(buf), "%s %.2f", label.c_str(), score);
  } else {
    std::snprintf(buf, sizeof(buf), "%s", label.c_str());
  }
  return std::string(buf);
}

PJ::TextAnnotation makeBboxLabel(const std::string& label_text, double cx, double cy, double hx,
                                  double hy, const PJ::ColorRGBA& color) {
  PJ::TextAnnotation t;
  t.text = label_text;
  t.position = {cx - hx, cy - hy - 18.0};
  t.font_size = 14.0;
  t.color = color;
  return t;
}

PJ::PointsAnnotation makeBboxLineLoop(double cx, double cy, double sx, double sy,
                                       const PJ::ColorRGBA& color) {
  const double hx = sx / 2.0;
  const double hy = sy / 2.0;
  PJ::PointsAnnotation bbox;
  bbox.topology = PJ::AnnotationTopology::kLineLoop;
  bbox.points = {
      {cx - hx, cy - hy},
      {cx + hx, cy - hy},
      {cx + hx, cy + hy},
      {cx - hx, cy + hy},
  };
  bbox.color = color;
  bbox.thickness = 2.0;
  return bbox;
}

}  // namespace pj_demos
