#pragma once

#include <stdint.h>

// 4x4 Bayer matrix for ordered dithering
inline const uint8_t bayer4x4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

// Apply Bayer dithering and quantize to 4 levels (0-3)
// Stateless - works correctly with any pixel processing order
//
// The four levels are 0, 85, 170 and 255, so a grey belongs to whichever is
// nearest and the boundaries fall at 42, 127 and 212. Thresholding at 64, 128
// and 192 instead — a quarter, half and three quarters of the range — is not
// the same thing, and it cost the picture both ends of its tonal range: a grey
// of 43 averaged out at 16 and a grey of 235 at pure white, so shadow and
// highlight detail collapsed into flat black and flat white and only the two
// middle levels carried any modelling at all.
//
// The offset spreads the matrix cells across one quantisation step, the same
// construction the 1-bit path below uses, which keeps it symmetric: the mean
// output over a 4x4 tile now tracks the input to within about one level across
// the whole range, which is the property that makes dithering work.
inline uint8_t applyBayerDither4Level(uint8_t gray, int x, int y) {
  const int offset = ((2 * bayer4x4[y & 3][x & 3] + 1) * 85) / 32 - 42;

  int adjusted = gray + offset;
  if (adjusted < 0) adjusted = 0;
  if (adjusted > 255) adjusted = 255;

  // Nearest of 0/85/170/255, i.e. boundaries at 42, 127 and 212.
  return static_cast<uint8_t>((adjusted * 3 + 127) / 255);
}

// Apply Bayer dithering for a 1-bit target. Returns 3 (white) or 0 (black) so the
// result flows through the same writePixel path as the 4-level values.
//
// A 1-bit panel cannot use the 4-level result: writePixel's BW branch treats
// every level below 3 as black, so three of the four levels collapse to black and
// a photograph loses its midtones. Thresholding the original grey against the
// Bayer matrix spreads the full 0-255 range across the 16 cells instead, so mid
// greys come out as an even black/white mix.
inline uint8_t applyBayerDither1Bit(uint8_t gray, int x, int y) {
  // Cell centres: (2*bayer + 1) * 255 / 32 maps 0..15 onto thresholds 7..247,
  // averaging 127 so mid grey dithers to roughly half black, half white.
  const int threshold = ((2 * bayer4x4[y & 3][x & 3] + 1) * 255) / 32;
  return gray > threshold ? 3 : 0;
}
