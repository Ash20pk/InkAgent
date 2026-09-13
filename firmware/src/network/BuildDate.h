#pragma once

#include <cstdint>

// The day this firmware was compiled, as a day number since 2000-01-01.
//
// Used as a sanity floor for the real-time clock: a reader cannot legitimately
// believe the date is earlier than the software it is running. Deriving it from
// __DATE__ rather than hard-coding a year means the check stays meaningful
// without anyone remembering to move it.
namespace inkagent {
namespace detail {

// __DATE__ is "Mmm dd yyyy", with a space-padded day.
constexpr int buildYear() {
  return (__DATE__[7] - '0') * 1000 + (__DATE__[8] - '0') * 100 + (__DATE__[9] - '0') * 10 + (__DATE__[10] - '0');
}

constexpr int buildMonth() {
  return __DATE__[0] == 'J'   ? (__DATE__[1] == 'a' ? 1 : (__DATE__[2] == 'n' ? 6 : 7))
         : __DATE__[0] == 'F' ? 2
         : __DATE__[0] == 'M' ? (__DATE__[2] == 'r' ? 3 : 5)
         : __DATE__[0] == 'A' ? (__DATE__[1] == 'p' ? 4 : 8)
         : __DATE__[0] == 'S' ? 9
         : __DATE__[0] == 'O' ? 10
         : __DATE__[0] == 'N' ? 11
                              : 12;
}

constexpr int buildDay() {
  return (__DATE__[4] == ' ' ? 0 : (__DATE__[4] - '0') * 10) + (__DATE__[5] - '0');
}

// days_from_civil, shifted to a 2000-01-01 epoch — the same arithmetic
// HalClock::dayNumber() uses, so the two are directly comparable.
constexpr int32_t civilToDays2000(int32_t y, uint32_t m, uint32_t d) {
  y -= m <= 2;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = static_cast<uint32_t>(y - era * 400);
  const uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int32_t>(doe) - 719468 - 10957;
}

}  // namespace detail

inline constexpr int32_t kBuildDayNumber =
    detail::civilToDays2000(detail::buildYear(), detail::buildMonth(), detail::buildDay());

// 2020-09-04, when the relay's trust anchor became valid. Below this nothing
// can verify at all, whatever the build date says.
static_assert(kBuildDayNumber > 7551, "build date must be after the trust anchor's notBefore");

}  // namespace inkagent
