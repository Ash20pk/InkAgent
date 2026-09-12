#pragma once
#include <InkAgentProtocol.h>

#include <string>

#include "activities/Activity.h"

// Settings › Ask the book › Pair this reader. Brings Wi-Fi up, asks the relay
// for a code, shows QR + code, polls until the browser claims it, stores the
// token. Restarts on exit like the other network-side settings screens so the
// heap TLS borrowed goes back to the reader.
class InkAgentPairActivity final : public Activity {
 public:
  explicit InkAgentPairActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("InkAgentPair", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == State::PairStart || state == State::PairWait; }

 private:
  enum class State : uint8_t { Wifi, PairStart, PairWait, Done, Error };
  State state = State::Wifi;
  inkagent::PairStart pairing{};
  unsigned long nextPollAt = 0;
  unsigned long pairingDeadline = 0;
  bool wifiActivated = false;
  std::string statusMessage;

  void onWifiSelectionComplete(bool success);
  void startPairing();
  void pollPairing();
  void fail(const char* msg);
  void renderPairing();
  void renderStatus(const char* msg);
};
