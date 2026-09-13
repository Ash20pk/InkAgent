#include "AboutActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include <cstring>

#include "engage/Manifest.h"
#include "engage/ScreenRenderer.h"

namespace {

// The About screen, as data. Values bind to the source whitelist in
// engage/DataSource.cpp; the title binds too, so the screen stays translated.
constexpr char kAboutManifest[] = R"JSON({
  "title": {"src": "i18n.about"},
  "rows": [
    {"kind": "logo"},
    {"kind": "text", "text": "InkAgent", "bold": true, "center": true, "gapAfter": 2},
    {"kind": "kv", "label": "Version",     "value": {"src": "device.version"}},
    {"kind": "kv", "label": "Device",      "value": {"src": "device.model"}},
    {"kind": "kv", "label": "Screen",      "value": {"src": "device.screen"}},
    {"kind": "kv", "label": "Battery",     "value": {"src": "device.battery"}},
    {"kind": "kv", "label": "Free memory", "value": {"src": "device.freeHeap"}}
  ]
})JSON";

}  // namespace

AboutActivity::AboutActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("About", renderer, mappedInput) {}

void AboutActivity::onEnter() {
  Activity::onEnter();

  screen = makeUniqueNoThrow<engage::Screen>();
  if (!screen) {
    LOG_ERR("ABOUT", "OOM: screen (%u bytes)", (unsigned)sizeof(engage::Screen));
    finish();
    return;
  }
  if (!engage::parseScreen(kAboutManifest, strlen(kAboutManifest), renderer, *screen)) {
    screen.reset();
    finish();
    return;
  }
  requestUpdate();
}

void AboutActivity::onExit() {
  screen.reset();
  Activity::onExit();
}

void AboutActivity::render(RenderLock&&) {
  if (screen) engage::renderScreen(renderer, mappedInput, *screen);
}

void AboutActivity::loop() {
  Activity::loop();
  int x, y;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) {
    finish();
  }
}
