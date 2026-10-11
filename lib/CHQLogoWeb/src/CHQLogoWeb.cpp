#include "CHQLogoWeb.h"
#include "CHQLogoProtocol.h"

#include <esp_system.h>
#include <stdio.h>
#include <string.h>

namespace {
bool safeText(const String& text, size_t maxLength, bool spaces = false) {
  if (text.isEmpty() || text.length() > maxLength) return false;
  for (size_t i = 0; i < text.length(); ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c < (spaces ? 32 : 33) || c > 126 || c == ',' || c == '\'' || c == '"' ||
        c == '<' || c == '>' || c == '&') return false;
  }
  return true;
}
bool safeId(const String& id) {
  if (!safeText(id, 32)) return false;
  for (size_t i = 0; i < id.length(); ++i) {
    const char c = id[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  }
  return true;
}
const char* transportMessage(CHQLogoError error) {
  switch (error) {
    case CHQLogoError::WifiDisconnected: return "Wi-Fi desconectado";
    case CHQLogoError::ConnectionFailed: return "Fallo de conexión TCP/TLS";
    case CHQLogoError::Timeout: return "Tiempo de espera agotado";
    case CHQLogoError::ResponseTooLarge: return "Respuesta excede el límite configurado";
    case CHQLogoError::IncompleteResponse: return "Respuesta HTTP incompleta";
    case CHQLogoError::HttpError: return "Estado HTTP no aceptado";
    case CHQLogoError::InvalidHttp: return "Respuesta HTTP inválida";
    case CHQLogoError::UnsupportedEncoding: return "Codificación HTTP no soportada";
    case CHQLogoError::TlsConfiguration: return "HTTPS requiere certificado raíz PEM";
    case CHQLogoError::OutOfMemory: return "Memoria insuficiente";
    default: return "Fallo del transporte";
  }
}
}  // namespace

CHQLogoProfile CHQLogoProfile::mockV1() { return CHQLogoProfile{}; }
CHQLogoProfile CHQLogoProfile::logo8AjaxExperimental() {
  CHQLogoProfile p;
  p.name = "LOGO8-AJAX-experimental";
  p.readAnalog = p.writeAnalog = false;
  return p;
}

CHQLogoWeb::CHQLogoWeb(const String& host, uint16_t port, CHQLogoTransport* transport)
    : _host(host), _port(port), _transport(transport ? transport : &chqLogoDefaultTransport()) {}

void CHQLogoWeb::resetDiagnostics() { _diagnostics = CHQLogoDiagnostics{}; _lastError = ""; }
bool CHQLogoWeb::fail(CHQLogoError code, const String& message) {
  _diagnostics.error = code;
  _lastError = message;
  return false;
}
void CHQLogoWeb::endSession() { _sessionHint = ""; }
bool CHQLogoWeb::isAuthenticated() const { return !_sessionHint.isEmpty(); }
String CHQLogoWeb::lastError() const { return _lastError; }
const CHQLogoDiagnostics& CHQLogoWeb::diagnostics() const { return _diagnostics; }
const CHQLogoProfile& CHQLogoWeb::profile() const { return _profile; }
const CHQLogoConnectionOptions& CHQLogoWeb::connectionOptions() const { return _options; }

bool CHQLogoWeb::setProfile(const CHQLogoProfile& profile) {
  resetDiagnostics();
  if (!safeText(profile.name, 64, true) || !safeText(profile.ajaxPath, 128) ||
      profile.ajaxPath[0] != '/' || profile.ajaxPath.indexOf('#') >= 0 ||
      !safeText(profile.webUser, 64, true) || !safeId(profile.readRequestId) ||
      !safeId(profile.readResponseId) || !safeId(profile.writeRequestId) ||
      !safeId(profile.writeResponseId) || profile.successCode < 700 ||
      profile.successCode > 799 || !profile.hintLength || profile.hintLength > 64 ||
      !profile.amCount || !profile.mCount || !profile.analogSize || !profile.digitalSize ||
      !profile.amAddressStride || !profile.mAddressStride ||
      profile.analogHexDigits != 4 || !profile.digitalHexDigits || profile.digitalHexDigits > 4)
    return fail(CHQLogoError::InvalidArgument, "Perfil de protocolo inválido");
  const uint32_t lastAM = profile.amAddressBase +
      static_cast<uint32_t>(profile.amCount - 1) * profile.amAddressStride;
  const uint32_t lastM = profile.mAddressBase +
      static_cast<uint32_t>(profile.mCount - 1) * profile.mAddressStride;
  if (lastAM > UINT16_MAX || lastM > UINT16_MAX)
    return fail(CHQLogoError::InvalidArgument, "Direcciones del perfil exceden 16 bits");
  _profile = profile;
  endSession();
  return true;
}

void CHQLogoWeb::setTimeout(uint16_t milliseconds) {
  _options.connectTimeoutMs = _options.ioTimeoutMs = milliseconds ? milliseconds : 1;
}
bool CHQLogoWeb::setConnectionOptions(const CHQLogoConnectionOptions& options) {
  resetDiagnostics();
  if (!options.connectTimeoutMs || !options.ioTimeoutMs || options.maxResponseBytes < 64 ||
      options.maxResponseBytes > 16384 || (options.https && options.rootCA.isEmpty()))
    return fail(CHQLogoError::InvalidArgument, "Opciones de conexión inválidas");
  _options = options;
  endSession();
  return true;
}
bool CHQLogoWeb::useHTTPS(const String& rootCA, uint16_t port) {
  resetDiagnostics();
  if (rootCA.isEmpty() || !port) return fail(CHQLogoError::TlsConfiguration, "HTTPS requiere CA y puerto válido");
  _options.https = true;
  _options.rootCA = rootCA;
  _port = port;
  endSession();
  return true;
}
void CHQLogoWeb::useHTTP(uint16_t port) {
  _options.https = false;
  _options.rootCA = "";
  _port = port;
  endSession();
  resetDiagnostics();
}

bool CHQLogoWeb::post(const String& command, const String& hint, String& response) {
  response = "";
  if (!safeText(_host, 253) || _host.indexOf('/') >= 0 || _host.indexOf(':') >= 0 ||
      _host.indexOf('@') >= 0 || _host.indexOf('?') >= 0 || _host.indexOf('#') >= 0 || !_port ||
      command.isEmpty() || command.length() > 2048)
    return fail(CHQLogoError::InvalidArgument, "Host/puerto/comando inválido");
  if (!_transport->post(_host, _port, _profile.ajaxPath, command, hint, _options, response, _diagnostics)) {
    response = "";
    if (_diagnostics.error == CHQLogoError::None) _diagnostics.error = CHQLogoError::ConnectionFailed;
    if (_diagnostics.error == CHQLogoError::WifiDisconnected || _diagnostics.httpStatus == 401 ||
        _diagnostics.httpStatus == 403) endSession();
    return fail(_diagnostics.error, transportMessage(_diagnostics.error));
  }
  // También se aplica a transportes externos; no aceptar éxito con HTTP != 200.
  if (_diagnostics.httpStatus != 200 || response.length() > _options.maxResponseBytes ||
      _diagnostics.error != CHQLogoError::None) {
    if (_diagnostics.httpStatus == 401 || _diagnostics.httpStatus == 403) endSession();
    const bool tooLarge = response.length() > _options.maxResponseBytes;
    response = "";
    return fail(tooLarge ? CHQLogoError::ResponseTooLarge :
                CHQLogoError::HttpError, "Respuesta de transporte no aceptada");
  }
  uint16_t status;
  if (chqlogo::parseStatus(response.c_str(), response.length(), status)) {
    _diagnostics.protocolStatus = status;
    if (status >= 700 && status <= 799 && status != _profile.successCode) {
      // Conservador: una respuesta UAM de error requiere reautenticación.
      endSession();
      return fail(CHQLogoError::ProtocolRejected, "Servidor rechazó la sesión/autenticación");
    }
  }
  _lastError = "";
  return true;
}

bool CHQLogoWeb::login(const String& password) {
  resetDiagnostics();
  endSession();
  if (password.length() > 256) return fail(CHQLogoError::InvalidArgument, "Contraseña demasiado larga");
  const uint32_t a = esp_random(), b = esp_random(), c = esp_random(), d = esp_random();
  String response;
  const String request = "UAMCHAL:3,4," + String(a) + "," + String(b) + "," + String(c) + "," + String(d);
  if (!post(request, "p", response)) return false;
  chqlogo::Challenge challenge{};
  if (!chqlogo::parseChallenge(response.c_str(), response.length(), _profile.successCode,
                               _profile.hintLength, challenge))
    return fail(CHQLogoError::UnexpectedResponse, "Respuesta UAMCHAL inválida");
  String pwToken = password + "+" + challenge.decimal;
  if (pwToken.length() > 32) pwToken.remove(32);
  const uint32_t pwProof = chqlogo::crc32(pwToken.c_str(), pwToken.length()) ^ challenge.value;
  const uint32_t serverProof = a ^ b ^ c ^ d ^ challenge.value;
  const String command = "UAMLOGIN:" + _profile.webUser + "," + String(pwProof) + "," + String(serverProof);
  if (!post(command, challenge.hint, response)) return false;
  char session[65]{};
  if (!chqlogo::parseLogin(response.c_str(), response.length(), _profile.successCode,
                           _profile.hintLength, session))
    return fail(CHQLogoError::UnexpectedResponse, "Respuesta UAMLOGIN inválida");
  _sessionHint = session;
  return true;
}

bool CHQLogoWeb::exchangeRaw(const String& command, String& response) {
  resetDiagnostics();
  response = "";
  if (!isAuthenticated()) return fail(CHQLogoError::NotAuthenticated, "Primero ejecuta login()");
  return post(command, _sessionHint, response);
}

bool CHQLogoWeb::parseScalar(const String& response, const String& id, uint8_t digits,
                             bool requireValue, uint16_t& raw) {
  chqlogo::ScalarReply reply{};
  const chqlogo::ParseResult result = chqlogo::parseScalar(
      response.c_str(), response.length(), id.c_str(), digits, requireValue, reply);
  if (result == chqlogo::ParseResult::Rejected) {
    _diagnostics.protocolStatus = reply.error;
    return fail(CHQLogoError::ProtocolRejected, "Servidor rechazó la operación sobre la variable");
  }
  if (result != chqlogo::ParseResult::Ok) {
    endSession();  // Una página de login/error no debe dejar un token reutilizable.
    return fail(CHQLogoError::UnexpectedResponse, "Respuesta XML inesperada/incompleta");
  }
  _diagnostics.protocolStatus = 0;
  raw = reply.value;
  return true;
}

bool CHQLogoWeb::writeScalar(uint16_t kind, uint16_t address, uint8_t size, uint16_t value, uint8_t digits) {
  char hex[5];
  snprintf(hex, sizeof(hex), "%0*X", digits, static_cast<unsigned>(value));
  const String command = "SETVARS:" + _profile.writeRequestId + "," + String(kind) + "," +
      String(_profile.area) + "," + String(address) + "," + String(size) + ",1," + hex;
  String response;
  if (!exchangeRaw(command, response)) return false;
  uint16_t raw;
  return parseScalar(response, _profile.writeResponseId, digits, false, raw);
}

bool CHQLogoWeb::readScalar(uint16_t kind, uint16_t address, uint8_t size, uint8_t digits, uint16_t& raw) {
  const String command = "GETVARS:" + _profile.readRequestId + "," + String(kind) + "," +
      String(_profile.area) + "," + String(address) + "," + String(size) + ",1";
  String response;
  return exchangeRaw(command, response) && parseScalar(response, _profile.readResponseId, digits, true, raw);
}

bool CHQLogoWeb::writeAM(int32_t number, int32_t value) {
  resetDiagnostics();
  if (!_profile.writeAnalog) return fail(CHQLogoError::CapabilityDisabled, "Escritura AM deshabilitada en este perfil");
  if (number < 1 || number > _profile.amCount || value < INT16_MIN || value > INT16_MAX)
    return fail(CHQLogoError::InvalidArgument, "Número AM o valor fuera del rango del perfil");
  const uint16_t address = _profile.amAddressBase + static_cast<uint32_t>(number - 1) * _profile.amAddressStride;
  return writeScalar(_profile.amType, address, _profile.analogSize, static_cast<uint16_t>(value), _profile.analogHexDigits);
}
bool CHQLogoWeb::readAM(int32_t number, int16_t& value) {
  resetDiagnostics();
  if (!_profile.readAnalog) return fail(CHQLogoError::CapabilityDisabled, "Lectura AM deshabilitada en este perfil");
  if (number < 1 || number > _profile.amCount) return fail(CHQLogoError::InvalidArgument, "AM fuera del rango del perfil");
  uint16_t raw;
  const uint16_t address = _profile.amAddressBase + static_cast<uint32_t>(number - 1) * _profile.amAddressStride;
  if (!readScalar(_profile.amType, address, _profile.analogSize, _profile.analogHexDigits, raw)) return false;
  value = raw >= 0x8000 ? static_cast<int16_t>(static_cast<int32_t>(raw) - 65536) : static_cast<int16_t>(raw);
  return true;
}
bool CHQLogoWeb::writeM(int32_t number, bool state) {
  resetDiagnostics();
  if (!_profile.writeDigital) return fail(CHQLogoError::CapabilityDisabled, "Escritura M deshabilitada en este perfil");
  if (number < 1 || number > _profile.mCount) return fail(CHQLogoError::InvalidArgument, "M fuera del rango del perfil");
  const uint16_t address = _profile.mAddressBase + static_cast<uint32_t>(number - 1) * _profile.mAddressStride;
  return writeScalar(_profile.mType, address, _profile.digitalSize, state ? 1 : 0, _profile.digitalHexDigits);
}
bool CHQLogoWeb::readM(int32_t number, bool& state) {
  resetDiagnostics();
  if (!_profile.readDigital) return fail(CHQLogoError::CapabilityDisabled, "Lectura M deshabilitada en este perfil");
  if (number < 1 || number > _profile.mCount) return fail(CHQLogoError::InvalidArgument, "M fuera del rango del perfil");
  uint16_t raw;
  const uint16_t address = _profile.mAddressBase + static_cast<uint32_t>(number - 1) * _profile.mAddressStride;
  if (!readScalar(_profile.mType, address, _profile.digitalSize, _profile.digitalHexDigits, raw)) return false;
  if (raw > 1) return fail(CHQLogoError::UnexpectedResponse, "Marca digital no binaria");
  state = raw == 1;
  return true;
}
