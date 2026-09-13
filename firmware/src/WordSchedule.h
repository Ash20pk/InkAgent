#pragma once

#include <cstdint>

// The spaced-retrieval rule, on its own.
//
// Deliberately free of storage, of the clock and of Arduino: it is the part of
// the word list that is worth reasoning about, and keeping it here means it can
// be tested on a host without pulling in a filesystem or a JSON parser.
//
// Day numbers come from HalClock::dayNumber(); a negative one means the device
// has no working clock.
namespace wordsched {

// Expanding lags, in days. Five successful retrievals carry a word out to two
// months, which is where the vocabulary studies put durable retention.
inline constexpr int32_t BOX_DAYS[] = {1, 3, 7, 21, 60};
inline constexpr uint8_t BOX_COUNT = 5;
// A word that has survived the last interval. Kept rather than deleted: seeing
// the pile of learned words grow is the only reward this app offers.
inline constexpr uint8_t RETIRED = BOX_COUNT;

struct Next {
  uint8_t box;
  int32_t due;
};

inline Next next(const uint8_t box, const bool remembered, const int32_t today) {
  Next out{};
  if (!remembered) {
    // Forgetting sends a word back to the start whatever it had earned. The
    // point of the schedule is retrieval, and a failed retrieval is evidence.
    out.box = 0;
  } else {
    out.box = box >= RETIRED ? RETIRED : static_cast<uint8_t>(box + 1);
  }

  if (today < 0) {
    // No clock: the word stays due, but its position still advances, so a
    // device that later gets the time back has not lost the progress.
    out.due = -1;
  } else {
    out.due = today + BOX_DAYS[out.box >= RETIRED ? BOX_COUNT - 1 : out.box];
  }
  return out;
}

// Whether a word is due. A word captured with no clock (due < 0) is always due:
// better to over-offer a review than to lose the word because the RTC was flat.
inline bool isDue(const uint8_t box, const int32_t due, const int32_t today) {
  if (box >= RETIRED) return false;
  return due < 0 || today < 0 || due <= today;
}

}  // namespace wordsched
