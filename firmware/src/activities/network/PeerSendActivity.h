#pragma once

#include <string>

#include "MappedInputManager.h"
#include "activities/Activity.h"

// Sending a book to another reader, directly.
//
// The receiving reader already knows how to do its half: File Transfer raises
// an open access point and serves an upload endpoint. So a transfer is the
// sending device joining that access point and posting the file — no server on
// this side, no pairing, no account, and no internet at any point.
//
// It is not AirDrop. AirDrop is AWDL, which is Apple's and undocumented, and
// nothing on this chip can speak it. This is the same idea reached the way this
// hardware actually can: one reader briefly becomes a network and the other
// joins it.
//
// The file is streamed from the card in small chunks. A book can be tens of
// megabytes and the device has a 380 KB heap, so it is never held in memory.
class PeerSendActivity final : public Activity {
 public:
  PeerSendActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == State::Scanning || state == State::Sending; }

 private:
  enum class State : uint8_t {
    Scanning,   // looking for a reader that is receiving
    NotFound,   // none in range
    Sending,    // joined, streaming the file
    Done,
    Failed,
  };

  std::string path;
  std::string fileName;
  State state = State::Scanning;
  std::string detail;
  int percent = 0;
  // Whether this activity raised the radio, and so should take it back down.
  bool wifiRaised = false;

  void runTransfer();
  bool sendFile();
  void renderStatus(const char* title, const char* body) const;
};
