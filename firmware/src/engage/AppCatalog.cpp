#include "AppCatalog.h"

#include <ArduinoJson.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

namespace engage {
namespace {
constexpr size_t NAME_BUFFER_SIZE = 128;
}

bool readManifest(const char* path, std::string& out) {
  out.clear();
  HalFile f;
  if (!Storage.openFileForRead("APPS", path, f)) return false;
  const size_t size = f.size();
  if (size == 0 || size > kMaxManifestBytes) {
    f.close();
    LOG_ERR("APPS", "%s is %u bytes, ignoring", path, static_cast<unsigned>(size));
    return false;
  }
  auto buf = makeUniqueNoThrow<char[]>(size + 1);
  if (!buf) {
    f.close();
    LOG_ERR("APPS", "OOM reading %s", path);
    return false;
  }
  const int read = f.read(reinterpret_cast<uint8_t*>(buf.get()), size);
  f.close();
  if (read <= 0) return false;
  buf[read] = '\0';
  out.assign(buf.get(), static_cast<size_t>(read));
  return true;
}

std::vector<CatalogEntry> scanApps() {
  std::vector<CatalogEntry> apps;

  // Created on first scan so the folder is already there to drop manifests
  // into over file transfer.
  if (!Storage.exists(kAppsFolder) && !Storage.mkdir(kAppsFolder)) {
    LOG_ERR("APPS", "could not create %s", kAppsFolder);
    return apps;
  }

  auto dir = Storage.open(kAppsFolder);
  if (!dir || !dir.isDirectory()) return apps;

  auto nameBuffer = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  if (!nameBuffer) {
    LOG_ERR("APPS", "OOM: name buffer");
    return apps;
  }

  dir.rewindDirectory();
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (apps.size() >= kMaxCatalogApps) break;
    file.getName(nameBuffer.get(), NAME_BUFFER_SIZE);
    if (file.isDirectory() || nameBuffer[0] == '.') continue;
    if (!FsHelpers::checkFileExtension(std::string_view{nameBuffer.get()}, ".json")) continue;

    CatalogEntry entry;
    entry.path = std::string(kAppsFolder) + "/" + nameBuffer.get();

    std::string json;
    if (!readManifest(entry.path.c_str(), json)) continue;

    // Only the tile metadata is parsed here. The screen itself is parsed when
    // the app is opened, so scanning the folder costs one small parse per app
    // rather than a whole screen's worth of fixed-size struct.
    JsonDocument filter;
    filter["name"] = true;
    filter["icon"] = true;
    JsonDocument doc;
    const auto err = deserializeJson(doc, json, DeserializationOption::Filter(filter));
    if (err) {
      LOG_ERR("APPS", "%s: %s", entry.path.c_str(), err.c_str());
      continue;
    }
    entry.name = doc["name"] | "";
    entry.icon = doc["icon"] | "";
    if (entry.name.empty()) {
      LOG_ERR("APPS", "%s has no name, skipping", entry.path.c_str());
      continue;
    }
    entry.name = entry.name.substr(0, 24);
    apps.push_back(std::move(entry));
  }
  dir.close();

  // Stable order so the grid does not reshuffle between opens.
  std::sort(apps.begin(), apps.end(), [](const CatalogEntry& a, const CatalogEntry& b) { return a.name < b.name; });
  return apps;
}

}  // namespace engage
