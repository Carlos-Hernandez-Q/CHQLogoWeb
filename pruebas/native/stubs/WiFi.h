#pragma once

#include "Arduino.h"
#include <deque>
#include <vector>

#define WL_CONNECTED 3
#define WIFI_STA 1

namespace fake {
struct SocketScript {
  std::string response;
  bool keepOpen = false;
  bool connectOK = true;
  uint32_t byteDelayMs = 0;
  uint32_t connectDelayMs = 0, writeDelayMs = 0, stopDelayMs = 0;
  bool noDelayOK = true;
  size_t maxWrite = 128;
};
extern uint32_t now;
extern uint32_t randomCounter;
extern bool wifiConnected;
extern std::deque<SocketScript> scripts;
extern std::vector<std::string> requests;
extern std::string caCertificate;
extern unsigned long handshakeTimeout;
extern uint16_t connectedPort;
extern int32_t connectTimeout;
extern bool noDelayEnabled;
extern uint32_t noDelayCalls;
void reset();
}

class WiFiClass {
 public:
  int status() const { return fake::wifiConnected ? WL_CONNECTED : 0; }
  int stationMode = 0;
  int beginCalls = 0, reconnectCalls = 0;
  bool autoReconnect = false;
  String ssid, password;
  void mode(int value) { stationMode = value; }
  void setAutoReconnect(bool value) { autoReconnect = value; }
  void begin(const char* network, const char* key) { ++beginCalls; ssid = network; password = key; }
  bool reconnect() { ++reconnectCalls; return true; }
  void reset() {
    stationMode = beginCalls = reconnectCalls = 0; autoReconnect = false;
    ssid = password = "";
  }
};
extern WiFiClass WiFi;

class WiFiClient {
 public:
  virtual ~WiFiClient() = default;
  virtual int setTimeout(uint32_t) { return 0; }
  virtual int setNoDelay(bool enabled) {
    ++fake::noDelayCalls;
    if (!_open || !_script.noDelayOK) return -1;
    fake::noDelayEnabled = enabled;
    return 0;
  }
  virtual int connect(const char*, uint16_t port, int32_t timeout) {
    fake::connectedPort = port;
    fake::connectTimeout = timeout;
    if (fake::scripts.empty()) return 0;
    _script = fake::scripts.front(); fake::scripts.pop_front();
    _stopped = false;
    fake::now += _script.connectDelayMs;
    if (!_script.connectOK) return 0;
    _open = true;
    _position = 0;
    _nextByte = fake::now + _script.byteDelayMs;
    _requestIndex = fake::requests.size();
    fake::requests.emplace_back();
    return 1;
  }
  virtual size_t write(const uint8_t* bytes, size_t length) {
    if (!_open) return 0;
    fake::now += _script.writeDelayMs;
    const size_t count = std::min(length, _script.maxWrite);
    fake::requests[_requestIndex].append(reinterpret_cast<const char*>(bytes), count);
    return count;
  }
  virtual int available() {
    return _open && _position < _script.response.size() &&
           static_cast<int32_t>(fake::now - _nextByte) >= 0 ? 1 : 0;
  }
  virtual int read() {
    if (!available()) return -1;
    _nextByte = fake::now + _script.byteDelayMs;
    return static_cast<unsigned char>(_script.response[_position++]);
  }
  virtual uint8_t connected() {
    return _open && (_position < _script.response.size() || _script.keepOpen);
  }
  virtual void stop() {
    if (!_stopped) { fake::now += _script.stopDelayMs; _stopped = true; }
    _open = false;
  }
 private:
  fake::SocketScript _script;
  bool _open = false;
  bool _stopped = true;
  size_t _position = 0;
  size_t _requestIndex = 0;
  uint32_t _nextByte = 0;
};
