#include "InkAgentProtocol.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace inkagent {

namespace {
constexpr const char* kKindWire[] = {"explain", "summary", "who", "translate", "define"};
constexpr const char* kKindLabel[] = {"Explain this", "Story so far", "Who is this?", "Translate", "Define word"};

bool append(char* out, size_t cap, size_t& pos, const char* s) {
  const size_t n = strlen(s);
  if (pos + n >= cap) return false;
  memcpy(out + pos, s, n);
  pos += n;
  out[pos] = '\0';
  return true;
}

bool appendField(char* out, size_t cap, size_t& pos, const char* key, const char* value, bool& first) {
  if (!value || !*value) return true;
  if (!append(out, cap, pos, first ? "\"" : ",\"")) return false;
  first = false;
  if (!append(out, cap, pos, key) || !append(out, cap, pos, "\":\"")) return false;
  if (!jsonEscapeAppend(out, cap, pos, value)) return false;
  return append(out, cap, pos, "\"");
}

bool appendIntField(char* out, size_t cap, size_t& pos, const char* key, long value, bool& first) {
  char buf[48];
  snprintf(buf, sizeof(buf), "%s\"%s\":%ld", first ? "" : ",", key, value);
  first = false;
  return append(out, cap, pos, buf);
}

// Skips whitespace; returns index of first non-space.
size_t skipWs(const char* s, size_t len, size_t i) {
  while (i < len && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) i++;
  return i;
}

// Finds the value start of a top-level key. Returns len if absent.
size_t findTopLevelValue(const char* s, size_t len, const char* key) {
  const size_t klen = strlen(key);
  size_t i = skipWs(s, len, 0);
  if (i >= len || s[i] != '{') return len;
  i++;
  int depth = 0;
  while (i < len) {
    i = skipWs(s, len, i);
    if (i >= len) return len;
    if (s[i] == '}') return len;
    if (s[i] != '"') return len;
    // read key
    size_t kstart = ++i;
    while (i < len && s[i] != '"') { if (s[i] == '\\') i++; i++; }
    if (i >= len) return len;
    const bool match = (i - kstart == klen) && memcmp(s + kstart, key, klen) == 0;
    i = skipWs(s, len, i + 1);
    if (i >= len || s[i] != ':') return len;
    i = skipWs(s, len, i + 1);
    if (match) return i;
    // skip value
    if (s[i] == '"') {
      i++;
      while (i < len && s[i] != '"') { if (s[i] == '\\') i++; i++; }
      i++;
    } else if (s[i] == '{' || s[i] == '[') {
      depth = 0;
      bool inStr = false;
      for (; i < len; i++) {
        const char c = s[i];
        if (inStr) { if (c == '\\') i++; else if (c == '"') inStr = false; continue; }
        if (c == '"') inStr = true;
        else if (c == '{' || c == '[') depth++;
        else if (c == '}' || c == ']') { if (--depth == 0) { i++; break; } }
      }
    } else {
      while (i < len && s[i] != ',' && s[i] != '}') i++;
    }
    i = skipWs(s, len, i);
    if (i < len && s[i] == ',') i++;
  }
  return len;
}

void putUtf8(char* out, size_t cap, size_t& pos, uint32_t cp, bool& trunc) {
  char tmp[4]; size_t n;
  if (cp < 0x80) { tmp[0] = (char)cp; n = 1; }
  else if (cp < 0x800) { tmp[0] = (char)(0xC0 | (cp >> 6)); tmp[1] = (char)(0x80 | (cp & 0x3F)); n = 2; }
  else if (cp < 0x10000) { tmp[0] = (char)(0xE0 | (cp >> 12)); tmp[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); tmp[2] = (char)(0x80 | (cp & 0x3F)); n = 3; }
  else { tmp[0] = (char)(0xF0 | (cp >> 18)); tmp[1] = (char)(0x80 | ((cp >> 12) & 0x3F)); tmp[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); tmp[3] = (char)(0x80 | (cp & 0x3F)); n = 4; }
  if (pos + n >= cap) { trunc = true; return; }
  memcpy(out + pos, tmp, n); pos += n;
}

uint32_t hex4(const char* p) {
  uint32_t v = 0;
  for (int k = 0; k < 4; k++) {
    const char c = p[k]; v <<= 4;
    if (c >= '0' && c <= '9') v |= c - '0';
    else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
  }
  return v;
}
}  // namespace

const char* kindWire(Kind k) { return static_cast<uint8_t>(k) < static_cast<uint8_t>(Kind::Count) ? kKindWire[static_cast<uint8_t>(k)] : "explain"; }
const char* kindLabel(Kind k) { return static_cast<uint8_t>(k) < static_cast<uint8_t>(Kind::Count) ? kKindLabel[static_cast<uint8_t>(k)] : "?"; }

bool jsonEscapeAppend(char* out, size_t cap, size_t& pos, const char* src) {
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(src); *p; p++) {
    const char* rep = nullptr; char buf[8];
    switch (*p) {
      case '"': rep = "\\\""; break;
      case '\\': rep = "\\\\"; break;
      case '\n': rep = "\\n"; break;
      case '\r': rep = "\\r"; break;
      case '\t': rep = "\\t"; break;
      default:
        if (*p < 0x20) { snprintf(buf, sizeof(buf), "\\u%04x", *p); rep = buf; }
    }
    if (rep) { if (!append(out, cap, pos, rep)) return false; }
    else { if (pos + 1 >= cap) return false; out[pos++] = (char)*p; out[pos] = '\0'; }
  }
  return true;
}

size_t buildAskRequest(const AskRequest& req, char* out, size_t cap) {
  if (!req.text || cap < 16) return 0;
  size_t pos = 0; bool first = true;
  if (!append(out, cap, pos, "{")) return 0;
  if (!appendField(out, cap, pos, "kind", kindWire(req.kind), first)) return 0;
  if (!appendField(out, cap, pos, "book", req.book, first)) return 0;
  if (!appendField(out, cap, pos, "author", req.author, first)) return 0;
  if (!appendField(out, cap, pos, "chapter", req.chapter, first)) return 0;
  if (req.pct >= 0 && !appendIntField(out, cap, pos, "pct", req.pct, first)) return 0;
  if (!appendField(out, cap, pos, "arg", req.arg, first)) return 0;
  if (!appendField(out, cap, pos, "text", req.text, first)) return 0;
  if (!append(out, cap, pos, "}")) return 0;
  return pos;
}

size_t buildPairStart(const char* hwId, const char* fwVersion, uint16_t budget, char* out, size_t cap) {
  size_t pos = 0; bool first = true;
  if (!append(out, cap, pos, "{")) return 0;
  if (!appendField(out, cap, pos, "hw", hwId, first)) return 0;
  if (!appendField(out, cap, pos, "fw", fwVersion, first)) return 0;
  if (!appendIntField(out, cap, pos, "budget", budget, first)) return 0;
  if (!append(out, cap, pos, "}")) return 0;
  return pos;
}

size_t buildPairPoll(const char* deviceCode, char* out, size_t cap) {
  size_t pos = 0; bool first = true;
  if (!append(out, cap, pos, "{")) return 0;
  if (!appendField(out, cap, pos, "device_code", deviceCode, first)) return 0;
  if (!append(out, cap, pos, "}")) return 0;
  return pos;
}

bool jsonGetString(const char* s, size_t len, const char* key, char* out, size_t cap, bool* truncated) {
  if (truncated) *truncated = false;
  if (cap == 0) return false;
  out[0] = '\0';
  size_t i = findTopLevelValue(s, len, key);
  if (i >= len || s[i] != '"') return false;
  i++;
  size_t pos = 0; bool trunc = false;
  while (i < len && s[i] != '"') {
    if (trunc) { i++; continue; }  // keep scanning to validate, stop copying
    if (s[i] == '\\' && i + 1 < len) {
      i++;
      char c = s[i];
      uint32_t cp = 0;
      switch (c) {
        case 'n': cp = '\n'; break; case 't': cp = '\t'; break; case 'r': cp = '\r'; break;
        case 'b': cp = '\b'; break; case 'f': cp = '\f'; break;
        case '"': case '\\': case '/': cp = (unsigned char)c; break;
        case 'u':
          if (i + 4 < len) {
            cp = hex4(s + i + 1); i += 4;
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 < len && s[i + 1] == '\\' && s[i + 2] == 'u') {
              const uint32_t lo = hex4(s + i + 3);
              if (lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); i += 6; }
            }
          }
          break;
        default: cp = (unsigned char)c;
      }
      putUtf8(out, cap, pos, cp, trunc);
      i++;
      continue;
    }
    // raw byte; copy whole UTF-8 sequence atomically
    const unsigned char b = (unsigned char)s[i];
    size_t n = 1;
    if (b >= 0xF0) n = 4; else if (b >= 0xE0) n = 3; else if (b >= 0xC0) n = 2;
    if (i + n > len) break;
    if (pos + n >= cap) { trunc = true; i += n; continue; }
    memcpy(out + pos, s + i, n); pos += n; i += n;
  }
  out[pos] = '\0';
  if (truncated) *truncated = trunc;
  return true;
}

bool jsonGetInt(const char* s, size_t len, const char* key, long& value) {
  size_t i = findTopLevelValue(s, len, key);
  if (i >= len) return false;
  if (s[i] != '-' && (s[i] < '0' || s[i] > '9')) return false;
  char buf[24]; size_t n = 0;
  while (i < len && n < sizeof(buf) - 1 && (s[i] == '-' || (s[i] >= '0' && s[i] <= '9'))) buf[n++] = s[i++];
  buf[n] = '\0';
  value = strtol(buf, nullptr, 10);
  return true;
}

bool jsonGetBool(const char* s, size_t len, const char* key, bool& value) {
  size_t i = findTopLevelValue(s, len, key);
  if (i >= len) return false;
  if (len - i >= 4 && memcmp(s + i, "true", 4) == 0) { value = true; return true; }
  if (len - i >= 5 && memcmp(s + i, "false", 5) == 0) { value = false; return true; }
  return false;
}

AskResponse parseAskResponse(int httpStatus, const char* body, size_t len, char* textOut, size_t textCap) {
  AskResponse r;
  if (textCap) textOut[0] = '\0';
  bool trunc = false;
  const bool hasText = body && jsonGetString(body, len, "text", textOut, textCap, &trunc);
  jsonGetString(body ? body : "", len, "sid", r.sid, sizeof(r.sid));
  bool wireTrunc = false;
  if (body) jsonGetBool(body, len, "trunc", wireTrunc);
  r.truncated = trunc || wireTrunc;
  if (httpStatus >= 200 && httpStatus < 300 && hasText && textOut[0]) { r.ok = true; return r; }
  char err[24] = {0};
  if (body) jsonGetString(body, len, "error", err, sizeof(err));
  r.revoked = httpStatus == 401 && strcmp(err, "revoked") == 0;
  r.noProvider = httpStatus == 402 && strcmp(err, "no_provider") == 0;
  if (!hasText || !textOut[0]) {
    const char* fallback = httpStatus <= 0 ? "No connection to the relay." : httpStatus == 401 ? "This reader is not paired." : "The relay returned an error.";
    snprintf(textOut, textCap, "%s", fallback);
  }
  return r;
}

PairStart parsePairStart(int httpStatus, const char* body, size_t len) {
  PairStart p;
  if (httpStatus < 200 || httpStatus >= 300 || !body) return p;
  const bool a = jsonGetString(body, len, "device_code", p.deviceCode, sizeof(p.deviceCode));
  const bool b = jsonGetString(body, len, "user_code", p.userCode, sizeof(p.userCode));
  const bool c = jsonGetString(body, len, "verify_url_complete", p.verifyUrlComplete, sizeof(p.verifyUrlComplete));
  long v;
  if (jsonGetInt(body, len, "interval", v) && v > 0 && v < 120) p.intervalS = (uint16_t)v;
  if (jsonGetInt(body, len, "expires_in", v) && v > 0 && v < 7200) p.expiresInS = (uint16_t)v;
  p.ok = a && b && c && p.deviceCode[0] && p.userCode[0] && p.verifyUrlComplete[0];
  return p;
}

PairPoll parsePairPoll(int httpStatus, const char* body, size_t len) {
  PairPoll p;
  if (!body) return p;
  char st[16] = {0};
  if (!jsonGetString(body, len, "status", st, sizeof(st))) return p;
  if (strcmp(st, "pending") == 0) { p.status = PairStatus::Pending; return p; }
  if (strcmp(st, "expired") == 0) { p.status = PairStatus::Expired; return p; }
  if (strcmp(st, "denied") == 0) { p.status = PairStatus::Denied; return p; }
  if (strcmp(st, "ok") == 0 && httpStatus >= 200 && httpStatus < 300) {
    if (!jsonGetString(body, len, "device_token", p.deviceToken, sizeof(p.deviceToken)) || !p.deviceToken[0]) return p;
    jsonGetString(body, len, "device_id", p.deviceId, sizeof(p.deviceId));
    jsonGetString(body, len, "owner", p.owner, sizeof(p.owner));
    p.status = PairStatus::Ok;
  }
  return p;
}

size_t choosePassageWindow(const char* text, size_t len, size_t focus, size_t maxBytes, size_t& start) {
  if (len <= maxBytes) { start = 0; return len; }
  if (focus > len) focus = len;
  // Centre the window on focus, clamp to the text.
  size_t s = focus > maxBytes / 2 ? focus - maxBytes / 2 : 0;
  if (s + maxBytes > len) s = len - maxBytes;
  size_t e = s + maxBytes;
  // Snap start forward to the first whitespace-following byte (never inside a word or UTF-8 seq).
  if (s > 0) {
    size_t k = s;
    while (k < e && !(text[k] == ' ' || text[k] == '\n')) k++;
    if (k < e) s = k + 1;
  }
  // Snap end back to whitespace.
  if (e < len) {
    size_t k = e;
    while (k > s && !(text[k] == ' ' || text[k] == '\n')) k--;
    if (k > s) e = k;
  }
  start = s;
  return e - s;
}

void formatHwId(const uint8_t mac[6], char out[13]) {
  snprintf(out, 13, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

}  // namespace inkagent
