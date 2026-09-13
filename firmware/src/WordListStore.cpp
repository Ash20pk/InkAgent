#include "WordListStore.h"

#include <HalClock.h>
#include <Logging.h>

#include <algorithm>

void WordListStore::toJson(JsonDocument& doc) const {
  JsonArray arr = doc["words"].to<JsonArray>();
  for (const auto& w : words) {
    JsonObject obj = arr.add<JsonObject>();
    obj["word"] = w.word;
    obj["book"] = w.book;
    obj["added"] = w.added;
    obj["due"] = w.due;
    obj["box"] = w.box;
  }
}

bool WordListStore::fromJson(JsonVariantConst doc) {
  words.clear();
  JsonArrayConst arr = doc["words"].as<JsonArrayConst>();
  words.reserve(std::min(arr.size(), MAX_WORDS));
  for (JsonVariantConst v : arr) {
    if (words.size() >= MAX_WORDS) break;
    WordEntry w;
    w.word = v["word"] | "";
    if (w.word.empty()) continue;
    w.book = v["book"] | "";
    w.added = v["added"] | -1;
    w.due = v["due"] | -1;
    w.box = v["box"] | 0;
    if (w.box > kRetired) w.box = 0;
    words.push_back(std::move(w));
  }
  return true;
}

void WordListStore::add(const std::string& word, const std::string& book) {
  ensureLoaded();
  if (word.empty()) return;
  const int32_t today = halClock.dayNumber();

  auto it = std::find_if(words.begin(), words.end(), [&](const WordEntry& w) { return w.word == word; });
  if (it != words.end()) {
    // Looking a word up again means it did not stick. Start it over.
    it->box = 0;
    it->due = today < 0 ? -1 : today + wordsched::BOX_DAYS[0];
    if (!book.empty()) it->book = book.substr(0, 60);
    saveToFile();
    return;
  }

  if (words.size() >= MAX_WORDS) {
    // Prefer dropping something already learned; only then the oldest entry.
    auto victim = std::find_if(words.begin(), words.end(), [](const WordEntry& w) { return w.box >= kRetired; });
    words.erase(victim != words.end() ? victim : words.begin());
  }

  WordEntry w;
  w.word = word.substr(0, 40);
  w.book = book.substr(0, 60);
  w.added = today;
  w.due = today < 0 ? -1 : today + wordsched::BOX_DAYS[0];
  w.box = 0;
  words.push_back(std::move(w));
  saveToFile();
}

bool WordListStore::removeWord(const std::string& word) {
  ensureLoaded();
  auto it = std::find_if(words.begin(), words.end(), [&](const WordEntry& w) { return w.word == word; });
  if (it == words.end()) return false;
  words.erase(it);
  saveToFile();
  return true;
}

std::vector<const WordEntry*> WordListStore::due(const int32_t today) const {
  std::vector<const WordEntry*> out;
  for (const auto& w : words) {
    if (wordsched::isDue(w.box, w.due, today)) out.push_back(&w);
  }
  std::sort(out.begin(), out.end(), [](const WordEntry* a, const WordEntry* b) { return a->due < b->due; });
  return out;
}

int WordListStore::dueCount(const int32_t today) const { return static_cast<int>(due(today).size()); }

void WordListStore::grade(const std::string& word, const bool remembered, const int32_t today) {
  ensureLoaded();
  auto it = std::find_if(words.begin(), words.end(), [&](const WordEntry& w) { return w.word == word; });
  if (it == words.end()) return;
  const wordsched::Next next = wordsched::next(it->box, remembered, today);
  it->box = next.box;
  it->due = next.due;
  saveToFile();
}
