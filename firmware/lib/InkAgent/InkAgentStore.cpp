#include "InkAgentStore.h"

#include <Logging.h>
#include <ObfuscationUtils.h>

void InkAgentStore::toJson(JsonDocument& doc) const {
  doc["relay_url"] = relayUrl;
  doc["device_id"] = deviceId;
  doc["owner"] = owner;
  doc["budget"] = budget;
  doc["password_obf"] = obfuscation::obfuscateToBase64(deviceToken);
}

bool InkAgentStore::fromJson(JsonVariantConst doc) {
  relayUrl = doc["relay_url"] | "";
  deviceId = doc["device_id"] | "";
  owner = doc["owner"] | "";
  const int b = doc["budget"] | 1536;
  budget = static_cast<uint16_t>(b < 256 ? 256 : b > 8192 ? 8192 : b);
  bool needsResave = false;
  deviceToken = extractPassword(doc, needsResave);
  if (needsResave) requestResave();
  LOG_DBG("INKA", "Loaded store: paired=%d relay=%s", isPaired() ? 1 : 0, getRelayUrl().c_str());
  return true;
}

std::string InkAgentStore::getRelayUrl() const {
  std::string url = relayUrl.empty() ? INKAGENT_DEFAULT_RELAY : relayUrl;
  while (!url.empty() && url.back() == '/') url.pop_back();
  return url;
}

void InkAgentStore::setRelayUrl(const std::string& url) {
  std::lock_guard<std::mutex> lock(storeMutex);
  relayUrl = url;
}

void InkAgentStore::setPaired(const std::string& token, const std::string& id, const std::string& ownerEmail) {
  std::lock_guard<std::mutex> lock(storeMutex);
  deviceToken = token;
  deviceId = id;
  owner = ownerEmail;
}

void InkAgentStore::clearPairing() {
  std::lock_guard<std::mutex> lock(storeMutex);
  deviceToken.clear();
  deviceId.clear();
  owner.clear();
}
