#pragma once

#include <Arduino.h>

enum class CHQLogoError {
  None, InvalidArgument, NotAuthenticated, WifiDisconnected, ConnectionFailed,
  Timeout, ResponseTooLarge, IncompleteResponse, HttpError, InvalidHttp,
  UnsupportedEncoding, TlsConfiguration, UnexpectedResponse, ProtocolRejected,
  CapabilityDisabled, OutOfMemory
};

struct CHQLogoDiagnostics {
  CHQLogoError error = CHQLogoError::None;
  int httpStatus = 0;
  int protocolStatus = -1;
  uint32_t elapsedMs = 0;
  size_t responseBytes = 0;
  // Fases del transporte ESP32, en ms. elapsedMs incluye tambien el cierre.
  uint32_t connectMs = 0;  // DNS/TCP y handshake TLS cuando corresponda.
  uint32_t sendMs = 0;
  uint32_t receiveMs = 0;  // Espera y lectura de cabeceras/cuerpo.
  uint32_t closeMs = 0;
  bool tcpNoDelayApplied = false;
};

struct CHQLogoConnectionOptions {
  bool https = false;
  bool tcpNoDelay = true;  // Evita agrupar los pequenos envios de una peticion HTTP.
  String rootCA;  // Copia propia del PEM; nunca se utiliza setInsecure().
  uint16_t connectTimeoutMs = 4000;
  uint16_t ioTimeoutMs = 4000;
  size_t maxResponseBytes = 2048;
};

// Transporte sustituible, p.ej. para pruebas o una futura implementación Ethernet.
// La librería no conserva ni reintenta escrituras dentro del transporte.
class CHQLogoTransport {
 public:
  virtual ~CHQLogoTransport() = default;
  virtual bool post(const String& host, uint16_t port, const String& path,
                    const String& command, const String& hint,
                    const CHQLogoConnectionOptions& options, String& response,
                    CHQLogoDiagnostics& diagnostics) = 0;
};

CHQLogoTransport& chqLogoDefaultTransport();
