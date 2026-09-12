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
inline uint8_t applyBayerDither4Level(uint8_t gray, int x, int y) {
  int bayer = bayer4x4[y & 3][x & 3];
  int dither = (bayer - 8) * 5;  // Scale to +/-40 (half of quantization step 85)

  int adjusted = gray + dither;
  if (adjusted < 0) adjusted = 0;
  if (adjusted > 255) adjusted = 255;

  if (adjusted < 64) return 0;
  if (adjusted < 128) return 1;
  if (adjusted < 192) return 2;
  return 3;
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
