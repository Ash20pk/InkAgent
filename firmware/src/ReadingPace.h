#pragma once

#include <cstdint>

// How fast this reader reads, compared with themselves.
//
// The agent uses "slower than usual" to decide which part of a passage to ask
// about. That only means anything against the reader's own pace: a page takes
// one person twenty seconds and another two minutes, and a fixed number turns
// the signal into a statement about reading speed rather than about this
// stretch of this book.
//
// Kept free of storage and Arduino so the arithmetic can be tested on a host.
namespace pace {

// Below this, there is no honest comparison to make and nothing is sent. Three
// sessions is little enough to be reached in an evening and enough that one
// unusual sitting does not define the reader.
inline constexpr uint16_t MIN_SESSIONS = 3;
// The baseline stops chasing the last session once it has this many, so a
// single distracted evening cannot move it far.
inline constexpr uint16_t WEIGHT_CAP = 10;

// Folds a session's mean page time into the running baseline. Returns the new
// baseline; `sessions` is how many went into the old one.
inline uint32_t fold(const uint32_t baselineMs, const uint32_t meanMs, const uint16_t sessions) {
  if (meanMs == 0) return baselineMs;
  if (baselineMs == 0 || sessions == 0) return meanMs;
  const uint32_t weight = sessions < WEIGHT_CAP ? sessions : WEIGHT_CAP;
  // Integer running average: baseline + (mean - baseline) / (weight + 1), done
  // without signed intermediates so a slow session cannot underflow.
  if (meanMs > baselineMs) return baselineMs + (meanMs - baselineMs) / (weight + 1);
  return baselineMs - (baselineMs - meanMs) / (weight + 1);
}

// This session's pace as a percentage of the reader's baseline, or -1 when
// there is not yet an honest comparison. Under 100 means slower than usual,
// because a slower page is a longer dwell.
inline int comparePct(const uint32_t meanMs, const uint32_t baselineMs, const uint16_t sessions) {
  if (meanMs == 0 || baselineMs == 0 || sessions < MIN_SESSIONS) return -1;
  const uint32_t pct = (baselineMs * 100) / meanMs;
  return static_cast<int>(pct > 400 ? 400 : (pct < 1 ? 1 : pct));
}

}  // namespace pace
