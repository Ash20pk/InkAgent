#include "CalibreWireless.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace calibre {
namespace {

// Calibre's broadcast reply: "calibre wireless device client (on HOST);CONTENT,PORT".
// The driver port is the last comma-separated field; the content-server port
// before it is for its web UI and is not ours to use.
constexpr char kClientString[] = "calibre wireless device client";

void appendEscaped(std::string& out, const char* s) {
  for (const char* p = s; *p; ++p) {
    switch (*p) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        // Control characters must be escaped; everything else (UTF-8 included)
        // passes through as bytes.
        if (static_cast<unsigned char>(*p) < 0x20) {
          char buf[7];
          snprintf(buf, sizeof(buf), "\\u%04x", *p);
          out += buf;
        } else {
          out += *p;
        }
    }
  }
}

void kv(std::string& out, const char* key, const char* value, bool first = false) {
  if (!first) out += ',';
  out += '"'; appendEscaped(out, key); out += "\":\"";
  appendEscaped(out, value); out += '"';
}
void kvRaw(std::string& out, const char* key, const char* raw, bool first = false) {
  if (!first) out += ',';
  out += '"'; appendEscaped(out, key); out += "\":";
  out += raw;
}
void kvNum(std::string& out, const char* key, long long value, bool first = false) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%lld", value);
  kvRaw(out, key, buf, first);
}
void kvBool(std::string& out, const char* key, bool value, bool first = false) {
  kvRaw(out, key, value ? "true" : "false", first);
}

}  // namespace

bool parseBroadcastReply(const char* reply, const size_t len, uint16_t& port, std::string& hostLabel) {
  const std::string s(reply, len);
  if (s.compare(0, strlen(kClientString), kClientString) != 0) return false;
  const size_t semi = s.rfind(';');
  if (semi == std::string::npos) return false;
  const size_t comma = s.find(',', semi);
  if (comma == std::string::npos) return false;
  const long p = strtol(s.c_str() + comma + 1, nullptr, 10);
  if (p <= 0 || p > 65535) return false;
  port = static_cast<uint16_t>(p);
  // The parenthesised "(on HOST)" is what a person recognises on the screen.
  const size_t open = s.find("(on ");
  const size_t close = s.find(')', open == std::string::npos ? 0 : open);
  hostLabel = (open != std::string::npos && close != std::string::npos && close > open + 4)
                  ? s.substr(open + 4, close - open - 4)
                  : std::string();
  return true;
}

Session::Session(const Config& config, BookSink bookSink, Space storage)
    : cfg(config), sink(std::move(bookSink)), space(std::move(storage)) {}

void Session::fail(const char* why) {
  if (errorText.empty()) errorText = why;
}

void Session::reply(const std::string& json) {
  // Framing: the byte count of the JSON, in ASCII, then the JSON itself.
  char prefix[24];
  snprintf(prefix, sizeof(prefix), "%zu", json.size());
  outbound += prefix;
  outbound += json;
}

std::string Session::takeOutbound() {
  std::string out;
  out.swap(outbound);
  return out;
}

// --- JSON field extraction ---------------------------------------------------

void Session::onArrayStart(void* ctx) {
  auto* s = static_cast<Session*>(ctx);
  s->sawOpcode = false;  // the opcode is the first number inside the outer array
}

void Session::onKey(void* ctx, const char* key, const size_t len) {
  static_cast<Session*>(ctx)->lastKey.assign(key, len);
}

void Session::onString(void* ctx, const char* value, const size_t len) {
  auto* s = static_cast<Session*>(ctx);
  if (s->lastKey == "lpath") s->lpath.assign(value, len);
  else if (s->lastKey == "passwordChallenge") s->passwordChallenge.assign(value, len);
  s->lastKey.clear();
}

void Session::onNumber(void* ctx, const char* value, const size_t len) {
  auto* s = static_cast<Session*>(ctx);
  const std::string text(value, len);
  if (!s->sawOpcode && s->lastKey.empty()) {
    s->opcode = static_cast<int>(strtol(text.c_str(), nullptr, 10));
    s->sawOpcode = true;
  } else if (s->lastKey == "length") {
    s->declaredLength = strtoull(text.c_str(), nullptr, 10);
  }
  s->lastKey.clear();
}

void Session::onBool(void* ctx, bool) { static_cast<Session*>(ctx)->lastKey.clear(); }

// --- the conversation --------------------------------------------------------

void Session::dispatch(const int op) {
  std::string j;
  switch (static_cast<Op>(op)) {
    case Op::GET_INITIALIZATION_INFO: {
      // The capability reply. Calibre reads these to decide what it may ask for
      // next, so each one is a promise about what the code below actually does.
      j = "[0,{";
      kvBool(j, "versionOK", true, true);
      kvNum(j, "maxBookContentPacketLen", cfg.maxBookContentPacketLen);
      kvBool(j, "canStreamBooks", true);
      kvBool(j, "canStreamMetadata", true);
      kvBool(j, "canReceiveBookBinary", true);
      // We keep no library of our own to diff against, so every one of these is
      // declined rather than half-answered: Calibre then sends whole books and
      // never asks us to reconcile a book list.
      kvBool(j, "canDeleteMultipleBooks", false);
      kvBool(j, "canUseCachedMetadata", false);
      kvBool(j, "cacheUsesLpaths", false);
      kvBool(j, "canSendOkToSendbook", true);
      kvBool(j, "canAcceptLibraryInfo", true);
      kvBool(j, "willAskForUpdateBooks", false);
      kvBool(j, "setTempMarkWhenReadInfoSynced", false);
      kvBool(j, "useUuidFileNames", false);
      kv(j, "deviceKind", cfg.deviceKind);
      kv(j, "deviceName", cfg.deviceName);
      kv(j, "appName", cfg.appName);
      kv(j, "ccVersionNumber", cfg.version);
      kvRaw(j, "acceptedExtensions", (std::string("[") + cfg.acceptedExtensions + "]").c_str());
      kvRaw(j, "extensionPathLengths", "{}");
      kvNum(j, "coverHeight", 0);
      kvNum(j, "coverWidth", 0);
      // Password support is deliberately absent: answering a challenge means
      // SHA1 over the shared secret, and a reader that cannot show a password
      // field has nowhere to put the secret. An empty hash against a Calibre
      // that wants one fails loudly at the far end, which is the honest outcome.
      kv(j, "passwordHash", "");
      j += "}]";
      handshaken = true;
      break;
    }

    case Op::GET_DEVICE_INFORMATION: {
      j = "[0,{\"device_info\":{";
      kv(j, "device_store_uuid", "inkagent-sd", true);
      kv(j, "device_name", cfg.deviceName);
      j += "},";
      kv(j, "version", cfg.version, true);
      kv(j, "device_version", cfg.version);
      j += "}]";
      break;
    }

    case Op::TOTAL_SPACE:
      j = "[0,{";
      kvNum(j, "total_space_on_device", space.total ? static_cast<long long>(space.total()) : 0, true);
      j += "}]";
      break;

    case Op::FREE_SPACE:
      j = "[0,{";
      kvNum(j, "free_space_on_device", space.free ? static_cast<long long>(space.free()) : 0, true);
      j += "}]";
      break;

    case Op::GET_BOOK_COUNT:
      // Zero books, willStream off: we told Calibre we keep no cached metadata,
      // so it has nothing to reconcile and goes straight to sending.
      j = "[0,{";
      kvNum(j, "count", 0, true);
      kvBool(j, "willStream", true);
      kvBool(j, "willScan", true);
      j += "}]";
      break;

    case Op::SEND_BOOK: {
      // The metadata frame. The book's bytes follow it raw and unframed, so the
      // OK goes out first and the socket switches to counting them.
      if (lpath.empty()) { fail("SEND_BOOK without an lpath"); return; }
      bookPath = lpath;
      bookBytes = declaredLength;
      bookSoFar = 0;
      bookOpen = sink.open ? sink.open(bookPath, bookBytes) : false;
      if (!bookOpen) { fail("could not open the destination file"); return; }
      phase = Phase::BOOK_BYTES;
      j = "[0,{";
      kvNum(j, "bookStarted", 1, true);
      j += "}]";
      break;
    }

    case Op::SET_CALIBRE_DEVICE_INFO:
    case Op::SET_CALIBRE_DEVICE_NAME:
    case Op::SET_LIBRARY_INFO:
    case Op::SEND_BOOK_METADATA:
    case Op::SEND_BOOKLISTS:
    case Op::BOOK_DONE:
    case Op::NOOP:
      j = "[0,{}]";
      break;

    case Op::DISPLAY_MESSAGE:
    case Op::CALIBRE_BUSY:
      j = "[0,{}]";
      break;

    case Op::ERROR:
      fail("Calibre reported an error");
      return;

    default:
      // An opcode we do not implement still gets a well-formed OK: refusing it
      // would drop the connection, and the ones left over (collections, segment
      // reads, deletes) are all things a device with no library never needs.
      j = "[0,{}]";
      break;
  }
  reply(j);
}

bool Session::feed(const uint8_t* data, const size_t len) {
  size_t i = 0;
  while (i < len) {
    if (phase == Phase::BOOK_BYTES) {
      const uint64_t remaining = bookBytes - bookSoFar;
      size_t take = len - i;
      if (static_cast<uint64_t>(take) > remaining) take = static_cast<size_t>(remaining);
      if (take > 0) {
        if (sink.write && !sink.write(data + i, take)) {
          if (sink.close) sink.close(false);
          bookOpen = false;
          fail("the card refused the write");
          return false;
        }
        bookSoFar += take;
        i += take;
      }
      if (bookSoFar >= bookBytes) {
        if (sink.close) sink.close(true);
        bookOpen = false;
        bookCount++;
        bookPath.clear();
        bookBytes = bookSoFar = 0;
        phase = Phase::FRAME;
      }
      continue;
    }

    // Framing: ASCII digits, then a JSON array of exactly that many bytes.
    inbound += static_cast<char>(data[i++]);
    const size_t open = inbound.find('[');
    if (open == std::string::npos) {
      // Guard against a peer that never sends a '[': the length prefix for any
      // real message is a handful of digits.
      if (inbound.size() > 24) { fail("no frame marker in the length prefix"); return false; }
      continue;
    }
    if (open == 0) { fail("frame had no length prefix"); return false; }
    const size_t total = static_cast<size_t>(strtoul(inbound.substr(0, open).c_str(), nullptr, 10));
    if (total == 0) { fail("frame length was not a number"); return false; }
    if (inbound.size() - open < total) continue;  // more bytes still to come

    const std::string json = inbound.substr(open, total);
    inbound.erase(0, open + total);

    opcode = -1;
    sawOpcode = false;
    lastKey.clear();
    lpath.clear();
    declaredLength = 0;

    JsonCallbacks cb{};
    cb.ctx = this;
    cb.onKey = &Session::onKey;
    cb.onString = &Session::onString;
    cb.onNumber = &Session::onNumber;
    cb.onBool = &Session::onBool;
    cb.onArrayStart = &Session::onArrayStart;
    StreamingJsonParser parser(cb);
    parser.feed(json.c_str(), json.size());
    if (parser.hasError() || !sawOpcode) { fail("could not parse a message from Calibre"); return false; }

    dispatch(opcode);
    if (!errorText.empty()) return false;
  }
  return true;
}

}  // namespace calibre
