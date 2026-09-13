#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "InkAgentProtocol.h"

using namespace inkagent;

TEST(BuildAsk, ProducesCompactJsonWithEscaping) {
  AskRequest r;
  r.kind = Kind::Explain;
  r.book = "Pride \"and\" Prejudice";
  r.author = "Austen";
  r.chapter = "3";
  r.pct = 8;
  r.text = "Line one\nLine \"two\"\ttab \\ backslash";
  char out[512];
  const size_t n = buildAskRequest(r, out, sizeof(out));
  ASSERT_GT(n, 0u);
  EXPECT_EQ(std::string(out),
            "{\"kind\":\"explain\",\"book\":\"Pride \\\"and\\\" "
            "Prejudice\",\"author\":\"Austen\",\"chapter\":\"3\",\"pct\":8,"
            "\"text\":\"Line one\\nLine \\\"two\\\"\\ttab \\\\ backslash\"}");
}

TEST(BuildAsk, OmitsUnknownFieldsAndKeepsArg) {
  AskRequest r;
  r.kind = Kind::Translate;
  r.text = "x";
  r.arg = "Japanese";
  char out[128];
  ASSERT_GT(buildAskRequest(r, out, sizeof(out)), 0u);
  EXPECT_EQ(std::string(out), "{\"kind\":\"translate\",\"arg\":\"Japanese\",\"text\":\"x\"}");
}

TEST(BuildAsk, RefusesToOverflow) {
  AskRequest r;
  r.text = "0123456789";
  char out[24];
  EXPECT_EQ(buildAskRequest(r, out, sizeof(out)), 0u);
  char big[kAskRequestCap];
  std::string passage(kMaxPassageBytes, 'a');
  r.text = passage.c_str();
  r.book = "B";
  r.author = "A";
  r.chapter = "C";
  r.pct = 50;
  EXPECT_GT(buildAskRequest(r, big, sizeof(big)), 0u) << "a max-size passage must fit the request cap";
}

TEST(BuildAsk, ControlCharsBecomeUnicodeEscapes) {
  AskRequest r;
  r.text =
      "a\x01"
      "b";
  char out[64];
  ASSERT_GT(buildAskRequest(r, out, sizeof(out)), 0u);
  EXPECT_NE(strstr(out, "\\u0001"), nullptr);
}

TEST(BuildPair, StartAndPoll) {
  char out[160];
  ASSERT_GT(buildPairStart("aabbccddeeff", "0.1.0", 1536, out, sizeof(out)), 0u);
  EXPECT_EQ(std::string(out), "{\"hw\":\"aabbccddeeff\",\"fw\":\"0.1.0\",\"budget\":1536}");
  ASSERT_GT(buildPairPoll("dc123", out, sizeof(out)), 0u);
  EXPECT_EQ(std::string(out), "{\"device_code\":\"dc123\"}");
}

TEST(JsonGet, FindsTopLevelKeysAndSkipsNested) {
  const char* j =
      R"({"a":{"text":"nested"},"list":[1,{"text":"inner"}],"n":-42,"flag":true,"text":"top \"level\" é😀\n","z":null})";
  char buf[64];
  long n;
  bool b;
  ASSERT_TRUE(jsonGetString(j, strlen(j), "text", buf, sizeof(buf)));
  EXPECT_EQ(std::string(buf), "top \"level\" é😀\n");
  ASSERT_TRUE(jsonGetInt(j, strlen(j), "n", n));
  EXPECT_EQ(n, -42);
  ASSERT_TRUE(jsonGetBool(j, strlen(j), "flag", b));
  EXPECT_TRUE(b);
  EXPECT_FALSE(jsonGetString(j, strlen(j), "missing", buf, sizeof(buf)));
  EXPECT_FALSE(jsonGetString(j, strlen(j), "n", buf, sizeof(buf))) << "int is not a string";
}

TEST(JsonGet, TruncatesAtUtf8BoundaryAndReportsIt) {
  std::string j = "{\"text\":\"" + std::string(5, 'x') + "ééé\"}";
  char buf[8];
  bool trunc = false;  // room for 7 bytes: xxxxx + one é (2 bytes)
  ASSERT_TRUE(jsonGetString(j.c_str(), j.size(), "text", buf, sizeof(buf), &trunc));
  EXPECT_TRUE(trunc);
  EXPECT_EQ(std::string(buf), "xxxxxé");
}

TEST(JsonGet, HandlesLongValuesBeyond512Bytes) {
  // The upstream StreamingJsonParser caps tokens at 511 bytes; ours must not.
  std::string body(1500, 'q');
  std::string j = "{\"sid\":\"s1\",\"text\":\"" + body + "\",\"trunc\":false}";
  char buf[2048];
  bool trunc = true;
  ASSERT_TRUE(jsonGetString(j.c_str(), j.size(), "text", buf, sizeof(buf), &trunc));
  EXPECT_FALSE(trunc);
  EXPECT_EQ(strlen(buf), 1500u);
}

TEST(JsonGet, RejectsGarbageWithoutCrashing) {
  char buf[16];
  EXPECT_FALSE(jsonGetString("", 0, "text", buf, sizeof(buf)));
  EXPECT_FALSE(jsonGetString("not json", 8, "text", buf, sizeof(buf)));
  EXPECT_FALSE(jsonGetString("{\"text\":\"unterminated", 21, "text", buf, sizeof(buf)) &&
               false);  // must return, content irrelevant
  EXPECT_FALSE(jsonGetString("{\"a\":[[[[", 9, "text", buf, sizeof(buf)));
}

TEST(ParseAsk, SuccessAndFlags) {
  const char* j = R"({"text":"Darcy is proud.","sid":"abc123","trunc":true})";
  char text[64];
  auto r = parseAskResponse(200, j, strlen(j), text, sizeof(text));
  EXPECT_TRUE(r.ok);
  EXPECT_TRUE(r.truncated);
  EXPECT_EQ(std::string(r.sid), "abc123");
  EXPECT_EQ(std::string(text), "Darcy is proud.");
}

TEST(ParseAsk, ErrorBodiesStillYieldScreenText) {
  const char* j = R"({"error":"no_provider","text":"No AI connected yet."})";
  char text[64];
  auto r = parseAskResponse(402, j, strlen(j), text, sizeof(text));
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.noProvider);
  EXPECT_FALSE(r.revoked);
  EXPECT_EQ(std::string(text), "No AI connected yet.");
  const char* rv = R"({"error":"revoked","text":"Unpaired."})";
  r = parseAskResponse(401, rv, strlen(rv), text, sizeof(text));
  EXPECT_TRUE(r.revoked);
  r = parseAskResponse(0, nullptr, 0, text, sizeof(text));
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(std::string(text), "No connection to the relay.");
  r = parseAskResponse(500, "<html>nginx</html>", 18, text, sizeof(text));
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(std::string(text), "The relay returned an error.");
}

TEST(ParseAsk, LocalTruncationWhenRelayOverflowsBudget) {
  std::string j = "{\"text\":\"" + std::string(3000, 'a') + "\"}";
  char text[1537];
  auto r = parseAskResponse(200, j.c_str(), j.size(), text, sizeof(text));
  EXPECT_TRUE(r.ok);
  EXPECT_TRUE(r.truncated);
  EXPECT_EQ(strlen(text), 1536u);
}

TEST(ParsePair, Start) {
  const char* j =
      R"({"device_code":"dc","user_code":"ABCD-EFGH","verify_url":"https://r/claim","verify_url_complete":"https://r/claim?code=ABCD-EFGH","interval":5,"expires_in":600})";
  auto p = parsePairStart(200, j, strlen(j));
  EXPECT_TRUE(p.ok);
  EXPECT_EQ(std::string(p.userCode), "ABCD-EFGH");
  EXPECT_EQ(std::string(p.verifyUrlComplete), "https://r/claim?code=ABCD-EFGH");
  EXPECT_EQ(p.intervalS, 5);
  EXPECT_EQ(p.expiresInS, 600);
  EXPECT_FALSE(parsePairStart(500, j, strlen(j)).ok);
  EXPECT_FALSE(parsePairStart(200, "{}", 2).ok);
}

TEST(ParsePair, Poll) {
  EXPECT_EQ(parsePairPoll(200, R"({"status":"pending","interval":5})", 33).status, PairStatus::Pending);
  EXPECT_EQ(parsePairPoll(400, R"({"status":"expired"})", 20).status, PairStatus::Expired);
  EXPECT_EQ(parsePairPoll(400, R"({"status":"denied"})", 19).status, PairStatus::Denied);
  const char* ok = R"({"status":"ok","device_token":"tok_123","device_id":"d1","owner":"ash@example.com"})";
  auto p = parsePairPoll(200, ok, strlen(ok));
  EXPECT_EQ(p.status, PairStatus::Ok);
  EXPECT_EQ(std::string(p.deviceToken), "tok_123");
  EXPECT_EQ(std::string(p.owner), "ash@example.com");
  EXPECT_EQ(parsePairPoll(200, R"({"status":"ok"})", 15).status, PairStatus::Error) << "ok without token is an error";
  EXPECT_EQ(parsePairPoll(0, nullptr, 0).status, PairStatus::Error);
}

TEST(PassageWindow, ShortTextIsSentWhole) {
  size_t start = 99;
  EXPECT_EQ(choosePassageWindow("hello world", 11, 5, 100, start), 11u);
  EXPECT_EQ(start, 0u);
}

TEST(PassageWindow, CentresOnFocusAndSnapsToWhitespace) {
  std::string t;
  for (int i = 0; i < 400; i++) t += "word" + std::to_string(i) + " ";
  size_t start = 0;
  const size_t n = choosePassageWindow(t.c_str(), t.size(), t.size() / 2, 300, start);
  EXPECT_LE(n, 300u);
  EXPECT_GT(n, 200u);
  EXPECT_EQ(t[start - 1], ' ') << "window starts after a space";
  EXPECT_EQ(t[start + n], ' ') << "window ends before a space";
  EXPECT_LT(start, t.size() / 2);
  EXPECT_GT(start + n, t.size() / 2);
}

TEST(PassageWindow, NeverSplitsUtf8) {
  std::string t;
  for (int i = 0; i < 300; i++) t += "日本語 ";
  size_t start = 0;
  const size_t n = choosePassageWindow(t.c_str(), t.size(), t.size() / 2, 200, start);
  EXPECT_TRUE(start == 0 || t[start - 1] == ' ');
  EXPECT_TRUE(start + n == t.size() || t[start + n] == ' ');
}

TEST(HwId, FormatsLowercaseHex) {
  const uint8_t mac[6] = {0xAA, 0xBB, 0x0C, 0xDD, 0xEE, 0x0F};
  char out[13];
  formatHwId(mac, out);
  EXPECT_EQ(std::string(out), "aabb0cddee0f");
}

// --- Engage -----------------------------------------------------------------
// The request the agent screens are fetched with, and the extractor that pulls
// a composed screen back out of the reply without parsing it.

TEST(BuildEngage, CarriesContextAndEscapesIt) {
  EngageRequest r;
  r.app = "recall";
  r.book = "Pride and \"Prejudice\"";
  r.author = "Austen";
  r.chapter = "3";
  r.pct = 8;
  r.text = "She was obliged to be uncivil.";
  char out[kAskRequestCap];
  const size_t n = buildEngageRequest(r, out, sizeof(out));
  ASSERT_GT(n, 0u);
  const std::string s(out, n);
  EXPECT_NE(s.find(R"("app":"recall")"), std::string::npos);
  EXPECT_NE(s.find(R"("book":"Pride and \"Prejudice\"")"), std::string::npos);
  EXPECT_NE(s.find(R"("pct":8)"), std::string::npos);
}

TEST(BuildEngage, OmitsFeaturesEntirelyWhenNothingWasMeasured) {
  // Zero regressions is a real reading; "not measured" is not. Sending a zero
  // would have the agent treat an unmeasured session as a smooth one.
  EngageRequest r;
  r.text = "passage";
  char out[kAskRequestCap];
  const size_t n = buildEngageRequest(r, out, sizeof(out));
  ASSERT_GT(n, 0u);
  EXPECT_EQ(std::string(out, n).find("features"), std::string::npos);
}

TEST(BuildEngage, IncludesOnlyTheFeaturesThatWereMeasured) {
  EngageRequest r;
  r.text = "passage";
  r.regressions = 0;  // measured, and genuinely zero
  char out[kAskRequestCap];
  size_t n = buildEngageRequest(r, out, sizeof(out));
  ASSERT_GT(n, 0u);
  std::string s(out, n);
  EXPECT_NE(s.find(R"("features":{"regressions":0})"), std::string::npos);
  EXPECT_EQ(s.find("speedPct"), std::string::npos);

  r.speedRatioPct = 60;
  n = buildEngageRequest(r, out, sizeof(out));
  s.assign(out, n);
  EXPECT_NE(s.find(R"("regressions":0)"), std::string::npos);
  EXPECT_NE(s.find(R"("speedPct":60)"), std::string::npos);
}

TEST(BuildEngage, RefusesToOverflow) {
  EngageRequest r;
  const std::string big(4000, 'x');
  r.text = big.c_str();
  char out[256];
  EXPECT_EQ(buildEngageRequest(r, out, sizeof(out)), 0u);
}

TEST(JsonGetRawObject, ReturnsTheScreenSpanVerbatim) {
  const std::string body =
      R"({"screen":{"title":"From where you stopped","rows":[{"kind":"text","center":true}]},"sid":"abc"})";
  const char* start = nullptr;
  size_t len = 0;
  ASSERT_TRUE(jsonGetRawObject(body.c_str(), body.size(), "screen", &start, &len));
  const std::string screen(start, len);
  EXPECT_EQ(screen.front(), '{');
  EXPECT_EQ(screen.back(), '}');
  EXPECT_NE(screen.find("From where you stopped"), std::string::npos);
  EXPECT_EQ(screen.find("sid"), std::string::npos) << "must not run past the object";
}

TEST(JsonGetRawObject, IgnoresBracesInsideStrings) {
  // The case naive brace matching gets wrong, and the one a model is most
  // likely to produce: punctuation inside the question itself.
  const std::string body = R"({"screen":{"rows":[{"text":"What did {she} say about }this{ ?"}]},"sid":"x"})";
  const char* start = nullptr;
  size_t len = 0;
  ASSERT_TRUE(jsonGetRawObject(body.c_str(), body.size(), "screen", &start, &len));
  const std::string screen(start, len);
  EXPECT_NE(screen.find("}this{"), std::string::npos);
  EXPECT_EQ(screen.back(), '}');
  EXPECT_EQ(screen.find("\"sid\""), std::string::npos);
}

TEST(JsonGetRawObject, HandlesEscapedQuotesInsideStrings) {
  const std::string body = R"({"screen":{"rows":[{"text":"a \" brace } here"}]},"after":1})";
  const char* start = nullptr;
  size_t len = 0;
  ASSERT_TRUE(jsonGetRawObject(body.c_str(), body.size(), "screen", &start, &len));
  EXPECT_EQ(std::string(start, len).back(), '}');
  EXPECT_EQ(std::string(start, len).find("after"), std::string::npos);
}

TEST(JsonGetRawObject, MissingOrUnterminatedYieldsNothing) {
  const std::string none = R"({"sid":"x"})";
  const char* start = nullptr;
  size_t len = 0;
  EXPECT_FALSE(jsonGetRawObject(none.c_str(), none.size(), "screen", &start, &len));

  const std::string truncated = R"({"screen":{"rows":[{"kind":"text")";
  EXPECT_FALSE(jsonGetRawObject(truncated.c_str(), truncated.size(), "screen", &start, &len));
  EXPECT_EQ(len, 0u);
}

TEST(JsonGetRawObject, RejectsAStringValueWhereAnObjectWasExpected) {
  const std::string body = R"({"screen":"not an object"})";
  const char* start = nullptr;
  size_t len = 0;
  EXPECT_FALSE(jsonGetRawObject(body.c_str(), body.size(), "screen", &start, &len));
}
