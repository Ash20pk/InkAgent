#include "ReadingStatsActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <I18n.h>

#include <algorithm>

#include "InkAgentSettings.h"
#include "ReadingStats.h"
#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

std::string duration(const uint32_t ms) {
  char buf[16];
  readstats::formatDuration(ms, buf, sizeof(buf));
  return buf;
}

std::string number(const uint32_t n) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(n));
  return buf;
}

}  // namespace

ReadingStatsActivity::ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                                           std::string bookTitle)
    : Activity("ReadingStats", renderer, mappedInput), bookPath(std::move(bookPath)), bookTitle(std::move(bookTitle)) {}

void ReadingStatsActivity::onEnter() {
  Activity::onEnter();
  READING_STATS.ensureLoaded();
  build();
  requestUpdate();
}

std::string ReadingStatsActivity::describeDay(const int32_t day, const int32_t today) const {
  if (day < 0 || today < 0 || day > today) return "";
  const int32_t ago = today - day;
  if (ago == 0) return tr(STR_STATS_TODAY);
  if (ago == 1) return tr(STR_STATS_YESTERDAY);
  return number(static_cast<uint32_t>(ago)) + " " + tr(STR_STATS_DAYS_AGO);
}

void ReadingStatsActivity::build() {
  rows.clear();
  selected = -1;
  top = 0;

  if (!bookPath.empty()) {
    const BookStats* stats = READING_STATS.forPath(bookPath);
    if (stats != nullptr) buildForBook(*stats);
    return;
  }
  buildForLibrary();

  for (size_t i = 0; i < rows.size(); i++) {
    if (rows[i].selectable()) {
      selected = static_cast<int>(i);
      break;
    }
  }
}

void ReadingStatsActivity::buildForBook(const BookStats& stats) {
  const int32_t today = halClock.dayNumber(SETTINGS.statusBarSpec().clockUtcOffsetQ);

  rows.push_back({tr(STR_STATS_TOTAL), duration(stats.readingMs), "", false});
  rows.push_back({tr(STR_STATS_PAGES), number(stats.pages), "", false});

  const uint32_t perHour = readstats::pagesPerHour(stats.pages, stats.readingMs);
  rows.push_back({tr(STR_STATS_PACE),
                  perHour == 0 ? tr(STR_STATS_TOO_SOON) : number(perHour) + " " + tr(STR_STATS_PER_HOUR), "", false});

  rows.push_back({tr(STR_STATS_SITTINGS), number(stats.sessions), "", false});
  // Reported, never scored: going back over a paragraph is often the most
  // careful reading there is.
  rows.push_back({tr(STR_STATS_REREAD), number(readstats::rereadPct(stats.pages, stats.regressions)) + "%", "", false});

  const std::string started = describeDay(stats.firstDay, today);
  if (!started.empty()) rows.push_back({tr(STR_STATS_STARTED), started, "", false});
  const std::string last = describeDay(stats.lastDay, today);
  if (!last.empty()) rows.push_back({tr(STR_STATS_LAST_READ), last, "", false});
}

void ReadingStatsActivity::buildForLibrary() {
  const int32_t today = halClock.dayNumber(SETTINGS.statusBarSpec().clockUtcOffsetQ);
  const auto& log = READING_STATS.days();

  rows.push_back({tr(STR_STATS_TOTAL), duration(READING_STATS.totalMs()), "", false});
  rows.push_back({tr(STR_STATS_WEEK), duration(log.sum(today, 7) * 60u * 1000u), "", false});

  const int32_t streak = log.streak(today);
  if (streak > 0) rows.push_back({tr(STR_STATS_STREAK), number(static_cast<uint32_t>(streak)), "", false});

  rows.push_back({tr(STR_STATS_PAGES), number(READING_STATS.totalPages()), "", false});

  const uint32_t perHour = readstats::pagesPerHour(READING_STATS.totalPages(), READING_STATS.totalMs());
  rows.push_back({tr(STR_STATS_PACE),
                  perHour == 0 ? tr(STR_STATS_TOO_SOON) : number(perHour) + " " + tr(STR_STATS_PER_HOUR), "", false});

  rows.push_back({tr(STR_STATS_SITTINGS), number(READING_STATS.totalSessions()), "", false});

  // Then the books themselves, most recently read first, each leading to its
  // own figures.
  const auto books = READING_STATS.byRecency();
  if (!books.empty()) {
    rows.push_back({tr(STR_STATS_BOOKS), "", "", false});
    for (const BookStats* b : books) {
      const std::string name = b->title.empty() ? b->path : b->title;
      rows.push_back({name, duration(b->readingMs), b->path, false});
    }
  }

  if (READING_STATS.totalSessions() > 0) {
    rows.push_back({tr(STR_STATS_FORGET), "", "", true});
  }
}

int ReadingStatsActivity::visibleRows() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int rowHeight = std::max(1, metrics.listRowHeight);
  const int startY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  return std::max(1, (renderer.getScreenHeight() - startY - metrics.buttonHintsHeight) / rowHeight);
}

// Steps to the next selectable row in `delta`'s direction, skipping the
// figures. Stops at the ends rather than wrapping: a list that is mostly
// unselectable is confusing to wrap around.
void ReadingStatsActivity::moveSelection(const int delta) {
  if (selected < 0 || rows.empty()) return;
  const int count = static_cast<int>(rows.size());
  for (int i = selected + delta; i >= 0 && i < count; i += delta) {
    if (!rows[i].selectable()) continue;
    selected = i;
    const int visible = visibleRows();
    if (selected < top) top = selected;
    if (selected >= top + visible) top = selected - visible + 1;
    requestUpdate();
    return;
  }
}

void ReadingStatsActivity::activateSelected() {
  if (selected < 0 || selected >= static_cast<int>(rows.size())) return;
  const Row& row = rows[selected];

  if (row.forget) {
    // Two presses, because this cannot be undone and the row sits at the
    // bottom of a list people scroll to the end of.
    if (!confirmingForget) {
      confirmingForget = true;
      requestUpdate();
      return;
    }
    READING_STATS.clear();
    confirmingForget = false;
    build();
    requestUpdate();
    return;
  }

  if (!row.path.empty()) {
    // Stacked, not replaced, so Back from a book returns to the library view
    // it was opened from.
    startActivityForResult(std::make_unique<ReadingStatsActivity>(renderer, mappedInput, row.path, row.label),
                           [this](const ActivityResult&) { requestUpdate(); });
  }
}

void ReadingStatsActivity::loop() {
  Activity::loop();

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (confirmingForget) {
      confirmingForget = false;
      requestUpdate();
      return;
    }
    finish();
    return;
  }
  if (selected < 0) return;

  buttonNavigator.onNext([this] {
    confirmingForget = false;
    moveSelection(1);
  });
  buttonNavigator.onPrevious([this] {
    confirmingForget = false;
    moveSelection(-1);
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) activateSelected();
}

void ReadingStatsActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int sidePadding = metrics.contentSidePadding;

  renderer.clearScreen();
  const char* heading = bookPath.empty()    ? tr(STR_READING_STATS)
                        : bookTitle.empty() ? tr(STR_STATS_BOOK)
                                            : bookTitle.c_str();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, heading);

  if (rows.empty()) {
    const int y = renderer.getScreenHeight() / 2 - 20;
    UITheme::drawCenteredWrappedText(
        renderer, Rect{sidePadding, y, pageWidth - sidePadding * 2, renderer.getLineHeight(UI_12_FONT_ID) * 3},
        UI_12_FONT_ID, tr(STR_STATS_EMPTY), 3);
  } else {
    const int rowHeight = std::max(1, metrics.listRowHeight);
    const int startY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int visible = visibleRows();

    for (int i = 0; i < visible && top + i < static_cast<int>(rows.size()); i++) {
      const Row& row = rows[top + i];
      const int rowY = startY + i * rowHeight;

      const char* label = (row.forget && confirmingForget) ? tr(STR_STATS_FORGET_PROMPT) : row.label.c_str();
      // Label left, figure right, so the numbers form a column the eye can run
      // down instead of sitting wherever each label happens to end.
      const auto labelLines = renderer.wrappedText(UI_12_FONT_ID, label, pageWidth - sidePadding * 2, 1);
      if (!labelLines.empty()) renderer.drawText(UI_12_FONT_ID, sidePadding, rowY, labelLines.front().c_str());

      if (!row.value.empty()) {
        const int width = renderer.getTextWidth(UI_12_FONT_ID, row.value.c_str());
        renderer.drawText(UI_12_FONT_ID, pageWidth - sidePadding - width, rowY, row.value.c_str());
      }

      if (top + i == selected) {
        renderer.fillRect(sidePadding, rowY + rowHeight - 6, pageWidth - sidePadding * 2, 3);
      }
    }
  }

  const auto labels = selected < 0
                          ? mappedInput.mapLabels(tr(STR_BACK), "", "", "")
                          : mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
