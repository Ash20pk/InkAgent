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

// One scale, and one shape for the whole page: a single large figure at the
// top, and below it nothing but label-left, figure-right rows under a section
// name. Two ranks of figure and two of text is the entire system.
//
// Everything that broke that pattern is gone. The rates used to be a row of
// centred cards with their labels underneath, which put a third label position
// on a page that already had two, and needed a rule above them and hairlines
// between them to hold it together. They are rows now, and the rules went with
// them: the only marks left on the page are the chart's own floor and the line
// under whichever row is selected, each of which is doing a job.
constexpr int kHeroFont = NOTOSANS_16_FONT_ID;   // the single number the page is about
constexpr int kValueFont = NOTOSANS_12_FONT_ID;  // a figure at the end of a row
constexpr int kRowFont = UI_12_FONT_ID;          // a row's label
constexpr int kHeadingFont = UI_10_FONT_ID;      // what a section of the page is
constexpr int kCaptionFont = UI_10_FONT_ID;      // the unit under a figure, the chart's range

// Vertical rhythm. Two gaps, not one: things that belong together are a
// kTight apart and separate bands are a kBand apart. Spacing everything by the
// theme's single 8px step is what made the page read as one block — a figure,
// its unit, a chart and a heading all equally far from each other is the same
// as none of them being grouped at all.
constexpr int kTight = 8;
constexpr int kBand = 24;

// The week the chart covers, and how much of each day's column the bar fills;
// the rest is the gap that separates one day from the next.
constexpr int kChartDays = 7;
constexpr int kBarFillPercent = 55;
// Tick marks, and the air between a tick and the figure that names it.
constexpr int kTickLength = 4;
constexpr int kTickGap = 3;
// Floor and ceiling for the chart; the height between them comes from the
// panel, in chartHeight().
constexpr int kMinChartHeight = 56;
constexpr int kMaxChartHeight = 120;
constexpr int kProgressHeight = 10;

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

// A figure the data cannot support yet. An em dash, not a zero: zero is a
// measurement, and this is the absence of one.
const char* kNoFigure = "\u2014";

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

// Day 0 of the log is 2000-01-01, a Saturday, so Monday sits five days on.
std::string ReadingStatsActivity::weekdayName(const int32_t day) {
  static constexpr StrId kNames[7] = {StrId::STR_DAY_MON, StrId::STR_DAY_TUE, StrId::STR_DAY_WED, StrId::STR_DAY_THU,
                                      StrId::STR_DAY_FRI, StrId::STR_DAY_SAT, StrId::STR_DAY_SUN};
  if (day < 0) return "";
  return I18N.get(kNames[(day + 5) % 7]);
}

// A scale the reader can do arithmetic against. The top is the week's peak
// rounded up onto a ladder of round durations, and the ticks halve it, so the
// marks read 0 / 30m / 1h rather than 0 / 2m / 4m.
void ReadingStatsActivity::buildScale(const uint16_t peakMinutes) {
  static constexpr uint16_t kLadder[] = {10, 20, 30, 60, 90, 120, 180, 240, 360, 480, 720};
  // An empty week still gets a scale: an unlabelled axis is worse than one
  // that happens to have nothing under it.
  chartMax = kLadder[0];
  for (const uint16_t step : kLadder) {
    chartMax = step;
    if (peakMinutes <= step) break;
  }

  chartTicks.clear();
  chartTicks.reserve(3);
  for (const uint16_t minutes : {static_cast<uint16_t>(0), static_cast<uint16_t>(chartMax / 2), chartMax}) {
    // Bare zero at the foot: "0s" next to "5m" and "10m" puts a unit on the one
    // figure that has none.
    chartTicks.push_back({minutes, minutes == 0 ? "0" : duration(minutes * 60u * 1000u)});
  }
}

// --- what goes on the screen -------------------------------------------------

void ReadingStatsActivity::build() {
  rows.clear();
  detail.clear();
  chart.clear();
  chartTicks.clear();
  chartDays.clear();
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

// Pages, and the two rates, in that order. Shared so the library page and a
// book page cannot drift apart. Each label names the whole figure — "a minute"
// under an em dash said nothing at all.
std::vector<ReadingStatsActivity::Row> ReadingStatsActivity::rateRows(const uint32_t pages, const uint32_t ms) {
  const uint32_t perMinute = readstats::pagesPerMinuteTenths(pages, ms);
  const uint32_t perHour = readstats::pagesPerHour(pages, ms);
  return {
      {tr(STR_STATS_PAGES), number(pages), ""},
      {tr(STR_STATS_PER_MIN), perMinute == 0 ? kNoFigure : tenths(perMinute), ""},
      {tr(STR_STATS_PER_HR), perHour == 0 ? kNoFigure : number(perHour), ""},
  };
}

void ReadingStatsActivity::buildForLibrary() {
  const int32_t today = halClock.dayNumber(SETTINGS.statusBarSpec().clockUtcOffsetQ);
  const auto& log = READING_STATS.days();
  const uint32_t totalMs = READING_STATS.totalMs();
  const uint32_t pages = READING_STATS.totalPages();

  // The week, because that is what the chart underneath shows: a figure over a
  // chart should be the chart's own total, or one of the two is lying. The
  // lifetime total keeps its place in the detail below.
  heroLabel = tr(STR_STATS_WEEK);
  heroValue = duration(log.sum(today, kChartDays) * 60u * 1000u);

  const int32_t streak = log.streak(today);
  if (streak > 0) {
    asideValue = number(static_cast<uint32_t>(streak));
    asideLabel = tr(STR_STATS_STREAK);
  }

  // Oldest day first, so the chart reads left to right like everything else.
  chart.reserve(kChartDays);
  chartDays.reserve(kChartDays);
  for (int32_t d = kChartDays - 1; d >= 0; d--) {
    chart.push_back(log.on(today - d));
    chartDays.push_back(weekdayName(today - d));
  }
  buildScale(*std::max_element(chart.begin(), chart.end()));
  detail = rateRows(pages, totalMs);
  detail.insert(detail.begin(), {tr(STR_STATS_TOTAL), duration(totalMs), ""});
  detailHeading = tr(STR_STATS_DETAIL);

  const auto books = READING_STATS.byRecency();
  if (!books.empty()) {
    rowsHeading = tr(STR_STATS_BOOKS);
    for (const BookStats* b : books) {
      rows.push_back({b->title.empty() ? b->path : b->title, duration(b->readingMs), b->path});
    }
  }
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

  // One block of detail, the rates at the top of it. On the library page the
  // same rates sit above the list instead, because the list there is books.
  rowsHeading = tr(STR_STATS_DETAIL);
  rows = rateRows(stats.pages, stats.readingMs);
  rows.push_back({tr(STR_STATS_SITTINGS), number(stats.sessions), ""});
  // Reported, never scored: going back over a paragraph is often the most
  // careful reading there is.
  rows.push_back({tr(STR_STATS_REREAD), number(readstats::rereadPct(stats.pages, stats.regressions)) + "%", ""});

  const std::string started = describeDay(stats.firstDay, today);
  if (!started.empty()) rows.push_back({tr(STR_STATS_STARTED), started, ""});
  const std::string last = describeDay(stats.lastDay, today);
  if (!last.empty()) rows.push_back({tr(STR_STATS_LAST_READ), last, ""});
}

// --- geometry ----------------------------------------------------------------

// Kept in step with the draw functions by using the same constants they do; if
// the two ever disagree the row region starts in the wrong place.
// A fixed 56px of chart is squat on a tall panel and crowds a short one. An
// eighth of the screen keeps the bars in proportion to the type around them,
// and the bounds stop either extreme from taking the page over.
int ReadingStatsActivity::chartHeight() const {
  return std::clamp(renderer.getScreenHeight() / 8, kMinChartHeight, kMaxChartHeight);
}

int ReadingStatsActivity::dashboardHeight() const {
  if (heroValue.empty()) return 0;

  // Tight to the chart when there is one: the gap that separates bands would
  // cut the figure off from the very thing it is the total of.
  int height =
      renderer.getLineHeight(kCaptionFont) + renderer.getLineHeight(kHeroFont) + (chart.empty() ? kBand : kTight);
  // Half a label line of air over the plot, because the topmost tick figure is
  // centred on the plot's top edge and so stands above it, then the plot, then
  // the line of day names under the x axis.
  if (!chart.empty()) {
    height += renderer.getLineHeight(kCaptionFont) * 3 / 2 + chartHeight() + kTight + kBand;
  }
  if (progressPct >= 0) height += kProgressHeight + kBand;
  if (!detail.empty()) {
    height += renderer.getLineHeight(kHeadingFont) + kTight + static_cast<int>(detail.size()) * rowHeight() + kBand;
  }
  return height;
}

int ReadingStatsActivity::listTop() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Must match where render() starts the dashboard, or the rows are measured
  // against a different origin than they are drawn at.
  return metrics.topPadding + metrics.headerHeight + kBand + dashboardHeight();
}

// One pitch for every row, set from the type rather than from the theme's
// touch height. The book list used to take the 44px list row, which on a page
// whose dashboard is built on an 8/24px rhythm made the list the loudest band
// on a screen that is meant to be about the figures above it — and left room
// for two or three books where the same space holds five at the size the rest
// of the page is set in. The few pixels under the label are where the
// selection rule goes, so it never touches the next one.
int ReadingStatsActivity::rowHeight() const { return std::max(1, renderer.getLineHeight(kRowFont) + kTight + 4); }

int ReadingStatsActivity::visibleRows() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int heading = rowsHeading.empty() ? 0 : renderer.getLineHeight(kHeadingFont) + kTight;
  // A band of air above the hints, so the last row reads as the end of the
  // list rather than as a row the hints are sitting on top of.
  const int space = renderer.getScreenHeight() - listTop() - heading - metrics.buttonHintsHeight - kTight;
  if (space < rowHeight()) return 0;
  return space / rowHeight();
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
    const int visible = std::max(1, visibleRows());
    if (selected < top) top = selected;
    if (selected >= top + visible) top = selected - visible + 1;
    requestUpdate();
    return;
  }
}

void ReadingStatsActivity::activateSelected() {
  if (selected < 0 || selected >= static_cast<int>(rows.size())) return;
  const Row& row = rows[selected];

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
    finish();
    return;
  }
  if (selected < 0) return;

  buttonNavigator.onNext([this] { moveSelection(1); });
  buttonNavigator.onPrevious([this] { moveSelection(-1); });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) activateSelected();
}

// --- the dashboard -----------------------------------------------------------

int ReadingStatsActivity::drawHero(int y) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pad = metrics.contentSidePadding;
  const int right = renderer.getScreenWidth() - pad;

  // The caption sits directly on the figure it names, with the band's air
  // below the pair rather than between them.
  renderer.drawText(kCaptionFont, pad, y, heroLabel.c_str());
  if (!asideLabel.empty()) {
    const int width = renderer.getTextWidth(kCaptionFont, asideLabel.c_str());
    renderer.drawText(kCaptionFont, right - width, y, asideLabel.c_str());
  }
  y += renderer.getLineHeight(kCaptionFont);

  renderer.drawText(kHeroFont, pad, y, heroValue.c_str());
  if (!asideValue.empty()) {
    const int width = renderer.getTextWidth(kHeroFont, asideValue.c_str());
    renderer.drawText(kHeroFont, right - width, y, asideValue.c_str());
  }
  return y + renderer.getLineHeight(kHeroFont) + (chart.empty() ? kBand : kTight);
}

int ReadingStatsActivity::drawChart(int y) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pad = metrics.contentSidePadding;
  const int labelHeight = renderer.getLineHeight(kCaptionFont);
  const int plotHeight = chartHeight();
  // Room for the half of the topmost tick figure that sits above the plot.
  y += labelHeight / 2;
  const int baseline = y + plotHeight;
  const int right = renderer.getScreenWidth() - pad;

  // The widest tick label sets the gutter, so the y axis stands clear of the
  // figures whatever they read.
  int gutter = 0;
  for (const Tick& tick : chartTicks) {
    gutter = std::max(gutter, renderer.getTextWidth(kCaptionFont, tick.label.c_str()));
  }
  const int axisX = pad + gutter + kTickGap + kTickLength;

  // Both axes as real lines. The x axis doubles as the floor the bars stand on,
  // so a day with no reading is a gap in the bars rather than a hole in the
  // chart.
  renderer.fillRect(axisX, y, 1, plotHeight + 1);
  renderer.fillRect(axisX, baseline, right - axisX, 1);

  for (const Tick& tick : chartTicks) {
    const int tickY = baseline - (tick.minutes * plotHeight) / std::max<uint16_t>(chartMax, 1);
    renderer.fillRect(axisX - kTickLength, tickY, kTickLength, 1);
    const int width = renderer.getTextWidth(kCaptionFont, tick.label.c_str());
    // Centred on its tick, except at the foot of the axis, where centring would
    // drop the figure onto the day names.
    const int textY = std::min(tickY - labelHeight / 2, baseline - labelHeight);
    renderer.drawText(kCaptionFont, axisX - kTickLength - kTickGap - width, textY, tick.label.c_str());
  }

  // One column per day, each with its own tick and its own name: seven bars are
  // few enough to label individually, which is the whole reason the chart shows
  // a week rather than the full log.
  const int column = std::max(1, (right - axisX) / static_cast<int>(chart.size()));
  const int barWidth = std::max(1, column * kBarFillPercent / 100);
  for (size_t i = 0; i < chart.size(); i++) {
    const int centre = axisX + column * static_cast<int>(i) + column / 2;
    renderer.fillRect(centre, baseline + 1, 1, kTickLength);

    const uint16_t minutes = chart[i];
    if (minutes > 0 && chartMax > 0) {
      // At least one pixel of bar, so twenty minutes next to a four-hour day is
      // still visibly a day that happened.
      const int height = std::max(1, (minutes * plotHeight) / chartMax);
      renderer.fillRect(centre - barWidth / 2, baseline - height, barWidth, height);
    }

    if (i < chartDays.size()) {
      const int width = renderer.getTextWidth(kCaptionFont, chartDays[i].c_str());
      renderer.drawText(kCaptionFont, centre - width / 2, baseline + kTight, chartDays[i].c_str());
    }
  }

  return baseline + kTight + labelHeight + kBand;
}

int ReadingStatsActivity::drawProgress(int y) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pad = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - pad * 2;

  renderer.drawRect(pad, y, width, kProgressHeight);
  const int filled = (width - 2) * progressPct / 100;
  if (filled > 0) renderer.fillRect(pad + 1, y + 1, filled, kProgressHeight - 2);
  return y + kProgressHeight + kBand;
}

// Small and bold, so a section name is told apart from the rows under it by
// weight rather than by size. Set at the row's own size and weight — which is
// what "Detail" and "Books" were — a heading is just the first row of the
// block, and the page stops having sections at all. Bold keeps it from reading
// as a footnote to the band above, which is the trap a smaller heading falls
// into on its own; the page's own name stays the only thing at header size.
int ReadingStatsActivity::drawHeading(int y, const std::string& text) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  renderer.drawText(kHeadingFont, metrics.contentSidePadding, y, text.c_str(), true, EpdFontFamily::BOLD);
  return y + renderer.getLineHeight(kHeadingFont) + kTight;
}

// Label left, figure right, so the numbers form a column the eye can run down
// instead of sitting wherever each label happens to end. The label is text and
// the value is a figure, so they take the two faces the scale assigns.
void ReadingStatsActivity::drawRow(const int y, const Row& row, const bool isSelected) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pad = metrics.contentSidePadding;
  const int pageWidth = renderer.getScreenWidth();

  const int valueWidth = row.value.empty() ? 0 : renderer.getTextWidth(kValueFont, row.value.c_str());
  const int labelRoom = pageWidth - pad * 2 - (valueWidth > 0 ? valueWidth + pad : 0);
  const auto labelLines = renderer.wrappedText(kRowFont, row.label.c_str(), labelRoom, 1);
  if (!labelLines.empty()) renderer.drawText(kRowFont, pad, y, labelLines.front().c_str());
  if (valueWidth > 0) {
    // Figure fonts and text fonts do not share a line height; matching the tops
    // would leave the number floating, so match the baselines.
    const int valueY = y + renderer.getFontAscenderSize(kRowFont) - renderer.getFontAscenderSize(kValueFont);
    renderer.drawText(kValueFont, pageWidth - pad - valueWidth, valueY, row.value.c_str());
  }

  if (isSelected) {
    // Tucked under the label, not sitting on the row's bottom edge: at this
    // pitch a rule hard against the next row reads as a divider between two
    // books rather than as a mark on one of them.
    renderer.fillRect(pad, y + renderer.getLineHeight(kRowFont) + 3, pageWidth - pad * 2, 2);
  }
}

// The rates, on the library page. Same rows as the list below, so the page has
// one way of setting a label against a figure and not two.
int ReadingStatsActivity::drawDetail(int y) const {
  y = drawHeading(y, detailHeading);
  for (const Row& row : detail) {
    drawRow(y, row, false);
    y += rowHeight();
  }
  return y + kBand;
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
    int y = metrics.topPadding + metrics.headerHeight + kBand;
    y = drawHero(y);
    if (!chart.empty()) y = drawChart(y);
    if (progressPct >= 0) y = drawProgress(y);
    if (!detail.empty()) y = drawDetail(y);

    if (!rowsHeading.empty() && visibleRows() > 0) y = drawHeading(y, rowsHeading);

    const int pitch = rowHeight();
    const int visible = visibleRows();
    for (int i = 0; i < visible && top + i < static_cast<int>(rows.size()); i++) {
      drawRow(y + i * pitch, rows[top + i], top + i == selected);
    }
  }

  const auto labels = selected < 0
                          ? mappedInput.mapLabels(tr(STR_BACK), "", "", "")
                          : mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
