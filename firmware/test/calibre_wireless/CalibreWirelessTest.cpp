#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "CalibreWireless/CalibreWireless.h"

// Calibre's side of the conversation, framed the way driver.py frames it: the
// JSON's byte count in ASCII, then the JSON. Tests drive the session with these
// rather than with hand-written byte strings, so a framing change is caught in
// one place.
namespace {

std::string frame(const std::string& json) { return std::to_string(json.size()) + json; }

struct Capture {
  std::string path;
  std::string bytes;
  uint64_t announced = 0;
  bool closed = false;
  bool closedOk = false;
  bool refuse = false;
};

calibre::Session::BookSink sinkFor(Capture& c) {
  return {
      [&c](const std::string& p, uint64_t n) {
        if (c.refuse) return false;
        c.path = p;
        c.announced = n;
        return true;
      },
      [&c](const uint8_t* d, size_t n) {
        c.bytes.append(reinterpret_cast<const char*>(d), n);
        return true;
      },
      [&c](bool ok) {
        c.closed = true;
        c.closedOk = ok;
      },
  };
}

calibre::Session::Space spaceFor(uint64_t total, uint64_t free) {
  return {[total] { return total; }, [free] { return free; }};
}

// Feed a whole string to the session in `chunk`-sized pieces, so a test can
// prove the parser survives a split anywhere. TCP will split wherever it likes.
bool feedInChunks(calibre::Session& s, const std::string& data, size_t chunk) {
  for (size_t i = 0; i < data.size(); i += chunk) {
    const size_t n = std::min(chunk, data.size() - i);
    if (!s.feed(reinterpret_cast<const uint8_t*>(data.data() + i), n)) return false;
  }
  return true;
}

calibre::Session::Config config() { return {}; }

}  // namespace

TEST(CalibreBroadcast, ParsesTheDriverPortOutOfTheReply) {
  uint16_t port = 0;
  std::string host;
  const std::string reply = "calibre wireless device client (on studio);8080,9090";
  ASSERT_TRUE(calibre::parseBroadcastReply(reply.data(), reply.size(), port, host));
  EXPECT_EQ(port, 9090);
  EXPECT_EQ(host, "studio");
}

TEST(CalibreBroadcast, RejectsAnythingThatIsNotCalibre) {
  uint16_t port = 0;
  std::string host;
  const std::string noise = "SSDP NOTIFY * HTTP/1.1";
  EXPECT_FALSE(calibre::parseBroadcastReply(noise.data(), noise.size(), port, host));
}

TEST(CalibreHandshake, AnswersInitializationWithTheCapabilitiesItHonours) {
  Capture c;
  calibre::Session s(config(), sinkFor(c), spaceFor(1 << 20, 1 << 19));
  const std::string msg = frame(R"([9,{"serverProtocolVersion":1,"passwordChallenge":""}])");
  ASSERT_TRUE(feedInChunks(s, msg, 3));
  ASSERT_TRUE(s.hasOutbound());
  const std::string out = s.takeOutbound();
  EXPECT_TRUE(s.handshakeComplete());
  // The reply is framed, and the frame's length is the JSON's byte count.
  const size_t open = out.find('[');
  ASSERT_NE(open, std::string::npos);
  EXPECT_EQ(std::stoul(out.substr(0, open)), out.size() - open);
  EXPECT_NE(out.find("\"versionOK\":true"), std::string::npos);
  EXPECT_NE(out.find("\"canReceiveBookBinary\":true"), std::string::npos);
  EXPECT_NE(out.find("\"canSendOkToSendbook\":true"), std::string::npos);
  // We keep no library, so these must be declined or Calibre will ask us to
  // reconcile a book list we cannot produce.
  EXPECT_NE(out.find("\"canUseCachedMetadata\":false"), std::string::npos);
}

TEST(CalibreSpace, ReportsWhatTheCardSays) {
  Capture c;
  calibre::Session s(config(), sinkFor(c), spaceFor(4000, 1234));
  ASSERT_TRUE(feedInChunks(s, frame("[4,{}]") + frame("[5,{}]"), 5));
  const std::string out = s.takeOutbound();
  EXPECT_NE(out.find("\"total_space_on_device\":4000"), std::string::npos);
  EXPECT_NE(out.find("\"free_space_on_device\":1234"), std::string::npos);
}

TEST(CalibreSendBook, WritesTheRawBytesThatFollowTheMetadata) {
  Capture c;
  calibre::Session s(config(), sinkFor(c), spaceFor(1 << 20, 1 << 19));
  const std::string body = "EPUB-ish bytes, arbitrary and unframed";
  const std::string msg =
      frame(R"([8,{"lpath":"Meditations.epub","length":)" + std::to_string(body.size()) + "}]") + body;
  ASSERT_TRUE(feedInChunks(s, msg, 7));
  EXPECT_EQ(c.path, "Meditations.epub");
  EXPECT_EQ(c.announced, body.size());
  EXPECT_EQ(c.bytes, body);
  EXPECT_TRUE(c.closed);
  EXPECT_TRUE(c.closedOk);
  EXPECT_EQ(s.booksReceived(), 1);
  EXPECT_TRUE(s.currentBook().empty());
}

TEST(CalibreSendBook, SurvivesAByteAtATime) {
  Capture c;
  calibre::Session s(config(), sinkFor(c), spaceFor(1 << 20, 1 << 19));
  const std::string body = "a book split across every possible boundary";
  const std::string msg =
      frame(R"([8,{"lpath":"x.epub","length":)" + std::to_string(body.size()) + "}]") + body;
  ASSERT_TRUE(feedInChunks(s, msg, 1));
  EXPECT_EQ(c.bytes, body);
  EXPECT_EQ(s.booksReceived(), 1);
}

TEST(CalibreSendBook, ResumesFramingAfterTheBookEnds) {
  Capture c;
  calibre::Session s(config(), sinkFor(c), spaceFor(1 << 20, 1 << 19));
  const std::string body = "0123456789";
  // A book, then an ordinary framed message riding directly behind its last byte.
  const std::string msg =
      frame(R"([8,{"lpath":"y.epub","length":10}])") + body + frame("[5,{}]");
  ASSERT_TRUE(feedInChunks(s, msg, 4));
  EXPECT_EQ(c.bytes, body);
  const std::string out = s.takeOutbound();
  EXPECT_NE(out.find("free_space_on_device"), std::string::npos);
}

TEST(CalibreSendBook, StopsWhenTheCardRefusesTheFile) {
  Capture c;
  c.refuse = true;
  calibre::Session s(config(), sinkFor(c), spaceFor(1 << 20, 1 << 19));
  const std::string msg = frame(R"([8,{"lpath":"z.epub","length":4}])");
  EXPECT_FALSE(feedInChunks(s, msg, 5));
  ASSERT_NE(s.error(), nullptr);
  EXPECT_EQ(s.booksReceived(), 0);
}

TEST(CalibreFraming, RefusesAFrameWithNoLengthPrefix) {
  Capture c;
  calibre::Session s(config(), sinkFor(c), spaceFor(0, 0));
  const std::string msg = "[9,{}]";
  EXPECT_FALSE(feedInChunks(s, msg, 1));
  ASSERT_NE(s.error(), nullptr);
}

TEST(CalibreFraming, RefusesAPrefixThatNeverBecomesAFrame) {
  Capture c;
  calibre::Session s(config(), sinkFor(c), spaceFor(0, 0));
  const std::string junk(64, '7');
  EXPECT_FALSE(feedInChunks(s, junk, 8));
  ASSERT_NE(s.error(), nullptr);
}

TEST(CalibreUnknownOpcodes, AreAnsweredRatherThanDroppingTheConnection) {
  Capture c;
  calibre::Session s(config(), sinkFor(c), spaceFor(0, 0));
  // 21/22 are the collections opcodes, which a device with no library never needs.
  ASSERT_TRUE(feedInChunks(s, frame("[21,{}]") + frame("[12,{}]"), 6));
  EXPECT_EQ(s.error(), nullptr);
  EXPECT_FALSE(s.takeOutbound().empty());
}

TEST(CalibreSession, ReportsProgressWhileABookIsArriving) {
  Capture c;
  calibre::Session s(config(), sinkFor(c), spaceFor(1 << 20, 1 << 19));
  const std::string head = frame(R"([8,{"lpath":"big.epub","length":100}])");
  ASSERT_TRUE(s.feed(reinterpret_cast<const uint8_t*>(head.data()), head.size()));
  const std::string half(40, 'x');
  ASSERT_TRUE(s.feed(reinterpret_cast<const uint8_t*>(half.data()), half.size()));
  EXPECT_EQ(s.currentBook(), "big.epub");
  EXPECT_EQ(s.bookTotal(), 100u);
  EXPECT_EQ(s.bookReceived(), 40u);
}
