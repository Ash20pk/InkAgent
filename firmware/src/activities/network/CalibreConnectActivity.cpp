#include "CalibreConnectActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <cstring>

#include "MappedInputManager.h"
#include "WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr const char* kTag = "CALW";
// Books land in the same place every other transfer path puts them.
constexpr const char* kIncomingDir = "/Books";
// Calibre may be started after the reader is already waiting, so the broadcast
// repeats rather than being a one-shot at entry.
constexpr unsigned long kBroadcastIntervalMs = 2000;
constexpr unsigned long kRenderIntervalMs = 500;

// A book Calibre sends carries an lpath of its own choosing, which may contain
// directories and, being from another machine, anything at all. Keep the leaf,
// drop everything that could climb out of the folder.
std::string safeLeafName(const std::string& lpath) {
  size_t cut = lpath.find_last_of("/\\");
  std::string name = cut == std::string::npos ? lpath : lpath.substr(cut + 1);
  std::string out;
  for (const char c : name) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                    c == '-' || c == '_' || c == ' ' || c == '(' || c == ')' ||
                    static_cast<unsigned char>(c) >= 0x80;  // keep UTF-8 titles intact
    out += ok ? c : '_';
  }
  // A name of dots would resolve to the directory itself.
  while (!out.empty() && out.front() == '.') out.erase(out.begin());
  if (out.empty()) out = "book.epub";
  if (out.size() > 96) out.resize(96);
  return out;
}
}  // namespace

void CalibreConnectActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
  state = CalibreConnectState::WIFI_SELECTION;
  connectedIP.clear();
  connectedSSID.clear();
  statusDetail.clear();
  exitRequested = false;

  if (WiFi.status() != WL_CONNECTED) {
    startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& wifi = std::get<WifiResult>(result.data);
                               connectedIP = wifi.ip;
                               connectedSSID = wifi.ssid;
                             }
                             onWifiSelectionComplete(!result.isCancelled);
                           });
  } else {
    connectedIP = WiFi.localIP().toString().c_str();
    connectedSSID = WiFi.SSID().c_str();
    beginDiscovery();
  }
}

void CalibreConnectActivity::onExit() {
  Activity::onExit();
  dropSession(nullptr);
  udp.stop();
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
  }
}

void CalibreConnectActivity::onWifiSelectionComplete(const bool connected) {
  if (!connected) {
    finish();
    return;
  }
  if (connectedIP.empty()) connectedIP = WiFi.localIP().toString().c_str();
  if (connectedSSID.empty()) connectedSSID = WiFi.SSID().c_str();
  beginDiscovery();
}

void CalibreConnectActivity::beginDiscovery() {
  state = CalibreConnectState::DISCOVERING;
  serverPort = 0;
  serverHost.clear();
  lastBroadcastAt = 0;
  // One local port receives every reply; Calibre answers to whatever port the
  // broadcast came from.
  udp.begin(0);
  requestUpdate();
}

void CalibreConnectActivity::pumpDiscovery() {
  const unsigned long nowMs = millis();
  if (nowMs - lastBroadcastAt >= kBroadcastIntervalMs) {
    lastBroadcastAt = nowMs;
    // "hi there" to each of Calibre's listening ports, on the subnet broadcast
    // address. Calibre replies with the port its driver is accepting on.
    const IPAddress broadcast(255, 255, 255, 255);
    for (size_t i = 0; i < calibre::BROADCAST_PORT_COUNT; i++) {
      udp.beginPacket(broadcast, calibre::BROADCAST_PORTS[i]);
      udp.write(reinterpret_cast<const uint8_t*>(calibre::BROADCAST_HELLO), strlen(calibre::BROADCAST_HELLO));
      udp.endPacket();
    }
  }

  const int size = udp.parsePacket();
  if (size <= 0) return;
  char buf[192];
  const int read = udp.read(buf, sizeof(buf) - 1);
  if (read <= 0) return;
  buf[read] = '\0';

  uint16_t port = 0;
  std::string host;
  if (!calibre::parseBroadcastReply(buf, static_cast<size_t>(read), port, host)) return;
  serverAddr = udp.remoteIP();
  serverPort = port;
  serverHost = host.empty() ? std::string(serverAddr.toString().c_str()) : host;
  LOG_DBG(kTag, "found calibre at %s:%u", serverAddr.toString().c_str(), port);
  openSession();
}

void CalibreConnectActivity::openSession() {
  if (!client.connect(serverAddr, serverPort)) {
    statusDetail = tr(STR_CALIBRE_NOT_FOUND);
    state = CalibreConnectState::DISCOVERING;
    requestUpdate();
    return;
  }
  client.setNoDelay(true);

  calibre::Session::Config cfg;
  cfg.deviceName = "InkAgent";
  cfg.deviceKind = "InkAgent";
  cfg.appName = "InkAgent";

  calibre::Session::BookSink sink{
      [this](const std::string& lpath, uint64_t) {
        Storage.ensureDirectoryExists(kIncomingDir);
        const std::string path = std::string(kIncomingDir) + "/" + safeLeafName(lpath);
        if (incoming) incoming.close();
        if (!Storage.openFileForWrite(kTag, path, incoming)) {
          LOG_ERR(kTag, "cannot open %s", path.c_str());
          return false;
        }
        return true;
      },
      [this](const uint8_t* data, const size_t len) { return incoming && incoming.write(data, len) == len; },
      [this](bool) {
        if (incoming) {
          incoming.flush();
          incoming.close();
        }
      },
  };

  calibre::Session::Space space{
      [] { return Storage.sdTotalBytes(); },
      [] {
        const uint64_t total = Storage.sdTotalBytes();
        const uint64_t used = Storage.sdUsedBytes();
        return total > used ? total - used : 0;
      },
  };

  session = std::make_unique<calibre::Session>(cfg, std::move(sink), std::move(space));
  state = CalibreConnectState::CONNECTED;
  statusDetail.clear();
  requestUpdate();
}

void CalibreConnectActivity::dropSession(const char* why) {
  if (incoming) {
    incoming.close();
  }
  if (client.connected()) client.stop();
  session.reset();
  if (why != nullptr) {
    LOG_DBG(kTag, "session ended: %s", why);
    statusDetail = why;
  }
}

void CalibreConnectActivity::pumpSession() {
  if (!session) return;

  // Anything the protocol wants to say goes out first, so an OK always precedes
  // the bytes it is permitting.
  if (session->hasOutbound()) {
    const std::string out = session->takeOutbound();
    if (client.write(reinterpret_cast<const uint8_t*>(out.data()), out.size()) != out.size()) {
      dropSession("write failed");
      beginDiscovery();
      return;
    }
  }

  // A modest read buffer: book bytes go straight to the card, so nothing here
  // needs to hold a page, let alone a book.
  uint8_t buf[512];
  while (client.available() > 0) {
    const int read = client.read(buf, sizeof(buf));
    if (read <= 0) break;
    if (!session->feed(buf, static_cast<size_t>(read))) {
      const char* err = session->error();
      dropSession(err ? err : "protocol error");
      state = CalibreConnectState::ERROR;
      requestUpdate();
      return;
    }
    if (session->hasOutbound()) {
      const std::string out = session->takeOutbound();
      if (client.write(reinterpret_cast<const uint8_t*>(out.data()), out.size()) != out.size()) {
        dropSession("write failed");
        beginDiscovery();
        return;
      }
    }
  }

  if (!client.connected()) {
    // Calibre closing the socket is the ordinary end of a session, not a fault:
    // go back to listening so the next "Send to device" just works.
    dropSession(nullptr);
    beginDiscovery();
  }
}

void CalibreConnectActivity::loop() {
  Activity::loop();

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    exitRequested = true;
    finish();
    return;
  }

  switch (state) {
    case CalibreConnectState::DISCOVERING:
      pumpDiscovery();
      break;
    case CalibreConnectState::CONNECTED:
      pumpSession();
      break;
    default:
      break;
  }

  // The screen only changes when a number does, and e-ink repaints cost more
  // than the loop does; twice a second is enough to watch a transfer.
  const unsigned long nowMs = millis();
  if (state == CalibreConnectState::CONNECTED && nowMs - lastRenderAt >= kRenderIntervalMs) {
    lastRenderAt = nowMs;
    requestUpdate();
  }
}

void CalibreConnectActivity::renderScreen() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CALIBRE_WIRELESS),
                 nullptr);

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 3;
  // Inside the side padding rather than across the whole panel: the Calibre
  // instructions name a menu path and are longer than a line, and drawn as one
  // they ran off both edges. Centred on the block, so a line that wraps stays
  // centred with the line above it.
  const int textWidth = pageWidth - metrics.contentSidePadding * 2;
  constexpr int kMaxLines = 3;
  constexpr int kLineGap = 6;

  const auto centred = [&](const int fontId, const char* text) {
    const int lineHeight = renderer.getLineHeight(fontId);
    const int lines =
        std::max<int>(1, static_cast<int>(renderer.wrappedText(fontId, text, textWidth, kMaxLines).size()));
    const int height = lines * lineHeight;
    UITheme::drawCenteredWrappedText(renderer, Rect{metrics.contentSidePadding, y, textWidth, height}, fontId, text,
                                     kMaxLines, true, EpdFontFamily::REGULAR, UITheme::TextVerticalAlignment::TOP);
    y += height + kLineGap;
  };

  switch (state) {
    case CalibreConnectState::DISCOVERING:
      centred(UI_12_FONT_ID, tr(STR_CALIBRE_LOOKING));
      y += metrics.verticalSpacing;
      centred(UI_10_FONT_ID, tr(STR_CALIBRE_START_HINT));
      break;

    case CalibreConnectState::CONNECTED: {
      char buf[128];
      snprintf(buf, sizeof(buf), "%s %s", tr(STR_CALIBRE_CONNECTED_TO), serverHost.c_str());
      centred(UI_12_FONT_ID, buf);
      y += metrics.verticalSpacing;
      if (session && !session->currentBook().empty()) {
        centred(UI_10_FONT_ID, session->currentBook().c_str());
        const uint64_t total = session->bookTotal();
        const int pct = total > 0 ? static_cast<int>((session->bookReceived() * 100) / total) : 0;
        snprintf(buf, sizeof(buf), "%d%%", pct);
        centred(UI_12_FONT_ID, buf);
      } else if (session && session->booksReceived() > 0) {
        snprintf(buf, sizeof(buf), "%d %s", session->booksReceived(), tr(STR_CALIBRE_BOOKS_RECEIVED));
        centred(UI_10_FONT_ID, buf);
        y += metrics.verticalSpacing;
        centred(UI_10_FONT_ID, tr(STR_CALIBRE_SEND_MORE));
      } else {
        centred(UI_10_FONT_ID, tr(STR_CALIBRE_SEND_HINT));
      }
      break;
    }

    case CalibreConnectState::ERROR:
      centred(UI_12_FONT_ID, tr(STR_CALIBRE_FAILED));
      if (!statusDetail.empty()) {
        y += metrics.verticalSpacing;
        centred(UI_10_FONT_ID, statusDetail.c_str());
      }
      break;

    default:
      break;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void CalibreConnectActivity::render(RenderLock&&) {
  if (exitRequested) return;
  renderScreen();
}
