#include <gtest/gtest.h>

#include <numeric>
#include <vector>

#include "ImageToneMapper.h"

using freeink::ImageToneMapper;

namespace {

// Collects everything the sink is handed, so the streaming contract itself can
// be asserted: one row per destination line, in order, exactly once.
struct Collected {
  int width = 0;
  std::vector<int> rowOrder;
  std::vector<std::vector<uint8_t>> rows;

  static void sink(void* ctx, int dstY, const uint8_t* levels, int dstWidth) {
    auto* self = static_cast<Collected*>(ctx);
    self->width = dstWidth;
    self->rowOrder.push_back(dstY);
    self->rows.emplace_back(levels, levels + dstWidth);
  }

  double mean(const int levels) const {
    double total = 0;
    size_t count = 0;
    for (const auto& row : rows) {
      total += std::accumulate(row.begin(), row.end(), 0.0);
      count += row.size();
    }
    // Back into 0-255 so it compares with the input grey directly.
    return count ? (total / count) * 255.0 / (levels - 1) : 0.0;
  }
};

// Runs a flat image of `gray` through the mapper.
Collected runFlat(const int srcW, const int srcH, const int dstW, const int dstH, const int levels,
                  const uint8_t gray) {
  Collected out;
  ImageToneMapper mapper;
  EXPECT_TRUE(mapper.begin(srcW, srcH, dstW, dstH, levels, &Collected::sink, &out));
  const std::vector<uint8_t> row(srcW, gray);
  for (int y = 0; y < srcH; y++) mapper.addSourceRow(row.data());
  mapper.finish();
  return out;
}

}  // namespace

TEST(ToneMapper, EmitsEveryDestinationRowOnceInOrder) {
  const Collected out = runFlat(64, 40, 16, 10, 4, 128);
  ASSERT_EQ(out.rows.size(), 10u);
  EXPECT_EQ(out.width, 16);
  for (int y = 0; y < 10; y++) EXPECT_EQ(out.rowOrder[y], y) << "rows must arrive in raster order";
}

TEST(ToneMapper, LevelsStayInRange) {
  for (const int levels : {2, 4, 16}) {
    for (const uint8_t gray : {uint8_t{0}, uint8_t{37}, uint8_t{128}, uint8_t{200}, uint8_t{255}}) {
      const Collected out = runFlat(32, 16, 8, 8, levels, gray);
      for (const auto& row : out.rows)
        for (const uint8_t v : row) EXPECT_LE(v, levels - 1) << "levels=" << levels << " gray=" << int(gray);
    }
  }
}

// The property that makes error diffusion worth having: a flat field averages
// back to itself, at any level count, anywhere in the range.
TEST(ToneMapper, FlatFieldPreservesBrightness) {
  for (int gray = 0; gray <= 255; gray += 5) {
    const Collected out = runFlat(64, 64, 32, 32, 4, static_cast<uint8_t>(gray));
    EXPECT_NEAR(out.mean(4), gray, 6.0) << "grey " << gray;
  }
}

TEST(ToneMapper, BothEndsAreExact) {
  EXPECT_DOUBLE_EQ(runFlat(32, 32, 16, 16, 4, 0).mean(4), 0.0);
  EXPECT_DOUBLE_EQ(runFlat(32, 32, 16, 16, 4, 255).mean(4), 255.0);
}

// Downscaling must average the source, not sample it: a one-pixel-wide white
// stripe on black has to survive as grey rather than vanish or take over.
TEST(ToneMapper, DownscaleAveragesRatherThanSamples) {
  Collected out;
  ImageToneMapper mapper;
  ASSERT_TRUE(mapper.begin(64, 8, 8, 8, 16, &Collected::sink, &out));
  std::vector<uint8_t> row(64, 0);
  for (int x = 0; x < 64; x += 8) row[x] = 255;  // one lit pixel per 8 -> 1/8 grey
  for (int y = 0; y < 8; y++) mapper.addSourceRow(row.data());
  mapper.finish();
  EXPECT_NEAR(out.mean(16), 255.0 / 8.0, 12.0) << "nearest-neighbour would give 0 or 255, not the average";
}

TEST(ToneMapper, UpscaleFillsEveryRow) {
  const Collected out = runFlat(8, 4, 16, 12, 4, 170);
  ASSERT_EQ(out.rows.size(), 12u);
  for (const auto& row : out.rows) EXPECT_EQ(row.size(), 16u);
  EXPECT_NEAR(out.mean(4), 170, 6.0);
}

TEST(ToneMapper, OneToOneIsNotDestructive) {
  const Collected out = runFlat(16, 16, 16, 16, 4, 85);
  EXPECT_NEAR(out.mean(4), 85, 4.0);
}

TEST(ToneMapper, GradientStaysMonotonic) {
  Collected out;
  ImageToneMapper mapper;
  ASSERT_TRUE(mapper.begin(256, 32, 256, 32, 16, &Collected::sink, &out));
  std::vector<uint8_t> row(256);
  for (int x = 0; x < 256; x++) row[x] = static_cast<uint8_t>(x);
  for (int y = 0; y < 32; y++) mapper.addSourceRow(row.data());
  mapper.finish();
  ASSERT_EQ(out.rows.size(), 32u);
  // Column-band means must rise left to right.
  double previous = -1.0;
  for (int band = 0; band < 16; band++) {
    double total = 0;
    for (const auto& r : out.rows)
      for (int x = band * 16; x < (band + 1) * 16; x++) total += r[x];
    const double mean = total / (out.rows.size() * 16);
    EXPECT_GT(mean, previous) << "band " << band;
    previous = mean;
  }
}

TEST(ToneMapper, RefusesNonsense) {
  Collected out;
  ImageToneMapper mapper;
  EXPECT_FALSE(mapper.begin(0, 8, 8, 8, 4, &Collected::sink, &out));
  EXPECT_FALSE(mapper.begin(8, 8, 8, 8, 1, &Collected::sink, &out));
  EXPECT_FALSE(mapper.begin(8, 8, 8, 8, 17, &Collected::sink, &out));
  EXPECT_FALSE(mapper.begin(8, 8, 8, 8, 4, nullptr, &out));
}

TEST(ToneMapper, FinishIsIdempotent) {
  Collected out;
  ImageToneMapper mapper;
  ASSERT_TRUE(mapper.begin(16, 16, 8, 8, 4, &Collected::sink, &out));
  const std::vector<uint8_t> row(16, 128);
  for (int y = 0; y < 16; y++) mapper.addSourceRow(row.data());
  mapper.finish();
  const size_t after = out.rows.size();
  mapper.finish();
  EXPECT_EQ(out.rows.size(), after) << "a second finish must not emit more rows";
}
