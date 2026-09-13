#include "Manifest.h"

#include <ArduinoJson.h>
#include <Logging.h>

#include <cstring>

#include "DataSource.h"

namespace engage {
namespace {

// A field is either a literal string or {"src": "<whitelisted name>"}. Anything
// else resolves to empty rather than failing the screen.
void resolveField(JsonVariantConst field, const GfxRenderer& renderer, char* out, size_t cap) {
  out[0] = '\0';
  if (field.is<const char*>()) {
    const char* literal = field.as<const char*>();
    if (literal != nullptr) snprintf(out, cap, "%s", literal);
    return;
  }
  if (field.is<JsonObjectConst>()) {
    const char* src = field["src"].as<const char*>();
    if (!resolveSource(src, renderer, out, cap)) {
      LOG_ERR("ENGAGE", "unknown source: %s", src != nullptr ? src : "(null)");
    }
  }
}

RowKind kindFromName(const char* name) {
  if (name == nullptr) return RowKind::Text;
  if (strcmp(name, "para") == 0) return RowKind::Para;
  if (strcmp(name, "kv") == 0) return RowKind::Kv;
  if (strcmp(name, "rule") == 0) return RowKind::Rule;
  if (strcmp(name, "logo") == 0) return RowKind::Logo;
  return RowKind::Text;
}

}  // namespace

bool parseScreen(const char* json, const size_t len, const GfxRenderer& renderer, Screen& out) {
  out.rowCount = 0;
  out.title[0] = '\0';
  out.paraUsed = 0;

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, json, len);
  if (err) {
    LOG_ERR("ENGAGE", "manifest parse failed: %s", err.c_str());
    return false;
  }

  resolveField(doc["title"], renderer, out.title, sizeof(out.title));

  JsonArrayConst rows = doc["rows"].as<JsonArrayConst>();
  for (JsonVariantConst entry : rows) {
    if (out.rowCount >= kMaxRows) {
      LOG_ERR("ENGAGE", "manifest exceeds %d rows, extra dropped", kMaxRows);
      break;
    }
    Row& row = out.rows[out.rowCount++];
    row.kind = kindFromName(entry["kind"].as<const char*>());
    row.bold = entry["bold"].as<bool>();
    row.centered = entry["center"].as<bool>();
    const int gap = entry["gapAfter"].is<int>() ? entry["gapAfter"].as<int>() : 1;
    row.gapAfter = static_cast<uint8_t>(gap < 0 ? 0 : (gap > 4 ? 4 : gap));

    switch (row.kind) {
      case RowKind::Text:
        resolveField(entry["text"], renderer, row.a, sizeof(row.a));
        resolveField(entry["prefix"], renderer, row.prefix, sizeof(row.prefix));
        break;
      case RowKind::Kv:
        resolveField(entry["label"], renderer, row.a, sizeof(row.a));
        resolveField(entry["value"], renderer, row.b, sizeof(row.b));
        break;
      case RowKind::Para: {
        // Resolved straight into the pool so a sentence is never copied through
        // a 64-byte field on its way there.
        const size_t avail = kMaxParaBytes > out.paraUsed ? kMaxParaBytes - out.paraUsed : 0;
        if (avail <= 1) {
          LOG_ERR("ENGAGE", "paragraph pool full, row dropped");
          out.rowCount--;
          break;
        }
        char* slot = out.paraPool + out.paraUsed;
        resolveField(entry["text"], renderer, slot, avail);
        const size_t len = strlen(slot);
        row.paraOffset = out.paraUsed;
        row.paraLen = static_cast<uint16_t>(len);
        out.paraUsed = static_cast<uint16_t>(out.paraUsed + len + 1);
        const int maxLines = entry["maxLines"].is<int>() ? entry["maxLines"].as<int>() : 4;
        row.maxLines = static_cast<uint8_t>(maxLines < 1 ? 1 : (maxLines > 8 ? 8 : maxLines));
        break;
      }
      case RowKind::Rule:
      case RowKind::Logo:
        break;
    }
  }
  // doc dies here: nothing below this point allocates.
  return true;
}

}  // namespace engage
