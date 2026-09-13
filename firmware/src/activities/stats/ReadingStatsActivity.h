#pragma once

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
// Two ways in, one screen. From the drawer it opens on everything — the whole
// library, most recently read first — and a book row leads to that book alone.
// From a book's own menu it opens straight on that book.
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
  struct Row {
    std::string label;
    std::string value;
    // Set on the rows that lead somewhere: a book, or the one destructive
    // action. Everything else is a figure to read and cannot be selected.
    std::string path;
    bool forget = false;
    bool selectable() const { return !path.empty() || forget; }
  };

  std::string bookPath;
  std::string bookTitle;
  std::vector<Row> rows;
  int selected = -1;  // -1 when nothing on the screen can be selected
  int top = 0;
  bool confirmingForget = false;
  ButtonNavigator buttonNavigator;

  void build();
  void buildForBook(const struct BookStats& stats);
  void buildForLibrary();
  int visibleRows() const;
  void moveSelection(int delta);
  void activateSelected();
  // "today", "yesterday", "6 days ago", or empty when the device had no clock.
  std::string describeDay(int32_t day, int32_t today) const;
};
