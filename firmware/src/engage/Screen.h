#pragma once

// Engage screen model — the parsed, fully-resolved form of a screen manifest.
//
// Everything here is fixed-size on purpose. A manifest arrives as JSON (from
// flash today, from SD or the relay later), is parsed once into this struct,
// and the JSON document is destroyed before anything is drawn. Rendering then
// touches no allocator at all, which is what keeps the renderer usable while
// TLS is holding ~40 KB and the largest free block is under 50 KB.
//
// Caps are hard. A manifest that exceeds them is truncated and logged, never
// grown into.

#include <cstddef>
#include <cstdint>

namespace engage {

constexpr int kMaxRows = 16;
constexpr size_t kMaxTextBytes = 64;
constexpr size_t kMaxTitleBytes = 40;

enum class RowKind : uint8_t {
  Text,  // one line of text, optionally centred/bold
  Kv,    // small label with a larger value beneath it, value wraps to 2 lines
  Rule,  // horizontal divider
  Logo,  // the 120px product mark, centred
};

struct Row {
  RowKind kind = RowKind::Text;
  bool bold = false;
  bool centered = false;
  // Trailing gap, in multiples of the theme's verticalSpacing.
  uint8_t gapAfter = 1;
  // Literal shown ahead of `a`, and only when `a` resolved to something. Lets
  // an optional row carry its own label without the format needing a branch.
  char prefix[kMaxTextBytes] = {0};
  char a[kMaxTextBytes] = {0};  // Text: the line. Kv: the label.
  char b[kMaxTextBytes] = {0};  // Kv: the resolved value.
};

struct Screen {
  char title[kMaxTitleBytes] = {0};
  Row rows[kMaxRows];
  uint8_t rowCount = 0;
};

// The whole point of the caps: a screen must stay affordable when Wi-Fi is up
// and the largest free block is under 50 KB. Raising a cap must move this too,
// deliberately.
static_assert(sizeof(Screen) <= 4096, "Engage Screen must stay under 4 KB");

}  // namespace engage
