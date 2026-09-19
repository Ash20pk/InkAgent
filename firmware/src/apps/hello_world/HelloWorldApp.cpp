// Template app: build scaffolding for developers, not a shipped feature, so it
// is compiled out by default and never appears in the drawer. Enable it while
// working on an app by adding to platformio.local.ini:
//   build_flags = ${base.build_flags} -DINKAGENT_EXAMPLE_APPS=1
#if INKAGENT_EXAMPLE_APPS

#include "HelloWorldApp.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <string>

#include "components/UITheme.h"
#include "components/icons/drawerIcons.h"
#include "fontIds.h"

HelloWorldApp::HelloWorldApp(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : InkApp("HelloWorld", renderer, mappedInput) {}

void HelloWorldApp::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void HelloWorldApp::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Hello");

  const int centreY = renderer.getScreenHeight() / 2;
  UITheme::drawCenteredText(renderer, Rect{0, centreY, pageWidth, renderer.getLineHeight(UI_12_FONT_ID)}, UI_12_FONT_ID,
                            centreY, "Hello from an InkAgent app");

  const std::string counter = "Confirm pressed " + std::to_string(taps) + " times";
  const int counterY = centreY + renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing;
  UITheme::drawCenteredText(renderer, Rect{0, counterY, pageWidth, renderer.getLineHeight(UI_10_FONT_ID)},
                            UI_10_FONT_ID, counterY, counter.c_str());

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Fast waveform: a half refresh inverts the panel and reads as a black flash.
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void HelloWorldApp::loop() {
  Activity::loop();

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    taps++;
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
  }
}

INKAGENT_REGISTER_APP(HelloWorldApp, "Hello", &icon_apps_32);

#endif  // INKAGENT_EXAMPLE_APPS
