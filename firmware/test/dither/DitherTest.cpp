#include <gtest/gtest.h>

#include "DitherUtils.h"

namespace {

// Mean output level of a 4x4 tile, expressed back in 0-255 so it can be
// compared with the input grey directly.
double tileMean4Level(const uint8_t gray) {
  int sum = 0;
  for (int y = 0; y < 4; y++)
    for (int x = 0; x < 4; x++) sum += applyBayerDither4Level(gray, x, y);
  return sum * 85.0 / 16.0;
}

double tileMean1Bit(const uint8_t gray) {
  int sum = 0;
  for (int y = 0; y < 4; y++)
    for (int x = 0; x < 4; x++) sum += applyBayerDither1Bit(gray, x, y) == 3 ? 255 : 0;
  return sum / 16.0;
}

}  // namespace

// The whole point of dithering: a tile of a flat grey must average back to that
// grey. Without this the picture is not dithered, it is re-graded.
TEST(Dither, FourLevelTilePreservesBrightness) {
  for (int gray = 0; gray <= 255; gray++) {
    EXPECT_NEAR(tileMean4Level(static_cast<uint8_t>(gray)), gray, 3.0) << "grey " << gray;
  }
}

// The regression this replaced: thresholds at 64/128/192 rather than at the
// midpoints between the four levels. Shadows crushed and highlights blew out,
// which is where a photograph keeps most of its detail.
TEST(Dither, ShadowsAndHighlightsSurvive) {
  EXPECT_GT(tileMean4Level(21), 10.0) << "a dark grey must not average to black";
  EXPECT_LT(tileMean4Level(235), 250.0) << "a light grey must not average to white";
  EXPECT_NEAR(tileMean4Level(43), 43, 3.0);
  EXPECT_NEAR(tileMean4Level(213), 213, 3.0);
}

TEST(Dither, EndsAreExact) {
  EXPECT_EQ(tileMean4Level(0), 0.0);
  EXPECT_EQ(tileMean4Level(255), 255.0);
}

TEST(Dither, FourLevelIsMonotonic) {
  double previous = -1.0;
  for (int gray = 0; gray <= 255; gray++) {
    const double mean = tileMean4Level(static_cast<uint8_t>(gray));
    EXPECT_GE(mean, previous - 0.01) << "brightness went backwards at " << gray;
    previous = mean;
  }
}

TEST(Dither, EveryLevelIsReachable) {
  bool seen[4] = {false, false, false, false};
  for (int gray = 0; gray <= 255; gray++)
    for (int y = 0; y < 4; y++)
      for (int x = 0; x < 4; x++) seen[applyBayerDither4Level(static_cast<uint8_t>(gray), x, y)] = true;
  for (int level = 0; level < 4; level++) EXPECT_TRUE(seen[level]) << "level " << level << " never used";
}

TEST(Dither, NeverEscapesTheFourLevels) {
  for (int gray = 0; gray <= 255; gray++)
    for (int y = 0; y < 4; y++)
      for (int x = 0; x < 4; x++) EXPECT_LE(applyBayerDither4Level(static_cast<uint8_t>(gray), x, y), 3);
}

// The 1-bit path was already correct; keep it that way.
TEST(Dither, OneBitTilePreservesBrightness) {
  for (int gray = 0; gray <= 255; gray += 1) {
    EXPECT_NEAR(tileMean1Bit(static_cast<uint8_t>(gray)), gray, 18.0) << "grey " << gray;
  }
}
