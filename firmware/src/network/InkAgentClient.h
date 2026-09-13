#pragma once
#include <InkAgentProtocol.h>

#include <cstddef>
#include <memory>

// Thin HTTPS layer over the portable protocol. One request at a time, one TLS
// session at a time; every buffer is sized up front from the store's budget.
class InkAgentClient {
 public:
  // Free heap required before we attempt TLS (mirrors HttpDownloader's gate).
  static constexpr uint32_t MIN_TLS_FREE_HEAP = 40000;
  static constexpr uint32_t MIN_TLS_MAX_ALLOC = 24000;
  static bool heapAllowsTls();
  // "heap 38k/21k" style summary for screens and the SD log.
  static void heapSummary(char* out, size_t cap);
  // Appends one line to /.inkagent/inkagent.log (best effort; USB logging
  // drops out on this board as soon as Wi-Fi starts, so this is the record).
  static void sdLog(const char* line);

  static inkagent::PairStart pairStart(uint16_t budget);
  static inkagent::PairPoll pairPoll(const char* deviceCode);

  // textOut must hold budget+1 bytes. Returns the parsed response; textOut
  // always contains something renderable, success or not.
  static inkagent::AskResponse ask(const inkagent::AskRequest& req, char* textOut, size_t textCap);

  // Result of an Engage turn. The screen itself is never inspected here: on
  // success it has already been written to the cache file, and the renderer is
  // the only thing that reads it.
  struct EngageResult {
    bool ok = false;
    bool revoked = false;     // 401 -> wipe token, re-pair
    bool noProvider = false;  // 402 -> owner must connect a model
  };

  // Fetches a screen and caches it at cachePath, replacing what is there only
  // on success: a failed turn must never blank an ambient surface that is
  // already showing something useful.
  // textOut receives the question in plain form for immediate display; the
  // screen itself is cached opaquely for the renderer.
  static EngageResult engage(const inkagent::EngageRequest& req, const char* cachePath, char* textOut, size_t textCap);

  // Where the sleep canvas looks for the last screen the agent composed.
  static constexpr const char* ENGAGE_CACHE = "/.inkagent/engage.json";

  static int lastHttpCode;
  static void hardwareId(char out[13]);
};
