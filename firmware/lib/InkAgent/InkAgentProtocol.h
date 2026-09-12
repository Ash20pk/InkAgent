#pragma once
// InkAgent device protocol v1 — portable, no Arduino dependencies.
// Everything writes into caller-provided fixed buffers: no heap, no std::string,
// so the C3's 380 KB stays predictable while TLS is holding 40 KB of it.
#include <cstddef>
#include <cstdint>

namespace inkagent {

constexpr uint16_t kDefaultBudget = 1536;   // bytes of answer text the device can hold and show
constexpr size_t kMaxPassageBytes = 1800;   // ≤ relay limit of 2000, leaves room for escaping
constexpr size_t kAskRequestCap = 2600;     // passage + escaping + metadata

enum class Kind : uint8_t { Explain = 0, Summary, Who, Translate, Define, Count };
const char* kindWire(Kind k);              // "explain" ...
const char* kindLabel(Kind k);             // "Explain this" ... (UI fallback; real UI uses tr())

struct AskRequest {
  Kind kind = Kind::Explain;
  const char* book = nullptr;
  const char* author = nullptr;
  const char* chapter = nullptr;
  int pct = -1;                 // -1 = unknown
  const char* text = nullptr;   // passage, UTF-8, ≤ kMaxPassageBytes
  const char* arg = nullptr;    // language / headword
};

// Appends `src` JSON-escaped to out[pos..cap). Returns false if it did not fit.
bool jsonEscapeAppend(char* out, size_t cap, size_t& pos, const char* src);
// Builds the /v1/ask body. Returns bytes written, 0 if it did not fit.
size_t buildAskRequest(const AskRequest& req, char* out, size_t cap);
// Builds the /v1/pair/start body.
size_t buildPairStart(const char* hwId, const char* fwVersion, uint16_t budget, char* out, size_t cap);
// Builds the /v1/pair/poll body.
size_t buildPairPoll(const char* deviceCode, char* out, size_t cap);

// Flat-object extractor: finds top-level "key" in a JSON object and copies its
// unescaped string value into out (NUL terminated, truncated to cap-1 bytes at a
// UTF-8 boundary). Returns true if the key was present with a string value.
// `truncated` is set when the value did not fit.
bool jsonGetString(const char* json, size_t len, const char* key, char* out, size_t cap, bool* truncated = nullptr);
bool jsonGetInt(const char* json, size_t len, const char* key, long& value);
bool jsonGetBool(const char* json, size_t len, const char* key, bool& value);

struct AskResponse {
  bool ok = false;              // HTTP 2xx and text present
  bool truncated = false;       // relay cut the answer to fit (trunc:true) or we cut it locally
  bool revoked = false;         // 401 revoked → wipe token, re-pair
  bool noProvider = false;      // 402 → user must add a key on the dashboard
  char sid[24] = {0};
  // `text` is filled by the caller-provided buffer; see parseAskResponse
};
// textOut must be at least budget+1 bytes. Works for both success and error
// bodies since the relay always sends `text`.
AskResponse parseAskResponse(int httpStatus, const char* body, size_t len, char* textOut, size_t textCap);

struct PairStart {
  bool ok = false;
  char deviceCode[64] = {0};
  char userCode[12] = {0};
  char verifyUrlComplete[160] = {0};
  uint16_t intervalS = 5;
  uint16_t expiresInS = 600;
};
PairStart parsePairStart(int httpStatus, const char* body, size_t len);

enum class PairStatus : uint8_t { Pending, Ok, Expired, Denied, Error };
struct PairPoll {
  PairStatus status = PairStatus::Error;
  char deviceToken[80] = {0};
  char deviceId[32] = {0};
  char owner[64] = {0};
};
PairPoll parsePairPoll(int httpStatus, const char* body, size_t len);

// Picks the passage to send: a window of at most maxBytes around `focus`
// (a byte offset into `text`, typically the current page's proportional
// position), snapped to whitespace so we do not cut words or UTF-8 sequences.
// Returns the window length and sets `start`.
size_t choosePassageWindow(const char* text, size_t len, size_t focus, size_t maxBytes, size_t& start);

// Hardware id: 6 MAC bytes → 12 lowercase hex chars + NUL (out must be ≥13).
void formatHwId(const uint8_t mac[6], char out[13]);

}  // namespace inkagent
