#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>

#ifndef INKAGENT_DEFAULT_RELAY
#define INKAGENT_DEFAULT_RELAY "https://relay.inkagent.dev"
#endif

// What the device remembers about its relay: the relay URL, the device token
// the relay issued at pairing, and who claimed it. Lives on SD under
// /.inkagent/inkagent.json like every other InkAgent store. The token is
// obfuscated with the hardware key the same way KOReader passwords are.
class InkAgentStore : public PersistableStore<InkAgentStore> {
 private:
  std::string relayUrl;
  std::string deviceToken;
  std::string deviceId;
  std::string owner;
  uint16_t budget = 1536;
  bool loaded = false;

  InkAgentStore() = default;
  ~InkAgentStore() = default;
  friend class PersistableStore<InkAgentStore>;

 public:
  static const char* getFilePath() { return "/.inkagent/inkagent.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Relay base URL without trailing slash; falls back to the build default.
  std::string getRelayUrl() const;
  void setRelayUrl(const std::string& url);

  // Load from SD once per boot; callers that only read (reader menu) use this.
  void ensureLoaded() {
    if (!loaded) {
      loadFromFile();
      loaded = true;
    }
  }
  bool isPaired() const { return !deviceToken.empty(); }
  const std::string& getDeviceToken() const { return deviceToken; }
  const std::string& getOwner() const { return owner; }
  uint16_t getBudget() const { return budget; }
  void setPaired(const std::string& token, const std::string& id, const std::string& ownerEmail);
  void clearPairing();
};

#define INKAGENT_STORE InkAgentStore::getInstance()
