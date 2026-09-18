#pragma once

#include <HalStorage.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>

#include <memory>
#include <string>

#include "CalibreWireless/CalibreWireless.h"
#include "activities/Activity.h"

// Calibre Wireless, speaking Calibre's own smart-device protocol.
//
// Stock Calibre, with its Wireless Device driver started, is all that is needed
// at the far end: no plugin, and nothing derived from any other firmware. The
// exchange itself lives in lib/CalibreWireless (and is host-tested there); this
// activity owns the sockets, the SD card and the screen.
//
// Calibre listens and the reader dials, so the states run
// discovering -> connected -> receiving, and a lost connection drops back to
// discovering rather than ending the session: Calibre is often started after
// the reader is already on this screen.
enum class CalibreConnectState { WIFI_SELECTION, DISCOVERING, CONNECTED, ERROR };

class CalibreConnectActivity final : public Activity {
  CalibreConnectState state = CalibreConnectState::WIFI_SELECTION;

  WiFiUDP udp;
  WiFiClient client;
  std::unique_ptr<calibre::Session> session;
  HalFile incoming;

  std::string connectedIP;
  std::string connectedSSID;
  std::string serverHost;  // what Calibre calls the machine it is running on
  IPAddress serverAddr;
  uint16_t serverPort = 0;

  unsigned long lastBroadcastAt = 0;
  unsigned long lastRenderAt = 0;
  std::string statusDetail;
  bool exitRequested = false;

  void onWifiSelectionComplete(bool connected);
  void beginDiscovery();
  void pumpDiscovery();
  void pumpSession();
  void openSession();
  void dropSession(const char* why);
  void renderScreen() const;

 public:
  explicit CalibreConnectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("CalibreConnect", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return state == CalibreConnectState::CONNECTED; }
  bool preventAutoSleep() override { return state != CalibreConnectState::WIFI_SELECTION; }
};
