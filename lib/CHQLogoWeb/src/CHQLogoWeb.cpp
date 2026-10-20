#include "CHQLogoWeb.h"
#include "CHQLogoProtocol.h"

#include <esp_system.h>
#include <WiFi.h>
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
  // Formato de js/variable.js del PLC: AM..4:numero-1, indice desde 1.
  p.analogSize = 4;
  p.symbolicAnalog = true;
  p.amAddressBase = 1;
  p.amAddressStride = 1;
  p.readAnalog = p.writeAnalog = false;
  return p;
}

CHQLogoConfig::CHQLogoConfig(const String& logoKey, const String& user, Print* output)
    : password(logoKey),
      profile(CHQLogoProfile::logo8AjaxExperimental()), debug(output) {
  profile.webUser = user;
  profile.readResponseId = profile.readRequestId;
}

CHQLogoWeb::CHQLogoWeb(const String& host, uint16_t port, CHQLogoTransport* transport)
    : _host(host), _port(port), _transport(transport ? transport : &chqLogoDefaultTransport()) {}

void CHQLogoWeb::resetDiagnostics() { _diagnostics = CHQLogoDiagnostics{}; _lastError = ""; }
bool CHQLogoWeb::fail(CHQLogoError code, const String& message) {
  _diagnostics.error = code;
  _lastError = message;
  return false;
}
void CHQLogoWeb::endSession() { _sessionHint = ""; _readIdDetected = false; invalidateCache(); }
bool CHQLogoWeb::isAuthenticated() const { return !_sessionHint.isEmpty(); }
String CHQLogoWeb::lastError() const { return _lastError; }
const CHQLogoDiagnostics& CHQLogoWeb::diagnostics() const { return _diagnostics; }
const CHQLogoProfile& CHQLogoWeb::profile() const { return _profile; }
const CHQLogoConnectionOptions& CHQLogoWeb::connectionOptions() const { return _options; }

void CHQLogoWeb::invalidateCache() {
  for (uint8_t i = 0; i < _watchCount; ++i) {
    _watched[i].valid = _watched[i].seen = _watched[i].attempted = false;
  }
  _nextWatch = 0;
}

int CHQLogoWeb::findWatched(bool analog, int32_t number) const {
  for (uint8_t i = 0; i < _watchCount; ++i)
    if (_watched[i].analog == analog && _watched[i].number == number) return i;
  return -1;
}

bool CHQLogoWeb::watch(bool analog, int32_t number) {
  if (number < 1 || number > (analog ? _profile.amCount : _profile.mCount))
    return fail(CHQLogoError::InvalidArgument, "Variable fuera del rango del perfil");
  if (!(analog ? _profile.readAnalog : _profile.readDigital))
    return fail(CHQLogoError::CapabilityDisabled, "Lectura deshabilitada en este perfil");
  if (findWatched(analog, number) >= 0) return true;
  if (_watchCount == MaxWatchedValues)
    return fail(CHQLogoError::InvalidArgument, "Maximo de 16 variables vigiladas");
  WatchedValue& item = _watched[_watchCount++];
  item.analog = analog;
  item.number = number;
  return true;
}

bool CHQLogoWeb::watchM(int32_t number) { return watch(false, number); }
bool CHQLogoWeb::watchAM(int32_t number) { return watch(true, number); }
bool CHQLogoWeb::connected() const {
  return WiFi.status() == WL_CONNECTED && isAuthenticated();
}
bool CHQLogoWeb::available(bool analog, int32_t number) const {
  const int index = findWatched(analog, number);
  return connected() && index >= 0 && _watched[index].valid;
}
bool CHQLogoWeb::availableM(int32_t number) const { return available(false, number); }
bool CHQLogoWeb::availableAM(int32_t number) const { return available(true, number); }
bool CHQLogoWeb::readM(int32_t number) {
  if (!watchM(number)) return false;
  return _watched[findWatched(false, number)].value != 0;
}
int16_t CHQLogoWeb::readAM(int32_t number) {
  if (!watchAM(number)) return 0;
  return _watched[findWatched(true, number)].value;
}
void CHQLogoWeb::onChangeM(DigitalChange callback) { _digitalChange = callback; }
void CHQLogoWeb::onChangeAM(AnalogChange callback) { _analogChange = callback; }

bool CHQLogoWeb::begin(const String& password, uint32_t pollIntervalMs,
                       uint32_t reconnectIntervalMs) {
  resetDiagnostics();
  if (password.length() > 256 || !pollIntervalMs || pollIntervalMs > INT32_MAX ||
      !reconnectIntervalMs || reconnectIntervalMs > INT32_MAX)
    return fail(CHQLogoError::InvalidArgument, "Clave o intervalos invalidos");
  String savedPassword;
  if (!savedPassword.reserve(password.length() + 1) || !savedPassword.concat(password))
    return fail(CHQLogoError::OutOfMemory, "No se pudo conservar la clave para reconexion");
  _managedPassword = savedPassword;
  if (_managedPassword != password) {
    end();
    return fail(CHQLogoError::OutOfMemory, "No se pudo conservar la clave para reconexion");
  }
  _pollIntervalMs = pollIntervalMs;
  _reconnectIntervalMs = reconnectIntervalMs;
  _retryPending = false;
  _managed = true;
  _autoReadId = false;
  _debug = nullptr;
  clearWrites();
  endSession();
  return true;
}

bool CHQLogoWeb::begin(const CHQLogoConfig& config) {
  if (!config.requestTimeoutMs ||
      !config.writeTimeoutMs || config.writeTimeoutMs > INT32_MAX ||
      config.password.length() > 256 || !config.pollIntervalMs || config.pollIntervalMs > INT32_MAX ||
      !config.reconnectIntervalMs || config.reconnectIntervalMs > INT32_MAX)
    return fail(CHQLogoError::InvalidArgument, "Configuracion de conexion invalida");
  if (!setProfile(config.profile)) return false;
  if (!begin(config.password, config.pollIntervalMs, config.reconnectIntervalMs)) {
    end();
    return false;
  }
  setTimeout(config.requestTimeoutMs);
  _writeTimeoutMs = config.writeTimeoutMs;
  _autoReadId = config.autoReadId;
  _debug = config.debug;
  _reportedConnected = false;
  _reportedError = CHQLogoError::None;
  if (_debug) _debug->printf("LOGO %s:%u | conexion y lecturas automaticas\n", _host.c_str(), _port);
  return true;
}

bool CHQLogoWeb::waitUntilReady(uint32_t timeoutMs, int32_t markerNumber) {
  return waitForReady(false, timeoutMs, markerNumber);
}

bool CHQLogoWeb::waitUntilReadyAM(uint32_t timeoutMs, int32_t markerNumber) {
  return waitForReady(true, timeoutMs, markerNumber);
}

bool CHQLogoWeb::waitForReady(bool analog, uint32_t timeoutMs, int32_t markerNumber) {
  if (!_managed || _inLoop || _suspendWrites || timeoutMs > INT32_MAX)
    return fail(CHQLogoError::InvalidArgument, "waitUntilReady requiere begin y una llamada externa a loop");
  if (!watch(analog, markerNumber)) return false;
  invalidateWritten(analog, markerNumber);
  const uint32_t started = millis();
  _suspendWrites = true;
  while (_managed) {
    if (timeoutMs && static_cast<uint32_t>(millis() - started) >= timeoutMs) break;
    loop();
    if (timeoutMs && static_cast<uint32_t>(millis() - started) >= timeoutMs) break;
    if (available(analog, markerNumber)) { _suspendWrites = false; return true; }
    delay(10);
  }
  _suspendWrites = false;
  return fail(_managed ? CHQLogoError::Timeout : CHQLogoError::NotAuthenticated,
              _managed ? "Tiempo agotado esperando login y lectura del LOGO" : "Espera LOGO cancelada por end()");
}

void CHQLogoWeb::end() {
  _managed = false;
  _managedPassword = "";
  _retryPending = false;
  clearWrites();
  endSession();
}

bool CHQLogoWeb::loop() {
  if (!_managed || _inLoop) return connected();
  serviceWriteTimeouts();
  const bool ok = service();
  report();
  return ok;
}

bool CHQLogoWeb::service() {
  if (!_managed || _inLoop) return connected();
  _queuedAckNeedsReadback = false;
  if (WiFi.status() != WL_CONNECTED) {
    endSession();
    _retryPending = false;
    return fail(CHQLogoError::WifiDisconnected, "Wi-Fi desconectado");
  }
  const uint32_t now = millis();
  if (!isAuthenticated()) {
    if (_retryPending && static_cast<uint32_t>(now - _lastFailureMs) < _reconnectIntervalMs)
      return false;
    _inLoop = true;
    const bool ok = login(_managedPassword);
    _inLoop = false;
    _retryPending = !ok;
    if (!ok) _lastFailureMs = millis();
    return ok;
  }
  for (uint8_t scanned = 0; scanned < _watchCount; ++scanned) {
    const uint8_t index = (_nextWatch + scanned) % _watchCount;
    WatchedValue& item = _watched[index];
    if (item.writeState == CHQLogoWriteState::Pending && !_suspendWrites &&
        (!_autoReadId || _readIdDetected)) {
      _inLoop = true;
      item.sent = item.desired;
      item.writeStartedMs = millis();
      item.readAfterWrite = false;
      item.writeState = CHQLogoWriteState::Verifying;
      const bool accepted = item.analog ? writeAM(item.number, item.sent) : writeM(item.number, item.sent != 0);
      item.writeStartedMs = millis();
      // Una unidad fisica aplica AM pese a e=16. Solo un perfil explicitamente
      // configurado permite comprobar ese codigo por GET, sin aceptarlo como ACK.
      const bool observedAMCode = !accepted && item.analog && _profile.symbolicAnalog &&
          _profile.readAnalog && _profile.analogWriteReadbackCode != 0 &&
          _diagnostics.httpStatus == 200 && _diagnostics.error == CHQLogoError::ProtocolRejected &&
          isAuthenticated() && _diagnostics.protocolStatus == _profile.analogWriteReadbackCode;
      _queuedAckNeedsReadback = observedAMCode ||
          (!accepted && _diagnostics.error == CHQLogoError::UnexpectedResponse);
      if (observedAMCode) {
        _lastError = "Respuesta AM con codigo observado; verificar por lectura";
        if (_debug) _debug->printf("AM%ld respuesta e=%d observada; requiere lectura posterior\n",
                                   static_cast<long>(item.number), _diagnostics.protocolStatus);
      }
      _nextWatch = (index + 1) % _watchCount;
      if (!accepted && (_diagnostics.error == CHQLogoError::InvalidArgument ||
          _diagnostics.error == CHQLogoError::CapabilityDisabled ||
          (_diagnostics.error == CHQLogoError::ProtocolRejected && isAuthenticated() && !observedAMCode))) {
        finishWrite(item, CHQLogoWriteState::Rejected);
      } else if (_debug) {
        _debug->printf("%s%ld solicitado=%d; verificando por lectura\n", item.analog ? "AM" : "M",
                       static_cast<long>(item.number), item.sent);
      }
      _inLoop = false;
      return connected();
    }
    if (item.attempted && static_cast<uint32_t>(now - item.lastAttemptMs) < _pollIntervalMs)
      continue;
    _inLoop = true;
    int16_t value = 0;
    bool digital = false;
    const bool ok = item.analog ? readAM(item.number, value) : readM(item.number, digital);
    if (!item.analog) value = digital ? 1 : 0;
    item.attempted = true;
    item.lastAttemptMs = millis();
    _nextWatch = (index + 1) % _watchCount;
    if (ok) {
      const bool changed = !item.seen || value != item.value;
      item.value = value;
      item.valid = item.seen = true;
      if (item.writeState == CHQLogoWriteState::Verifying) {
        item.readAfterWrite = true;
        if (value == item.sent) finishWrite(item, CHQLogoWriteState::Matched);
      }
      if (changed && _debug) _debug->printf("%s%ld=%d\n", item.analog ? "AM" : "M",
                                           static_cast<long>(item.number), value);
      if (changed) {
        if (item.analog && _analogChange) _analogChange(item.number, value);
        else if (!item.analog && _digitalChange) _digitalChange(item.number, value != 0);
      }
    } else {
      item.valid = false;
      // Un rechazo de esta variable no requiere repetir un login valido.
      if (!isAuthenticated() || (_diagnostics.error != CHQLogoError::ProtocolRejected &&
          _diagnostics.error != CHQLogoError::CapabilityDisabled &&
          _diagnostics.error != CHQLogoError::InvalidArgument)) {
        endSession();
        _retryPending = true;
        _lastFailureMs = millis();
      }
    }
    _inLoop = false;
    return connected();
  }
  return connected();
}

void CHQLogoWeb::clearWrites() {
  for (uint8_t i = 0; i < _watchCount; ++i) {
    _watched[i].hasDesired = false;
    _watched[i].writeState = CHQLogoWriteState::None;
  }
}

bool CHQLogoWeb::setDesired(bool analog, int32_t number, int32_t value) {
  if (!_managed) return fail(CHQLogoError::InvalidArgument, "Primero ejecuta begin()");
  if (!(analog ? _profile.writeAnalog : _profile.writeDigital))
    return fail(CHQLogoError::CapabilityDisabled, "Escritura deshabilitada en este perfil");
  if ((analog && (value < INT16_MIN || value > INT16_MAX)) || (!analog && value != 0 && value != 1))
    return fail(CHQLogoError::InvalidArgument, "Valor fuera de rango");
  if (!watch(analog, number)) return false;
  WatchedValue& item = _watched[findWatched(analog, number)];
  if (item.hasDesired && item.desired == value) return true;
  item.hasDesired = true;
  item.desired = static_cast<int16_t>(value);
  if (item.writeState != CHQLogoWriteState::Verifying) item.writeState = CHQLogoWriteState::Pending;
  return true;
}

bool CHQLogoWeb::setM(int32_t number, bool value) { return setDesired(false, number, value ? 1 : 0); }
bool CHQLogoWeb::setAM(int32_t number, int32_t value) { return setDesired(true, number, value); }
CHQLogoWriteState CHQLogoWeb::writeState(bool analog, int32_t number) const {
  const int index = findWatched(analog, number);
  return index < 0 ? CHQLogoWriteState::None : _watched[index].writeState;
}
CHQLogoWriteState CHQLogoWeb::writeStateM(int32_t number) const { return writeState(false, number); }
CHQLogoWriteState CHQLogoWeb::writeStateAM(int32_t number) const { return writeState(true, number); }

void CHQLogoWeb::finishWrite(WatchedValue& item, CHQLogoWriteState state) {
  item.writeState = item.desired == item.sent ? state : CHQLogoWriteState::Pending;
  if (!_debug) return;
  const char* result = state == CHQLogoWriteState::Matched ? "coincide por lectura" :
      state == CHQLogoWriteState::Mismatch ? "lectura no coincide" :
      state == CHQLogoWriteState::Rejected ? "rechazada" : "resultado desconocido";
  _debug->printf("%s%ld solicitado=%d: %s\n", item.analog ? "AM" : "M",
                 static_cast<long>(item.number), item.sent, result);
}

void CHQLogoWeb::serviceWriteTimeouts() {
  for (uint8_t i = 0; i < _watchCount; ++i) {
    WatchedValue& item = _watched[i];
    if (item.writeState == CHQLogoWriteState::Verifying &&
        static_cast<uint32_t>(millis() - item.writeStartedMs) >= _writeTimeoutMs)
      finishWrite(item, item.readAfterWrite && available(item.analog, item.number) ?
                  CHQLogoWriteState::Mismatch : CHQLogoWriteState::Unknown);
  }
}

void CHQLogoWeb::report() {
  if (!_debug) return;
  const bool online = connected();
  if (online != _reportedConnected) {
    _reportedConnected = online;
    _debug->println(online ? "LOGO conectado" : "LOGO desconectado; recuperacion automatica");
  }
  if (_reportedError == _diagnostics.error) return;
  if (_queuedAckNeedsReadback) return;
  _reportedError = _diagnostics.error;
  if (_diagnostics.error == CHQLogoError::None) return;
  if (_diagnostics.error == CHQLogoError::WifiDisconnected) { _debug->println("Esperando Wi-Fi"); return; }
  _debug->printf("LOGO: %s | HTTP=%d protocolo=%d error=%d\n", _lastError.c_str(),
                 _diagnostics.httpStatus, _diagnostics.protocolStatus, static_cast<int>(_diagnostics.error));
}

void CHQLogoWeb::invalidateWritten(bool analog, int32_t number) {
  const int index = findWatched(analog, number);
  if (index >= 0) _watched[index].valid = _watched[index].attempted = false;
}

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
      (profile.symbolicAnalog && (profile.analogSize != 4 || !profile.amAddressBase)) ||
      profile.analogHexDigits != 4 || !profile.digitalHexDigits || profile.digitalHexDigits > 4)
    return fail(CHQLogoError::InvalidArgument, "Perfil de protocolo inválido");
  const uint32_t lastAM = profile.amAddressBase +
      static_cast<uint32_t>(profile.amCount - 1) * profile.amAddressStride;
  const uint32_t lastM = profile.mAddressBase +
      static_cast<uint32_t>(profile.mCount - 1) * profile.mAddressStride;
  if (lastAM > UINT16_MAX || lastM > UINT16_MAX)
    return fail(CHQLogoError::InvalidArgument, "Direcciones del perfil exceden 16 bits");
  _profile = profile;
  clearWrites();
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
  clearWrites();
  endSession();
  return true;
}
bool CHQLogoWeb::useHTTPS(const String& rootCA, uint16_t port) {
  resetDiagnostics();
  if (rootCA.isEmpty() || !port) return fail(CHQLogoError::TlsConfiguration, "HTTPS requiere CA y puerto válido");
  _options.https = true;
  _options.rootCA = rootCA;
  _port = port;
  clearWrites();
  endSession();
  return true;
}
void CHQLogoWeb::useHTTP(uint16_t port) {
  _options.https = false;
  _options.rootCA = "";
  _port = port;
  clearWrites();
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
    // En modo gestionado un ACK desconocido no prueba que el token caduco.
    // La siguiente lectura comprueba la sesion; nunca convertir el ACK en exito.
    if (requireValue || !_managed) endSession();
    return fail(CHQLogoError::UnexpectedResponse, requireValue ?
        "Respuesta XML inesperada/incompleta" :
        "Confirmacion de escritura invalida; verificar por lectura");
  }
  _diagnostics.protocolStatus = 0;
  raw = reply.value;
  return true;
}

String CHQLogoWeb::scalarCommand(const String& verb, const String& requestId, uint16_t kind,
                                 uint16_t address, uint8_t size, bool symbolic) const {
  if (symbolic)
    return verb + ":_local_=" + requestId + ",AM.." + String(size) + ":" + String(address) + "-1";
  return verb + ":" + requestId + "," + String(kind) + "," + String(_profile.area) + "," +
      String(address) + "," + String(size) + ",1";
}

bool CHQLogoWeb::writeScalar(uint16_t kind, uint16_t address, uint8_t size, uint16_t value,
                              uint8_t digits, bool symbolic) {
  char hex[5];
  // variable.js usa toString(16): reproducir sus minusculas en el formato simbolico.
  snprintf(hex, sizeof(hex), symbolic ? "%0*x" : "%0*X", digits, static_cast<unsigned>(value));
  const String command = scalarCommand("SETVARS", _profile.writeRequestId, kind, address, size, symbolic) + "," + hex;
  String response;
  if (!exchangeRaw(command, response)) return false;
  uint16_t raw;
  return parseScalar(response, _profile.writeResponseId, digits, false, raw);
}

bool CHQLogoWeb::readScalar(uint16_t kind, uint16_t address, uint8_t size, uint8_t digits,
                             uint16_t& raw, bool symbolic) {
  if (_autoReadId && !_readIdDetected) return detectReadId(kind, address, size, digits, raw, symbolic);
  const String command = scalarCommand("GETVARS", _profile.readRequestId, kind, address, size, symbolic);
  String response;
  return exchangeRaw(command, response) && parseScalar(response, _profile.readResponseId, digits, true, raw);
}

bool CHQLogoWeb::discoveryRead(const String& requestId, uint16_t kind, uint16_t address,
                               uint8_t size, uint8_t digits, char (&replyId)[33], uint16_t& raw,
                               bool symbolic) {
  const String command = scalarCommand("GETVARS", requestId, kind, address, size, symbolic);
  String response;
  if (!exchangeRaw(command, response)) return false;
  chqlogo::ScalarReply reply{};
  const auto result = chqlogo::discoverScalar(response.c_str(), response.length(), digits, reply, replyId);
  if (result == chqlogo::ParseResult::Rejected) {
    _diagnostics.protocolStatus = reply.error;
    return fail(CHQLogoError::ProtocolRejected, "Lectura rechazada durante deteccion de identificador");
  }
  if (result != chqlogo::ParseResult::Ok || (kind == _profile.mType && reply.value > 1)) {
    endSession();
    return fail(CHQLogoError::UnexpectedResponse, "Deteccion requiere XML completo con una sola lectura valida");
  }
  _diagnostics.protocolStatus = 0;
  raw = reply.value;
  return true;
}

bool CHQLogoWeb::detectReadId(uint16_t kind, uint16_t address, uint8_t size,
                             uint8_t digits, uint16_t& raw, bool symbolic) {
  char firstId[33]{}, secondId[33]{};
  uint16_t firstValue = 0, secondValue = 0;
  const String probeId = _profile.readRequestId == "v2" ? "v1" : "v2";
  if (!discoveryRead(_profile.readRequestId, kind, address, size, digits, firstId, firstValue, symbolic) ||
      !discoveryRead(probeId, kind, address, size, digits, secondId, secondValue, symbolic)) return false;
  const bool echo = !strcmp(firstId, _profile.readRequestId.c_str()) && !strcmp(secondId, probeId.c_str());
  const bool fixed = !strcmp(firstId, secondId);
  if (!echo && !fixed) {
    endSession();
    return fail(CHQLogoError::UnexpectedResponse, "Identificadores de lectura inconsistentes; configurar perfil manual");
  }
  _profile.readResponseId = firstId;
  if (strcmp(_profile.readResponseId.c_str(), firstId)) {
    endSession();
    return fail(CHQLogoError::OutOfMemory, "No se pudo conservar el identificador de lectura");
  }
  _readIdDetected = true;
  raw = secondValue;  // Puede cambiar entre consultas: conservar la mas reciente.
  if (_debug) _debug->printf("ID lectura=%s (%s), confirmado por dos lecturas\n", firstId, echo ? "eco" : "fijo");
  return true;
}

bool CHQLogoWeb::writeAM(int32_t number, int32_t value) {
  resetDiagnostics();
  if (!_profile.writeAnalog) return fail(CHQLogoError::CapabilityDisabled, "Escritura AM deshabilitada en este perfil");
  if (number < 1 || number > _profile.amCount || value < INT16_MIN || value > INT16_MAX)
    return fail(CHQLogoError::InvalidArgument, "Número AM o valor fuera del rango del perfil");
  const uint16_t address = _profile.amAddressBase + static_cast<uint32_t>(number - 1) * _profile.amAddressStride;
  const bool ok = writeScalar(_profile.amType, address, _profile.analogSize, static_cast<uint16_t>(value),
                              _profile.analogHexDigits, _profile.symbolicAnalog);
  invalidateWritten(true, number);
  return ok;
}
bool CHQLogoWeb::readAM(int32_t number, int16_t& value) {
  resetDiagnostics();
  if (!_profile.readAnalog) return fail(CHQLogoError::CapabilityDisabled, "Lectura AM deshabilitada en este perfil");
  if (number < 1 || number > _profile.amCount) return fail(CHQLogoError::InvalidArgument, "AM fuera del rango del perfil");
  uint16_t raw;
  const uint16_t address = _profile.amAddressBase + static_cast<uint32_t>(number - 1) * _profile.amAddressStride;
  if (!readScalar(_profile.amType, address, _profile.analogSize, _profile.analogHexDigits, raw,
                   _profile.symbolicAnalog)) return false;
  value = raw >= 0x8000 ? static_cast<int16_t>(static_cast<int32_t>(raw) - 65536) : static_cast<int16_t>(raw);
  return true;
}
bool CHQLogoWeb::writeM(int32_t number, bool state) {
  resetDiagnostics();
  if (!_profile.writeDigital) return fail(CHQLogoError::CapabilityDisabled, "Escritura M deshabilitada en este perfil");
  if (number < 1 || number > _profile.mCount) return fail(CHQLogoError::InvalidArgument, "M fuera del rango del perfil");
  const uint16_t address = _profile.mAddressBase + static_cast<uint32_t>(number - 1) * _profile.mAddressStride;
  const bool ok = writeScalar(_profile.mType, address, _profile.digitalSize, state ? 1 : 0, _profile.digitalHexDigits);
  invalidateWritten(false, number);
  return ok;
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
