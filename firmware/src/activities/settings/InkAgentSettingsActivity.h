#pragma once

#include <string>

#include "activities/UiListActivity.h"

// Settings › Ask the book: where the reader's relay lives and who it is paired
// with. Rows: relay URL (keyboard), pairing status (activate to unpair),
// reset URL to the build default.
class InkAgentSettingsActivity final : public UiListActivity {
 public:
  explicit InkAgentSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  static constexpr int MENU_ITEMS = 5;

 private:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  // Result of the last sync, shown on the row rather than in a popup: the
  // outcome is worth reading once, not worth a screen to dismiss.
  std::string syncStatus_;
  void syncApps();

  std::string rowValues_[MENU_ITEMS];
  freeink::ui::ListItem rowItems_[MENU_ITEMS]{};
};
