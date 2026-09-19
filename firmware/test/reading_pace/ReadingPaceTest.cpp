// Pace against the reader's own baseline.
//
// The agent uses "slower than usual" to pick which part of a passage to ask
// about, so the comparison has to be with this reader and not with an average
// one. These tests exist because the first version compared against a
// hard-coded 25 s/page, which made every fast reader look permanently rushed
// and every slow one permanently fine.
#include <gtest/gtest.h>

#include "ReadingPace.h"

using namespace pace;

TEST(Pace, SaysNothingUntilItHasSeenEnoughSessions) {
  EXPECT_EQ(comparePct(30000, 25000, 0), -1);
  EXPECT_EQ(comparePct(30000, 25000, MIN_SESSIONS - 1), -1) << "no honest comparison yet";
  EXPECT_NE(comparePct(30000, 25000, MIN_SESSIONS), -1);
}

TEST(Pace, SaysNothingWithoutABaselineOrASession) {
  EXPECT_EQ(comparePct(0, 25000, 10), -1);
  EXPECT_EQ(comparePct(25000, 0, 10), -1);
}

TEST(Pace, SlowerThanUsualIsUnderAHundred) {
  // Twice as long on a page as usual.
  EXPECT_EQ(comparePct(50000, 25000, 10), 50);
  // The relay treats anything under 80 as slowed; this is the boundary it sees.
  EXPECT_LT(comparePct(40000, 25000, 10), 80);
  EXPECT_GT(comparePct(26000, 25000, 10), 80) << "a slightly slow page is not a signal";
}

TEST(Pace, FasterThanUsualIsOverAHundred) {
  EXPECT_EQ(comparePct(12500, 25000, 10), 200);
  EXPECT_EQ(comparePct(25000, 25000, 10), 100) << "reading at your own pace is exactly 100";
}

TEST(Pace, AFastReaderIsNotPermanentlySlow) {
  // The bug this replaced: a 10 s/page reader measured against a fixed 25 s
  // baseline looked 250% fast forever, and a 60 s/page reader looked slowed on
  // every single session.
  const uint32_t fastBaseline = 10000, slowBaseline = 60000;
  EXPECT_EQ(comparePct(10000, fastBaseline, 10), 100);
  EXPECT_EQ(comparePct(60000, slowBaseline, 10), 100);
}

TEST(Pace, ExtremesAreClampedRatherThanOverflowing) {
  EXPECT_LE(comparePct(1, 60000, 10), 400);
  EXPECT_GE(comparePct(60000, 1, 10), 1) << "never zero or negative";
}

TEST(Pace, TheFirstSessionBecomesTheBaseline) { EXPECT_EQ(fold(0, 30000, 0), 30000u); }

TEST(Pace, TheBaselineMovesTowardsNewSessionsButNotAllTheWay) {
  const uint32_t before = 20000;
  const uint32_t after = fold(before, 40000, 1);
  EXPECT_GT(after, before);
  EXPECT_LT(after, 40000u) << "one session does not become the baseline";
}

TEST(Pace, TheBaselineStopsChasingOnceItIsEstablished) {
  // A single distracted evening should barely move a settled baseline.
  const uint32_t settled = 25000;
  const uint32_t nudged = fold(settled, 100000, 50);
  EXPECT_LT(nudged - settled, 7000u) << "one bad session must not redefine the reader";
}

TEST(Pace, TheBaselineConvergesOnASustainedChange) {
  // Someone who genuinely speeds up should be followed, just not instantly.
  uint32_t baseline = 40000;
  for (int i = 0; i < 60; i++) baseline = fold(baseline, 15000, static_cast<uint16_t>(i));
  EXPECT_NEAR(static_cast<double>(baseline), 15000.0, 1500.0);
}

TEST(Pace, ASlowSessionNeverUnderflows) {
  // The subtraction is unsigned; a mean far below the baseline must not wrap.
  EXPECT_LT(fold(60000, 1, 5), 60000u);
  EXPECT_GT(fold(60000, 1, 5), 0u);
}

TEST(Pace, AnEmptySessionLeavesTheBaselineAlone) { EXPECT_EQ(fold(25000, 0, 5), 25000u); }
