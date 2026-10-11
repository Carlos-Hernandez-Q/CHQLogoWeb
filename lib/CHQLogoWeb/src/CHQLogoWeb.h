#pragma once

#include <Arduino.h>
#include <stdint.h>
#include "CHQLogoTransport.h"

// Parámetros del protocolo AJAX observado. Las variantes se establecerán con
// capturas reales; estos perfiles NO certifican generaciones de firmware.
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
  uint8_t analogSize = 1;
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
  // Primer PLC: solo M habilitada. AM se habilita explícitamente tras captura.
  static CHQLogoProfile logo8AjaxExperimental();
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

  bool readAM(int32_t amNumber, int16_t& value);
  bool writeAM(int32_t amNumber, int32_t value);
  bool readM(int32_t markerNumber, bool& state);
  bool writeM(int32_t markerNumber, bool state);

  // Puerta experimental, sin validación de semántica/privilegios de comandos.
  // No reintenta ninguna petición. El usuario responde por su uso.
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

  bool fail(CHQLogoError code, const String& message);
  void resetDiagnostics();
  bool post(const String& command, const String& hint, String& response);
  bool writeScalar(uint16_t kind, uint16_t address, uint8_t size, uint16_t value, uint8_t digits);
  bool readScalar(uint16_t kind, uint16_t address, uint8_t size, uint8_t digits, uint16_t& raw);
  bool parseScalar(const String& response, const String& id, uint8_t digits,
                   bool requireValue, uint16_t& raw);
};
