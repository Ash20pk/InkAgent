#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// What you have read, and for how long.
//
// The device was already measuring this to decide whether a stretch was slower
// than usual for you; the totals were used once and thrown away. This is the
// same measurement, kept and shown.
//
// Laid out as a dashboard rather than a list of figures: one number at the top
// that answers "how much have I read", a four-week bar chart underneath it
// because a shape shows a habit in a way a total never can, then the rates in
// a row, then the books. The chart is why this is worth the screen — twenty-
// eight bars cost almost nothing to draw and say what twenty-eight numbers
// could not.
//
// Two ways in. From the drawer it opens on everything, most recently read
// first, and a book row leads to that book alone. From a book's own menu it
// opens straight on that book, where the chart is replaced by how far through
// it you are.
//
// There is no goal here, no target, no quota, nothing to fail and nothing that
// notifies. A device that argues reading should not be gamified cannot then
// award points for it. These are numbers for when you go looking.
class ReadingStatsActivity final : public Activity {
 public:
  // An empty bookPath opens the library view; a path opens that book.
  ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath = "",
                       std::string bookTitle = "");

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // A line in the region below the dashboard: a book, which leads to its own
  // page, or a plain figure that cannot be selected.
  struct Row {
    std::string label;
    std::string value;
    std::string path;
    bool selectable() const { return !path.empty(); }
  };

  std::string bookPath;
  std::string bookTitle;

  // --- the dashboard, built once on entry ------------------------------------
  std::string heroValue;   // total time, large
  std::string heroLabel;   // what it is, small, above it
  std::string asideValue;  // streak, or how far through the book
  std::string asideLabel;
  // The rates, on the library page, where they sit under the chart rather than
  // in the list: the list there is books, and a figure is not a book.
  std::vector<Row> detail;
  std::string detailHeading;
  // Daily minutes, oldest first, for the library view's chart. A week, not the
  // whole 28-day log: seven bars can each carry their own label on the x axis,
  // where twenty-eight can only be labelled at the ends and left to be guessed
  // at in between. Empty in the book view, which shows progress instead —
  // daily totals are kept for the device, not per book.
  std::vector<uint16_t> chart;
  // A marked value on the y axis.
  struct Tick {
    uint16_t minutes;
    std::string label;
  };
  // Top of the y scale: the week's peak rounded up to a figure worth printing,
  // so the ticks land on round numbers instead of on whatever was read.
  uint16_t chartMax = 0;
  std::vector<Tick> chartTicks;
  // One label per bar, under the day it belongs to.
  std::vector<std::string> chartDays;
  // 0-100, or -1 when this book has no known position. Book view only.
  int progressPct = -1;

  // --- the scrolling region --------------------------------------------------
  std::vector<Row> rows;
  std::string rowsHeading;
  int selected = -1;  // -1 when nothing on the screen can be selected
  int top = 0;
  ButtonNavigator buttonNavigator;

  void build();
  void buildForBook(const struct BookStats& stats);
  void buildForLibrary();
  // Pages and the two rates, identical on both pages.
  static std::vector<Row> rateRows(uint32_t pages, uint32_t ms);

  // Chart height for this panel; the draw and the measurement must agree.
  int chartHeight() const;
  // Height of the dashboard above the scrolling region, so both the renderer
  // and the row arithmetic agree on where the list starts.
  int dashboardHeight() const;
  int listTop() const;
  int visibleRows() const;
  int rowHeight() const;
  void moveSelection(int delta);
  void activateSelected();
  std::string describeDay(int32_t day, int32_t today) const;
  // Short name of the weekday `day` falls on; day 0 is 2000-01-01, a Saturday.
  static std::string weekdayName(int32_t day);
  // Top of the scale, and the values marked on it, for a week peaking at
  // `peakMinutes`.
  void buildScale(uint16_t peakMinutes);

  // Each returns the y it finished at, so the bands stack without any of them
  // knowing what came before.
  int drawHero(int y) const;
  int drawChart(int y) const;
  int drawProgress(int y) const;
  int drawDetail(int y) const;
  // Every label/figure pair on the page goes through here, so a row in the
  // fixed band and a row in the list cannot be set differently.
  void drawRow(int y, const Row& row, bool isSelected) const;
  // A section name, in the one style the page uses for them.
  int drawHeading(int y, const std::string& text) const;
};
