#include "CHQLogoTransport.h"
#include "CHQLogoProtocol.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <stdio.h>

namespace {
// Lectura acotada: evita getString()/writeToStream() del core Arduino 2.x.
class HttpReader {
 public:
  HttpReader(WiFiClient& client, const CHQLogoConnectionOptions& options,
             CHQLogoDiagnostics& diagnostics)
      : _client(client), _options(options), _diag(diagnostics), _started(millis()) {}

  bool write(const String& request) {
    size_t sent = 0;
    while (sent < request.length()) {
      if (!withinDeadline()) return false;
      if (!_client.connected()) return fail(CHQLogoError::ConnectionFailed);
      const size_t remaining = request.length() - sent;
      const size_t amount = remaining < 128 ? remaining : 128;
      const size_t written = _client.write(
          reinterpret_cast<const uint8_t*>(request.c_str()) + sent, amount);
      if (written > amount) return fail(CHQLogoError::InvalidHttp);
      sent += written;
      if (!written) delay(1);
    }
    return withinDeadline();
  }

  bool read(String& response) {
    String line;
    if (!readLine(line)) return false;
    if (line.length() < 12 ||
        (line.substring(0, 9) != "HTTP/1.0 " && line.substring(0, 9) != "HTTP/1.1 ") ||
        (line.length() > 12 && line[12] != ' ')) return fail(CHQLogoError::InvalidHttp);
    uint32_t code;
    if (!chqlogo::parseUInt32(line.c_str() + 9, 3, code) || code < 100 || code > 599)
      return fail(CHQLogoError::InvalidHttp);
    _diag.httpStatus = static_cast<int>(code);
    // No se leen cuerpos de error, que pueden contener secretos.
    if (code != 200) return fail(CHQLogoError::HttpError);

    bool hasLength = false, chunked = false, hasTransfer = false, hasEncoding = false;
    uint32_t length = 0;
    for (;;) {
      if (!readLine(line)) return false;
      if (line.isEmpty()) break;
      const int colon = line.indexOf(':');
      if (colon <= 0) return fail(CHQLogoError::InvalidHttp);
      String name = line.substring(0, colon);
      for (size_t i = 0; i < name.length(); ++i) {
        const char c = name[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '-')) return fail(CHQLogoError::InvalidHttp);
      }
      name.toLowerCase();
      String value = line.substring(colon + 1);
      value.trim();
      if (name == "content-length") {
        if (hasLength || !chqlogo::parseUInt32(value.c_str(), value.length(), length))
          return fail(CHQLogoError::InvalidHttp);
        hasLength = true;
        if (length > _options.maxResponseBytes) return fail(CHQLogoError::ResponseTooLarge);
      } else if (name == "transfer-encoding") {
        if (hasTransfer) return fail(CHQLogoError::InvalidHttp);
        hasTransfer = true;
        value.toLowerCase();
        if (value != "chunked") return fail(CHQLogoError::UnsupportedEncoding);
        chunked = true;
      } else if (name == "content-encoding") {
        if (hasEncoding) return fail(CHQLogoError::InvalidHttp);
        hasEncoding = true;
        value.toLowerCase();
        if (value != "identity") return fail(CHQLogoError::UnsupportedEncoding);
      }
    }
    if (hasLength && chunked) return fail(CHQLogoError::InvalidHttp);
    const size_t reserve = hasLength ? length : _options.maxResponseBytes;
    if (reserve && !response.reserve(reserve)) return fail(CHQLogoError::OutOfMemory);
    if (chunked) {
      for (;;) {
        if (!readLine(line)) return false;
        const int semicolon = line.indexOf(';');
        const String size = semicolon < 0 ? line : line.substring(0, semicolon);
        uint32_t chunk;
        if (!chunkSize(size, chunk))
          return fail(CHQLogoError::InvalidHttp);
        if (!chunk) {
          // Trailers sujetos al mismo presupuesto de cabeceras y tiempo.
          do {
            if (!readLine(line)) return false;
            if (!line.isEmpty() && line.indexOf(':') <= 0) return fail(CHQLogoError::InvalidHttp);
          } while (!line.isEmpty());
          return withinDeadline();
        }
        if (!readCount(chunk, response)) return false;
        uint8_t cr, lf;
        if (!readByte(cr) || !readByte(lf)) return false;
        if (cr != '\r' || lf != '\n') return fail(CHQLogoError::InvalidHttp);
      }
    }
    if (hasLength) return readCount(length, response) && withinDeadline();
    // Cuerpo delimitado por cierre; Connection: close se pide explícitamente.
    while (_client.connected() || _client.available()) {
      if (!withinDeadline()) return false;
      if (!_client.available()) { delay(1); continue; }
      uint8_t byte;
      if (!readByte(byte) || !append(response, byte)) return false;
    }
    return withinDeadline();
  }

 private:
  WiFiClient& _client;
  const CHQLogoConnectionOptions& _options;
  CHQLogoDiagnostics& _diag;
  uint32_t _started;
  size_t _headerBytes = 0;

  bool fail(CHQLogoError error) { _diag.error = error; return false; }
  static bool chunkSize(const String& text, uint32_t& value) {
    if (text.isEmpty() || text.length() > 8) return false;
    uint32_t parsed = 0;
    for (size_t i = 0; i < text.length(); ++i) {
      uint16_t digit;
      if (!chqlogo::parseHex(text.c_str() + i, 1, digit)) return false;
      parsed = (parsed << 4) | digit;
    }
    value = parsed;
    return true;
  }
  bool withinDeadline() {
    return millis() - _started < _options.ioTimeoutMs || fail(CHQLogoError::Timeout);
  }
  bool readByte(uint8_t& byte) {
    for (;;) {
      if (!withinDeadline()) return false;
      if (_client.available()) {
        const int value = _client.read();
        if (value >= 0) { byte = static_cast<uint8_t>(value); return true; }
      } else if (!_client.connected()) return fail(CHQLogoError::IncompleteResponse);
      delay(1);
    }
  }
  bool readLine(String& line) {
    line = "";
    for (;;) {
      uint8_t byte;
      if (!readByte(byte)) return false;
      if (++_headerBytes > 8192) return fail(CHQLogoError::ResponseTooLarge);
      if (byte == '\r') {
        if (!readByte(byte)) return false;
        if (++_headerBytes > 8192) return fail(CHQLogoError::ResponseTooLarge);
        return byte == '\n' || fail(CHQLogoError::InvalidHttp);
      }
      if ((byte < 32 && byte != '\t') || byte == 127) return fail(CHQLogoError::InvalidHttp);
      if (line.length() >= 512) return fail(CHQLogoError::ResponseTooLarge);
      if (!line.concat(static_cast<char>(byte))) return fail(CHQLogoError::OutOfMemory);
    }
  }
  bool append(String& response, uint8_t byte) {
    if (response.length() >= _options.maxResponseBytes) return fail(CHQLogoError::ResponseTooLarge);
    if (!response.concat(static_cast<char>(byte))) return fail(CHQLogoError::OutOfMemory);
    return true;
  }
  bool readCount(size_t count, String& response) {
    if (count > _options.maxResponseBytes - response.length())
      return fail(CHQLogoError::ResponseTooLarge);
    for (size_t i = 0; i < count; ++i) {
      uint8_t byte;
      if (!readByte(byte) || !append(response, byte)) return false;
    }
    return true;
  }
};

class Esp32Transport : public CHQLogoTransport {
 public:
  bool post(const String& host, uint16_t port, const String& path,
            const String& command, const String& hint,
            const CHQLogoConnectionOptions& options, String& response,
            CHQLogoDiagnostics& diagnostics) override {
    diagnostics = CHQLogoDiagnostics{};
    response = "";
    const uint32_t started = millis();
    if (WiFi.status() != WL_CONNECTED) {
      diagnostics.error = CHQLogoError::WifiDisconnected;
      return false;
    }
    if (options.https && options.rootCA.isEmpty()) {
      diagnostics.error = CHQLogoError::TlsConfiguration;
      return false;
    }
    if (options.https) {
      WiFiClientSecure secure;
      secure.setCACert(options.rootCA.c_str());
      secure.setHandshakeTimeout((options.connectTimeoutMs + 999UL) / 1000UL);
      return exchange(secure, host, port, path, command, hint, options, response, diagnostics, started);
    }
    WiFiClient plain;
    return exchange(plain, host, port, path, command, hint, options, response, diagnostics, started);
  }

 private:
  static void closeAndMeasure(WiFiClient& client, CHQLogoDiagnostics& diagnostics, uint32_t started) {
    const uint32_t closing = millis();
    client.stop();
    diagnostics.closeMs = millis() - closing;
    diagnostics.elapsedMs = millis() - started;
  }

  static bool exchange(WiFiClient& client, const String& host, uint16_t port,
                       const String& path, const String& command, const String& hint,
                       const CHQLogoConnectionOptions& options, String& response,
                       CHQLogoDiagnostics& diagnostics, uint32_t started) {
    // DNS/TCP/TLS dependen de los límites del core; nuestros bucles usan millis().
    client.setTimeout((options.ioTimeoutMs + 999UL) / 1000UL);
    const uint32_t connecting = millis();
    const bool connected = client.connect(host.c_str(), port, options.connectTimeoutMs);
    diagnostics.connectMs = millis() - connecting;
    if (!connected) {
      diagnostics.error = CHQLogoError::ConnectionFailed;
      closeAndMeasure(client, diagnostics, started);
      return false;
    }
    const int noDelayResult = client.setNoDelay(options.tcpNoDelay);
    diagnostics.tcpNoDelayApplied = options.tcpNoDelay && noDelayResult == 0;
    char header[768];
    const int headerLength = snprintf(header, sizeof(header),
        "POST %s HTTP/1.1\r\nHost: %s:%u\r\nConnection: close\r\nAccept-Encoding: identity\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\nSecurity-Hint: %s\r\n"
        "Content-Length: %u\r\n\r\n", path.c_str(), host.c_str(), static_cast<unsigned>(port),
        hint.c_str(), static_cast<unsigned>(command.length()));
    String request;
    if (headerLength < 0 || static_cast<size_t>(headerLength) >= sizeof(header) ||
        !request.reserve(static_cast<size_t>(headerLength) + command.length()) ||
        !request.concat(header) || !request.concat(command)) {
      diagnostics.error = CHQLogoError::OutOfMemory;
      closeAndMeasure(client, diagnostics, started);
      return false;
    }
    HttpReader reader(client, options, diagnostics);
    const uint32_t sending = millis();
    bool ok = reader.write(request);
    diagnostics.sendMs = millis() - sending;
    if (ok) {
      const uint32_t receiving = millis();
      ok = reader.read(response);
      diagnostics.receiveMs = millis() - receiving;
    }
    diagnostics.responseBytes = response.length();
    closeAndMeasure(client, diagnostics, started);
    if (!ok) response = "";
    return ok;
  }
};
}  // namespace

CHQLogoTransport& chqLogoDefaultTransport() {
  static Esp32Transport transport;
  return transport;
}
