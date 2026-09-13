// The spaced-retrieval rule behind the word list.
//
// This is the part worth testing without hardware: whether a word that is
// remembered moves out along the expanding lags, whether forgetting really
// sends it back, and whether a device with a flat clock loses anything.
#include <gtest/gtest.h>

#include "WordSchedule.h"

using namespace wordsched;

TEST(WordSchedule, FirstSuccessSchedulesTomorrow) {
  const auto n = next(/*box=*/0, /*remembered=*/true, /*today=*/100);
  EXPECT_EQ(n.box, 1);
  EXPECT_EQ(n.due, 100 + 3) << "box 1 is the three-day lag";
}

TEST(WordSchedule, LagsExpandAllTheWayOut) {
  // Five successful retrievals should carry a word roughly two months out,
  // which is where the vocabulary studies put durable retention.
  int32_t day = 0;
  uint8_t box = 0;
  std::vector<int32_t> gaps;
  for (int i = 0; i < BOX_COUNT; i++) {
    const auto n = next(box, true, day);
    gaps.push_back(n.due - day);
    box = n.box;
    day = n.due;
  }
  EXPECT_EQ(gaps, (std::vector<int32_t>{3, 7, 21, 60, 60}));
  EXPECT_EQ(box, RETIRED);
  EXPECT_GT(day, 60) << "a learned word is two months out, not two days";
}

TEST(WordSchedule, ForgettingSendsItBackToTheStart) {
  const auto n = next(/*box=*/3, /*remembered=*/false, /*today=*/500);
  EXPECT_EQ(n.box, 0);
  EXPECT_EQ(n.due, 500 + BOX_DAYS[0]) << "back to tomorrow, not to where it was";
}

TEST(WordSchedule, ForgettingARetiredWordRevivesIt) {
  const auto n = next(RETIRED, false, 500);
  EXPECT_EQ(n.box, 0);
  EXPECT_TRUE(isDue(n.box, n.due, 501)) << "it is in the queue again";
}

TEST(WordSchedule, RetiredStaysRetiredAndNeverComesBackDue) {
  const auto n = next(RETIRED, true, 1000);
  EXPECT_EQ(n.box, RETIRED);
  EXPECT_FALSE(isDue(n.box, n.due, 1'000'000)) << "learned words leave the queue for good";
}

TEST(WordSchedule, NoClockKeepsTheWordDueButStillAdvancesIt) {
  // A flat RTC must not cost the reader their progress: the box moves on even
  // though there is no date to schedule against.
  const auto n = next(/*box=*/2, /*remembered=*/true, /*today=*/-1);
  EXPECT_EQ(n.box, 3);
  EXPECT_EQ(n.due, -1);
  EXPECT_TRUE(isDue(n.box, n.due, -1));
  EXPECT_TRUE(isDue(n.box, n.due, 900)) << "still due once the clock comes back";
}

TEST(WordSchedule, AWordIsNotDueBeforeItsDay) {
  EXPECT_FALSE(isDue(/*box=*/1, /*due=*/110, /*today=*/109));
  EXPECT_TRUE(isDue(1, 110, 110)) << "due on the day, not the day after";
  EXPECT_TRUE(isDue(1, 110, 111));
}

TEST(WordSchedule, ClockLostAfterCaptureStillOffersTheWord) {
  // due is set, but the device can no longer tell what day it is.
  EXPECT_TRUE(isDue(/*box=*/1, /*due=*/110, /*today=*/-1));
}
