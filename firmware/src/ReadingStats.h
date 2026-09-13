#pragma once

#include <cstdint>
#include <cstdio>

// What the reader has actually read, and for how long.
//
// The device already measured this and threw it away: the reader folds each
// session's mean page time into one baseline (ReadingPace.h) and discards the
// totals. Everything here is that same measurement kept.
//
// One deliberate omission: there is no goal, no target, no daily quota and
// nothing that can be failed. A device whose whole argument is that reading
// should not be gamified cannot then award points for it. These are numbers to
// look at when you go looking, and the reader is never told about them.
//
// Free of storage, Arduino and the clock, so the arithmetic is tested on a
// host. ReadingStatsStore holds it; this decides what it means.
namespace readstats {

// Four weeks of daily totals. Long enough to show a habit and a month's
// reading, short enough that the whole log is 56 bytes and stays resident.
inline constexpr int32_t DAYS = 28;

// A page left open on a table is not reading. The reader already discards any
// single dwell over five minutes; this is the same judgement at session scale.
inline constexpr uint32_t MAX_SESSION_MS = 6UL * 60 * 60 * 1000;

// Minutes read per day, as a ring indexed by day number, plus the most recent
// day written. A plain array would need shifting on every new day; the ring
// only has to clear the days that were skipped.
struct DayLog {
  int32_t lastDay = -1;
  uint16_t minutes[DAYS] = {};

  // Minutes recorded on `day`, or 0 if that day is outside the window.
  uint16_t on(const int32_t day) const {
    if (day < 0 || lastDay < 0 || day > lastDay || day <= lastDay - DAYS) return 0;
    return minutes[day % DAYS];
  }

  // Adds to `day`. A day newer than lastDay first clears every slot that was
  // skipped, so a fortnight away does not leave a fortnight of stale minutes
  // waiting to be counted again.
  void add(const int32_t day, const uint32_t mins) {
    if (day < 0 || mins == 0) return;
    if (lastDay < 0) {
      lastDay = day;
    } else if (day > lastDay) {
      const int32_t skipped = day - lastDay;
      for (int32_t d = 1; d <= skipped && d <= DAYS; d++) minutes[(lastDay + d) % DAYS] = 0;
      lastDay = day;
    } else if (day <= lastDay - DAYS) {
      return;  // older than the window keeps
    }
    uint16_t& slot = minutes[day % DAYS];
    const uint32_t sum = static_cast<uint32_t>(slot) + mins;
    slot = sum > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(sum);
  }

  // Minutes over the last `days` days ending at `today`, inclusive.
  uint32_t sum(const int32_t today, const int32_t days) const {
    uint32_t total = 0;
    for (int32_t d = 0; d < days && d < DAYS; d++) total += on(today - d);
    return total;
  }

  // Consecutive days read, counting back from today. A day that has not
  // happened yet does not break it: at nine in the morning nothing has been
  // read, and a streak that reads zero every morning is measuring the clock
  // rather than the habit — so an empty today falls back to yesterday.
  int32_t streak(const int32_t today) const {
    if (today < 0 || lastDay < 0) return 0;
    int32_t day = on(today) > 0 ? today : today - 1;
    int32_t run = 0;
    while (run < DAYS && on(day) > 0) {
      run++;
      day--;
    }
    return run;
  }
};

// Pages an hour, rounded. Zero when there is not enough to divide.
inline uint32_t pagesPerHour(const uint32_t pages, const uint32_t ms) {
  if (pages == 0 || ms < 60000) return 0;
  return static_cast<uint32_t>((static_cast<uint64_t>(pages) * 3600000 + ms / 2) / ms);
}

// Mean seconds on a page. Zero when nothing has been turned.
inline uint32_t secondsPerPage(const uint32_t pages, const uint32_t ms) {
  if (pages == 0) return 0;
  return (ms / pages + 500) / 1000;
}

// How much of the session was spent going back. Re-reading is not a fault —
// it is often the most careful reading there is — so this is reported and
// never scored.
inline uint32_t rereadPct(const uint32_t forwardTurns, const uint32_t regressions) {
  const uint32_t total = forwardTurns + regressions;
  if (total == 0) return 0;
  return (regressions * 100 + total / 2) / total;
}

// "4h 12m", "37m", "45s". Never "0h 37m", and never a bare zero for a session
// that did happen — under a minute reads in seconds.
inline void formatDuration(const uint32_t ms, char* out, const size_t n) {
  if (out == nullptr || n == 0) return;
  const uint32_t totalSeconds = ms / 1000;
  const uint32_t hours = totalSeconds / 3600;
  const uint32_t minutes = (totalSeconds % 3600) / 60;
  if (hours > 0) {
    snprintf(out, n, "%uh %02um", static_cast<unsigned>(hours), static_cast<unsigned>(minutes));
  } else if (minutes > 0) {
    snprintf(out, n, "%um", static_cast<unsigned>(minutes));
  } else {
    snprintf(out, n, "%us", static_cast<unsigned>(totalSeconds));
  }
}

}  // namespace readstats
