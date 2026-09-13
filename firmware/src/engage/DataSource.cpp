#include "DataSource.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <I18n.h>

#include <cstdio>
#include <cstring>

namespace engage {
namespace {

// Dev builds carry a "-dev-<branch>-<sha>" suffix; screens show the release
// version alone.
void deviceVersion(char* out, size_t cap) {
  snprintf(out, cap, "%s", INKAGENT_VERSION);
  if (char* dev = strstr(out, "-dev")) *dev = '\0';
}

}  // namespace

bool resolveSource(const char* name, const GfxRenderer& renderer, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return false;
  out[0] = '\0';
  if (name == nullptr) return false;

  if (strcmp(name, "device.version") == 0) {
    deviceVersion(out, cap);
    return true;
  }
  if (strcmp(name, "device.model") == 0) {
    snprintf(out, cap, "%s", gpio.deviceIsX3() ? "Xteink X3" : "Xteink X4");
    return true;
  }
  if (strcmp(name, "device.screen") == 0) {
    snprintf(out, cap, "%d x %d", renderer.getScreenWidth(), renderer.getScreenHeight());
    return true;
  }
  if (strcmp(name, "device.battery") == 0) {
    snprintf(out, cap, "%d%%", powerManager.getBatteryPercentage());
    return true;
  }
  if (strcmp(name, "device.freeHeap") == 0) {
    snprintf(out, cap, "%u KB", static_cast<unsigned>(ESP.getFreeHeap() / 1024));
    return true;
  }
  if (strcmp(name, "i18n.about") == 0) {
    snprintf(out, cap, "%s", tr(STR_ABOUT));
    return true;
  }
  return false;
}

}  // namespace engage
