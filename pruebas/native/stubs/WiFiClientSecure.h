#pragma once
#include "WiFi.h"

class WiFiClientSecure : public WiFiClient {
 public:
  void setCACert(const char* cert) { fake::caCertificate = cert; }
  void setHandshakeTimeout(unsigned long seconds) { fake::handshakeTimeout = seconds; }
};
