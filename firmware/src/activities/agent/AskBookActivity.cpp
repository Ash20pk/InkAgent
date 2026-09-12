#include "AskBookActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <InkAgentStore.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/reader/DictionaryDefinitionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/InkAgentClient.h"

namespace {
constexpr const char* kTag = "ASKB";
const StrId kKindLabels[] = {StrId::STR_ASK_EXPLAIN, StrId::STR_ASK_SUMMARY, StrId::STR_ASK_WHO, StrId::STR_ASK_TRANSLATE};
}  // namespace

AskBookActivity::AskBookActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string passage,
                                 std::string bookTitle, std::string author, std::string chapter, int bookPercent)
    : Activity("AskBook", renderer, mappedInput),
      passage(std::move(passage)),
      bookTitle(std::move(bookTitle)),
      author(std::move(author)),
      chapter(std::move(chapter)),
      bookPercent(bookPercent) {}

inkagent::Kind AskBookActivity::chosenKind() const {
  static constexpr inkagent::Kind kinds[kKindCount] = {inkagent::Kind::Explain, inkagent::Kind::Summary,
                                                       inkagent::Kind::Who, inkagent::Kind::Translate};
  return kinds[kindIndex < 0 ? 0 : kindIndex >= kKindCount ? kKindCount - 1 : kindIndex];
}

void AskBookActivity::onEnter() {
  Activity::onEnter();
  INKAGENT_STORE.loadFromFile();
  answerCap = static_cast<size_t>(INKAGENT_STORE.getBudget()) + 1;
  answer.reset(new (std::nothrow) char[answerCap]);
  if (!answer) {
    fail("Not enough memory.");
    return;
  }
  answer[0] = '\0';
  if (!INKAGENT_STORE.isPaired()) {
    fail(tr(STR_ASK_NOT_PAIRED_HINT));
    return;
  }
  state = State::ChooseKind;
  requestUpdate();
}

void AskBookActivity::onExit() {
  Activity::onExit();
  answer.reset();
  if (wifiActivated) {
    WiFi.disconnect(false);
    delay(30);
    silentRestartToReader();
  }
}

void AskBookActivity::fail(const char* msg) {
  LOG_ERR(kTag, "%s", msg);
  if (statusMessage.c_str() != msg) statusMessage = msg;
  state = State::Error;
  requestUpdate();
}

void AskBookActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    finish();
    return;
  }
  WiFi.setSleep(false);
  if (!InkAgentClient::heapAllowsTls()) {
    char heap[48];
    InkAgentClient::heapSummary(heap, sizeof(heap));
    InkAgentClient::sdLog(heap);
    statusMessage = std::string(tr(STR_ASK_LOW_MEMORY)) + " (" + heap + ")";
    fail(statusMessage.c_str());
    return;
  }
  state = State::Asking;
  requestUpdate(true);
  performAsk();
}

void AskBookActivity::performAsk() {
  inkagent::AskRequest req;
  req.kind = chosenKind();
  req.book = bookTitle.c_str();
  req.author = author.c_str();
  req.chapter = chapter.c_str();
  req.pct = bookPercent;
  req.text = passage.c_str();
  req.arg = req.kind == inkagent::Kind::Translate ? I18N.get(StrId::STR_ASK_TRANSLATE_TARGET) : nullptr;

  const auto r = InkAgentClient::ask(req, answer.get(), answerCap);
  if (!r.ok && InkAgentClient::lastHttpCode <= 0) {
    // Transport failure: put the numbers on screen, USB logging is gone by now.
    char heap[48];
    InkAgentClient::heapSummary(heap, sizeof(heap));
    snprintf(answer.get(), answerCap, "%s\n\n(code %d, %s)\n%s", tr(STR_ASK_RELAY_UNREACHABLE), InkAgentClient::lastHttpCode,
             heap, INKAGENT_STORE.getRelayUrl().c_str());
  }
  if (r.revoked) {
    INKAGENT_STORE.clearPairing();
    INKAGENT_STORE.saveToFile();
  }
  // Success or not, `answer` holds something the reader can show.
  showAnswer(I18N.get(kKindLabels[kindIndex]));
}

void AskBookActivity::showAnswer(const char* headword) {
  state = State::Showing;
  std::string text(answer.get());
  answer.reset();  // the definition activity owns its own copy from here
  startActivityForResult(std::make_unique<DictionaryDefinitionActivity>(renderer, mappedInput, headword, std::move(text)),
                         [this](const ActivityResult&) {
                           state = State::Done;
                           finish();
                         });
}

void AskBookActivity::loop() {
  switch (state) {
    case State::ChooseKind: {
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        finish();
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
          mappedInput.wasReleased(MappedInputManager::Button::Left)) {
        kindIndex = (kindIndex + kKindCount - 1) % kKindCount;
        requestUpdate();
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
                 mappedInput.wasReleased(MappedInputManager::Button::Right)) {
        kindIndex = (kindIndex + 1) % kKindCount;
        requestUpdate();
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        state = State::Wifi;
        wifiActivated = true;
        if (WiFi.status() == WL_CONNECTED) {
          onWifiSelectionComplete(true);
        } else {
          startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                                 [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
        }
      }
      return;
    }
    case State::Error:
      if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
          mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        finish();
      }
      return;
    default:
      return;
  }
}

void AskBookActivity::renderStatus(const char* title, const char* msg) {
  renderer.clearScreen();
  const auto metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight}, title, nullptr);
  renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() / 2, msg);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void AskBookActivity::renderKindMenu() {
  renderer.clearScreen();
  const auto metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight}, tr(STR_ASK_BOOK), nullptr);
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID) + 8;
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 2;
  for (int i = 0; i < kKindCount; i++) {
    const bool selected = i == kindIndex;
    if (selected) renderer.fillRect(8, y - 4, renderer.getScreenWidth() - 16, lineHeight, true);
    renderer.drawText(UI_12_FONT_ID, 20, y, I18N.get(kKindLabels[i]), !selected);
    y += lineHeight;
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_ASK_BOOK), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void AskBookActivity::render(RenderLock&&) {
  switch (state) {
    case State::ChooseKind: renderKindMenu(); return;
    case State::Asking: renderStatus(tr(STR_ASK_BOOK), tr(STR_ASK_THINKING)); return;
    case State::Error: renderStatus(tr(STR_ASK_BOOK), statusMessage.c_str()); return;
    default: return;  // Wifi / Showing: a child activity owns the screen
  }
}
