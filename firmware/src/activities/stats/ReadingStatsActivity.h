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
  // A figure and what it is, in the row under the chart.
  struct Card {
    std::string value;
    std::string label;
  };

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
  std::vector<Card> cards;
  // Daily minutes, oldest first, for the library view's chart. Empty in the
  // book view, which shows progress instead — daily totals are kept for the
  // device, not per book.
  std::vector<uint16_t> chart;
  uint16_t chartPeak = 0;
  std::string chartCaption;
  std::string chartAside;
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
  static std::vector<Card> rateCards(uint32_t pages, uint32_t ms);

  // Height of the dashboard above the scrolling region, so both the renderer
  // and the row arithmetic agree on where the list starts.
  int dashboardHeight() const;
  int listTop() const;
  int visibleRows() const;
  int rowHeight() const;
  void moveSelection(int delta);
  void activateSelected();
  std::string describeDay(int32_t day, int32_t today) const;

  // Each returns the y it finished at, so the bands stack without any of them
  // knowing what came before.
  int drawHero(int y) const;
  int drawChart(int y) const;
  int drawProgress(int y) const;
  int drawCards(int y) const;
};
