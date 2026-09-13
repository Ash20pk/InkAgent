#pragma once

#include <string>
#include <vector>

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Every bookmark you have saved, across every book, in one place.
//
// Bookmarks are stored per book beside the book itself, which means they are
// only ever visible from inside the book that holds them — so the passage you
// marked three books ago is effectively gone. This gathers them.
//
// Reading is the only thing that fills this list, and it terminates: there is
// nothing to clear and no count of it anywhere else.
class HighlightsActivity final : public Activity {
 public:
  HighlightsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct Highlight {
    std::string summary;  // the passage's opening words
    std::string book;     // title it belongs to
    std::string path;     // book path, so Confirm can open it
    uint8_t percent = 0;
  };

  // Bounded: this is resident while the app is open, and ten books' worth of
  // bookmarks with no cap would be the largest allocation on the device.
  static constexpr size_t MAX_HIGHLIGHTS = 60;

  std::vector<Highlight> highlights;
  int selected = 0;
  int top = 0;
  ButtonNavigator buttonNavigator;

  void load();
  int visibleRows() const;
};
