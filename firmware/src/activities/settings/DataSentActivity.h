#pragma once

#include <string>
#include <vector>

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// What leaves this device, said plainly.
//
// The reader sends a passage of whatever you are reading to a relay, along with
// two numbers derived from how you turned the pages. Until now nothing in the
// interface said so. That was tolerable while every request was a button press
// and is not now that a fetch can happen on its own after a reading session.
//
// This screen is written from the wire format rather than from intent: the
// fields listed here are the fields buildEngageRequest and buildAskRequest
// actually emit. If one is added there, it belongs here in the same change.
class DataSentActivity final : public Activity {
 public:
  DataSentActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // One paragraph, or a heading. Built on open so the live state (paired,
  // background fetch) is read once rather than on every frame.
  struct Block {
    std::string text;
    bool heading = false;
  };

  std::vector<Block> blocks;
  int scrollY = 0;
  // Total laid-out height, refreshed whenever the screen is drawn. loop() must
  // not recompute it: wrapping every paragraph allocates a vector of strings,
  // and doing that per input poll is the kind of churn that fragments a 380 KB
  // heap for no reason.
  mutable int contentHeight = 0;
  ButtonNavigator buttonNavigator;

  void buildBlocks();
  int viewportHeight() const;
  // Wraps and positions every block; draws only what is in view. Returns the
  // total height, which is where the scroll limit comes from — the text wraps
  // differently in each orientation, so the extent cannot be a constant.
  int layout(bool draw) const;
};
