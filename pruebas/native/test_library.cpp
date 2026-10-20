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
bool noDelayEnabled = false;
uint32_t noDelayCalls = 0;
bool allowStringReserve = true;
int concatBudget = -1;
void reset() {
  WiFi.reset();
  now = randomCounter = 0; wifiConnected = true;
  scripts.clear(); requests.clear(); caCertificate.clear();
  handshakeTimeout = 0; connectedPort = 0; connectTimeout = 0;
  noDelayEnabled = false; noDelayCalls = 0;
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
std::vector<std::pair<int32_t, int16_t>> digitalEvents, analogEvents;
CHQLogoWeb* callbackLogo = nullptr;
void digitalChanged(int32_t number, bool value) { digitalEvents.emplace_back(number, value ? 1 : 0); }
void analogChanged(int32_t number, int16_t value) { analogEvents.emplace_back(number, value); }
void mirrorChanged(int32_t number, bool value) {
  if (number == 1) CHECK(callbackLogo->setM(2,value));
}
void reentrantChanged(int32_t number, bool value) {
  CHECK(callbackLogo->readM(number) == value);
  CHECK(callbackLogo->loop());  // No debe iniciar otra operacion en el callback.
  callbackLogo->end();
}

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
void managedLogin(CHQLogoWeb& logo) {
  queue("700," + TEMP_HINT + ",123456");
  queue("700," + SESSION_HINT);
  CHECK(logo.loop() && logo.connected());
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

// Los tests previos de cola usan el perfil manual ya conocido.
CHQLogoConfig fixedConfig(const String& password, const String& user = "Web User", Print* output = nullptr) {
  CHQLogoConfig config(password,user,output); config.autoReadId = false; return config;
}

class CapturePrint : public Print {
 public:
  std::string text;
  size_t write(uint8_t byte) override { text.push_back(static_cast<char>(byte)); return 1; }
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
      p.analogSize = 2; p.symbolicAnalog = false; p.amCount = 32; p.readAnalog = true;
      CHECK(logo.setProfile(p)); login(logo);
      CHECK(body(1) == "UAMLOGIN:Test User,3682121608,123460");
      queue("<rs><r i='result' e='0' v='0001'/></rs>"); bool state = false;
      CHECK(logo.readM(64,state) && state); CHECK(body(2) == "GETVARS:read,131,3,66,1,1");
      CHECK(fake::requests[2].find("POST /test/AJAX ") == 0);
      queue("<rs><r i='result' e='0' v='00FA'/></rs>");
      CHECK(logo.readAM(32,value) && value == 250); CHECK(body(3) == "GETVARS:read,20,3,72,2,1");
      p.mAddressStride = UINT16_MAX; CHECK(!logo.setProfile(p));
    }},
    {"experimental symbolic analog matches PLC variable page commands", [] {
      const auto mock = CHQLogoProfile::mockV1();
      CHECK(mock.analogSize == 1 && mock.amAddressStride == 1);
      auto p = CHQLogoProfile::logo8AjaxExperimental();
      CHECK(p.symbolicAnalog && p.analogSize == 4 && p.amAddressBase == 1 && p.amAddressStride == 1);
      CHECK(!p.readAnalog && !p.writeAnalog);
      p.readAnalog = p.writeAnalog = true;
      p.readResponseId = "v1";
      CHQLogoWeb logo("127.0.0.1"); CHECK(logo.setProfile(p)); login(logo);
      int16_t value = 0;
      queue("<rs><r i='v1' e='0' v='012C'/></rs>");
      CHECK(logo.readAM(1,value) && value == 300);
      CHECK(body(2) == "GETVARS:_local_=v1,AM..4:1-1");
      queue("<rs><r i='v1' e='0' v='0320'/></rs>");
      CHECK(logo.readAM(2,value) && value == 800);
      CHECK(body(3) == "GETVARS:_local_=v1,AM..4:2-1");
      queue("<rs><r i='v0' e='0'/></rs>");
      CHECK(logo.writeAM(2,-120));
      CHECK(body(4) == "SETVARS:_local_=v0,AM..4:2-1,ff88");
      queue("<rs><r i='v1' e='0' v='01'/></rs>"); bool state = false;
      CHECK(logo.readM(2,state) && state);
      CHECK(body(5) == "GETVARS:v1,131,0,1,1,1");
      // Captura fisica de la peticion AM incorrecta: un byte no es una palabra.
      queue("<rs><r i='v1' e='0' v='00'/></rs>");
      CHECK(!logo.readAM(2,value) && value == 800);
      CHECK(logo.diagnostics().error == CHQLogoError::UnexpectedResponse);
    }},
    {"symbolic writes use the portal lowercase hex without changing numeric encoding", [] {
      CHQLogoWeb logo("127.0.0.1"); auto p = CHQLogoProfile::logo8AjaxExperimental();
      p.writeAnalog = true; CHECK(logo.setProfile(p)); login(logo);
      size_t index = 2;
      for (const auto& item : std::vector<std::pair<int,std::string>>{
          {30,"001e"},{60,"003c"},{255,"00ff"},{-1,"ffff"},{32767,"7fff"}}) {
        queue("<rs><r i='v0' e='0'/></rs>"); CHECK(logo.writeAM(2,item.first));
        CHECK(body(index++) == "SETVARS:_local_=v0,AM..4:2-1," + item.second);
      }
      p.symbolicAnalog = false; p.amAddressBase = 0;
      CHECK(logo.setProfile(p)); login(logo);
      queue("<rs><r i='v0' e='0'/></rs>"); CHECK(logo.writeAM(2,30));
      CHECK(body(fake::requests.size()-1) == "SETVARS:v0,20,0,1,4,1,001E");
    }},
    {"without opt in captured AM error 16 remains rejected despite an equal value", [] {
      CHQLogoWeb logo("127.0.0.1"); auto config = fixedConfig("secret");
      config.profile.readAnalog = config.profile.writeAnalog = true;
      CHECK(logo.begin(config) && logo.watchAM(2)); managedLogin(logo);
      queue("<rs><r i='v1' e='0' v='001E'/></rs>");
      CHECK(logo.loop() && logo.readAM(2) == 30);
      CHECK(logo.setAM(2,30)); queue("<rs><r i='v0' e='16'/></rs>"); CHECK(logo.loop());
      CHECK(logo.writeStateAM(2) == CHQLogoWriteState::Rejected);
      CHECK(logo.diagnostics().protocolStatus == 16 && logo.connected());
      fake::now += 500; queue("<rs><r i='v1' e='0' v='001E'/></rs>"); CHECK(logo.loop());
      CHECK(logo.readAM(2) == 30 && logo.writeStateAM(2) == CHQLogoWriteState::Rejected);
      CHECK(logo.setAM(2,30) && logo.loop() && fake::requests.size() == 5);
    }},
    {"observed AM code opt in confirms only through fresh readback and logs the code", [] {
      CapturePrint output; CHQLogoWeb logo("127.0.0.1"); auto config = fixedConfig("secret","Web User",&output);
      CHECK(config.profile.analogWriteReadbackCode == 0);
      config.profile.readAnalog = config.profile.writeAnalog = true;
      config.profile.analogWriteReadbackCode = 16;
      CHECK(logo.begin(config) && logo.watchAM(2)); managedLogin(logo);
      queue("<rs><r i='v1' e='0' v='001E'/></rs>"); CHECK(logo.loop());
      CHECK(logo.readAM(2) == 30 && logo.setAM(2,40));
      queue("<rs><r i='v0' e='16'/></rs>"); CHECK(logo.loop());
      CHECK(body(3) == "SETVARS:_local_=v0,AM..4:2-1,0028");
      CHECK(logo.writeStateAM(2) == CHQLogoWriteState::Verifying && !logo.availableAM(2));
      CHECK(logo.diagnostics().protocolStatus == 16 && logo.diagnostics().error == CHQLogoError::ProtocolRejected);
      CHECK(output.text.find("respuesta e=16 observada") != std::string::npos);
      queue("<rs><r i='v1' e='0' v='001E'/></rs>"); CHECK(logo.loop());
      CHECK(logo.writeStateAM(2) == CHQLogoWriteState::Verifying);
      fake::now += 500; queue("<rs><r i='v1' e='0' v='0028'/></rs>"); CHECK(logo.loop());
      CHECK(logo.writeStateAM(2) == CHQLogoWriteState::Matched && logo.readAM(2) == 40);
      CHECK(logo.setAM(2,40) && logo.loop() && fake::requests.size() == 6);
      CHECK(output.text.find("coincide por lectura") != std::string::npos);
      CHECK(output.text.find("secret") == std::string::npos);
    }},
    {"observed AM code cannot use a stale equal cache as confirmation", [] {
      CHQLogoWeb logo("127.0.0.1"); auto config = fixedConfig("secret");
      config.profile.readAnalog = config.profile.writeAnalog = true;
      config.profile.analogWriteReadbackCode = 16;
      CHECK(logo.begin(config) && logo.watchAM(2)); managedLogin(logo);
      queue("<rs><r i='v1' e='0' v='0028'/></rs>"); CHECK(logo.loop());
      CHECK(logo.setAM(2,40)); queue("<rs><r i='v0' e='16'/></rs>"); CHECK(logo.loop());
      CHECK(!logo.availableAM(2) && logo.readAM(2) == 40);
      CHECK(logo.writeStateAM(2) == CHQLogoWriteState::Verifying);
      queue("<rs><r i='v1' e='0' v='0028'/></rs>"); CHECK(logo.loop());
      CHECK(logo.writeStateAM(2) == CHQLogoWriteState::Matched);
    }},
    {"observed AM code exception excludes digital numeric other codes and disabled reads", [] {
      for (int variant = 0; variant < 4; ++variant) {
        fake::reset(); CHQLogoWeb logo("127.0.0.1"); auto config = fixedConfig("secret");
        config.profile.readAnalog = config.profile.writeAnalog = true;
        config.profile.analogWriteReadbackCode = 16;
        if (variant == 1) config.profile.symbolicAnalog = false;
        if (variant == 3) config.profile.readAnalog = false;
        CHECK(logo.begin(config));
        if (variant == 3) {
          CHECK(!logo.setAM(2,40) && fake::requests.empty());
          CHECK(logo.diagnostics().error == CHQLogoError::CapabilityDisabled);
          continue;
        }
        CHECK(variant == 0 ? logo.setM(2,true) : logo.setAM(2,40)); managedLogin(logo);
        queue(variant == 2 ? "<rs><r i='v0' e='3'/></rs>" : "<rs><r i='v0' e='16'/></rs>");
        CHECK(logo.loop());
        CHECK((variant == 0 ? logo.writeStateM(2) : logo.writeStateAM(2)) == CHQLogoWriteState::Rejected);
      }
    }},
    {"observed AM code timeout reports mismatch or unknown and never replays after recovery", [] {
      for (bool haveRead : {false,true}) {
        fake::reset(); CHQLogoWeb logo("127.0.0.1"); auto config = fixedConfig("secret");
        config.profile.readAnalog = config.profile.writeAnalog = true;
        config.profile.analogWriteReadbackCode = 16;
        CHECK(logo.begin(config) && logo.setAM(2,40)); managedLogin(logo);
        queue("<rs><r i='v0' e='16'/></rs>"); CHECK(logo.loop());
        if (haveRead) { queue("<rs><r i='v1' e='0' v='001E'/></rs>"); CHECK(logo.loop()); }
        fake::now += 5000;
        if (haveRead) {
          queue("<rs><r i='v1' e='0' v='001E'/></rs>"); CHECK(logo.loop());
        }
        fake::wifiConnected = false; CHECK(!logo.loop());
        const auto state = haveRead ? CHQLogoWriteState::Mismatch : CHQLogoWriteState::Unknown;
        CHECK(logo.writeStateAM(2) == state);
        fake::wifiConnected = true; managedLogin(logo);
        queue("<rs><r i='v1' e='0' v='0028'/></rs>"); CHECK(logo.loop());
        CHECK(logo.writeStateAM(2) == state && logo.readAM(2) == 40);
        CHECK(logo.setAM(2,40) && logo.loop());
        size_t writes = 0;
        for (size_t i = 0; i < fake::requests.size(); ++i) if (body(i).find("SETVARS:") == 0) ++writes;
        CHECK(writes == 1);
      }
    }},
    {"direct AM writes still return the observed code as an error with opt in enabled", [] {
      CHQLogoWeb logo("127.0.0.1"); auto config = fixedConfig("secret");
      config.profile.readAnalog = config.profile.writeAnalog = true;
      config.profile.analogWriteReadbackCode = 16;
      CHECK(logo.begin(config)); managedLogin(logo);
      queue("<rs><r i='v0' e='16'/></rs>"); CHECK(!logo.writeAM(2,40));
      CHECK(logo.diagnostics().protocolStatus == 16 && logo.diagnostics().error == CHQLogoError::ProtocolRejected);
      CHECK(logo.writeStateAM(2) == CHQLogoWriteState::None);
    }},
    {"AM read error 16 is not made valid by the write readback exception", [] {
      CHQLogoWeb logo("127.0.0.1"); auto config = fixedConfig("secret");
      config.profile.readAnalog = config.profile.writeAnalog = true;
      config.profile.analogWriteReadbackCode = 16;
      CHECK(logo.begin(config) && logo.setAM(2,40)); managedLogin(logo);
      queue("<rs><r i='v0' e='16'/></rs>"); CHECK(logo.loop());
      queue("<rs><r i='v1' e='16' v='0028'/></rs>"); CHECK(logo.loop());
      CHECK(!logo.availableAM(2) && logo.writeStateAM(2) == CHQLogoWriteState::Verifying);
      CHECK(logo.diagnostics().protocolStatus == 16 && logo.diagnostics().error == CHQLogoError::ProtocolRejected);
      fake::now += 500; queue("<rs><r i='v1' e='0' v='0028'/></rs>"); CHECK(logo.loop());
      CHECK(logo.writeStateAM(2) == CHQLogoWriteState::Matched);
    }},
    {"symbolic analog configuration requires a word and one-based address", [] {
      CHQLogoWeb logo("127.0.0.1"); auto p = CHQLogoProfile::logo8AjaxExperimental();
      p.amAddressBase = 0; CHECK(!logo.setProfile(p));
      p.amAddressBase = 1; p.analogSize = 2; CHECK(!logo.setProfile(p));
      CHECK(fake::requests.empty());
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
    {"HTTP phase diagnostics include close time and tolerate millis rollover", [] {
      for (bool rollover : {false,true}) {
        fake::reset(); fake::now = rollover ? UINT32_MAX-5 : 0;
        fake::SocketScript script; script.response = http("abc");
        script.connectDelayMs = 4; script.writeDelayMs = 7; script.stopDelayMs = 13;
        script.maxWrite = 4096; script.byteDelayMs = 1; fake::scripts.push_back(script);
        String response; CHQLogoDiagnostics d; CHQLogoConnectionOptions options;
        CHECK(chqLogoDefaultTransport().post("127.0.0.1",80,"/AJAX","GET","p",options,response,d));
        CHECK(response == "abc" && d.connectMs == 4 && d.sendMs == 14 && d.closeMs == 13);
        CHECK(d.receiveMs > 0 && d.responseBytes == 3);
        CHECK(d.elapsedMs == d.connectMs+d.sendMs+d.receiveMs+d.closeMs);
      }
    }},
    {"failed TCP connection phase diagnostics include cleanup", [] {
      fake::SocketScript script; script.connectOK = false;
      script.connectDelayMs = 12; script.stopDelayMs = 5; fake::scripts.push_back(script);
      String response; CHQLogoDiagnostics d; CHQLogoConnectionOptions options;
      CHECK(!chqLogoDefaultTransport().post("127.0.0.1",80,"/AJAX","GET","p",options,response,d));
      CHECK(d.error == CHQLogoError::ConnectionFailed && d.connectMs == 12);
      CHECK(d.sendMs == 0 && d.receiveMs == 0 && d.closeMs == 5 && d.elapsedMs == 17);
    }},
    {"receive timeout phase diagnostics retain send and cleanup time", [] {
      fake::SocketScript script; script.keepOpen = true; script.maxWrite = 4096;
      script.connectDelayMs = 4; script.writeDelayMs = 2; script.stopDelayMs = 6;
      fake::scripts.push_back(script);
      String response; CHQLogoDiagnostics d; CHQLogoConnectionOptions options; options.ioTimeoutMs = 10;
      CHECK(!chqLogoDefaultTransport().post("127.0.0.1",80,"/AJAX","GET","p",options,response,d));
      CHECK(d.error == CHQLogoError::Timeout && d.connectMs == 4 && d.sendMs == 4);
      CHECK(d.receiveMs == 6 && d.closeMs == 6 && d.elapsedMs == 20);
    }},
    {"send timeout phase diagnostics never report a receive phase", [] {
      fake::SocketScript script; script.maxWrite = 0; script.keepOpen = true;
      script.connectDelayMs = 4; script.stopDelayMs = 6; fake::scripts.push_back(script);
      String response; CHQLogoDiagnostics d; CHQLogoConnectionOptions options; options.ioTimeoutMs = 10;
      CHECK(!chqLogoDefaultTransport().post("127.0.0.1",80,"/AJAX","GET","p",options,response,d));
      CHECK(d.error == CHQLogoError::Timeout && d.connectMs == 4 && d.sendMs == 10);
      CHECK(d.receiveMs == 0 && d.closeMs == 6 && d.elapsedMs == 20);
    }},
    {"TCP_NODELAY is configured on connected HTTP and HTTPS sockets and is optional", [] {
      for (bool https : {false,true}) for (bool enabled : {false,true}) {
        fake::reset(); CHQLogoConnectionOptions options; options.tcpNoDelay = enabled;
        options.https = https; options.rootCA = "test-ca";
        queue("abc"); String response; CHQLogoDiagnostics d;
        CHECK(chqLogoDefaultTransport().post("127.0.0.1",https?443:80,"/AJAX","GET","p",options,response,d));
        CHECK(fake::noDelayCalls == 1 && fake::noDelayEnabled == enabled);
        CHECK(d.tcpNoDelayApplied == enabled && response == "abc");
      }
    }},
    {"TCP_NODELAY failure is reported without changing HTTP validity or connection errors", [] {
      fake::SocketScript script; script.response = http("abc"); script.noDelayOK = false;
      fake::scripts.push_back(script); String response; CHQLogoDiagnostics d; CHQLogoConnectionOptions options;
      CHECK(chqLogoDefaultTransport().post("127.0.0.1",80,"/AJAX","GET","p",options,response,d));
      CHECK(!d.tcpNoDelayApplied && d.error == CHQLogoError::None && response == "abc");
      fake::reset(); script.connectOK = false; fake::scripts.push_back(script);
      CHECK(!chqLogoDefaultTransport().post("127.0.0.1",80,"/AJAX","GET","p",options,response,d));
      CHECK(fake::noDelayCalls == 0 && !d.tcpNoDelayApplied && d.error == CHQLogoError::ConnectionFailed);
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
    {"managed cached reads have no HTTP and callbacks only notify changes", [] {
      CHQLogoWeb logo("127.0.0.1"); digitalEvents.clear(); logo.onChangeM(digitalChanged);
      CHECK(!logo.readM(1) && !logo.availableM(1));
      CHECK(logo.begin("demo123") && fake::requests.empty());
      managedLogin(logo); CHECK(!logo.availableM(1));
      queue("<rs><r i='v0' e='0' v='01'/></rs>"); CHECK(logo.loop());
      CHECK(logo.availableM(1) && logo.readM(1)); CHECK(digitalEvents.size() == 1);
      for (int i = 0; i < 100; ++i) { CHECK(logo.loop()); CHECK(logo.readM(1)); }
      CHECK(fake::requests.size() == 3);
      fake::now += 500; queue("<rs><r i='v0' e='0' v='01'/></rs>"); CHECK(logo.loop());
      CHECK(digitalEvents.size() == 1);
      fake::now += 500; queue("<rs><r i='v0' e='0' v='00'/></rs>"); CHECK(logo.loop());
      CHECK(!logo.readM(1) && logo.availableM(1));
      CHECK(digitalEvents.size() == 2 && digitalEvents.back().second == 0);
    }},
    {"managed analog cache preserves signed values and reports changes", [] {
      CHQLogoWeb logo("127.0.0.1"); analogEvents.clear(); logo.onChangeAM(analogChanged);
      CHECK(logo.watchAM(1) && logo.begin("demo123",100)); managedLogin(logo);
      queue("<rs><r i='v0' e='0' v='FF88'/></rs>"); CHECK(logo.loop());
      CHECK(logo.availableAM(1) && logo.readAM(1) == -120);
      fake::now += 100; queue("<rs><r i='v0' e='0' v='FF88'/></rs>"); CHECK(logo.loop());
      CHECK(analogEvents.size() == 1);
      fake::now += 100; queue("<rs><r i='v0' e='0' v='7FFF'/></rs>"); CHECK(logo.loop());
      CHECK(logo.readAM(1) == 32767 && analogEvents.size() == 2);
    }},
    {"managed mixed watch list polls fairly with one scalar request per loop", [] {
      CHQLogoWeb logo("127.0.0.1"); CHECK(logo.watchM(1) && logo.watchM(2) && logo.watchAM(1));
      CHECK(logo.begin("demo123",500)); managedLogin(logo);
      for (const char* value : {"01","00","00FA"}) {
        queue(std::string("<rs><r i='v0' e='0' v='")+value+"'/></rs>"); CHECK(logo.loop());
      }
      CHECK(fake::requests.size() == 5 && logo.readM(1) && !logo.readM(2) && logo.readAM(1) == 250);
      CHECK(body(2) == "GETVARS:v1,131,0,0,1,1");
      CHECK(body(3) == "GETVARS:v1,131,0,1,1,1");
      CHECK(body(4) == "GETVARS:v1,20,0,0,1,1");
      CHECK(logo.loop() && fake::requests.size() == 5);
    }},
    {"managed failed login waits before retrying", [] {
      CHQLogoWeb logo("127.0.0.1"); CHECK(logo.begin("demo123",500,3000));
      raw("HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n"); CHECK(!logo.loop());
      CHECK(!logo.connected() && fake::requests.size() == 1);
      for (int i = 0; i < 100; ++i) CHECK(!logo.loop());
      fake::now = 2999; CHECK(!logo.loop() && fake::requests.size() == 1);
      fake::now = 3000; managedLogin(logo); CHECK(fake::requests.size() == 3);
    }},
    {"managed WiFi recovery invalidates cache and resynchronizes", [] {
      CHQLogoWeb logo("127.0.0.1"); digitalEvents.clear(); logo.onChangeM(digitalChanged);
      CHECK(logo.watchM(1) && logo.begin("demo123")); managedLogin(logo);
      queue("<rs><r i='v0' e='0' v='01'/></rs>"); CHECK(logo.loop());
      fake::wifiConnected = false; CHECK(!logo.availableM(1) && !logo.connected());
      CHECK(!logo.loop() && !logo.isAuthenticated()); CHECK(fake::requests.size() == 3);
      CHECK(logo.readM(1));  // Ultima muestra retenida, marcada no disponible.
      fake::wifiConnected = true; managedLogin(logo); CHECK(!logo.availableM(1));
      queue("<rs><r i='v0' e='0' v='01'/></rs>"); CHECK(logo.loop());
      CHECK(logo.availableM(1) && digitalEvents.size() == 2);
    }},
    {"managed UAM expiry reconnects without retrying writes", [] {
      CHQLogoWeb logo("127.0.0.1"); CHECK(logo.watchM(2) && logo.begin("demo123")); managedLogin(logo);
      queue("<rs><r i='v0' e='0' v='00'/></rs>"); CHECK(logo.loop());
      queue("<rs><r i='v0' e='0'/></rs>"); CHECK(logo.writeM(2,true));
      CHECK(!logo.availableM(2));
      queue("704,rejected"); CHECK(!logo.loop()); CHECK(!logo.availableM(2));
      const size_t count = fake::requests.size(); CHECK(!logo.loop() && fake::requests.size() == count);
      fake::now += 3000; managedLogin(logo);
      queue("<rs><r i='v0' e='0' v='01'/></rs>"); CHECK(logo.loop() && logo.readM(2));
      unsigned writes = 0;
      for (size_t i = 0; i < fake::requests.size(); ++i) if (body(i).find("SETVARS:") == 0) ++writes;
      CHECK(writes == 1);
    }},
    {"observed malformed write response is never success and next read verifies session", [] {
      CHQLogoWeb logo("127.0.0.1"); auto p = logo.profile(); p.readResponseId = "v1";
      CHECK(logo.setProfile(p) && logo.watchM(1) && logo.watchM(2) && logo.begin("demo123"));
      managedLogin(logo);
      queue("<rs><r i='v1' e='0' v='01'/></rs>"); CHECK(logo.loop());
      queue("<rs><r i='v1' e='0' v='01'/></rs>"); CHECK(logo.loop());
      queue("<rs><r i='v0' e='1'/>/<rs>"); CHECK(!logo.writeM(2,false));
      CHECK(logo.diagnostics().error == CHQLogoError::UnexpectedResponse);
      CHECK(logo.connected() && logo.availableM(1) && !logo.availableM(2));
      CHECK(logo.readM(2));  // Conserva muestra anterior, aun no disponible.
      queue("<rs><r i='v1' e='0' v='00'/></rs>"); CHECK(logo.loop());
      CHECK(logo.availableM(2) && !logo.readM(2) && fake::requests.size() == 6);
      CHECK(body(4) == "SETVARS:v0,131,0,1,1,1,00");
      CHECK(body(5) == "GETVARS:v1,131,0,1,1,1");
      CHECK(logo.loop() && fake::requests.size() == 6);
    }},
    {"unrecognized managed ACK followed by expired session recovers without rewriting", [] {
      CHQLogoWeb logo("127.0.0.1"); CHECK(logo.watchM(2) && logo.begin("demo123")); managedLogin(logo);
      queue("<rs><r i='v0' e='0' v='00'/></rs>"); CHECK(logo.loop());
      queue("<html>login required</html>"); CHECK(!logo.writeM(2,true) && logo.connected());
      raw("HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n"); CHECK(!logo.loop());
      CHECK(!logo.connected() && !logo.availableM(2));
      fake::now += 3000; managedLogin(logo);
      queue("<rs><r i='v0' e='0' v='00'/></rs>"); CHECK(logo.loop());
      unsigned writes = 0;
      for (size_t i = 0; i < fake::requests.size(); ++i) if (body(i).find("SETVARS:") == 0) ++writes;
      CHECK(writes == 1 && logo.availableM(2) && !logo.readM(2));
    }},
    {"well formed nonzero write error remains rejected", [] {
      CHQLogoWeb logo("127.0.0.1"); CHECK(logo.begin("demo123")); managedLogin(logo);
      queue("<rs><r i='v0' e='1'/></rs>"); CHECK(!logo.writeM(2,true));
      CHECK(logo.diagnostics().error == CHQLogoError::ProtocolRejected);
      CHECK(logo.diagnostics().protocolStatus == 1 && logo.connected());
      CHECK(fake::requests.size() == 3);
    }},
    {"managed malformed replies retain last value but invalidate availability", [] {
      CHQLogoWeb logo("127.0.0.1"); CHECK(logo.watchM(1) && logo.begin("demo123")); managedLogin(logo);
      queue("<rs><r i='v0' e='0' v='01'/></rs>"); CHECK(logo.loop());
      fake::now += 500; queue("<rs><r i='wrong' e='0' v='00'/></rs>"); CHECK(!logo.loop());
      CHECK(!logo.availableM(1) && logo.readM(1));
      CHECK(logo.diagnostics().error == CHQLogoError::UnexpectedResponse);
      CHECK(!logo.loop() && fake::requests.size() == 4);
    }},
    {"managed variable rejection does not discard another valid variable", [] {
      CHQLogoWeb logo("127.0.0.1"); CHECK(logo.watchM(1) && logo.watchM(2) && logo.begin("demo123"));
      managedLogin(logo); queue("<rs><r i='v0' e='0' v='01'/></rs>"); CHECK(logo.loop());
      queue("<rs><r i='v0' e='5'/></rs>"); CHECK(logo.loop());
      CHECK(logo.connected() && logo.availableM(1) && !logo.availableM(2));
      CHECK(logo.diagnostics().error == CHQLogoError::ProtocolRejected);
      CHECK(logo.loop() && fake::requests.size() == 4);
    }},
    {"managed timers survive rollover for polling and reconnection", [] {
      CHQLogoWeb logo("127.0.0.1"); CHECK(logo.watchM(1) && logo.begin("demo123",20,30));
      fake::now = UINT32_MAX - 10; managedLogin(logo);
      queue("<rs><r i='v0' e='0' v='00'/></rs>"); CHECK(logo.loop());
      fake::now += 19; CHECK(logo.loop() && fake::requests.size() == 3);
      fake::now += 1; queue("704,expired"); CHECK(!logo.loop());
      fake::now += 29; CHECK(!logo.loop() && fake::requests.size() == 4);
      fake::now += 1; managedLogin(logo); CHECK(fake::requests.size() == 6);
    }},
    {"managed callback reads are local and recursive loop is guarded", [] {
      CHQLogoWeb logo("127.0.0.1"); callbackLogo = &logo; logo.onChangeM(reentrantChanged);
      CHECK(logo.watchM(1) && logo.begin("demo123")); managedLogin(logo);
      queue("<rs><r i='v0' e='0' v='01'/></rs>"); CHECK(!logo.loop());
      CHECK(!logo.availableM(1) && !logo.connected() && fake::requests.size() == 3);
      CHECK(!logo.loop() && fake::requests.size() == 3); callbackLogo = nullptr;
    }},
    {"managed watch capacity and invalid setup do not generate traffic", [] {
      CHQLogoWeb logo("127.0.0.1");
      CHECK(!logo.begin("demo123",0) && !logo.begin("demo123",1,0));
      CHECK(!logo.begin(std::string(257,'x')));
      CHECK(!logo.watchM(0) && !logo.watchM(257) && !logo.watchAM(9));
      for (int i = 1; i <= 16; ++i) CHECK(logo.watchM(i));
      CHECK(logo.watchM(1) && !logo.watchM(17) && !logo.watchAM(1));
      CHECK(fake::requests.empty());
      fake::allowStringReserve = false; CHECK(!logo.begin("demo123"));
      CHECK(logo.diagnostics().error == CHQLogoError::OutOfMemory && !logo.loop());
    }},
    {"managed profile changes invalidate cached data and use the new ID", [] {
      CHQLogoWeb logo("127.0.0.1"); CHECK(logo.watchM(1) && logo.begin("demo123")); managedLogin(logo);
      queue("<rs><r i='v0' e='0' v='01'/></rs>"); CHECK(logo.loop());
      auto profile = logo.profile(); profile.readResponseId = "v1"; CHECK(logo.setProfile(profile));
      CHECK(!logo.availableM(1)); managedLogin(logo);
      queue("<rs><r i='v1' e='0' v='00'/></rs>"); CHECK(logo.loop());
      CHECK(logo.availableM(1) && !logo.readM(1));
      logo.end(); CHECK(!logo.connected() && !logo.loop());
    }},
    {"simple configuration leaves WiFi entirely under application control", [] {
      CHQLogoWeb logo("127.0.0.1"); CHQLogoConfig config = fixedConfig("logo-secret");
      fake::wifiConnected = false;
      WiFi.mode(7); WiFi.setAutoReconnect(false); WiFi.begin("external-net","wifi-secret");
      CHECK(logo.begin(config));
      CHECK(WiFi.beginCalls == 1 && WiFi.stationMode == 7 && !WiFi.autoReconnect);
      CHECK(WiFi.ssid == "external-net" && WiFi.password == "wifi-secret");
      CHECK(logo.profile().readResponseId == "v1" && fake::requests.empty());
      CHECK(!logo.loop() && WiFi.reconnectCalls == 0);
      fake::now = 10000; CHECK(!logo.loop() && WiFi.reconnectCalls == 0);
      fake::wifiConnected = true; managedLogin(logo);
      CHECK(!logo.readM(1) && !logo.availableM(1));
      queue("<rs><r i='v1' e='0' v='01'/></rs>"); CHECK(logo.loop());
      CHECK(logo.readM(1) && logo.availableM(1));
      fake::wifiConnected = false; CHECK(!logo.loop() && !logo.availableM(1));
      fake::now += 3000; CHECK(!logo.loop() && WiFi.reconnectCalls == 0);
      WiFi.reconnect(); fake::wifiConnected = true; managedLogin(logo);
      logo.end(); CHECK(WiFi.beginCalls == 1 && WiFi.reconnectCalls == 1);
      CHECK(WiFi.stationMode == 7 && !WiFi.autoReconnect);
    }},
    {"desired digital values coalesce and repeated calls send only one write", [] {
      CHQLogoWeb logo("127.0.0.1"); CHECK(logo.begin(fixedConfig("secret")));
      CHECK(logo.setM(2,true) && logo.setM(2,false));
      CHECK(logo.writeStateM(2) == CHQLogoWriteState::Pending && fake::requests.empty());
      managedLogin(logo); queue("<rs><r i='v0' e='1'/>/<rs>"); CHECK(logo.loop());
      CHECK(body(2) == "SETVARS:v0,131,0,1,1,1,00");
      CHECK(logo.writeStateM(2) == CHQLogoWriteState::Verifying);
      queue("<rs><r i='v1' e='0' v='00'/></rs>"); CHECK(logo.loop());
      CHECK(logo.writeStateM(2) == CHQLogoWriteState::Matched && logo.availableM(2));
      for (int i = 0; i < 1000; ++i) { CHECK(logo.setM(2,false)); CHECK(logo.loop()); }
      CHECK(fake::requests.size() == 4);
      fake::now += 500; queue("<rs><r i='v1' e='0' v='01'/></rs>"); CHECK(logo.loop());
      CHECK(logo.setM(2,false) && logo.loop() && fake::requests.size() == 5);
      // No fuerza continuamente el estado si el programa remoto lo cambia.
    }},
    {"latest desired value waits for current verification before sending", [] {
      CHQLogoWeb logo("127.0.0.1"); CHECK(logo.begin(fixedConfig("secret")));
      CHECK(logo.setM(2,true)); managedLogin(logo);
      queue("<rs><r i='v0' e='1'/>/<rs>"); CHECK(logo.loop());
      CHECK(logo.setM(2,false)); CHECK(logo.writeStateM(2) == CHQLogoWriteState::Verifying);
      queue("<rs><r i='v1' e='0' v='01'/></rs>"); CHECK(logo.loop());
      CHECK(logo.writeStateM(2) == CHQLogoWriteState::Pending);
      queue("<rs><r i='v0' e='1'/>/<rs>"); CHECK(logo.loop());
      CHECK(body(4) == "SETVARS:v0,131,0,1,1,1,00");
      queue("<rs><r i='v1' e='0' v='00'/></rs>"); CHECK(logo.loop());
      CHECK(logo.writeStateM(2) == CHQLogoWriteState::Matched);
    }},
    {"queued analog writes validate ranges and verify signed values", [] {
      CHQLogoWeb logo("127.0.0.1"); CHECK(!logo.setAM(1,250));
      CHQLogoConfig config = fixedConfig("secret"); CHECK(logo.begin(config));
      CHECK(!logo.setAM(1,250)); config.profile.readAnalog = config.profile.writeAnalog = true;
      CHECK(logo.begin(config)); CHECK(!logo.setAM(1,32768) && !logo.setAM(1,-32769));
      CHECK(!logo.setAM(9,250) && logo.setAM(1,-120)); managedLogin(logo);
      queue("<rs><r i='v0' e='0'/></rs>"); CHECK(logo.loop());
      CHECK(body(2) == "SETVARS:_local_=v0,AM..4:1-1,ff88");
      queue("<rs><r i='v1' e='0' v='FF88'/></rs>"); CHECK(logo.loop());
      CHECK(logo.readAM(1) == -120 && logo.writeStateAM(1) == CHQLogoWriteState::Matched);
      CHECK(logo.writeStateAM(2) == CHQLogoWriteState::None);
    }},
    {"uncertain queued write is not replayed after WiFi recovery", [] {
      CHQLogoWeb logo("127.0.0.1"); CHECK(logo.begin(fixedConfig("secret")));
      CHECK(logo.setM(2,true)); managedLogin(logo);
      raw("HTTP/1.1 200 OK\r\nContent-Length: 20\r\n\r\nx"); CHECK(logo.loop());
      CHECK(logo.writeStateM(2) == CHQLogoWriteState::Verifying);
      fake::wifiConnected = false; fake::now += 5000; CHECK(!logo.loop());
      CHECK(logo.writeStateM(2) == CHQLogoWriteState::Unknown);
      CHECK(logo.setM(2,true)); fake::wifiConnected = true; managedLogin(logo);
      queue("<rs><r i='v1' e='0' v='01'/></rs>"); CHECK(logo.loop());
      CHECK(logo.writeStateM(2) == CHQLogoWriteState::Unknown);
      CHECK(fake::requests.size() == 6 && body(2).find("SETVARS:") == 0);
      for (size_t i = 3; i < fake::requests.size(); ++i) CHECK(body(i).find("SETVARS:") != 0);
    }},
    {"readback waits for delayed application then matches without another write", [] {
      CHQLogoWeb logo("127.0.0.1"); CHECK(logo.begin(fixedConfig("secret")));
      CHECK(logo.setM(2,true)); managedLogin(logo);
      queue("<rs><r i='v0' e='1'/>/<rs>"); CHECK(logo.loop());
      queue("<rs><r i='v1' e='0' v='00'/></rs>"); CHECK(logo.loop());
      CHECK(logo.writeStateM(2) == CHQLogoWriteState::Verifying);
      fake::now += 500; queue("<rs><r i='v1' e='0' v='01'/></rs>"); CHECK(logo.loop());
      CHECK(logo.writeStateM(2) == CHQLogoWriteState::Matched && fake::requests.size() == 5);
    }},
    {"queued write reports mismatch at deadline and does not resend", [] {
      CHQLogoWeb logo("127.0.0.1"); CHECK(logo.begin(fixedConfig("secret")));
      CHECK(logo.setM(2,true)); managedLogin(logo);
      queue("<rs><r i='v0' e='1'/>/<rs>"); CHECK(logo.loop());
      queue("<rs><r i='v1' e='0' v='00'/></rs>"); CHECK(logo.loop());
      fake::now = 4999; queue("<rs><r i='v1' e='0' v='00'/></rs>"); CHECK(logo.loop());
      fake::now = 5000; CHECK(logo.loop()); CHECK(logo.setM(2,true));
      CHECK(logo.writeStateM(2) == CHQLogoWriteState::Mismatch && fake::requests.size() == 5);
    }},
    {"explicit write rejection cannot become matched by a later equal reading", [] {
      CHQLogoWeb logo("127.0.0.1"); CHECK(logo.begin(fixedConfig("secret")));
      CHECK(logo.setM(2,true)); managedLogin(logo);
      queue("<rs><r i='v0' e='1'/></rs>"); CHECK(logo.loop());
      CHECK(logo.writeStateM(2) == CHQLogoWriteState::Rejected);
      queue("<rs><r i='v1' e='0' v='01'/></rs>"); CHECK(logo.loop());
      CHECK(logo.writeStateM(2) == CHQLogoWriteState::Rejected);
      CHECK(logo.setM(2,true) && logo.loop() && fake::requests.size() == 4);
    }},
    {"pending output intents are cleared on end profile and trust changes", [] {
      CHQLogoWeb logo("127.0.0.1"); CHECK(logo.begin(fixedConfig("secret")));
      CHECK(logo.setM(2,true)); auto profile = logo.profile(); CHECK(logo.setProfile(profile));
      CHECK(logo.writeStateM(2) == CHQLogoWriteState::None);
      CHECK(logo.setM(2,true)); logo.useHTTP(); CHECK(logo.writeStateM(2) == CHQLogoWriteState::None);
      CHECK(logo.setM(2,true)); CHECK(logo.useHTTPS("test-ca"));
      CHECK(logo.writeStateM(2) == CHQLogoWriteState::None);
      CHECK(logo.setM(2,true)); logo.end(); CHECK(!logo.loop());
      CHECK(logo.writeStateM(2) == CHQLogoWriteState::None && fake::requests.empty());
    }},
    {"simple debug reports readback while keeping bodies and credentials private", [] {
      CapturePrint output; CHQLogoWeb logo("127.0.0.1");
      CHQLogoConfig config = fixedConfig("logo-secret","Web User",&output);
      CHECK(logo.begin(config) && logo.setM(2,true)); managedLogin(logo);
      queue("<rs><r i='v0' e='1'/>/<rs>"); CHECK(logo.loop());
      queue("<rs><r i='v1' e='0' v='01'/></rs>"); CHECK(logo.loop());
      CHECK(output.text.find("coincide por lectura") != std::string::npos);
      for (const auto& secret : {"wifi-secret","logo-secret",TEMP_HINT.c_str(),SESSION_HINT.c_str(),"<rs>"})
        CHECK(output.text.find(secret) == std::string::npos);
      CHECK(output.text.find("Confirmacion de escritura invalida") == std::string::npos);
    }},
    {"change callback can schedule output without network recursion", [] {
      CHQLogoWeb logo("127.0.0.1"); callbackLogo = &logo;
      CHECK(logo.begin(fixedConfig("secret")) && logo.watchM(1));
      logo.onChangeM(mirrorChanged); managedLogin(logo);
      queue("<rs><r i='v1' e='0' v='01'/></rs>"); CHECK(logo.loop());
      CHECK(fake::requests.size() == 3 && logo.writeStateM(2) == CHQLogoWriteState::Pending);
      queue("<rs><r i='v0' e='1'/>/<rs>"); CHECK(logo.loop());
      CHECK(body(3) == "SETVARS:v0,131,0,1,1,1,01"); callbackLogo = nullptr;
    }},
    {"queued verification timeout survives millis rollover", [] {
      CHQLogoWeb logo("127.0.0.1"); CHQLogoConfig config = fixedConfig("secret"); config.writeTimeoutMs = 20;
      CHECK(logo.begin(config) && logo.setM(2,true)); fake::now = UINT32_MAX - 10; managedLogin(logo);
      queue("<rs><r i='v0' e='1'/>/<rs>"); CHECK(logo.loop());
      fake::wifiConnected = false; fake::now += 19; CHECK(!logo.loop());
      CHECK(logo.writeStateM(2) == CHQLogoWriteState::Verifying);
      fake::now += 1; CHECK(!logo.loop()); CHECK(logo.writeStateM(2) == CHQLogoWriteState::Unknown);
    }},
    {"invalid LOGO configuration never changes network or sends a request", [] {
      CHQLogoWeb logo("127.0.0.1"); CHQLogoConfig config(std::string(257,'x'));
      CHECK(!logo.begin(config));
      config.password = "secret"; config.writeTimeoutMs = 0; CHECK(!logo.begin(config));
      config.writeTimeoutMs = 5000; config.requestTimeoutMs = 0; CHECK(!logo.begin(config));
      config.requestTimeoutMs = 4000; config.profile.ajaxPath = ""; CHECK(!logo.begin(config));
      CHECK(WiFi.beginCalls == 0 && WiFi.reconnectCalls == 0 && fake::requests.empty());
    }},
    {"discovery parser accepts only one complete scalar and safe result ID", [] {
      chqlogo::ScalarReply reply{}; char id[33] = "unchanged";
      const std::string good = "<?xml version='1.0'?><rs><r i='variable_7' e='0' v='01'/></rs>";
      CHECK(chqlogo::discoverScalar(good.data(),good.size(),2,reply,id) == ParseResult::Ok);
      CHECK(std::string(id) == "variable_7" && reply.value == 1);
      const std::vector<std::string> invalid = {
        "<rs><r i='v1' e='0' v='01'/><r i='v2' e='0' v='00'/></rs>",
        "<rs><r i='v1' e='0' v='01'/><r i='v1' e='0' v='01'/></rs>",
        "<rs><r i='bad,id' e='0' v='01'/></rs>", "<rs><r i='' e='0' v='01'/></rs>",
        "<rs><r i='v1' e='0' e='1' v='01'/></rs>", "<rs><r i='v1' e='0' v='1'/></rs>",
        "<rs><r i='v1' e='0' v='01'/>/<rs>", "<rs><r i='v1' e='0' v='01'/></rs>extra",
        "<rs><r i='"+std::string(33,'x')+"' e='0' v='01'/></rs>"};
      for (const auto& xml : invalid) {
        strcpy(id,"unchanged"); CHECK(chqlogo::discoverScalar(xml.data(),xml.size(),2,reply,id) != ParseResult::Ok);
        CHECK(std::string(id) == "unchanged");
      }
      const std::string rejected = "<rs><r i='v0' e='5'/></rs>";
      CHECK(chqlogo::discoverScalar(rejected.data(),rejected.size(),2,reply,id) == ParseResult::Rejected);
      CHECK(reply.error == 5 && std::string(id) == "unchanged");
      for (size_t n = 0; n < good.size(); ++n)
        CHECK(chqlogo::discoverScalar(good.data(),n,2,reply,id) != ParseResult::Ok);
    }},
    {"automatic IDs learn echo from two reads and retain most recent value", [] {
      CHQLogoWeb logo("127.0.0.1"); CHQLogoConfig config("secret"); CHECK(config.autoReadId);
      CHECK(logo.begin(config) && logo.watchM(1)); managedLogin(logo);
      queue("<rs><r i='v1' e='0' v='00'/></rs>"); queue("<rs><r i='v2' e='0' v='01'/></rs>");
      CHECK(logo.loop() && logo.availableM(1) && logo.readM(1));
      CHECK(logo.profile().readResponseId == "v1" && fake::requests.size() == 4);
      CHECK(body(2) == "GETVARS:v1,131,0,0,1,1" && body(3) == "GETVARS:v2,131,0,0,1,1");
      fake::now += 500; queue("<rs><r i='v1' e='0' v='00'/></rs>"); CHECK(logo.loop());
      CHECK(!logo.readM(1) && fake::requests.size() == 5);
    }},
    {"symbolic analog can discover read IDs before any digital read", [] {
      CHQLogoWeb logo("127.0.0.1"); CHQLogoConfig config("secret");
      config.profile.readAnalog = true;
      CHECK(logo.begin(config) && logo.watchAM(2)); managedLogin(logo);
      queue("<rs><r i='v1' e='0' v='02BC'/></rs>");
      queue("<rs><r i='v2' e='0' v='0320'/></rs>");
      CHECK(logo.loop() && logo.availableAM(2) && logo.readAM(2) == 800);
      CHECK(body(2) == "GETVARS:_local_=v1,AM..4:2-1");
      CHECK(body(3) == "GETVARS:_local_=v2,AM..4:2-1");
      CHECK(logo.profile().readResponseId == "v1");
      fake::now += 500; queue("<rs><r i='v1' e='0' v='FF88'/></rs>");
      CHECK(logo.loop() && logo.readAM(2) == -120);
      CHECK(body(4) == "GETVARS:_local_=v1,AM..4:2-1");
    }},
    {"symbolic AM2 uncertain writes verify once and explicit errors remain rejected", [] {
      CHQLogoWeb logo("127.0.0.1"); auto config = fixedConfig("secret");
      config.profile.readAnalog = config.profile.writeAnalog = true;
      CHECK(logo.begin(config) && logo.setAM(2,-120)); managedLogin(logo);
      queue("<rs><r i='v0' e='1'/>/<rs>"); CHECK(logo.loop());
      CHECK(body(2) == "SETVARS:_local_=v0,AM..4:2-1,ff88");
      CHECK(logo.writeStateAM(2) == CHQLogoWriteState::Verifying);
      queue("<rs><r i='v1' e='0' v='FF88'/></rs>"); CHECK(logo.loop());
      CHECK(body(3) == "GETVARS:_local_=v1,AM..4:2-1");
      CHECK(logo.writeStateAM(2) == CHQLogoWriteState::Matched);
      CHECK(logo.setAM(2,-120) && logo.loop() && fake::requests.size() == 4);
      CHECK(logo.setAM(2,700)); queue("<rs><r i='v0' e='3'/></rs>"); CHECK(logo.loop());
      CHECK(body(4) == "SETVARS:_local_=v0,AM..4:2-1,02bc");
      CHECK(logo.writeStateAM(2) == CHQLogoWriteState::Rejected);
      fake::now += 500; queue("<rs><r i='v1' e='0' v='02BC'/></rs>"); CHECK(logo.loop());
      CHECK(logo.readAM(2) == 700 && logo.writeStateAM(2) == CHQLogoWriteState::Rejected);
    }},
    {"automatic IDs learn fixed v0 v7 and other safe identifiers", [] {
      for (const std::string id : {"v0","v7","marker_12"}) {
        fake::reset(); CHQLogoWeb logo("127.0.0.1"); CHECK(logo.begin(CHQLogoConfig("secret")) && logo.watchM(1));
        managedLogin(logo);
        queue("<rs><r i='"+id+"' e='0' v='01'/></rs>");
        queue("<rs><r i='"+id+"' e='0' v='00'/></rs>");
        CHECK(logo.loop() && logo.availableM(1) && !logo.readM(1));
        CHECK(logo.profile().readResponseId == id.c_str());
      }
    }},
    {"automatic IDs reject inconsistent replies without publishing cache", [] {
      CHQLogoWeb logo("127.0.0.1"); CHECK(logo.begin(CHQLogoConfig("secret")) && logo.watchM(1)); managedLogin(logo);
      queue("<rs><r i='v0' e='0' v='01'/></rs>"); queue("<rs><r i='v8' e='0' v='01'/></rs>");
      CHECK(!logo.loop() && !logo.availableM(1) && !logo.connected());
      CHECK(logo.diagnostics().error == CHQLogoError::UnexpectedResponse && fake::requests.size() == 4);
    }},
    {"automatic IDs reject ambiguous truncated and nonbinary replies before probing again", [] {
      for (const std::string xml : {
          "<rs><r i='v1' e='0' v='01'/><r i='v2' e='0' v='00'/></rs>",
          "<rs><r i='v1' e='0' v='01'", "<rs><r i='v1' e='0' v='02'/></rs>"}) {
        fake::reset(); CHQLogoWeb logo("127.0.0.1"); CHECK(logo.begin(CHQLogoConfig("secret")) && logo.watchM(1));
        managedLogin(logo); queue(xml); CHECK(!logo.loop() && !logo.availableM(1));
        CHECK(fake::requests.size() == 3 && !logo.isAuthenticated());
      }
    }},
    {"no output is sent until automatic read ID validation finishes", [] {
      CHQLogoWeb logo("127.0.0.1"); CHECK(logo.begin(CHQLogoConfig("secret")) && logo.setM(2,true)); managedLogin(logo);
      queue("<rs><r i='v1' e='0' v='00'/></rs>"); queue("<rs><r i='v2' e='0' v='00'/></rs>"); CHECK(logo.loop());
      CHECK(logo.writeStateM(2) == CHQLogoWriteState::Pending && fake::requests.size() == 4);
      CHECK(body(2).find("GETVARS:") == 0 && body(3).find("GETVARS:") == 0);
      queue("<rs><r i='v0' e='1'/>/<rs>"); CHECK(logo.loop());
      queue("<rs><r i='v1' e='0' v='01'/></rs>"); CHECK(logo.loop());
      CHECK(logo.writeStateM(2) == CHQLogoWriteState::Matched && fake::requests.size() == 6);
    }},
    {"waitUntilReady waits for fresh data and suppresses all queued writes", [] {
      CHQLogoWeb logo("127.0.0.1"); CHECK(logo.begin(CHQLogoConfig("secret")) && logo.setM(2,true));
      queue("700,"+TEMP_HINT+",123456"); queue("700,"+SESSION_HINT);
      queue("<rs><r i='v1' e='0' v='00'/></rs>"); queue("<rs><r i='v2' e='0' v='00'/></rs>");
      queue("<rs><r i='v1' e='0' v='01'/></rs>");
      CHECK(logo.waitUntilReady() && logo.availableM(1));
      CHECK(logo.writeStateM(2) == CHQLogoWriteState::Pending && fake::requests.size() == 5);
      for (size_t i = 0; i < fake::requests.size(); ++i) CHECK(body(i).find("SETVARS:") != 0);
      queue("<rs><r i='v0' e='1'/>/<rs>"); CHECK(logo.loop());
      CHECK(body(5) == "SETVARS:v0,131,0,1,1,1,01");
    }},
    {"analog startup waits for AM only and suspends its queued write", [] {
      CHQLogoWeb logo("127.0.0.1"); CHQLogoConfig config("secret");
      config.profile.readAnalog = config.profile.writeAnalog = true;
      CHECK(logo.begin(config) && logo.setAM(2,30));
      queue("700,"+TEMP_HINT+",123456"); queue("700,"+SESSION_HINT);
      queue("<rs><r i='v1' e='0' v='02BC'/></rs>");
      queue("<rs><r i='v2' e='0' v='0320'/></rs>");
      CHECK(logo.waitUntilReadyAM(100,2) && logo.availableAM(2) && logo.readAM(2) == 800);
      CHECK(logo.writeStateAM(2) == CHQLogoWriteState::Pending && fake::requests.size() == 4);
      CHECK(!logo.availableM(1));
      for (size_t i = 0; i < fake::requests.size(); ++i) {
        CHECK(body(i).find("SETVARS:") != 0);
        CHECK(body(i).find(",131,") == std::string::npos);
      }
      queue("<rs><r i='v0' e='0'/></rs>"); CHECK(logo.loop());
      CHECK(body(4) == "SETVARS:_local_=v0,AM..4:2-1,001e");
    }},
    {"analog startup requires fresh signed data and accepts zero with a manual ID", [] {
      CHQLogoWeb logo("127.0.0.1"); auto config = fixedConfig("secret");
      config.profile.readAnalog = true; config.profile.readResponseId = "v9";
      CHECK(logo.begin(config));
      queue("700,"+TEMP_HINT+",123456"); queue("700,"+SESSION_HINT);
      queue("<rs><r i='v9' e='0' v='0000'/></rs>");
      CHECK(logo.waitUntilReadyAM(100,2) && logo.availableAM(2) && logo.readAM(2) == 0);
      queue("<rs><r i='v9' e='0' v='FF88'/></rs>");
      CHECK(logo.waitUntilReadyAM(100,2) && logo.readAM(2) == -120);
      CHECK(fake::requests.size() == 4);
    }},
    {"analog startup rejects disabled capability invalid range and missing begin", [] {
      CHQLogoWeb logo("127.0.0.1"); CHECK(!logo.waitUntilReadyAM(100,2));
      CHECK(logo.begin(CHQLogoConfig("secret"))); CHECK(!logo.waitUntilReadyAM(100,2));
      CHECK(logo.diagnostics().error == CHQLogoError::CapabilityDisabled);
      CHQLogoConfig config("secret"); config.profile.readAnalog = true; CHECK(logo.begin(config));
      CHECK(!logo.waitUntilReadyAM(100,0) && !logo.waitUntilReadyAM(100,9));
      CHECK(!logo.waitUntilReadyAM(UINT32_MAX,2) && fake::requests.empty());
    }},
    {"analog startup timeout survives rollover and leaves WiFi external", [] {
      CHQLogoWeb logo("127.0.0.1"); CHQLogoConfig config("secret"); config.profile.readAnalog = true;
      CHECK(logo.begin(config)); fake::wifiConnected = false; fake::now = UINT32_MAX-10;
      CHECK(!logo.waitUntilReadyAM(20,2) && fake::now == 9 && fake::requests.empty());
      CHECK(logo.diagnostics().error == CHQLogoError::Timeout && WiFi.beginCalls == 0 && WiFi.reconnectCalls == 0);
    }},
    {"waitUntilReady accepts a valid zero and manual ID override", [] {
      CHQLogoWeb logo("127.0.0.1"); auto config = fixedConfig("secret"); config.profile.readResponseId = "v9";
      CHECK(logo.begin(config)); queue("700,"+TEMP_HINT+",123456"); queue("700,"+SESSION_HINT);
      queue("<rs><r i='v9' e='0' v='00'/></rs>");
      CHECK(logo.waitUntilReady(100) && logo.availableM(1) && !logo.readM(1));
      CHECK(fake::requests.size() == 3);
      queue("<rs><r i='v9' e='0' v='01'/></rs>"); CHECK(logo.waitUntilReady(100));
      CHECK(logo.readM(1) && fake::requests.size() == 4);  // No usa cache antigua.
    }},
    {"waitUntilReady times out with no WiFi and can succeed after external recovery", [] {
      CHQLogoWeb logo("127.0.0.1"); CHECK(logo.begin(CHQLogoConfig("secret"))); fake::wifiConnected = false;
      CHECK(!logo.waitUntilReady(100) && fake::now == 100 && fake::requests.empty());
      CHECK(logo.diagnostics().error == CHQLogoError::Timeout && WiFi.beginCalls == 0 && WiFi.reconnectCalls == 0);
      fake::wifiConnected = true; queue("700,"+TEMP_HINT+",123456"); queue("700,"+SESSION_HINT);
      queue("<rs><r i='v1' e='0' v='01'/></rs>"); queue("<rs><r i='v2' e='0' v='01'/></rs>");
      CHECK(logo.waitUntilReady(100) && logo.availableM(1));
    }},
    {"blocking wait respects failed-login backoff", [] {
      CHQLogoWeb logo("127.0.0.1"); CHECK(logo.begin(CHQLogoConfig("secret")));
      raw("HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n");
      CHECK(!logo.waitUntilReady(50) && fake::now == 50 && fake::requests.size() == 1);
      CHECK(!logo.availableM(1) && !logo.connected());
    }},
    {"read ID is learned again after session loss", [] {
      CHQLogoWeb logo("127.0.0.1"); CHECK(logo.begin(CHQLogoConfig("secret")) && logo.watchM(1)); managedLogin(logo);
      queue("<rs><r i='v1' e='0' v='01'/></rs>"); queue("<rs><r i='v2' e='0' v='01'/></rs>"); CHECK(logo.loop());
      fake::wifiConnected = false; CHECK(!logo.loop()); fake::wifiConnected = true; managedLogin(logo);
      CHECK(!logo.availableM(1));
      queue("<rs><r i='v0' e='0' v='00'/></rs>"); queue("<rs><r i='v0' e='0' v='00'/></rs>"); CHECK(logo.loop());
      CHECK(logo.profile().readResponseId == "v0" && logo.availableM(1));
      fake::now += 500; queue("<rs><r i='v8' e='0' v='01'/></rs>"); CHECK(!logo.loop());
      CHECK(!logo.connected());  // No aprende silenciosamente otro ID en cada lectura.
    }},
    {"alternate probe ID is different even when configured request is v2", [] {
      CHQLogoWeb logo("127.0.0.1"); CHQLogoConfig config("secret"); config.profile.readRequestId = "v2";
      CHECK(logo.begin(config) && logo.watchM(1)); managedLogin(logo);
      queue("<rs><r i='v2' e='0' v='00'/></rs>"); queue("<rs><r i='v1' e='0' v='01'/></rs>"); CHECK(logo.loop());
      CHECK(logo.profile().readResponseId == "v2" && body(3).find("GETVARS:v1,") == 0);
    }},
    {"startup wait validates arguments without blocking or sending traffic", [] {
      CHQLogoWeb logo("127.0.0.1"); CHECK(!logo.waitUntilReady(100));
      CHECK(logo.begin(CHQLogoConfig("secret")));
      CHECK(!logo.waitUntilReady(100,0) && !logo.waitUntilReady(UINT32_MAX));
      CHECK(fake::requests.empty());
    }},
    {"startup wait timeout survives millis rollover", [] {
      CHQLogoWeb logo("127.0.0.1"); CHECK(logo.begin(CHQLogoConfig("secret")));
      fake::wifiConnected = false; fake::now = UINT32_MAX - 10;
      CHECK(!logo.waitUntilReady(20)); CHECK(fake::now == 9 && fake::requests.empty());
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
