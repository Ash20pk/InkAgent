#include "ReadingStatsActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <I18n.h>

#include <algorithm>

#include "InkAgentSettings.h"
#include "ReadingStats.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

// The dashboard's three type sizes: a figure, a heading, and the small caps
// under a figure saying what it is.
constexpr int kFigureFont = NOTOSANS_18_FONT_ID;
constexpr int kCardFont = NOTOSANS_16_FONT_ID;
constexpr int kLabelFont = UI_10_FONT_ID;

// Bars any narrower than this stop reading as a chart and start reading as
// noise, so a narrow panel shows fewer days rather than thinner days.
constexpr int kMinBarWidth = 3;
constexpr int kBarGap = 2;
constexpr int kChartHeight = 40;

std::string duration(const uint32_t ms) {
  char buf[16];
  readstats::formatDuration(ms, buf, sizeof(buf));
  return buf;
}

std::string tenths(const uint32_t value) {
  char buf[12];
  readstats::formatTenths(value, buf, sizeof(buf));
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

// --- what goes on the screen -------------------------------------------------

void ReadingStatsActivity::build() {
  rows.clear();
  cards.clear();
  chart.clear();
  heroValue.clear();
  asideValue.clear();
  progressPct = -1;
  selected = -1;
  top = 0;

  if (!bookPath.empty()) {
    const BookStats* stats = READING_STATS.forPath(bookPath);
    if (stats != nullptr) buildForBook(*stats);
  } else {
    buildForLibrary();
  }

  for (size_t i = 0; i < rows.size(); i++) {
    if (rows[i].selectable()) {
      selected = static_cast<int>(i);
      break;
    }
  }
}

void ReadingStatsActivity::buildForLibrary() {
  const int32_t today = halClock.dayNumber(SETTINGS.statusBarSpec().clockUtcOffsetQ);
  const auto& log = READING_STATS.days();
  const uint32_t totalMs = READING_STATS.totalMs();
  const uint32_t pages = READING_STATS.totalPages();

  heroLabel = tr(STR_STATS_TOTAL);
  heroValue = duration(totalMs);

  const int32_t streak = log.streak(today);
  if (streak > 0) {
    asideValue = number(static_cast<uint32_t>(streak));
    asideLabel = tr(STR_STATS_STREAK);
  }

  // Oldest day first, so the chart reads left to right like everything else.
  chart.reserve(readstats::DAYS);
  for (int32_t d = readstats::DAYS - 1; d >= 0; d--) chart.push_back(log.on(today - d));
  chartPeak = *std::max_element(chart.begin(), chart.end());
  chartCaption = tr(STR_STATS_WINDOW);
  if (chartPeak > 0) {
    chartCaption += " · " + std::string(tr(STR_STATS_PEAK)) + " " + duration(chartPeak * 60u * 1000u);
  }
  chartAside = std::string(tr(STR_STATS_WEEK)) + " " + duration(log.sum(today, 7) * 60u * 1000u);

  cards.push_back({number(pages), tr(STR_STATS_PAGES)});
  cards.push_back({tenths(readstats::pagesPerMinuteTenths(pages, totalMs)), tr(STR_STATS_PER_MIN)});
  cards.push_back({number(readstats::pagesPerHour(pages, totalMs)), tr(STR_STATS_PER_HR)});

  const auto books = READING_STATS.byRecency();
  if (!books.empty()) {
    rowsHeading = tr(STR_STATS_BOOKS);
    for (const BookStats* b : books) {
      rows.push_back({b->title.empty() ? b->path : b->title, duration(b->readingMs), b->path, false});
    }
  }
  if (READING_STATS.totalSessions() > 0) rows.push_back({tr(STR_STATS_FORGET), "", "", true});
}

void ReadingStatsActivity::buildForBook(const BookStats& stats) {
  const int32_t today = halClock.dayNumber(SETTINGS.statusBarSpec().clockUtcOffsetQ);

  heroLabel = tr(STR_STATS_TOTAL);
  heroValue = duration(stats.readingMs);

  // How far through, from the position the reader already saves. There is no
  // per-book daily history to chart — the day log is the device's, not each
  // book's — so progress takes the chart's place here.
  const RecentBook book = RECENT_BOOKS.getDataFromBook(stats.path);
  if (book.percent >= 0) {
    progressPct = std::min(book.percent, 100);
    asideValue = number(static_cast<uint32_t>(progressPct)) + "%";
    asideLabel = tr(STR_STATS_READ);
  }

  cards.push_back({number(stats.pages), tr(STR_STATS_PAGES)});
  cards.push_back({tenths(readstats::pagesPerMinuteTenths(stats.pages, stats.readingMs)), tr(STR_STATS_PER_MIN)});
  cards.push_back({number(stats.sessions), tr(STR_STATS_SITTINGS)});

  const uint32_t perHour = readstats::pagesPerHour(stats.pages, stats.readingMs);
  rows.push_back({tr(STR_STATS_PACE),
                  perHour == 0 ? tr(STR_STATS_TOO_SOON) : number(perHour) + " " + tr(STR_STATS_PER_HOUR), "", false});
  // Reported, never scored: going back over a paragraph is often the most
  // careful reading there is.
  rows.push_back({tr(STR_STATS_REREAD), number(readstats::rereadPct(stats.pages, stats.regressions)) + "%", "", false});

  const std::string started = describeDay(stats.firstDay, today);
  if (!started.empty()) rows.push_back({tr(STR_STATS_STARTED), started, "", false});
  const std::string last = describeDay(stats.lastDay, today);
  if (!last.empty()) rows.push_back({tr(STR_STATS_LAST_READ), last, "", false});
}

// --- geometry ----------------------------------------------------------------

int ReadingStatsActivity::dashboardHeight() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (heroValue.empty()) return 0;

  int height = renderer.getLineHeight(kLabelFont) + renderer.getLineHeight(kFigureFont) + metrics.verticalSpacing;
  if (!chart.empty()) height += kChartHeight + renderer.getLineHeight(kLabelFont) + metrics.verticalSpacing;
  if (progressPct >= 0) height += 8 + metrics.verticalSpacing;
  if (!cards.empty()) {
    height += renderer.getLineHeight(kCardFont) + renderer.getLineHeight(kLabelFont) + metrics.verticalSpacing * 2;
  }
  return height;
}

int ReadingStatsActivity::listTop() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + dashboardHeight();
}

int ReadingStatsActivity::visibleRows() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int rowHeight = std::max(1, metrics.listRowHeight);
  const int heading = rowsHeading.empty() ? 0 : renderer.getLineHeight(kLabelFont) + metrics.verticalSpacing;
  const int space = renderer.getScreenHeight() - listTop() - heading - metrics.buttonHintsHeight;
  return std::max(1, space / rowHeight);
}

// --- input -------------------------------------------------------------------

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

// --- the dashboard -----------------------------------------------------------

int ReadingStatsActivity::drawHero(int y) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pad = metrics.contentSidePadding;
  const int right = renderer.getScreenWidth() - pad;

  renderer.drawText(kLabelFont, pad, y, heroLabel.c_str());
  if (!asideLabel.empty()) {
    const int width = renderer.getTextWidth(kLabelFont, asideLabel.c_str());
    renderer.drawText(kLabelFont, right - width, y, asideLabel.c_str());
  }
  y += renderer.getLineHeight(kLabelFont);

  renderer.drawText(kFigureFont, pad, y, heroValue.c_str());
  if (!asideValue.empty()) {
    const int width = renderer.getTextWidth(kFigureFont, asideValue.c_str());
    renderer.drawText(kFigureFont, right - width, y, asideValue.c_str());
  }
  return y + renderer.getLineHeight(kFigureFont) + metrics.verticalSpacing;
}

int ReadingStatsActivity::drawChart(int y) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pad = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - pad * 2;

  // As many of the most recent days as will fit at a legible bar width. A
  // narrow panel drops the oldest days rather than drawing hairlines.
  int shown = static_cast<int>(chart.size());
  while (shown > 1 && (width - (shown - 1) * kBarGap) / shown < kMinBarWidth) shown--;
  const int barWidth = std::max(1, (width - (shown - 1) * kBarGap) / shown);
  const int used = shown * barWidth + (shown - 1) * kBarGap;
  const int originX = pad + (width - used) / 2;
  const int baseline = y + kChartHeight;

  for (int i = 0; i < shown; i++) {
    const uint16_t minutes = chart[chart.size() - shown + i];
    const int x = originX + i * (barWidth + kBarGap);
    if (minutes == 0 || chartPeak == 0) {
      // A day with no reading is a mark on the baseline, not a gap: the shape
      // of the month is only legible if every day is accounted for.
      renderer.fillRect(x, baseline - 1, barWidth, 1);
      continue;
    }
    // At least one pixel of bar, so twenty minutes next to a four-hour day is
    // still visibly a day that happened.
    const int height = std::max(1, (minutes * kChartHeight) / chartPeak);
    renderer.fillRect(x, baseline - height, barWidth, height);
  }

  y = baseline + 2;
  renderer.drawText(kLabelFont, pad, y, chartCaption.c_str());
  if (!chartAside.empty()) {
    const int aside = renderer.getTextWidth(kLabelFont, chartAside.c_str());
    renderer.drawText(kLabelFont, renderer.getScreenWidth() - pad - aside, y, chartAside.c_str());
  }
  return y + renderer.getLineHeight(kLabelFont) + metrics.verticalSpacing;
}

int ReadingStatsActivity::drawProgress(int y) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pad = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - pad * 2;

  renderer.drawRect(pad, y, width, 8);
  const int filled = (width - 2) * progressPct / 100;
  if (filled > 0) renderer.fillRect(pad + 1, y + 1, filled, 6);
  return y + 8 + metrics.verticalSpacing;
}

int ReadingStatsActivity::drawCards(int y) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pad = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - pad * 2;
  const int count = static_cast<int>(cards.size());
  const int column = width / count;

  // A rule above the row rather than a box around each figure: a box per card
  // is three times the ink for the same grouping, and ink is what e-ink costs.
  renderer.fillRect(pad, y, width, 1);
  y += metrics.verticalSpacing;

  for (int i = 0; i < count; i++) {
    const int centre = pad + column * i + column / 2;
    const int valueWidth = renderer.getTextWidth(kCardFont, cards[i].value.c_str());
    renderer.drawText(kCardFont, centre - valueWidth / 2, y, cards[i].value.c_str());
    const int labelWidth = renderer.getTextWidth(kLabelFont, cards[i].label.c_str());
    renderer.drawText(kLabelFont, centre - labelWidth / 2, y + renderer.getLineHeight(kCardFont),
                      cards[i].label.c_str());
    // Hairlines between the columns, drawn short so they separate the figures
    // without boxing them in.
    if (i > 0) {
      renderer.fillRect(pad + column * i, y, 1, renderer.getLineHeight(kCardFont));
    }
  }
  return y + renderer.getLineHeight(kCardFont) + renderer.getLineHeight(kLabelFont) + metrics.verticalSpacing;
}

// --- render ------------------------------------------------------------------

void ReadingStatsActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pad = metrics.contentSidePadding;

  renderer.clearScreen();
  const char* heading = bookPath.empty()    ? tr(STR_READING_STATS)
                        : bookTitle.empty() ? tr(STR_STATS_BOOK)
                                            : bookTitle.c_str();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, heading);

  if (heroValue.empty()) {
    const int y = renderer.getScreenHeight() / 2 - 20;
    UITheme::drawCenteredWrappedText(renderer,
                                     Rect{pad, y, pageWidth - pad * 2, renderer.getLineHeight(UI_12_FONT_ID) * 3},
                                     UI_12_FONT_ID, tr(STR_STATS_EMPTY), 3);
  } else {
    int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    y = drawHero(y);
    if (!chart.empty()) y = drawChart(y);
    if (progressPct >= 0) y = drawProgress(y);
    if (!cards.empty()) y = drawCards(y);

    if (!rowsHeading.empty()) {
      renderer.drawText(kLabelFont, pad, y, rowsHeading.c_str());
      y += renderer.getLineHeight(kLabelFont) + metrics.verticalSpacing;
    }

    const int rowHeight = std::max(1, metrics.listRowHeight);
    const int visible = visibleRows();
    for (int i = 0; i < visible && top + i < static_cast<int>(rows.size()); i++) {
      const Row& row = rows[top + i];
      const int rowY = y + i * rowHeight;

      const char* label = (row.forget && confirmingForget) ? tr(STR_STATS_FORGET_PROMPT) : row.label.c_str();
      // Label left, figure right, so the numbers form a column the eye can run
      // down instead of sitting wherever each label happens to end.
      const int valueWidth = row.value.empty() ? 0 : renderer.getTextWidth(UI_12_FONT_ID, row.value.c_str());
      const int labelRoom = pageWidth - pad * 2 - (valueWidth > 0 ? valueWidth + pad : 0);
      const auto labelLines = renderer.wrappedText(UI_12_FONT_ID, label, labelRoom, 1);
      if (!labelLines.empty()) renderer.drawText(UI_12_FONT_ID, pad, rowY, labelLines.front().c_str());
      if (valueWidth > 0) renderer.drawText(UI_12_FONT_ID, pageWidth - pad - valueWidth, rowY, row.value.c_str());

      if (top + i == selected) {
        renderer.fillRect(pad, rowY + rowHeight - 6, pageWidth - pad * 2, 3);
      }
    }
  }

  const auto labels = selected < 0
                          ? mappedInput.mapLabels(tr(STR_BACK), "", "", "")
                          : mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
