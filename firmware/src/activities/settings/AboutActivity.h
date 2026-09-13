#pragma once

#include <Memory.h>

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "engage/Screen.h"

// Build and hardware identity, reached from Settings > System > About and from
// the app drawer.
//
// The first screen described by an Engage manifest rather than by drawing code:
// the layout lives in a JSON blob in flash, every value is a whitelisted data
// source, and the shared renderer draws it. Nothing here knows what a row is.
//
// Deliberately not FullScreenMessageActivity: that one is terminal (crash
// screens, no-SD) and handles no input, so opening it from a menu strands the
// device with no way back.
class AboutActivity final : public Activity {
 public:
  AboutActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // Parsed on enter, released on exit: the screen is the only state, and it is
  // ~2 KB of fixed-size struct rather than a live JSON document.
  std::unique_ptr<engage::Screen> screen;
};
