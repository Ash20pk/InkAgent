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

  static int lastHttpCode;
  static void hardwareId(char out[13]);
};
