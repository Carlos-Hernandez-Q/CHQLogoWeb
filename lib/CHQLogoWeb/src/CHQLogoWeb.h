#pragma once

#include <Arduino.h>
#include <stdint.h>
#include "CHQLogoTransport.h"

// Formato y capacidades del portal AJAX. Los perfiles se ajustan por equipo.
struct CHQLogoProfile {
  String name = "MockV1";
  String ajaxPath = "/AJAX";
  String webUser = "Web User";
  String readRequestId = "v1";
  String readResponseId = "v0";
  String writeRequestId = "v0";
  String writeResponseId = "v0";
  uint16_t successCode = 700;
  uint8_t hintLength = 32;
  uint16_t amType = 20;
  uint16_t mType = 131;
  uint16_t amCount = 8;
  uint16_t mCount = 32;
  uint16_t area = 0;
  // Selector del protocolo, no bytes: el mock historico usa 1 para AM.
  // El perfil fisico experimental usa AM simbolica con selector 4 (word).
  uint8_t analogSize = 1;
  bool symbolicAnalog = false;  // _local_=ID,AM..4:numero-1; no cambia M.
  // Excepcion explicita por equipo: este codigo AM simbolico requiere readback.
  // 0 = deshabilitada. No convierte el codigo en ACK valido ni afecta writeAM directo.
  uint16_t analogWriteReadbackCode = 0;
  uint8_t digitalSize = 1;
  uint16_t amAddressBase = 0;
  uint16_t mAddressBase = 0;
  uint16_t amAddressStride = 1;
  uint16_t mAddressStride = 1;
  uint8_t analogHexDigits = 4;
  uint8_t digitalHexDigits = 2;
  bool readAnalog = true;
  bool writeAnalog = true;
  bool readDigital = true;
  bool writeDigital = true;

  static CHQLogoProfile mockV1();
  // Perfil LOGO! 8: AM se habilita por separado en la configuracion.
  static CHQLogoProfile logo8AjaxExperimental();
};

// Configuracion simple del portal AJAX. La deteccion solo aprende IDs de lectura;
// no identifica firmware, algoritmos de login, tipos ni direcciones.
struct CHQLogoConfig {
  explicit CHQLogoConfig(const String& logoKey, const String& webUser = "Web User",
                         Print* output = nullptr);
  String password;
  CHQLogoProfile profile;
  uint32_t pollIntervalMs = 500;
  uint32_t reconnectIntervalMs = 3000;
  uint32_t writeTimeoutMs = 5000;
  uint16_t requestTimeoutMs = 4000;
  bool autoReadId = true;  // Dos lecturas corroboran ID eco o ID fijo por sesion.
  Print* debug = nullptr;  // Opcional, p.ej. &Serial. Sin cuerpos ni credenciales.
};

enum class CHQLogoWriteState {
  None, Pending, Verifying, Matched, Mismatch, Unknown, Rejected
};

class CHQLogoWeb {
 public:
  // El transporte opcional no es propiedad de la librería; debe vivir más
  // tiempo que este objeto. Sin él se utiliza Wi-Fi HTTP/HTTPS para ESP32.
  explicit CHQLogoWeb(const String& host, uint16_t port = 80,
                      CHQLogoTransport* transport = nullptr);

  bool setProfile(const CHQLogoProfile& profile);
  const CHQLogoProfile& profile() const;
  void setTimeout(uint16_t milliseconds);  // Conexión y presupuesto total de E/S.
  bool setConnectionOptions(const CHQLogoConnectionOptions& options);
  const CHQLogoConnectionOptions& connectionOptions() const;
  bool useHTTPS(const String& rootCA, uint16_t port = 443);
  void useHTTP(uint16_t port = 80);

  bool login(const String& password);
  void endSession();  // LOCAL; no hay logout remoto confirmado.
  bool isAuthenticated() const;  // Solo confirma posesión local del token.

  // Modo gestionado: login/reconexion y sondeo desde loop(), en la misma tarea.
  // La sesion se reutiliza; el transporte HTTP sigue abriendo cada peticion.
  // begin() conserva una copia de la clave en RAM hasta end(). No conecta aun.
  bool begin(const String& password, uint32_t pollIntervalMs = 500,
             uint32_t reconnectIntervalMs = 3000);
  bool begin(const CHQLogoConfig& config);  // Perfil, sesion y sondeo; red externa.
  // Espera login + lectura digital valida; 0 = sin limite. No inicia Wi-Fi.
  // Mientras espera no envia escrituras pendientes. Requiere begin previo.
  bool waitUntilReady(uint32_t timeoutMs = 0, int32_t markerNumber = 1);
  // Igual que waitUntilReady, comprobando una marca analogica.
  bool waitUntilReadyAM(uint32_t timeoutMs = 0, int32_t markerNumber = 1);
  void end();
  bool loop();  // Puede bloquear durante una operacion HTTP o un login.
  bool connected() const;  // Wi-Fi y token local; no garantiza salud remota.
  bool watchM(int32_t number);
  bool watchAM(int32_t number);
  bool availableM(int32_t number) const;
  bool availableAM(int32_t number) const;
  // Lecturas locales inmediatas. Registran la variable si no se vigila aun.
  // Devuelven ultimo valor (0 inicial); comprobar available antes de usarlo.
  bool readM(int32_t number);
  int16_t readAM(int32_t number);
  // Estado deseado: acepta/coalesce una orden local, loop la envia UNA vez y
  // verifica por lectura. El bool indica aceptacion local, no exito remoto.
  // Repetir el mismo valor no vuelve a escribir, tampoco despues de un fallo.
  bool setM(int32_t number, bool value);
  bool setAM(int32_t number, int32_t value);
  CHQLogoWriteState writeStateM(int32_t number) const;
  CHQLogoWriteState writeStateAM(int32_t number) const;
  using DigitalChange = void (*)(int32_t number, bool value);
  using AnalogChange = void (*)(int32_t number, int16_t value);
  // Primera muestra y resincronizacion notifican; luego solo cambios.
  void onChangeM(DigitalChange callback);
  void onChangeAM(AnalogChange callback);

  // API directa existente: realiza HTTP sin usar la cache.
  bool readAM(int32_t amNumber, int16_t& value);
  bool writeAM(int32_t amNumber, int32_t value);
  bool readM(int32_t markerNumber, bool& state);
  bool writeM(int32_t markerNumber, bool state);

  // Envia un comando AJAX sin interpretar su respuesta. No reintenta.
  bool exchangeRaw(const String& command, String& response);
  String lastError() const;
  const CHQLogoDiagnostics& diagnostics() const;  // Última petición HTTP.

 private:
  String _host;
  uint16_t _port;
  CHQLogoProfile _profile;
  CHQLogoConnectionOptions _options;
  CHQLogoTransport* _transport;
  String _sessionHint;
  String _lastError;
  CHQLogoDiagnostics _diagnostics;

  struct WatchedValue {
    bool analog = false;
    int32_t number = 0;
    int16_t value = 0;
    bool valid = false;
    bool seen = false;
    bool attempted = false;
    uint32_t lastAttemptMs = 0;
    bool hasDesired = false;
    int16_t desired = 0;
    int16_t sent = 0;
    CHQLogoWriteState writeState = CHQLogoWriteState::None;
    uint32_t writeStartedMs = 0;
    bool readAfterWrite = false;
  };
  static const uint8_t MaxWatchedValues = 16;
  WatchedValue _watched[MaxWatchedValues];
  uint8_t _watchCount = 0;
  uint8_t _nextWatch = 0;
  bool _managed = false;
  bool _inLoop = false;
  bool _retryPending = false;
  uint32_t _lastFailureMs = 0;
  uint32_t _pollIntervalMs = 500;
  uint32_t _reconnectIntervalMs = 3000;
  String _managedPassword;
  DigitalChange _digitalChange = nullptr;
  AnalogChange _analogChange = nullptr;
  uint32_t _writeTimeoutMs = 5000;
  Print* _debug = nullptr;
  bool _reportedConnected = false;
  CHQLogoError _reportedError = CHQLogoError::None;
  bool _queuedAckNeedsReadback = false;
  bool _autoReadId = false;
  bool _readIdDetected = false;
  bool _suspendWrites = false;

  bool service();
  bool waitForReady(bool analog, uint32_t timeoutMs, int32_t markerNumber);
  bool setDesired(bool analog, int32_t number, int32_t value);
  CHQLogoWriteState writeState(bool analog, int32_t number) const;
  void clearWrites();
  void finishWrite(WatchedValue& item, CHQLogoWriteState state);
  void serviceWriteTimeouts();
  void report();
  int findWatched(bool analog, int32_t number) const;
  bool watch(bool analog, int32_t number);
  bool available(bool analog, int32_t number) const;
  void invalidateCache();
  void invalidateWritten(bool analog, int32_t number);

  bool fail(CHQLogoError code, const String& message);
  void resetDiagnostics();
  bool post(const String& command, const String& hint, String& response);
  String scalarCommand(const String& verb, const String& requestId, uint16_t kind,
                       uint16_t address, uint8_t size, bool symbolic) const;
  bool writeScalar(uint16_t kind, uint16_t address, uint8_t size, uint16_t value,
                   uint8_t digits, bool symbolic = false);
  bool readScalar(uint16_t kind, uint16_t address, uint8_t size, uint8_t digits,
                  uint16_t& raw, bool symbolic = false);
  bool detectReadId(uint16_t kind, uint16_t address, uint8_t size, uint8_t digits,
                    uint16_t& raw, bool symbolic = false);
  bool discoveryRead(const String& requestId, uint16_t kind, uint16_t address,
                     uint8_t size, uint8_t digits, char (&replyId)[33], uint16_t& raw,
                     bool symbolic = false);
  bool parseScalar(const String& response, const String& id, uint8_t digits,
                   bool requireValue, uint16_t& raw);
};
