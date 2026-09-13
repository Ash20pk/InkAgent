#include "ReadingStatsStore.h"

#include <Logging.h>

#include <algorithm>

void ReadingStatsStore::toJson(JsonDocument& doc) const {
  doc["ms"] = lifetimeMs;
  doc["pages"] = lifetimePages;
  doc["sessions"] = lifetimeSessions;
  doc["lastDay"] = dayLog.lastDay;
  JsonArray mins = doc["mins"].to<JsonArray>();
  for (const uint16_t m : dayLog.minutes) mins.add(m);

  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& b : books) {
    JsonObject obj = arr.add<JsonObject>();
    obj["path"] = b.path;
    obj["title"] = b.title;
    obj["author"] = b.author;
    obj["ms"] = b.readingMs;
    obj["pages"] = b.pages;
    obj["back"] = b.regressions;
    obj["sessions"] = b.sessions;
    obj["first"] = b.firstDay;
    obj["last"] = b.lastDay;
  }
}

bool ReadingStatsStore::fromJson(JsonVariantConst doc) {
  books.clear();
  dayLog = readstats::DayLog{};

  lifetimeMs = doc["ms"] | 0u;
  lifetimePages = doc["pages"] | 0u;
  lifetimeSessions = doc["sessions"] | 0u;
  dayLog.lastDay = doc["lastDay"] | -1;
  JsonArrayConst mins = doc["mins"].as<JsonArrayConst>();
  int32_t i = 0;
  for (JsonVariantConst v : mins) {
    if (i >= readstats::DAYS) break;
    dayLog.minutes[i++] = v.as<uint16_t>();
  }

  JsonArrayConst arr = doc["books"].as<JsonArrayConst>();
  books.reserve(std::min(arr.size(), MAX_BOOKS));
  for (JsonVariantConst v : arr) {
    if (books.size() >= MAX_BOOKS) break;
    BookStats b;
    b.path = v["path"] | "";
    if (b.path.empty()) continue;
    b.title = v["title"] | "";
    b.author = v["author"] | "";
    b.readingMs = v["ms"] | 0u;
    b.pages = v["pages"] | 0u;
    b.regressions = v["back"] | 0u;
    b.sessions = v["sessions"] | 0u;
    b.firstDay = v["first"] | -1;
    b.lastDay = v["last"] | -1;
    books.push_back(std::move(b));
  }
  return true;
}

void ReadingStatsStore::recordSession(const std::string& path, const std::string& title, const std::string& author,
                                      const uint32_t readingMs, const uint32_t forwardTurns, const uint32_t regressions,
                                      const int32_t day) {
  ensureLoaded();
  if (path.empty() || forwardTurns == 0 || readingMs == 0) return;
  // A session longer than the cap is the clock having moved, not an afternoon
  // of reading; count the pages and leave the time out rather than let one
  // bad reading distort every total that follows.
  const uint32_t ms = readingMs > readstats::MAX_SESSION_MS ? 0 : readingMs;

  auto it = std::find_if(books.begin(), books.end(), [&](const BookStats& b) { return b.path == path; });
  if (it == books.end()) {
    if (books.size() >= MAX_BOOKS) {
      // Drop the book read longest ago. An entry with no day (recorded while
      // the device had no clock) sorts oldest and goes first.
      auto oldest = std::min_element(books.begin(), books.end(),
                                     [](const BookStats& a, const BookStats& b) { return a.lastDay < b.lastDay; });
      books.erase(oldest);
    }
    BookStats fresh;
    fresh.path = path;
    fresh.firstDay = day;
    books.push_back(std::move(fresh));
    it = books.end() - 1;
  }

  // Title and author are refreshed every session: an entry created before the
  // metadata was parsed would otherwise stay nameless forever.
  if (!title.empty()) it->title = title;
  if (!author.empty()) it->author = author;
  it->readingMs += ms;
  it->pages += forwardTurns;
  it->regressions += regressions;
  if (it->sessions < UINT16_MAX) it->sessions++;
  if (it->firstDay < 0) it->firstDay = day;
  if (day >= 0) it->lastDay = day;

  lifetimeMs += ms;
  lifetimePages += forwardTurns;
  lifetimeSessions++;
  // Rounded, but never down to nothing: a ten-minute sitting that landed as
  // zero minutes would silently break a streak the reader did keep.
  if (ms > 0) {
    const uint32_t mins = (ms + 30000) / 60000;
    dayLog.add(day, mins > 0 ? mins : 1);
  }

  if (!saveToFile()) LOG_ERR("ReadingStats", "could not save");
}

const BookStats* ReadingStatsStore::forPath(const std::string& path) const {
  const auto it = std::find_if(books.begin(), books.end(), [&](const BookStats& b) { return b.path == path; });
  return it == books.end() ? nullptr : &(*it);
}

std::vector<const BookStats*> ReadingStatsStore::byRecency() const {
  std::vector<const BookStats*> out;
  out.reserve(books.size());
  for (const auto& b : books) out.push_back(&b);
  std::sort(out.begin(), out.end(), [](const BookStats* a, const BookStats* b) {
    if (a->lastDay != b->lastDay) return a->lastDay > b->lastDay;
    return a->readingMs > b->readingMs;
  });
  return out;
}

void ReadingStatsStore::clear() {
  ensureLoaded();
  books.clear();
  dayLog = readstats::DayLog{};
  lifetimeMs = lifetimePages = lifetimeSessions = 0;
  if (!saveToFile()) LOG_ERR("ReadingStats", "could not clear");
}
