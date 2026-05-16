# Image Annotations Format

PlotJuggler uses a canonical ImageAnnotations wire format when annotation overlays need to be stored, transported, or replayed as bytes. Source-specific adapters convert third-party messages, such as ROS messages, into `PJ::sdk::ImageAnnotations`; the codec then serializes that canonical value using the Foxglove `ImageAnnotations` protobuf schema.

This keeps PlotJuggler internals and renderers independent from the original message schema. Consumers should decode the bytes into `PJ::sdk::ImageAnnotations` and operate on the canonical values, not on the source message type that produced them.

For the broader builtin type catalog, see [builtin_type.md](builtin_type.md).

## Schema

The schema identifier for this format is:

```text
foxglove.ImageAnnotations
```

The public C++ helpers live in:

```cpp
#include <pj_base/builtin/image_annotations_codec.h>
```

`serializeImageAnnotations()` writes this payload. `deserializeImageAnnotations()`
reads it back into `PJ::sdk::ImageAnnotations`.

The encoded payload is a protobuf message. The current codec writes only the fields listed below and skips unknown fields while reading.

## Top-Level Message

`foxglove.ImageAnnotations`

| Field | Type | Meaning |
| --- | --- | --- |
| `1` | repeated `CircleAnnotation` | Circle overlays |
| `2` | repeated `PointsAnnotation` | Point and line overlays |
| `3` | repeated `TextAnnotation` | Text overlays |

An annotation payload with no circles, points, or texts serializes to an empty byte buffer. Decoding an empty buffer is treated as invalid input by the current reader.

## Shared Messages

`foxglove.Point2`

| Field | Type | Meaning |
| --- | --- | --- |
| `1` | double | X coordinate in image pixels |
| `2` | double | Y coordinate in image pixels |

`foxglove.Color`

| Field | Type | Meaning |
| --- | --- | --- |
| `1` | double | Red channel, normalized to `[0, 1]` |
| `2` | double | Green channel, normalized to `[0, 1]` |
| `3` | double | Blue channel, normalized to `[0, 1]` |
| `4` | double | Alpha channel, normalized to `[0, 1]` |

Canonical colors use 8-bit channels. Encoding converts each channel to a normalized double; decoding converts the normalized value back to an 8-bit channel. Round trips may differ by plus or minus 1 because of numeric rounding.

## Circle Annotations

`foxglove.CircleAnnotation`

| Field | Type | Canonical value |
| --- | --- | --- |
| `2` | `Point2` | `center` |
| `3` | double | `radius * 2.0` |
| `4` | double | `thickness` |
| `5` | `Color` | `fill_color` |
| `6` | `Color` | `outline_color` |

The wire schema stores circle size as a diameter. The canonical type stores it as a radius, so the codec converts between the two.

## Point Annotations

`foxglove.PointsAnnotation`

| Field | Type | Canonical value |
| --- | --- | --- |
| `2` | enum | `type` |
| `3` | repeated `Point2` | `points` |
| `4` | `Color` | `outline_color` |
| `5` | repeated `Color` | `outline_colors` |
| `6` | `Color` | `fill_color` |
| `7` | double | `thickness` |

The point topology is encoded as:

| Wire value | Canonical topology |
| --- | --- |
| `1` | `kPoints` |
| `2` | `kLineLoop` |
| `3` | `kLineStrip` |
| `4` | `kLineList` |

The writer never emits `0`. The reader treats an unknown or zero topology as `kPoints` so old or incomplete payloads still decode to a renderable annotation.

`outline_colors` is optional. If it is empty, field `5` is omitted and the annotation uses the uniform `outline_color`.

## Text Annotations

`foxglove.TextAnnotation`

| Field | Type | Canonical value |
| --- | --- | --- |
| `2` | `Point2` | `position` |
| `3` | string | `text` |
| `4` | double | `font_size` |
| `5` | `Color` | `text_color` |
| `6` | `Color` | skipped by the current canonical type |

`background_color` is present in the wire schema but is not represented by `PJ::sdk::ImageAnnotations`, so it is skipped during decoding and not emitted during encoding.

## Unsupported Canonical Fields

`PJ::sdk::ImageAnnotations` also carries metadata that is useful inside PlotJuggler but is not part of this wire payload:

| Canonical field | Wire behavior |
| --- | --- |
| `timestamp` | Not serialized |
| `image_topic` | Not serialized |

Adapters that need those fields must preserve them outside this payload.

## Reader Behavior

The reader accepts protobuf fields with wire types `VARINT`, `I64`, `LEN`, and `I32`. It decodes the fields listed in this document and skips unknown fields, including unknown nested fields, so the format can tolerate compatible schema additions.

Malformed protobuf data, invalid length-delimited fields, or truncated nested messages fail decoding.
