#pragma once

#include <Memory.h>

#include <string>

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "engage/Screen.h"

// Runs an app that is a file.
//
// Parses a manifest from SD, renders it through the shared renderer, and
// leaves on Back. It holds no interpreter and runs no code: the manifest can
// name rows and bind to the data-source whitelist, and everything else it
// might try is simply not in the vocabulary.
//
// Values resolve once, on open. A screen that wants fresher numbers is
// reopened, which on a panel that takes a second to repaint is the right
// bargain anyway.
class ManifestActivity final : public Activity {
 public:
  ManifestActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string manifestPath,
                   std::string fallbackTitle);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string path;
  std::string fallbackTitle;
  std::unique_ptr<engage::Screen> screen;
  bool failed = false;
};
