#pragma once

#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

// Words looked up while reading, and when to show them again.
//
// The evidence this exists for: words met once in context are mostly forgotten,
// and retrieval spaced over expanding intervals is what makes them stick —
// roughly five encounters, stretching towards weekly, beats any amount of
// re-reading. So this is a scheduler, not a list. A word that is never
// retrieved is a word the reader will not keep.
//
// Nothing here notifies. The queue is finite and empties; it is somewhere you
// go, never something that interrupts you.
struct WordEntry {
  std::string word;  // headword, as the dictionary resolved it
  std::string book;  // where it was met, for context on review
  // Day numbers from HalClock::dayNumber(); -1 when the device had no clock at
  // capture time, which makes the word due immediately and forever after.
  int32_t added = -1;
  int32_t due = -1;
  // 0..4 while learning, kRetired once it has survived the last interval.
  uint8_t box = 0;

  bool operator==(const WordEntry& other) const { return word == other.word; }
};

class WordListStore : public PersistableStore<WordListStore> {
 private:
  std::vector<WordEntry> words;
  bool loaded = false;

  WordListStore() = default;
  ~WordListStore() = default;
  friend class PersistableStore<WordListStore>;

 public:
  // Expanding lags, in days. Five successful retrievals carry a word out to two
  // months, which is where the vocabulary studies put durable retention.
  static constexpr int32_t BOX_DAYS[5] = {1, 3, 7, 21, 60};
  static constexpr uint8_t BOX_COUNT = 5;
  static constexpr uint8_t kRetired = BOX_COUNT;
  // Bounded on purpose: this whole vector is resident while the app is open,
  // and an unbounded list would quietly become the largest allocation on a
  // 380 KB device.
  static constexpr size_t MAX_WORDS = 120;

  static const char* getFilePath() { return "/.inkagent/wordlist.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Read from SD once per boot. Callers that only glance (the sleep canvas)
  // use this so a cold read costs one file open, not one per query.
  void ensureLoaded() {
    if (!loaded) {
      loadFromFile();
      loaded = true;
    }
  }

  const std::vector<WordEntry>& getWords() const { return words; }
  int count() const { return static_cast<int>(words.size()); }

  // Records a lookup. Re-looking up a word already held is itself a signal that
  // it has not stuck, so it resets to the first box rather than being ignored.
  // Oldest retired entry is dropped when full, then the oldest entry.
  void add(const std::string& word, const std::string& book);

  bool removeWord(const std::string& word);

  // Words due on or before `today`, oldest due first. A device with no clock
  // reports everything as due rather than nothing.
  std::vector<const WordEntry*> due(int32_t today) const;
  int dueCount(int32_t today) const;

  // Grades a retrieval: remembered moves the word to the next box, forgotten
  // sends it back to the first. Persists.
  void grade(const std::string& word, bool remembered, int32_t today);
};

#define WORD_LIST WordListStore::getInstance()
