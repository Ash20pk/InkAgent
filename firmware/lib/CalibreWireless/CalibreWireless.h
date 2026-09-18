#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "JsonParser/StreamingJsonParser.h"

// Calibre's "smart device app" wireless protocol, device side.
//
// This is the protocol Calibre's own Wireless Device driver speaks, so a reader
// running it is discovered and driven by stock Calibre with no plugin on either
// end. It replaces the arrangement where "Calibre Wireless" was really the HTTP
// upload server plus a screen telling you to go and install somebody else's
// Calibre plugin.
//
// Two things about the shape of it are worth knowing before reading on.
//
// CALIBRE LISTENS; THE DEVICE DIALS. Calibre opens a TCP socket (9090 by
// default) and waits. The device finds it by broadcasting "hi there" over UDP
// to five fixed ports and reading the reply, then opens the TCP connection
// itself. That is the reverse of every other transfer path in this firmware,
// where the reader is the server.
//
// CALIBRE DRIVES. Once connected, the device answers; it never initiates a
// command. So this class is a pure sink/source: feed it the bytes that arrived,
// take the bytes it wants sent. It owns no socket, no filesystem and no clock,
// which is what lets the whole protocol be tested on a host against a mock
// Calibre (test/calibre_wireless/) rather than only against the real thing.
//
// Framing is a decimal byte count in ASCII, immediately followed by a JSON
// array: `17[8,{"lpath":...}]`. The count covers the JSON only. A book's bytes
// are NOT framed — after a SEND_BOOK exchange, `length` raw bytes follow on the
// socket, which is why this class carries an explicit streaming state.
namespace calibre {

// driver.py: `opcodes`. Only the ones the device actually answers are named.
enum class Op : int {
  OK = 0,
  SET_CALIBRE_DEVICE_INFO = 1,
  SET_CALIBRE_DEVICE_NAME = 2,
  GET_DEVICE_INFORMATION = 3,
  TOTAL_SPACE = 4,
  FREE_SPACE = 5,
  GET_BOOK_COUNT = 6,
  SEND_BOOKLISTS = 7,
  SEND_BOOK = 8,
  GET_INITIALIZATION_INFO = 9,
  BOOK_DONE = 11,
  NOOP = 12,
  DELETE_BOOK = 13,
  GET_BOOK_FILE_SEGMENT = 14,
  GET_BOOK_METADATA = 15,
  SEND_BOOK_METADATA = 16,
  DISPLAY_MESSAGE = 17,
  CALIBRE_BUSY = 18,
  SET_LIBRARY_INFO = 19,
  ERROR = 20,
  GET_COLLECTIONS = 21,
  UPDATE_COLLECTIONS = 22,
};

// The UDP ports Calibre's broadcast listener sits on (driver.py BROADCAST_PORTS).
inline constexpr uint16_t BROADCAST_PORTS[] = {54982, 48123, 39001, 44044, 59678};
inline constexpr size_t BROADCAST_PORT_COUNT = sizeof(BROADCAST_PORTS) / sizeof(BROADCAST_PORTS[0]);
inline constexpr char BROADCAST_HELLO[] = "hi there";

// Parses Calibre's UDP reply: "calibre wireless device client (on HOST);CONTENT,PORT".
// Returns false if it is not a reply we recognise. `port` is the TCP port to dial.
bool parseBroadcastReply(const char* reply, size_t len, uint16_t& port, std::string& hostLabel);

class Session {
 public:
  // Everything the device has to tell Calibre about itself.
  struct Config {
    const char* deviceName = "InkAgent";
    const char* deviceKind = "InkAgent";
    const char* appName = "InkAgent";
    const char* version = "1.0.0";
    // Extensions we will accept a book in. Calibre converts to the first match.
    const char* acceptedExtensions = "\"epub\",\"txt\",\"md\",\"xtc\"";
    // The largest book payload chunk Calibre may push at us in one go. This is
    // the device's headroom talking: the bytes land in a small stack buffer and
    // go straight to the card, so the cap is about socket pacing, not RAM.
    int maxBookContentPacketLen = 4096;
    // Optional shared password; empty means the device accepts any challenge.
    const char* password = "";
  };

  // Where a book's bytes go. `open` returns false to refuse the transfer.
  struct BookSink {
    std::function<bool(const std::string& lpath, uint64_t totalBytes)> open;
    std::function<bool(const uint8_t* data, size_t len)> write;
    std::function<void(bool ok)> close;
  };

  // What the device reports about its storage, asked for repeatedly.
  struct Space {
    std::function<uint64_t()> total;
    std::function<uint64_t()> free;
  };

  Session(const Config& config, BookSink sink, Space space);

  // Bytes off the socket. Returns false on a protocol error; `error()` says what.
  bool feed(const uint8_t* data, size_t len);

  // Bytes to put on the socket. Cleared by takeOutbound().
  bool hasOutbound() const { return !outbound.empty(); }
  std::string takeOutbound();

  bool handshakeComplete() const { return handshaken; }
  const char* error() const { return errorText.empty() ? nullptr : errorText.c_str(); }

  // The book currently arriving, for the screen. Empty when idle.
  const std::string& currentBook() const { return bookPath; }
  uint64_t bookReceived() const { return bookSoFar; }
  uint64_t bookTotal() const { return bookBytes; }
  int booksReceived() const { return bookCount; }

 private:
  enum class Phase : uint8_t { FRAME, BOOK_BYTES };

  void dispatch(int op);
  void reply(const std::string& json);
  void fail(const char* why);

  // --- incoming message fields, filled by the JSON callbacks ---
  static void onKey(void* ctx, const char* key, size_t len);
  static void onString(void* ctx, const char* value, size_t len);
  static void onNumber(void* ctx, const char* value, size_t len);
  static void onBool(void* ctx, bool value);
  static void onArrayStart(void* ctx);

  Config cfg;
  BookSink sink;
  Space space;

  Phase phase = Phase::FRAME;
  std::string inbound;   // bytes of the frame being assembled
  std::string outbound;  // bytes waiting to go out
  std::string errorText;

  bool handshaken = false;
  int bookCount = 0;

  // Fields lifted out of the current message.
  int opcode = -1;
  bool sawOpcode = false;
  std::string lastKey;
  std::string lpath;
  std::string passwordChallenge;
  uint64_t declaredLength = 0;

  // The book in flight.
  std::string bookPath;
  uint64_t bookBytes = 0;
  uint64_t bookSoFar = 0;
  bool bookOpen = false;
};

}  // namespace calibre
