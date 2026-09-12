#include "InkAgentPairActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <InkAgentStore.h>
#include <Logging.h>
#include <WiFi.h>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/InkAgentClient.h"
#include "util/QrUtils.h"

namespace {
constexpr const char* kTag = "INKP";
constexpr unsigned long kPairPollMinMs = 3000;
}  // namespace

void InkAgentPairActivity::onEnter() {
  Activity::onEnter();
  INKAGENT_STORE.loadFromFile();
  state = State::Wifi;
  wifiActivated = true;
  requestUpdate();
  if (WiFi.status() == WL_CONNECTED) {
    onWifiSelectionComplete(true);
    return;
  }
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void InkAgentPairActivity::onExit() {
  Activity::onExit();
  if (wifiActivated && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void InkAgentPairActivity::fail(const char* msg) {
  LOG_ERR(kTag, "%s", msg);
  statusMessage = msg;
  state = State::Error;
  requestUpdate();
}

void InkAgentPairActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    finish();
    return;
  }
  WiFi.setSleep(false);
  if (!InkAgentClient::heapAllowsTls()) {
    fail(tr(STR_ASK_LOW_MEMORY));
    return;
  }
  startPairing();
}

void InkAgentPairActivity::startPairing() {
  state = State::PairStart;
  requestUpdate(true);
  pairing = InkAgentClient::pairStart(INKAGENT_STORE.getBudget());
  if (!pairing.ok) {
    char heap[48];
    InkAgentClient::heapSummary(heap, sizeof(heap));
    statusMessage = std::string(tr(STR_ASK_RELAY_UNREACHABLE)) + " (code " + std::to_string(InkAgentClient::lastHttpCode) +
                    ", " + heap + ")";
    LOG_ERR(kTag, "%s %s", statusMessage.c_str(), INKAGENT_STORE.getRelayUrl().c_str());
    state = State::Error;
    requestUpdate();
    return;
  }
  pairingDeadline = millis() + static_cast<unsigned long>(pairing.expiresInS) * 1000UL;
  nextPollAt = millis() + static_cast<unsigned long>(pairing.intervalS) * 1000UL;
  state = State::PairWait;
  requestUpdate();
}

void InkAgentPairActivity::pollPairing() {
  if (millis() > pairingDeadline) {
    fail(tr(STR_ASK_PAIR_EXPIRED));
    return;
  }
  const auto poll = InkAgentClient::pairPoll(pairing.deviceCode);
  const unsigned long interval = static_cast<unsigned long>(pairing.intervalS) * 1000UL;
  nextPollAt = millis() + (interval < kPairPollMinMs ? kPairPollMinMs : interval);
  switch (poll.status) {
    case inkagent::PairStatus::Pending:
    case inkagent::PairStatus::Error:
      return;
    case inkagent::PairStatus::Ok:
      INKAGENT_STORE.setPaired(poll.deviceToken, poll.deviceId, poll.owner);
      INKAGENT_STORE.saveToFile();
      LOG_DBG(kTag, "Paired with %s", poll.owner);
      statusMessage = std::string(tr(STR_ASK_PAIR_DONE)) + " " + poll.owner;
      state = State::Done;
      requestUpdate();
      return;
    case inkagent::PairStatus::Expired:
      fail(tr(STR_ASK_PAIR_EXPIRED));
      return;
    case inkagent::PairStatus::Denied:
      fail(tr(STR_ASK_PAIR_DENIED));
      return;
  }
}

void InkAgentPairActivity::loop() {
  const bool back = mappedInput.wasReleased(MappedInputManager::Button::Back);
  const bool confirm = mappedInput.wasReleased(MappedInputManager::Button::Confirm);
  switch (state) {
    case State::PairWait:
      if (back) { finish(); return; }
      if (millis() >= nextPollAt) pollPairing();
      return;
    case State::Done:
    case State::Error:
      if (back || confirm) finish();
      return;
    default:
      return;
  }
}

void InkAgentPairActivity::renderStatus(const char* msg) {
  renderer.clearScreen();
  const auto metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 tr(STR_ASK_PAIR_TITLE), nullptr);
  renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() / 2, msg);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void InkAgentPairActivity::renderPairing() {
  renderer.clearScreen();
  const auto metrics = UITheme::getInstance().getMetrics();
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, w, metrics.headerHeight}, tr(STR_ASK_PAIR_TITLE), nullptr);
  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int codeBlock = renderer.getLineHeight(UI_12_FONT_ID) * 3;
  int qrSide = h - top - codeBlock - metrics.buttonHintsHeight - 16;
  if (qrSide > w - 40) qrSide = w - 40;
  QrUtils::drawQrCode(renderer, Rect{(w - qrSide) / 2, top, qrSide, qrSide}, std::string(pairing.verifyUrlComplete));
  int y = top + qrSide + 8;
  renderer.drawCenteredText(UI_12_FONT_ID, y, pairing.userCode, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID);
  renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_ASK_PAIR_HINT));
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void InkAgentPairActivity::render(RenderLock&&) {
  switch (state) {
    case State::PairStart: renderStatus(tr(STR_ASK_CONNECTING)); return;
    case State::PairWait: renderPairing(); return;
    case State::Done:
    case State::Error: renderStatus(statusMessage.c_str()); return;
    default: return;  // Wi-Fi selection owns the screen
  }
}
