#pragma once

#include <InkAgentProtocol.h>

#include <string>

// Relay traffic, off the UI thread.
//
// Everything the relay does takes seconds: a Wi-Fi association, a TLS
// handshake, a model call. Doing that on the activity task freezes the screen
// for as long as it lasts, which is why the Engage question and app sync were
// user-initiated actions with a visible status. This owns that work instead.
//
// Three rules make it safe on a device with one CPU and 380 KB:
//
//   1. It never touches the renderer, an Activity, or anything an Activity
//      owns. Results are written to SD; the UI picks them up the next time it
//      draws. There are no callbacks to outlive a deleted activity.
//   2. It waits for the heap rather than assuming it. A job submitted as the
//      reader exits would otherwise open TLS while the EPUB is still resident.
//   3. It serialises with foreground relay calls, so an Ask in progress and a
//      background fetch can never hold two TLS sessions at once.
//
// Wi-Fi is brought up only if it is not already up, and taken back down only
// if this task was the one that raised it.
class RelayTask {
 public:
  // Creates the task and its queue. Call once from setup().
  static void begin();

  // Queues a question about the passage the reader just left. Copies
  // everything it needs, so the caller may be destroyed immediately after.
  // Returns false when the feature is off, the device is unpaired, the queue
  // is full, or it ran too recently.
  static bool submitRecall(const inkagent::EngageRequest& req);

  // Queues an app sync. Same gating, minus the throttle: the owner changing
  // their apps is a deliberate act and should not wait out a timer.
  static bool submitAppSync();

  // True while a job is running. The foreground relay paths check this so a
  // user-initiated Ask reports honestly rather than queuing behind a fetch.
  static bool busy();

  // Held for the length of any relay call, foreground or background. Public so
  // the interactive paths can take it too — one TLS session at a time is an
  // invariant of the whole firmware, not of this file.
  static bool acquireRelay(uint32_t waitMs);
  static void releaseRelay();
};
