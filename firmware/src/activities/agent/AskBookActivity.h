#pragma once
#include <InkAgentProtocol.h>

#include <memory>
#include <string>

#include "activities/Activity.h"

// "Ask the book": the reader has already released its EPUB (heap for TLS) and
// handed us the passage plus metadata as plain strings. We bring Wi-Fi up,
// pair with the relay if needed (QR + code on screen, poll until claimed),
// send the question, and show the answer through DictionaryDefinitionActivity,
// which already knows how to page plain text in the reader font. On exit the
// device restarts into the reader exactly like KOReaderSyncActivity does.
class AskBookActivity final : public Activity {
 public:
  explicit AskBookActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string passage,
                           std::string bookTitle, std::string author, std::string chapter, int bookPercent,
                           int regressions = -1, int speedPct = -1);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state != State::Done && state != State::Error; }

 private:
  enum class State : uint8_t { ChooseKind, Wifi, Asking, Showing, Error, Done };

  std::string passage, bookTitle, author, chapter;
  int bookPercent;
  // How this stretch actually went, from page turns alone. Negative means the
  // reader did not measure it, and the request omits it rather than sending a
  // zero that would read as a real observation.
  int regressions = -1;
  int speedPct = -1;

  State state = State::ChooseKind;
  int kindIndex = 0;
  // recall, explain, summary, who, translate (define goes via the dictionary)
  static constexpr int kKindCount = 5;
  bool isRecall() const;
  inkagent::Kind chosenKind() const;

  bool wifiActivated = false;
  bool wifiWasUp = false;
  std::string statusMessage;

  std::unique_ptr<char[]> answer;  // budget + 1 bytes
  size_t answerCap = 0;

  void onWifiSelectionComplete(bool success);
  void performAsk();
  void fail(const char* msg);
  void showAnswer(const char* headword);

  void renderKindMenu();
  void renderStatus(const char* title, const char* msg);
};
