#include "CHQLogoWeb.h"
#include "CHQLogoProtocol.h"
#include "WiFi.h"

#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fake {
uint32_t now = 0;
uint32_t randomCounter = 0;
bool wifiConnected = true;
std::deque<SocketScript> scripts;
std::vector<std::string> requests;
std::string caCertificate;
unsigned long handshakeTimeout = 0;
uint16_t connectedPort = 0;
int32_t connectTimeout = 0;
bool allowStringReserve = true;
int concatBudget = -1;
void reset() {
  now = randomCounter = 0; wifiConnected = true;
  scripts.clear(); requests.clear(); caCertificate.clear();
  handshakeTimeout = 0; connectedPort = 0; connectTimeout = 0;
  allowStringReserve = true; concatBudget = -1;
}
}
WiFiClass WiFi;
uint32_t millis() { return fake::now; }
void delay(uint32_t ms) { fake::now += ms; }
uint32_t esp_random() { return ++fake::randomCounter; }

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error( \
    std::string("line ") + std::to_string(__LINE__) + ": " + #condition); } while (0)

const std::string TEMP_HINT = "0123456789ABCDEF0123456789ABCDEF";
const std::string SESSION_HINT = "ABCDEF0123456789ABCDEF0123456789";

std::string http(const std::string& body) {
  return "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}
void queue(const std::string& body) {
  fake::SocketScript script; script.response = http(body); fake::scripts.push_back(script);
}
void raw(const std::string& response, bool keepOpen = false, uint32_t byteDelay = 0) {
  fake::SocketScript script; script.response = response;
  script.keepOpen = keepOpen; script.byteDelayMs = byteDelay; fake::scripts.push_back(script);
}
void login(CHQLogoWeb& logo, const String& password = "demo123") {
  queue("700," + TEMP_HINT + ",123456");
  queue("700," + SESSION_HINT);
  CHECK(logo.login(password)); CHECK(logo.isAuthenticated());
}
std::string body(size_t index) {
  const std::string& request = fake::requests.at(index);
  const size_t from = request.find("\r\n\r\n");
  CHECK(from != std::string::npos);
  return request.substr(from + 4);
}
chqlogo::ParseResult scalar(const std::string& xml, chqlogo::ScalarReply& reply,
                           bool requireValue = true, size_t digits = 4) {
  return chqlogo::parseScalar(xml.data(), xml.size(), "v0", digits, requireValue, reply);
}
void transportFails(const std::string& response, CHQLogoError expected, bool keepOpen = false) {
  raw(response, keepOpen);
  CHQLogoConnectionOptions options; options.ioTimeoutMs = 10;
  String result = "previous"; CHQLogoDiagnostics diagnostics;
  CHECK(!chqLogoDefaultTransport().post("127.0.0.1", 80, "/AJAX", "GET", "p", options, result, diagnostics));
  CHECK(diagnostics.error == expected); CHECK(result.isEmpty());
}

class InjectedTransport : public CHQLogoTransport {
 public:
  std::deque<String> responses;
  bool success = true;
  int status = 200;
  CHQLogoError error = CHQLogoError::None;
  size_t calls = 0;
  bool post(const String&, uint16_t, const String&, const String&, const String&,
            const CHQLogoConnectionOptions&, String& response, CHQLogoDiagnostics& d) override {
    ++calls; d = CHQLogoDiagnostics{}; d.httpStatus = status; d.error = error;
    if (!responses.empty()) { response = responses.front(); responses.pop_front(); }
    return success;
  }
};

int main() {
  using chqlogo::ParseResult;
  std::vector<std::pair<const char*, std::function<void()>>> tests = {
    {"CRC32 known vectors", [] {
      CHECK(chqlogo::crc32("123456789", 9) == 0xCBF43926u);
      CHECK(chqlogo::crc32("", 0) == 0);
    }},
    {"uint32 bounds and output preservation", [] {
      uint32_t value = 99;
      CHECK(chqlogo::parseUInt32("4294967295", 10, value) && value == UINT32_MAX);
      for (const char* text : {"4294967296", "-1", "+1", "1x", "", "12345678901"}) {
        value = 99; CHECK(!chqlogo::parseUInt32(text, strlen(text), value)); CHECK(value == 99);
      }
    }},
    {"login envelopes reject extra fields/header injection", [] {
      chqlogo::Challenge challenge{};
      const std::string good = "700," + TEMP_HINT + ",4294967295\r\n";
      CHECK(chqlogo::parseChallenge(good.data(), good.size(), 700, 32, challenge));
      CHECK(challenge.value == UINT32_MAX);
      for (const auto& text : std::vector<std::string>{"700," + TEMP_HINT + ",1,2", "700," + TEMP_HINT + ",4294967296",
                               "700,short,1", "701," + TEMP_HINT + ",1"})
        CHECK(!chqlogo::parseChallenge(text.data(), text.size(), 700, 32, challenge));
      char session[65]{};
      uint16_t status = 99;
      CHECK(!chqlogo::parseStatus(nullptr,0,status) && status == 99);
      CHECK(!chqlogo::parseChallenge(nullptr,0,700,32,challenge));
      CHECK(!chqlogo::parseLogin(nullptr,0,700,32,session));
      for (const auto& text : {"700," + SESSION_HINT + ",extra", "700," + SESSION_HINT + "\r\nX: a"})
        CHECK(!chqlogo::parseLogin(text.data(), text.size(), 700, 32, session));
    }},
    {"XML whitespace, quote styles, declarations and comments", [] {
      chqlogo::ScalarReply reply{};
      CHECK(scalar("<?xml version=\"1.0\"?>\n<!-- e='5' --> <rs><r v = \"fF88\" e = '0' i='v0'></r></rs>", reply) == ParseResult::Ok);
      CHECK(reply.value == 0xFF88 && reply.hasValue);
      CHECK(scalar("<rs><r i='v0' e='0'/></rs>", reply, false) == ParseResult::Ok && !reply.hasValue);
    }},
    {"success belongs to the selected result", [] {
      chqlogo::ScalarReply reply{};
      CHECK(scalar("<rs><r i='v0' code='0' e='5' v='00FA'/></rs>", reply) == ParseResult::Rejected);
      CHECK(reply.error == 5);
      CHECK(scalar("<rs><r i='v0' e='5' v='00FA'/><r i='other' e='0' v='0000'/></rs>", reply) == ParseResult::Rejected);
      CHECK(scalar("<rs><r i='other' e='0' v='00FA'/></rs>", reply) == ParseResult::MissingResult);
      CHECK(scalar("<rs><r i='v0' e='0' v='00FA'/><r i='v0' e='0' v='0000'/></rs>", reply) == ParseResult::DuplicateResult);
    }},
    {"malformed XML/hex never succeeds", [] {
      chqlogo::ScalarReply reply{};
      for (const char* xml : {"error e='0' v='00FA'", "<rs><r i='v0' e='0' v='00FA'",
          "<rs><r i='v0' e='0' e='5' v='00FA'/></rs>", "<rs><r i='v0' e='0' v='0xFF'/></rs>",
          "<rs><r i='v0' e='0' v='1_00'/></rs>", "<rs><r i='v0' e='0' v='FA'/></rs>",
          "<rs><r i='v0' e='0' v='10000'/></rs>", "<rs><r i='v0' e='0' v='00FA'/></rs>junk",
          "<rs><r i='v0' v='00FA'/></rs>", "<!DOCTYPE rs><rs><r i='v0' e='0' v='00FA'/></rs>"})
        CHECK(scalar(xml, reply) != ParseResult::Ok);
      const std::string complete = "<rs><r i='v0' e='0' v='00FA'/></rs>";
      for (size_t n = 0; n < complete.size(); ++n) CHECK(scalar(complete.substr(0, n), reply) != ParseResult::Ok);
    }},
    {"fixed-seed malformed-input stress", [] {
      uint32_t seed = 42; chqlogo::ScalarReply reply{};
      for (unsigned n = 0; n < 10000; ++n) {
        std::string input;
        for (unsigned j = 0; j < n % 128; ++j) {
          seed = seed * 1664525u + 1013904223u;
          input += static_cast<char>(seed >> 24);
        }
        CHECK(scalar(input, reply) != ParseResult::Ok);
      }
    }},
    {"library login sends independently calculated proofs", [] {
      CHQLogoWeb logo("127.0.0.1", 8088); login(logo);
      CHECK(body(0) == "UAMCHAL:3,4,1,2,3,4");
      // Valores de referencia calculados con Python zlib, no con el CRC del cliente.
      CHECK(body(1) == "UAMLOGIN:Web User,3682121608,123460");
      CHECK(fake::requests[1].find("Security-Hint: " + TEMP_HINT) != std::string::npos);
      CHECK(fake::requests[0].find("Connection: close") != std::string::npos);
    }},
    {"long password truncation uses bytes", [] {
      CHQLogoWeb logo("127.0.0.1"); login(logo, std::string(100, 'a'));
      CHECK(body(1) == "UAMLOGIN:Web User,3400594743,123460");
    }},
    {"UTF8 password proof", [] {
      CHQLogoWeb logo("127.0.0.1"); login(logo, u8"contraseña");
      CHECK(body(1) == "UAMLOGIN:Web User,1161583543,123460");
    }},
    {"analog signed bounds and negative encoding", [] {
      CHQLogoWeb logo("127.0.0.1"); login(logo);
      for (const auto& pair : std::vector<std::pair<int32_t, const char*>>{{-32768,"8000"},{32767,"7FFF"},{-120,"FF88"}}) {
        queue(std::string("<rs><r i='v0' e='0' v='") + pair.second + "'/></rs>");
        CHECK(logo.writeAM(8, pair.first));
        CHECK(body(fake::requests.size()-1) == std::string("SETVARS:v0,20,0,7,1,1,") + pair.second);
        queue(std::string("<rs><r i='v0' e='0' v='") + pair.second + "'/></rs>");
        int16_t value = 0; CHECK(logo.readAM(8,value)); CHECK(value == pair.first);
      }
    }},
    {"no index/value narrowing and no requests for invalid arguments", [] {
      CHQLogoWeb logo("127.0.0.1"); login(logo);
      const size_t before = fake::requests.size();
      int16_t value = 123;
      for (int number : {-1,0,9,257,65537}) { CHECK(!logo.writeAM(number,250)); CHECK(!logo.readAM(number,value)); }
      CHECK(!logo.writeAM(1,32768)); CHECK(!logo.writeAM(1,-32769));
      CHECK(!logo.writeM(0,true)); CHECK(!logo.writeM(33,true));
      CHECK(fake::requests.size() == before && value == 123);
    }},
    {"digital states and non-binary rejection", [] {
      CHQLogoWeb logo("127.0.0.1"); login(logo);
      queue("<rs><r i='v0' e='0' v='01'/></rs>"); CHECK(logo.writeM(32,true));
      CHECK(body(2) == "SETVARS:v0,131,0,31,1,1,01");
      bool state = false; queue("<rs><r i='v0' e='0' v='01'/></rs>"); CHECK(logo.readM(32,state) && state);
      queue("<rs><r i='v0' e='0' v='02'/></rs>"); CHECK(!logo.readM(32,state) && state);
    }},
    {"physical profile capabilities and custom variant", [] {
      CHQLogoWeb logo("127.0.0.1"); auto p = CHQLogoProfile::logo8AjaxExperimental();
      CHECK(logo.setProfile(p)); int16_t value = 99;
      CHECK(!logo.readAM(1,value)); CHECK(logo.diagnostics().error == CHQLogoError::CapabilityDisabled);
      CHECK(!logo.writeAM(1,250)); CHECK(fake::requests.empty());
      p.name = "fixture-variant"; p.ajaxPath = "/test/AJAX"; p.webUser = "Test User";
      p.readRequestId = "read"; p.readResponseId = "result"; p.mCount = 64; p.digitalHexDigits = 4;
      p.area = 3; p.mAddressBase = 3; p.amAddressBase = 10; p.amAddressStride = 2;
      p.analogSize = 2; p.amCount = 32; p.readAnalog = true;
      CHECK(logo.setProfile(p)); login(logo);
      CHECK(body(1) == "UAMLOGIN:Test User,3682121608,123460");
      queue("<rs><r i='result' e='0' v='0001'/></rs>"); bool state = false;
      CHECK(logo.readM(64,state) && state); CHECK(body(2) == "GETVARS:read,131,3,66,1,1");
      CHECK(fake::requests[2].find("POST /test/AJAX ") == 0);
      queue("<rs><r i='result' e='0' v='00FA'/></rs>");
      CHECK(logo.readAM(32,value) && value == 250); CHECK(body(3) == "GETVARS:read,20,3,72,2,1");
      p.mAddressStride = UINT16_MAX; CHECK(!logo.setProfile(p));
    }},
    {"configuration changes invalidate session and reject injection", [] {
      CHQLogoWeb logo("127.0.0.1"); login(logo);
      auto p = logo.profile(); p.webUser = "x\r\nHeader: injected";
      CHECK(!logo.setProfile(p) && logo.isAuthenticated());
      p = logo.profile(); p.readResponseId = "x'"; CHECK(!logo.setProfile(p));
      p = logo.profile(); CHECK(logo.setProfile(p) && !logo.isAuthenticated());
      CHQLogoConnectionOptions options; options.maxResponseBytes = 1;
      CHECK(!logo.setConnectionOptions(options)); options.maxResponseBytes = 2048;
      CHECK(logo.setConnectionOptions(options));
    }},
    {"rejected/malformed scalar preserves output", [] {
      CHQLogoWeb logo("127.0.0.1"); login(logo); int16_t value = 123;
      queue("<rs><r i='v0' e='5'/></rs>"); CHECK(!logo.readAM(1,value));
      CHECK(value == 123 && logo.diagnostics().protocolStatus == 5 && logo.isAuthenticated());
      queue("<rs><r i='v0' e='0' v='00FA'"); CHECK(!logo.readAM(1,value));
      CHECK(value == 123 && !logo.isAuthenticated());
    }},
    {"HTTP 200 authentication errors clear session and relogin works", [] {
      CHQLogoWeb logo("127.0.0.1"); login(logo); bool state = true;
      queue("701,invalid session"); CHECK(!logo.readM(1,state)); CHECK(!logo.isAuthenticated());
      CHECK(logo.diagnostics().protocolStatus == 701 && state);
      login(logo); queue("<rs><r i='v0' e='0' v='00'/></rs>"); CHECK(logo.readM(1,state) && !state);
    }},
    {"HTTP 401/403 and WiFi loss clear session without logging body", [] {
      for (int code : {401,403}) {
        fake::reset(); CHQLogoWeb logo("127.0.0.1"); login(logo);
        raw("HTTP/1.1 " + std::to_string(code) + " Error\r\nContent-Length: 999999\r\n\r\nSECRET");
        bool state = false; CHECK(!logo.readM(1,state)); CHECK(!logo.isAuthenticated());
        CHECK(logo.diagnostics().httpStatus == code); CHECK(logo.lastError().indexOf("SECRET") < 0);
      }
      fake::reset(); CHQLogoWeb logo("127.0.0.1"); login(logo); fake::wifiConnected = false;
      bool state = true; CHECK(!logo.readM(1,state)); CHECK(!logo.isAuthenticated() && state);
    }},
    {"raw output cleared without authentication; writes never retry", [] {
      CHQLogoWeb logo("127.0.0.1"); String result = "stale";
      CHECK(!logo.exchangeRaw("GET",result) && result.isEmpty());
      login(logo); raw("HTTP/1.1 200 OK\r\nContent-Length: 20\r\n\r\nx");
      CHECK(!logo.writeM(1,true)); CHECK(fake::requests.size() == 3);
    }},
    {"HTTP complete body, partial writes and case-insensitive headers", [] {
      fake::SocketScript script; script.response = "HTTP/1.0 200 OK\r\ncOnTeNt-LeNgTh: 3\r\n\r\nabc";
      script.maxWrite = 7; fake::scripts.push_back(script);
      String result; CHQLogoDiagnostics d; CHQLogoConnectionOptions options;
      CHECK(chqLogoDefaultTransport().post("127.0.0.1",80,"/AJAX","GET","p",options,result,d));
      CHECK(result == "abc" && d.responseBytes == 3 && d.httpStatus == 200);
      CHECK(body(0) == "GET");
    }},
    {"HTTP chunked and connection-close framing", [] {
      CHQLogoConnectionOptions options; CHQLogoDiagnostics d; String result;
      raw("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2;foo=bar\r\nab\r\n1\r\nc\r\n0\r\nX-Test: yes\r\n\r\n");
      CHECK(chqLogoDefaultTransport().post("127.0.0.1",80,"/AJAX","GET","p",options,result,d)); CHECK(result == "abc");
      raw("HTTP/1.0 200 OK\r\n\r\nabc");
      CHECK(chqLogoDefaultTransport().post("127.0.0.1",80,"/AJAX","GET","p",options,result,d)); CHECK(result == "abc");
    }},
    {"truncated body/header/chunk are discarded", [] {
      for (const char* response : {"HTTP/1.1 200 OK\r\nContent-Length: 9\r\n\r\nabc",
                                   "HTTP/1.1 200 OK\r\nContent-Len",
                                   "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nab"})
        transportFails(response,CHQLogoError::IncompleteResponse);
    }},
    {"stalled and slow-drip responses have a total IO deadline", [] {
      transportFails("HTTP/1.1 200 OK\r\nContent-Length: 9\r\n\r\nabc",CHQLogoError::Timeout,true);
      CHECK(fake::now == 10);
      CHQLogoConnectionOptions options; options.ioTimeoutMs = 10; String result; CHQLogoDiagnostics d;
      raw(http("abc"),true,2);
      CHECK(!chqLogoDefaultTransport().post("127.0.0.1",80,"/AJAX","GET","p",options,result,d));
      CHECK(d.error == CHQLogoError::Timeout && d.elapsedMs == 10);
    }},
    {"IO deadline tolerates millis rollover", [] {
      fake::now = UINT32_MAX - 4;
      transportFails("HTTP/1.1 200 OK\r\nContent-Length: 9\r\n\r\nabc",CHQLogoError::Timeout,true);
      CHECK(fake::now == 5);
    }},
    {"response/header sizes are bounded", [] {
      transportFails("HTTP/1.1 200 OK\r\nContent-Length: 2049\r\n\r\n",CHQLogoError::ResponseTooLarge);
      transportFails("HTTP/1.1 200 OK\r\n\r\n"+std::string(2049,'x'),CHQLogoError::ResponseTooLarge);
      transportFails("HTTP/1.1 200 OK\r\nX: "+std::string(513,'x')+"\r\n\r\n",CHQLogoError::ResponseTooLarge);
      transportFails("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n801\r\n",CHQLogoError::ResponseTooLarge);
    }},
    {"malformed/ambiguous HTTP framing is rejected", [] {
      for (const char* response : {"NOT HTTP\r\n", "HTTP/1.1 20x OK\r\n",
          "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\nx",
          "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\n",
          "HTTP/1.1 200 OK\r\nBad header\r\n\r\n",
          "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n+1\r\nx\r\n",
          "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\nxXX"})
        transportFails(response,CHQLogoError::InvalidHttp);
      transportFails("HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\n\r\n",CHQLogoError::UnsupportedEncoding);
    }},
    {"HTTPS requires CA and uses TLS transport settings", [] {
      CHQLogoWeb logo("127.0.0.1"); CHECK(!logo.useHTTPS(""));
      CHECK(logo.useHTTPS("TEST CA PEM")); logo.setTimeout(1500); login(logo);
      CHECK(fake::caCertificate == "TEST CA PEM" && fake::connectedPort == 443);
      CHECK(fake::handshakeTimeout == 2 && fake::connectTimeout == 1500);
      logo.useHTTP(8088); CHECK(!logo.isAuthenticated() && !logo.connectionOptions().https);
    }},
    {"invalid host rejected before any network access", [] {
      for (const char* host : {"", "http://127.0.0.1", "host\r\nInjected: x", "user@host"}) {
        CHQLogoWeb logo(host); CHECK(!logo.login("demo123")); CHECK(fake::requests.empty());
      }
    }},
    {"alternate UAM code/token length is configurable", [] {
      CHQLogoWeb logo("127.0.0.1"); auto p = logo.profile(); p.successCode = 750; p.hintLength = 16;
      CHECK(logo.setProfile(p)); queue("750,"+TEMP_HINT.substr(0,16)+",123456");
      queue("750,"+SESSION_HINT.substr(0,16)); CHECK(logo.login("demo123")); CHECK(logo.isAuthenticated());
    }},
    {"injected transport obeys HTTP/error/size contract", [] {
      InjectedTransport transport; CHQLogoWeb logo("127.0.0.1",80,&transport);
      transport.responses.push_back("700,"+TEMP_HINT+",123456");
      transport.responses.push_back("700,"+SESSION_HINT); CHECK(logo.login("demo123"));
      CHECK(fake::requests.empty() && transport.calls == 2);
      transport.responses.push_back(std::string(2049,'x')); String response = "previous";
      CHECK(!logo.exchangeRaw("GET",response) && response.isEmpty());
      CHECK(logo.diagnostics().error == CHQLogoError::ResponseTooLarge);
      transport.success = false; transport.responses.push_back("partial");
      CHECK(!logo.exchangeRaw("GET",response) && response.isEmpty());
      CHECK(logo.diagnostics().error == CHQLogoError::ConnectionFailed);
      transport.success = true; transport.status = 403;
      CHECK(!logo.exchangeRaw("GET",response) && !logo.isAuthenticated());
    }},
    {"allocation failures do not send partial requests or accept partial responses", [] {
      fake::allowStringReserve = false;
      transportFails(http("abc"),CHQLogoError::OutOfMemory);
      CHECK(fake::requests.size() == 1 && fake::requests[0].empty());
      fake::allowStringReserve = true; fake::concatBudget = 2; // Solo construir la petición.
      transportFails(http("abc"),CHQLogoError::OutOfMemory);
      CHECK(body(1) == "GET");
    }},
    {"response limit includes exact boundary and cumulative chunks", [] {
      CHQLogoConnectionOptions options; options.maxResponseBytes = 64;
      String result; CHQLogoDiagnostics d; raw(http(std::string(64,'x')));
      CHECK(chqLogoDefaultTransport().post("127.0.0.1",80,"/AJAX","GET","p",options,result,d));
      CHECK(result.length() == 64);
      raw("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n00000020\r\n" +
          std::string(32,'x') + "\r\n21\r\n");
      CHECK(!chqLogoDefaultTransport().post("127.0.0.1",80,"/AJAX","GET","p",options,result,d));
      CHECK(d.error == CHQLogoError::ResponseTooLarge && result.isEmpty());
    }},
    {"header budget and stalled request writes are bounded", [] {
      std::string response = "HTTP/1.1 200 OK\r\n";
      for (unsigned i = 0; i < 1500; ++i) response += "X: a\r\n";
      transportFails(response,CHQLogoError::ResponseTooLarge);
      fake::SocketScript script; script.response = http("abc"); script.maxWrite = 0;
      fake::scripts.push_back(script); CHQLogoConnectionOptions options; options.ioTimeoutMs = 10;
      CHQLogoDiagnostics d; String result;
      CHECK(!chqLogoDefaultTransport().post("127.0.0.1",80,"/AJAX","GET","p",options,result,d));
      CHECK(d.error == CHQLogoError::Timeout && d.elapsedMs == 10);
    }}
  };
  unsigned failed = 0;
  for (const auto& test : tests) {
    fake::reset();
    try { test.second(); std::cout << "PASS " << test.first << '\n'; }
    catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << test.first << ": " << error.what() << '\n'; }
  }
  std::cout << tests.size() << " native C++ cases, " << failed << " failed\n";
  return failed ? 1 : 0;
}
