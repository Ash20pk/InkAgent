#include <gtest/gtest.h>

#include "ReadingStats.h"

using readstats::DayLog;

namespace {

std::string duration(const uint32_t ms) {
  char buf[16];
  readstats::formatDuration(ms, buf, sizeof(buf));
  return buf;
}

constexpr uint32_t kMinute = 60u * 1000;
constexpr uint32_t kHour = 60 * kMinute;

}  // namespace

TEST(DayLog, RecordsAndReadsBackTheSameDay) {
  DayLog log;
  log.add(100, 20);
  log.add(100, 15);
  EXPECT_EQ(log.on(100), 35);
  EXPECT_EQ(log.on(99), 0);
}

TEST(DayLog, IgnoresNonsense) {
  DayLog log;
  log.add(-1, 30);
  log.add(100, 0);
  EXPECT_EQ(log.lastDay, -1);
  EXPECT_EQ(log.on(100), 0);
  EXPECT_EQ(log.streak(100), 0);
}

// The ring is the whole reason a month of history costs 56 bytes, and the trap
// it comes with is a slot from four weeks ago still holding minutes.
TEST(DayLog, SkippedDaysAreClearedRatherThanCountedAgain) {
  DayLog log;
  log.add(100, 45);
  ASSERT_EQ(log.on(100), 45);

  // Exactly one lap of the ring: day 128 lands on the slot day 100 wrote.
  log.add(128, 10);
  EXPECT_EQ(log.on(128), 10) << "the new day has only its own minutes";
  EXPECT_EQ(log.on(100), 0) << "and the day it displaced is gone, not doubled";
}

TEST(DayLog, AwayLongerThanTheWindowLeavesNothingBehind) {
  DayLog log;
  for (int32_t d = 100; d < 110; d++) log.add(d, 30);
  log.add(400, 5);
  EXPECT_EQ(log.sum(400, readstats::DAYS), 5u) << "a year later, only today counts";
  EXPECT_EQ(log.streak(400), 1);
}

TEST(DayLog, WritesOlderThanTheWindowAreDropped) {
  DayLog log;
  log.add(200, 30);
  log.add(100, 999);  // long since rotated out
  EXPECT_EQ(log.on(100), 0);
  EXPECT_EQ(log.sum(200, readstats::DAYS), 30u);
}

TEST(DayLog, MinutesSaturateRatherThanWrap) {
  DayLog log;
  log.add(10, 60000);
  log.add(10, 60000);
  EXPECT_EQ(log.on(10), UINT16_MAX) << "a wrapped total would read as almost no reading at all";
}

TEST(DayLog, SumsAWindowAndNoMore) {
  DayLog log;
  for (int32_t d = 0; d < 10; d++) log.add(100 + d, 10);
  EXPECT_EQ(log.sum(109, 7), 70u);
  EXPECT_EQ(log.sum(109, 100), 100u) << "clamped to what the ring holds";
}

TEST(DayLog, StreakCountsConsecutiveDays) {
  DayLog log;
  for (int32_t d = 100; d <= 104; d++) log.add(d, 20);
  EXPECT_EQ(log.streak(104), 5);
}

TEST(DayLog, StreakBreaksOnAMissedDay) {
  DayLog log;
  log.add(100, 20);
  log.add(101, 20);
  log.add(103, 20);  // 102 missed
  EXPECT_EQ(log.streak(103), 1);
}

// A streak that reads zero every morning is reporting the time of day.
TEST(DayLog, AMorningWithNoReadingYetDoesNotBreakTheStreak) {
  DayLog log;
  for (int32_t d = 100; d <= 103; d++) log.add(d, 20);
  EXPECT_EQ(log.streak(104), 4) << "yesterday still counts until today has had its chance";
  log.add(104, 5);
  EXPECT_EQ(log.streak(104), 5);
}

TEST(DayLog, TwoQuietDaysDoEndIt) {
  DayLog log;
  for (int32_t d = 100; d <= 103; d++) log.add(d, 20);
  EXPECT_EQ(log.streak(105), 0);
}

TEST(DayLog, StreakIsCappedByTheWindow) {
  DayLog log;
  for (int32_t d = 0; d < 200; d++) log.add(d, 10);
  EXPECT_EQ(log.streak(199), readstats::DAYS);
}

TEST(Rates, PagesPerHourRoundsAndRefusesTinySamples) {
  EXPECT_EQ(readstats::pagesPerHour(30, kHour), 30u);
  EXPECT_EQ(readstats::pagesPerHour(45, 30 * kMinute), 90u);
  EXPECT_EQ(readstats::pagesPerHour(0, kHour), 0u);
  EXPECT_EQ(readstats::pagesPerHour(2, 30 * 1000), 0u) << "half a minute says nothing about an hour";
}

TEST(Rates, PagesPerMinuteKeepsTheTenth) {
  EXPECT_EQ(readstats::pagesPerMinuteTenths(60, kHour), 10u) << "a page a minute";
  EXPECT_EQ(readstats::pagesPerMinuteTenths(84, kHour), 14u) << "1.4, and the .4 is the part that moves";
  EXPECT_EQ(readstats::pagesPerMinuteTenths(30, kHour), 5u) << "0.5";
  EXPECT_EQ(readstats::pagesPerMinuteTenths(0, kHour), 0u);
  EXPECT_EQ(readstats::pagesPerMinuteTenths(2, 30 * 1000), 0u) << "half a minute is not a rate";
}

TEST(Format, Tenths) {
  char buf[8];
  readstats::formatTenths(14, buf, sizeof(buf));
  EXPECT_STREQ(buf, "1.4");
  readstats::formatTenths(5, buf, sizeof(buf));
  EXPECT_STREQ(buf, "0.5");
  readstats::formatTenths(120, buf, sizeof(buf));
  EXPECT_STREQ(buf, "12.0");
  readstats::formatTenths(14, nullptr, 0);  // must not crash
}

TEST(Rates, SecondsPerPage) {
  EXPECT_EQ(readstats::secondsPerPage(60, kHour), 60u);
  EXPECT_EQ(readstats::secondsPerPage(0, kHour), 0u);
  EXPECT_EQ(readstats::secondsPerPage(3, 100 * 1000), 33u);
}

TEST(Rates, RereadShareIsAProportionOfAllTurns) {
  EXPECT_EQ(readstats::rereadPct(90, 10), 10u);
  EXPECT_EQ(readstats::rereadPct(0, 0), 0u);
  EXPECT_EQ(readstats::rereadPct(1, 1), 50u);
}

TEST(Format, ReadsAsSomethingAPersonWouldSay) {
  EXPECT_EQ(duration(0), "0s");
  EXPECT_EQ(duration(45 * 1000), "45s");
  EXPECT_EQ(duration(37 * kMinute), "37m");
  EXPECT_EQ(duration(kHour), "1h 00m");
  EXPECT_EQ(duration(4 * kHour + 12 * kMinute), "4h 12m");
  EXPECT_EQ(duration(100 * kHour), "100h 00m");
}

TEST(Format, SurvivesATinyBuffer) {
  char buf[4] = {'x', 'x', 'x', 'x'};
  readstats::formatDuration(4 * kHour, buf, sizeof(buf));
  EXPECT_EQ(buf[3], '\0');
  readstats::formatDuration(4 * kHour, nullptr, 0);  // must not crash
}
