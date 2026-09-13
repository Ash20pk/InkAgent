#include "PeerSendActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include "components/UITheme.h"
#include "fontIds.h"

namespace {

// The receiving side is the File Transfer screen, so these mirror it exactly.
constexpr const char* PEER_SSID = "InkAgent-Reader";
constexpr const char* PEER_HOST = "192.168.4.1";
constexpr uint16_t PEER_PORT = 80;
// Books land where the reader looks for them.
constexpr const char* PEER_DEST = "/Books";

constexpr uint32_t JOIN_TIMEOUT_MS = 20000;
constexpr uint32_t SOCKET_TIMEOUT_MS = 15000;
// Small enough that a transfer never needs a meaningful slice of the heap, big
// enough that a 20 MB book does not take all afternoon.
constexpr size_t CHUNK = 2048;

const char* BOUNDARY = "----InkAgentPeerBoundary";

std::string baseName(const std::string& path) {
  const auto slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace

PeerSendActivity::PeerSendActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath)
    : Activity("PeerSend", renderer, mappedInput), path(std::move(filePath)) {
  fileName = baseName(path);
}

void PeerSendActivity::onEnter() {
  Activity::onEnter();
  state = State::Scanning;
  requestUpdateAndWait();  // paint "looking" before the radio blocks us
  runTransfer();
}

void PeerSendActivity::onExit() {
  if (wifiRaised) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  Activity::onExit();
}

void PeerSendActivity::runTransfer() {
  WiFi.mode(WIFI_STA);
  wifiRaised = true;

  // Scan rather than connecting blind: "no reader is receiving" is a far more
  // useful thing to say than a twenty-second timeout.
  const int found = WiFi.scanNetworks();
  bool present = false;
  for (int i = 0; i < found; i++) {
    if (WiFi.SSID(i) == PEER_SSID) {
      present = true;
      break;
    }
  }
  WiFi.scanDelete();
  if (!present) {
    state = State::NotFound;
    requestUpdate();
    return;
  }

  WiFi.begin(PEER_SSID);  // the receiver's access point is open
  const uint32_t deadline = millis() + JOIN_TIMEOUT_MS;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
    delay(200);
  }
  if (WiFi.status() != WL_CONNECTED) {
    state = State::Failed;
    detail = tr(STR_PEER_JOIN_FAILED);
    requestUpdate();
    return;
  }

  state = State::Sending;
  percent = 0;
  requestUpdateAndWait();

  if (sendFile()) {
    state = State::Done;
  } else {
    state = State::Failed;
  }
  requestUpdate();
}

bool PeerSendActivity::sendFile() {
  HalFile file;
  if (!Storage.openFileForRead("PEER", path.c_str(), file)) {
    detail = tr(STR_PEER_READ_FAILED);
    return false;
  }
  const size_t size = file.size();
  if (size == 0) {
    file.close();
    detail = tr(STR_PEER_READ_FAILED);
    return false;
  }

  // The multipart envelope is built first so Content-Length is exact. The
  // receiver's HTTP stack needs a length; it will not accept chunked.
  char head[320];
  const int headLen = snprintf(head, sizeof(head),
                               "--%s\r\n"
                               "Content-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
                               "Content-Type: application/octet-stream\r\n\r\n",
                               BOUNDARY, fileName.c_str());
  char tail[64];
  const int tailLen = snprintf(tail, sizeof(tail), "\r\n--%s--\r\n", BOUNDARY);
  if (headLen <= 0 || tailLen <= 0) {
    file.close();
    detail = tr(STR_PEER_SEND_FAILED);
    return false;
  }
  const size_t contentLength = static_cast<size_t>(headLen) + size + static_cast<size_t>(tailLen);

  WiFiClient client;
  client.setTimeout(SOCKET_TIMEOUT_MS / 1000);
  if (!client.connect(PEER_HOST, PEER_PORT)) {
    file.close();
    detail = tr(STR_PEER_JOIN_FAILED);
    return false;
  }

  client.printf("POST /upload?path=%s HTTP/1.1\r\n", PEER_DEST);
  client.printf("Host: %s\r\n", PEER_HOST);
  client.printf("Content-Type: multipart/form-data; boundary=%s\r\n", BOUNDARY);
  client.printf("Content-Length: %u\r\n", static_cast<unsigned>(contentLength));
  client.print("Connection: close\r\n\r\n");
  client.write(reinterpret_cast<const uint8_t*>(head), headLen);

  auto buffer = makeUniqueNoThrow<uint8_t[]>(CHUNK);
  if (!buffer) {
    file.close();
    client.stop();
    LOG_ERR("PEER", "OOM: transfer buffer");
    detail = tr(STR_PEER_SEND_FAILED);
    return false;
  }

  size_t sent = 0;
  while (sent < size) {
    const int read = file.read(buffer.get(), CHUNK);
    if (read <= 0) break;
    const size_t wrote = client.write(buffer.get(), static_cast<size_t>(read));
    if (wrote != static_cast<size_t>(read)) {
      file.close();
      client.stop();
      detail = tr(STR_PEER_SEND_FAILED);
      return false;
    }
    sent += wrote;
    // Repaint on whole percents only: the panel is far slower than the socket,
    // and redrawing per chunk would make the transfer take longer than it does.
    const int next = static_cast<int>((sent * 100) / size);
    if (next != percent) {
      percent = next;
      requestUpdate();
    }
  }
  client.write(reinterpret_cast<const uint8_t*>(tail), tailLen);
  file.close();

  // The receiver answers 200 on success and 400 with the reason in the body for
  // everything else — including a name collision, which is the common case. Its
  // own message is the most accurate thing available, so it is shown verbatim
  // rather than guessed at from the status code.
  const uint32_t deadline = millis() + SOCKET_TIMEOUT_MS;
  std::string statusLine;
  std::string body;
  bool inBody = false;
  while (millis() < deadline && (client.connected() || client.available())) {
    if (!client.available()) {
      delay(20);
      continue;
    }
    const std::string line = client.readStringUntil('\n').c_str();
    if (statusLine.empty()) {
      statusLine = line;
      continue;
    }
    if (!inBody) {
      // A bare CR marks the end of the headers.
      if (line.empty() || line == "\r") inBody = true;
      continue;
    }
    body += line;
    if (body.size() > 120) break;
  }
  client.stop();

  if (statusLine.find(" 200") != std::string::npos) return true;

  // Trim the trailing CR the server's line ending leaves behind.
  while (!body.empty() && (body.back() == '\r' || body.back() == '\n')) body.pop_back();
  detail = body.empty() ? tr(STR_PEER_SEND_FAILED) : body;
  return false;
}

void PeerSendActivity::renderStatus(const char* title, const char* body) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_PEER_SEND));

  const int y = pageHeight / 2 - 40;
  UITheme::drawCenteredText(renderer, Rect{0, y, pageWidth, renderer.getLineHeight(UI_12_FONT_ID)}, UI_12_FONT_ID, y,
                            title, true, EpdFontFamily::BOLD);

  const int bodyY = y + renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing;
  UITheme::drawCenteredWrappedText(renderer,
                                   Rect{metrics.contentSidePadding, bodyY, pageWidth - metrics.contentSidePadding * 2,
                                        renderer.getLineHeight(UI_10_FONT_ID) * 3},
                                   UI_10_FONT_ID, body, 3);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void PeerSendActivity::render(RenderLock&&) {
  char line[96];
  switch (state) {
    case State::Scanning:
      renderStatus(tr(STR_PEER_LOOKING), fileName.c_str());
      break;
    case State::NotFound:
      renderStatus(tr(STR_PEER_NONE_FOUND), tr(STR_PEER_NONE_FOUND_HINT));
      break;
    case State::Sending:
      snprintf(line, sizeof(line), "%s  %d%%", fileName.c_str(), percent);
      renderStatus(tr(STR_PEER_SENDING), line);
      break;
    case State::Done:
      renderStatus(tr(STR_PEER_SENT), fileName.c_str());
      break;
    case State::Failed:
      renderStatus(tr(STR_PEER_FAILED), detail.empty() ? fileName.c_str() : detail.c_str());
      break;
  }
}

void PeerSendActivity::loop() {
  Activity::loop();
  // Deliberately only Back, and only once the radio work is over: there is
  // nothing useful to do mid-transfer except wait.
  if (state == State::Scanning || state == State::Sending) return;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) finish();
}
