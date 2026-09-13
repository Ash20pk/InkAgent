#include "ManifestActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include "components/UITheme.h"
#include "engage/AppCatalog.h"
#include "engage/Manifest.h"
#include "engage/ScreenRenderer.h"
#include "fontIds.h"

ManifestActivity::ManifestActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string manifestPath,
                                   std::string fallbackTitle)
    : Activity("Manifest", renderer, mappedInput),
      path(std::move(manifestPath)),
      fallbackTitle(std::move(fallbackTitle)) {}

void ManifestActivity::onEnter() {
  Activity::onEnter();

  std::string json;
  if (!engage::readManifest(path.c_str(), json)) {
    failed = true;
    requestUpdate();
    return;
  }

  screen = makeUniqueNoThrow<engage::Screen>();
  if (!screen) {
    LOG_ERR("APPS", "OOM: screen for %s", path.c_str());
    failed = true;
    requestUpdate();
    return;
  }
  if (!engage::parseScreen(json.c_str(), json.size(), renderer, *screen)) {
    // A broken app says so on its own screen rather than bouncing the user back
    // to the drawer with nothing to explain what happened.
    screen.reset();
    failed = true;
  }
  // json goes out of scope here: the screen is already fully resolved.
  requestUpdate();
}

void ManifestActivity::onExit() {
  screen.reset();
  Activity::onExit();
}

void ManifestActivity::render(RenderLock&&) {
  if (screen) {
    engage::renderScreen(renderer, mappedInput, *screen);
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, fallbackTitle.c_str());
  const int y = renderer.getScreenHeight() / 2 - 20;
  UITheme::drawCenteredWrappedText(renderer,
                                   Rect{metrics.contentSidePadding, y, pageWidth - metrics.contentSidePadding * 2,
                                        renderer.getLineHeight(UI_12_FONT_ID) * 3},
                                   UI_12_FONT_ID, tr(STR_APP_MANIFEST_BROKEN), 3);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void ManifestActivity::loop() {
  Activity::loop();
  int x, y;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) {
    finish();
  }
}
