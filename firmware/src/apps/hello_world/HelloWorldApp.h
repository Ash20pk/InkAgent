#pragma once

// Compiled only when INKAGENT_EXAMPLE_APPS is set; see HelloWorldApp.cpp.
#if INKAGENT_EXAMPLE_APPS

#include "apps/InkApp.h"

// Template app: copy this folder, rename the class, and edit render().
// Registered in the .cpp; nothing outside this folder refers to it.
class HelloWorldApp : public InkApp {
 public:
  HelloWorldApp(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  int taps = 0;
};

#endif  // INKAGENT_EXAMPLE_APPS
