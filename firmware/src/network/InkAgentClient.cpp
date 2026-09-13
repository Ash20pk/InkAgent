#include "InkAgentClient.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <InkAgentStore.h>
#include <Logging.h>
#include <SecureClient.h>
#include <SecureHttpClient.h>
#include <WiFi.h>
#include <esp_mac.h>

#include <string>
#include <vector>

#include "RelayCa.h"
#include "engage/AppCatalog.h"

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

// Sends `method` to relay + path. Returns HTTP status (<=0 on transport error)
// and leaves the response body in `out` (bounded by SecureHttpClient).
int request(const char* method, const char* path, const char* body, size_t len, bool withToken, std::string& out) {
  freeink::SecureHttpClient http;
#if INKAGENT_RELAY_INSECURE
  // Escape hatch for pointing a dev build at a relay with a self-signed cert.
  // Never set in a shipped build.
  http.setInsecure();
#else
  http.setCACert(inkagent::kRelayRootCAs);
#endif
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
  {
    char pre[64];
    snprintf(pre, sizeof(pre), "%s connect: free %uk max %uk", path, (unsigned)(ESP.getFreeHeap() / 1024),
             (unsigned)(ESP.getMaxAllocHeap() / 1024));
    InkAgentClient::sdLog(pre);
  }
  int code = http.sendRequest(method, reinterpret_cast<const uint8_t*>(body), len);
  if (code <= 0) {
    // One retry: the first TLS handshake after Wi-Fi comes up fails now and then.
    InkAgentClient::heapSummary(heap, sizeof(heap));
    LOG_ERR("INKA", "%s -> %d, retrying (%s)", path, code, heap);
    http.end();
    delay(800);
    if (http.begin(url)) {
      commonHeaders(http, withToken);
      code = http.sendRequest(method, reinterpret_cast<const uint8_t*>(body), len);
    }
  }
  out = http.getString();
  http.end();
  InkAgentClient::lastHttpCode = code;
  InkAgentClient::heapSummary(heap, sizeof(heap));
  char line[200];
  snprintf(line, sizeof(line), "%s -> %d (%u B) %s", path, code, (unsigned)out.size(), heap);
  LOG_DBG("INKA", "%s", line);
  if (code <= 0 || code >= 400) {
    char det[96];
    snprintf(det, sizeof(det), "%s tls stage %d err %d", path, freeink::SecureClient::lastConnectStage,
             freeink::SecureClient::lastConnectError);
    InkAgentClient::sdLog(line);
    if (code <= 0) InkAgentClient::sdLog(det);
  }
  return code;
}

int post(const char* path, const char* body, size_t len, bool withToken, std::string& out) {
  return request("POST", path, body, len, withToken, out);
}
}  // namespace

bool InkAgentClient::heapAllowsTls() {
  return ESP.getFreeHeap() >= MIN_TLS_FREE_HEAP && ESP.getMaxAllocHeap() >= MIN_TLS_MAX_ALLOC;
}

void InkAgentClient::heapSummary(char* out, size_t cap) {
  snprintf(out, cap, "heap %uk/%uk wifi %d", (unsigned)(ESP.getFreeHeap() / 1024),
           (unsigned)(ESP.getMaxAllocHeap() / 1024), (int)WiFi.status());
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

InkAgentClient::EngageResult InkAgentClient::engage(const inkagent::EngageRequest& req, const char* cachePath,
                                                    char* textOut, const size_t textCap) {
  EngageResult out;
  if (textOut && textCap) textOut[0] = '\0';

  std::unique_ptr<char[]> body(new (std::nothrow) char[inkagent::kAskRequestCap]);
  if (!body) {
    LOG_ERR("INKA", "OOM: engage request buffer");
    return out;
  }
  const size_t n = inkagent::buildEngageRequest(req, body.get(), inkagent::kAskRequestCap);
  std::string resp;
  const int code = n ? post("/v1/engage", body.get(), n, true, resp) : -1;
  body.reset();

  // The relay sends screen text on every outcome it can, so a failure still has
  // something to put on screen rather than a bare code.
  if (textOut && textCap) {
    inkagent::jsonGetString(resp.c_str(), resp.size(), "text", textOut, textCap);
  }

  if (code == 401) {
    out.revoked = true;
    return out;
  }
  if (code == 402) {
    out.noProvider = true;
    return out;
  }
  if (code < 200 || code >= 300) return out;

  // Cache the screen verbatim. The device does not parse it here: the renderer
  // is the only thing that understands a manifest, and keeping it opaque on
  // this path means a relay that grows a row kind needs no change up here.
  const char* screen = nullptr;
  size_t screenLen = 0;
  if (!inkagent::jsonGetRawObject(resp.c_str(), resp.size(), "screen", &screen, &screenLen) || screenLen == 0) {
    LOG_ERR("INKA", "engage: no screen in response");
    return out;
  }

  HalFile f;
  if (!Storage.openFileForWrite("INKA", cachePath, f)) {
    LOG_ERR("INKA", "engage: cannot write %s", cachePath);
    return out;
  }
  const bool wrote = f.write(reinterpret_cast<const uint8_t*>(screen), screenLen) == static_cast<int>(screenLen);
  f.close();
  if (!wrote) {
    LOG_ERR("INKA", "engage: short write to %s", cachePath);
    // A half-written screen is worse than none: the renderer would show a
    // truncated question as though the agent meant it.
    Storage.remove(cachePath);
    return out;
  }

  out.ok = true;
  return out;
}

namespace {

// Relay-managed manifests are named so a sync can replace its own files without
// touching anything the owner copied onto the card by hand.
constexpr const char* kRelayAppPrefix = "relay-";

std::string relayAppPath(const char* id) {
  return std::string(engage::kAppsFolder) + "/" + kRelayAppPrefix + id + ".json";
}

// Removes every manifest a previous sync wrote. Hand-placed apps are left
// alone: the owner put them there, and the relay does not own them.
void clearRelayApps() {
  auto dir = Storage.open(engage::kAppsFolder);
  if (!dir || !dir.isDirectory()) return;
  char name[128];
  std::vector<std::string> doomed;
  dir.rewindDirectory();
  for (auto f = dir.openNextFile(); f; f = dir.openNextFile()) {
    f.getName(name, sizeof(name));
    if (!f.isDirectory() && strncmp(name, kRelayAppPrefix, strlen(kRelayAppPrefix)) == 0) {
      doomed.emplace_back(std::string(engage::kAppsFolder) + "/" + name);
    }
  }
  dir.close();
  // Deleted after the directory handle is closed: removing entries while
  // walking them is how a directory iterator loses its place.
  for (const auto& path : doomed) Storage.remove(path.c_str());
}

bool writeManifest(const std::string& path, const std::string& body) {
  HalFile f;
  if (!Storage.openFileForWrite("INKA", path.c_str(), f)) return false;
  const bool ok = f.write(reinterpret_cast<const uint8_t*>(body.data()), body.size()) == static_cast<int>(body.size());
  f.close();
  if (!ok) Storage.remove(path.c_str());
  return ok;
}

std::string readSyncedVersion() {
  HalFile f;
  if (!Storage.openFileForRead("INKA", InkAgentClient::APPS_VERSION_FILE, f)) return {};
  char buf[32] = {0};
  const int n = f.read(reinterpret_cast<uint8_t*>(buf), sizeof(buf) - 1);
  f.close();
  return n > 0 ? std::string(buf, static_cast<size_t>(n)) : std::string{};
}

}  // namespace

InkAgentClient::SyncResult InkAgentClient::syncApps() {
  SyncResult out;

  std::string indexBody;
  const int code = request("GET", "/v1/apps", nullptr, 0, true, indexBody);
  if (code == 401) {
    out.revoked = true;
    return out;
  }
  if (code < 200 || code >= 300) return out;

  char version[24] = {0};
  inkagent::jsonGetString(indexBody.c_str(), indexBody.size(), "version", version, sizeof(version));
  if (version[0] != '\0' && readSyncedVersion() == version) {
    out.ok = true;
    out.unchanged = true;
    return out;
  }

  // The index is small by design, so parsing it whole is affordable. The
  // manifests are not, and are never held together with it.
  std::vector<std::string> ids;
  {
    JsonDocument filter;
    filter["apps"][0]["id"] = true;
    JsonDocument doc;
    if (deserializeJson(doc, indexBody, DeserializationOption::Filter(filter))) {
      LOG_ERR("INKA", "apps index did not parse");
      return out;
    }
    for (JsonVariantConst a : doc["apps"].as<JsonArrayConst>()) {
      const char* appId = a["id"].as<const char*>();
      if (appId && appId[0] != '\0' && ids.size() < engage::kMaxCatalogApps) ids.emplace_back(appId);
    }
  }
  indexBody.clear();
  indexBody.shrink_to_fit();

  if (!Storage.exists(engage::kAppsFolder) && !Storage.mkdir(engage::kAppsFolder)) {
    LOG_ERR("INKA", "cannot create %s", engage::kAppsFolder);
    return out;
  }
  clearRelayApps();

  for (const auto& appId : ids) {
    std::string body;
    const std::string path = std::string("/v1/apps?id=") + appId;
    const int one = request("GET", path.c_str(), nullptr, 0, true, body);
    if (one < 200 || one >= 300 || body.empty() || body.size() > engage::kMaxManifestBytes) {
      LOG_ERR("INKA", "app %s: http %d, %u bytes", appId.c_str(), one, static_cast<unsigned>(body.size()));
      out.failed++;
      continue;
    }
    if (writeManifest(relayAppPath(appId.c_str()), body)) {
      out.written++;
    } else {
      out.failed++;
    }
    // body dies here, before the next fetch: one manifest resident at a time.
  }

  // The version is only recorded when everything landed. A partial sync must
  // retry next time rather than believing it is up to date.
  if (out.failed == 0 && version[0] != '\0') {
    HalFile f;
    if (Storage.openFileForWrite("INKA", APPS_VERSION_FILE, f)) {
      f.write(reinterpret_cast<const uint8_t*>(version), strlen(version));
      f.close();
    }
  }
  out.ok = out.failed == 0;
  return out;
}
