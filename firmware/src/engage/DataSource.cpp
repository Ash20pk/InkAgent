#include "DataSource.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <I18n.h>

#include <cstdio>
#include <cstring>

#include "InkAgentSettings.h"
#include "InkAgentState.h"
#include "RecentBooksStore.h"
#include "WordListStore.h"

namespace engage {
namespace {

// Dev builds carry a "-dev-<branch>-<sha>" suffix; screens show the release
// version alone.
void deviceVersion(char* out, size_t cap) {
  snprintf(out, cap, "%s", INKAGENT_VERSION);
  if (char* dev = strstr(out, "-dev")) *dev = '\0';
}

// The book the device is "in": the one the reader has open, else the top of
// the recents list. Null when neither exists, which every reading.* source
// treats as an empty value rather than an error.
const RecentBook* currentBook() {
  const auto& books = RECENT_BOOKS.getBooks();
  if (books.empty()) return nullptr;
  const std::string& open = APP_STATE.openEpubPath;
  if (!open.empty()) {
    for (const auto& book : books) {
      if (book.path == open) return &book;
    }
  }
  return &books.front();
}

}  // namespace

bool resolveSource(const char* name, const GfxRenderer& renderer, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return false;
  out[0] = '\0';
  if (name == nullptr) return false;

  if (strcmp(name, "device.version") == 0) {
    deviceVersion(out, cap);
    return true;
  }
  if (strcmp(name, "device.model") == 0) {
    snprintf(out, cap, "%s", gpio.deviceIsX3() ? "Xteink X3" : "Xteink X4");
    return true;
  }
  if (strcmp(name, "device.screen") == 0) {
    snprintf(out, cap, "%d x %d", renderer.getScreenWidth(), renderer.getScreenHeight());
    return true;
  }
  if (strcmp(name, "device.battery") == 0) {
    snprintf(out, cap, "%d%%", powerManager.getBatteryPercentage());
    return true;
  }
  if (strcmp(name, "device.freeHeap") == 0) {
    snprintf(out, cap, "%u KB", static_cast<unsigned>(ESP.getFreeHeap() / 1024));
    return true;
  }
  if (strcmp(name, "reading.title") == 0) {
    const RecentBook* book = currentBook();
    if (book != nullptr) snprintf(out, cap, "%s", book->title.c_str());
    return true;
  }
  if (strcmp(name, "reading.author") == 0) {
    const RecentBook* book = currentBook();
    if (book != nullptr) snprintf(out, cap, "%s", book->author.c_str());
    return true;
  }
  if (strcmp(name, "reading.percent") == 0) {
    const RecentBook* book = currentBook();
    // A book opened before progress mirroring existed has no percent yet;
    // showing nothing beats showing a confident zero.
    if (book != nullptr && book->percent >= 0) snprintf(out, cap, "%d%%", book->percent);
    return true;
  }
  if (strcmp(name, "review.word") == 0) {
    // One word, not a count: a number is a badge to feel behind on, a word is
    // a retrieval you either do or ignore at no cost. Empty when nothing is
    // due, and the renderer collapses the row.
    WORD_LIST.ensureLoaded();
    const auto due = WORD_LIST.due(halClock.dayNumber());
    if (!due.empty()) snprintf(out, cap, "%s", due.front()->word.c_str());
    return true;
  }
  if (strcmp(name, "device.clock") == 0) {
    const auto sb = SETTINGS.statusBarSpec();
    if (!halClock.isAvailable() || !halClock.formatTime(out, cap, sb.clockUtcOffsetQ, sb.clock12h)) out[0] = '\0';
    return true;
  }
  if (strcmp(name, "i18n.about") == 0) {
    snprintf(out, cap, "%s", tr(STR_ABOUT));
    return true;
  }
  return false;
}

}  // namespace engage
