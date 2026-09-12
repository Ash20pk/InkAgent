#include "InkAgentClient.h"

#include <Arduino.h>
#include <InkAgentStore.h>
#include <Logging.h>
#include <SecureHttpClient.h>
#include <esp_mac.h>
#include <HalStorage.h>
#include <WiFi.h>

#include <string>

int InkAgentClient::lastHttpCode = 0;

namespace {
constexpr uint32_t kTimeoutMs = 45000;

void commonHeaders(freeink::SecureHttpClient& http, bool withToken) {
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  http.addHeader("X-Ink-Proto", "1");
  http.setUserAgent("InkAgent/" INKAGENT_VERSION " (ESP32-C3)");
  if (withToken) http.addHeader("X-Ink-Device", INKAGENT_STORE.getDeviceToken());
}

// POSTs `body` to relay + path. Returns HTTP status (<=0 on transport error)
// and leaves the response body in `out` (bounded by SecureHttpClient).
int post(const char* path, const char* body, size_t len, bool withToken, std::string& out) {
  freeink::SecureHttpClient http;
  http.setInsecure();  // TODO(release): pin the relay's CA once the relay has a stable cert
  http.setTimeout(kTimeoutMs);
  const std::string url = INKAGENT_STORE.getRelayUrl() + path;
  char heap[48];
  if (!http.begin(url)) {
    InkAgentClient::heapSummary(heap, sizeof(heap));
    char line[200];
    snprintf(line, sizeof(line), "begin failed %s %s", url.c_str(), heap);
    LOG_ERR("INKA", "%s", line);
    InkAgentClient::sdLog(line);
    return -1;
  }
  commonHeaders(http, withToken);
  int code = http.sendRequest("POST", reinterpret_cast<const uint8_t*>(body), len);
  if (code <= 0) {
    // One retry: the first TLS handshake after Wi-Fi comes up fails now and then.
    InkAgentClient::heapSummary(heap, sizeof(heap));
    LOG_ERR("INKA", "%s -> %d, retrying (%s)", path, code, heap);
    http.end();
    delay(800);
    if (http.begin(url)) {
      commonHeaders(http, withToken);
      code = http.sendRequest("POST", reinterpret_cast<const uint8_t*>(body), len);
    }
  }
  out = http.getString();
  http.end();
  InkAgentClient::lastHttpCode = code;
  InkAgentClient::heapSummary(heap, sizeof(heap));
  char line[200];
  snprintf(line, sizeof(line), "%s -> %d (%u B) %s", path, code, (unsigned)out.size(), heap);
  LOG_DBG("INKA", "%s", line);
  if (code <= 0 || code >= 400) InkAgentClient::sdLog(line);
  return code;
}
}  // namespace

bool InkAgentClient::heapAllowsTls() {
  return ESP.getFreeHeap() >= MIN_TLS_FREE_HEAP && ESP.getMaxAllocHeap() >= MIN_TLS_MAX_ALLOC;
}

void InkAgentClient::heapSummary(char* out, size_t cap) {
  snprintf(out, cap, "heap %uk/%uk wifi %d", (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getMaxAllocHeap() / 1024),
           (int)WiFi.status());
}

void InkAgentClient::sdLog(const char* line) {
  // Read-modify-write keeps the last ~4 KB: HalStorage has no append mode.
  std::string prev;
  {
    HalFile in;
    if (Storage.openFileForRead("INKA", "/.inkagent/inkagent.log", in)) {
      char buf[256];
      int n;
      while ((n = in.read(reinterpret_cast<uint8_t*>(buf), sizeof(buf))) > 0) prev.append(buf, n);
      in.close();
    }
  }
  if (prev.size() > 4096) prev.erase(0, prev.size() - 4096);
  char stamp[24];
  snprintf(stamp, sizeof(stamp), "[%lu] ", (unsigned long)millis());
  prev += stamp;
  prev += line;
  prev += "\n";
  HalFile out;
  if (Storage.openFileForWrite("INKA", "/.inkagent/inkagent.log", out)) {
    out.write(reinterpret_cast<const uint8_t*>(prev.data()), prev.size());
    out.close();
  }
}

void InkAgentClient::hardwareId(char out[13]) {
  uint8_t mac[6] = {0};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
    LOG_ERR("INKA", "esp_read_mac failed");
  }
  inkagent::formatHwId(mac, out);
}

inkagent::PairStart InkAgentClient::pairStart(uint16_t budget) {
  char hw[13];
  hardwareId(hw);
  char body[160];
  const size_t n = inkagent::buildPairStart(hw, INKAGENT_VERSION, budget, body, sizeof(body));
  std::string resp;
  const int code = n ? post("/v1/pair/start", body, n, false, resp) : -1;
  return inkagent::parsePairStart(code, resp.c_str(), resp.size());
}

inkagent::PairPoll InkAgentClient::pairPoll(const char* deviceCode) {
  char body[128];
  const size_t n = inkagent::buildPairPoll(deviceCode, body, sizeof(body));
  std::string resp;
  const int code = n ? post("/v1/pair/poll", body, n, false, resp) : -1;
  return inkagent::parsePairPoll(code, resp.c_str(), resp.size());
}

inkagent::AskResponse InkAgentClient::ask(const inkagent::AskRequest& req, char* textOut, size_t textCap) {
  // Request body lives on the heap for its short life: ~2.6 KB is too much for
  // the activity task's stack under the project's 256-byte-local rule.
  std::unique_ptr<char[]> body(new (std::nothrow) char[inkagent::kAskRequestCap]);
  if (!body) {
    return inkagent::parseAskResponse(-1, nullptr, 0, textOut, textCap);
  }
  const size_t n = inkagent::buildAskRequest(req, body.get(), inkagent::kAskRequestCap);
  std::string resp;
  const int code = n ? post("/v1/ask", body.get(), n, true, resp) : -1;
  body.reset();
  return inkagent::parseAskResponse(code, resp.c_str(), resp.size(), textOut, textCap);
}
