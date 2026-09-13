#pragma once

#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

#include "ReadingStats.h"

// One book's reading, accumulated across every session it has had.
struct BookStats {
  std::string path;   // the key; the file on the SD card
  std::string title;  // kept so a book removed from the card still has a name
  std::string author;
  uint32_t readingMs = 0;    // dwell on pages actually read, idle excluded
  uint32_t pages = 0;        // forward turns
  uint32_t regressions = 0;  // turns back
  uint16_t sessions = 0;
  int32_t firstDay = -1;  // day numbers from HalClock::dayNumber()
  int32_t lastDay = -1;

  bool operator==(const BookStats& other) const { return path == other.path; }
};

// Where the reading a session measured actually goes.
//
// Every number here was already being computed by the reader and discarded
// after it had nudged the pace baseline. Nothing new is timed, nothing new is
// watched, and none of it leaves the device — the relay is never told about
// any of this, and the privacy screen says so.
class ReadingStatsStore : public PersistableStore<ReadingStatsStore> {
 private:
  std::vector<BookStats> books;
  readstats::DayLog dayLog;
  uint32_t lifetimeMs = 0;
  uint32_t lifetimePages = 0;
  uint32_t lifetimeSessions = 0;
  bool loaded = false;

  ReadingStatsStore() = default;
  ~ReadingStatsStore() = default;
  friend class PersistableStore<ReadingStatsStore>;

 public:
  // Bounded like every other list on a 380 KB device. Forty books is more than
  // this card holds in practice, and the least recently read is dropped first.
  static constexpr size_t MAX_BOOKS = 40;

  static const char* getFilePath() { return "/.inkagent/stats.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  void ensureLoaded() {
    if (!loaded) {
      loadFromFile();
      loaded = true;
    }
  }

  // Folds reading in and persists. `readingMs` is dwell on pages read, which
  // the reader already caps per page, so an open book left on a table does not
  // become an evening of reading.
  //
  // A sitting is written in pieces as it happens rather than once when the book
  // is closed, because the close is the least reliable moment there is: a flat
  // battery, a crash or a firmware flash all end a session without one, and
  // everything measured up to that point was lost with it. `newSitting` is true
  // for the first piece only; the rest are the same sitting continuing and must
  // not each count as another.
  void recordSession(const std::string& path, const std::string& title, const std::string& author, uint32_t readingMs,
                     uint32_t forwardTurns, uint32_t regressions, int32_t day, bool newSitting = true);

  const std::vector<BookStats>& getBooks() const { return books; }
  const readstats::DayLog& days() const { return dayLog; }
  uint32_t totalMs() const { return lifetimeMs; }
  uint32_t totalPages() const { return lifetimePages; }
  uint32_t totalSessions() const { return lifetimeSessions; }

  // Null when the book has no recorded reading yet.
  const BookStats* forPath(const std::string& path) const;

  // Books most recently read first — the order the stats screen lists them in.
  std::vector<const BookStats*> byRecency() const;

  // Forgets everything. Offered because a reader who does not want their
  // reading counted should be able to stop it having been counted.
  void clear();
};

#define READING_STATS ReadingStatsStore::getInstance()
