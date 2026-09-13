#pragma once

#include <string>
#include <vector>

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// An inbox you fill yourself.
//
// Articles you want to read go into /ReadLater from your computer, over the
// file transfer the firmware already runs. Nothing arrives on its own: the
// queue only grows when you deliberately put something in it, and it empties
// as you read. That is the whole difference between this and a feed.
//
// Removing a piece once read is a first-class action here, because a
// read-later pile that never shrinks is just a different kind of guilt.
class ReadLaterActivity final : public Activity {
 public:
  static constexpr const char* FOLDER = "/ReadLater";

  ReadLaterActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // Resident while open; a folder someone dumps a thousand files into should
  // not become the largest allocation on the device.
  static constexpr size_t MAX_ITEMS = 80;

  std::vector<std::string> items;  // file names, not full paths
  int selected = 0;
  int top = 0;
  ButtonNavigator buttonNavigator;

  void load();
  int visibleRows() const;
  std::string pathFor(int index) const;
};
