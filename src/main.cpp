#include <Arduino.h>
#include <WiFi.h>
#include <CHQLogoWeb.h>

// CONFIGURAR: WIFI y la IP LAN DE LA PC donde corre el simulador.
// Si el simulador corre en tu PC NO uses 127.0.0.1: esa sería la propia ESP32.
static const char* WIFI_SSID = "TU_RED_WIFI";
static const char* WIFI_PASSWORD = "TU_PASSWORD_WIFI";
static const char* LOGO_HOST = "192.168.1.100";
static const uint16_t LOGO_PORT = 8088;
static const char* LOGO_PASSWORD = "demo123";  // EXCLUSIVAMENTE MOCK

CHQLogoWeb logo(LOGO_HOST, LOGO_PORT);
uint32_t lastCycle = 0;
uint32_t nextLoginAttempt = 0;
bool loginRetryPending = false;
bool wifiWasConnected = false;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("CHQ Labs | CHQLogoWeb V1 (laboratorio) | ESP32-S3");
  Serial.println("AM1 (web) -> ESP32 -> AM2 | Sensor simulado -> AM3");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void loop() {
  const uint32_t now = millis();
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiWasConnected) {
      Serial.println("Wi-Fi perdido, sesión descartada");
      logo.endSession();
      loginRetryPending = false;
    }
    wifiWasConnected = false;
    if (now - lastCycle >= 2000) {
      lastCycle = now;
      Serial.println("Esperando Wi-Fi...");
    }
    return;
  }
  wifiWasConnected = true;

  if (!logo.isAuthenticated()) {
    // Comparación tolerante al desbordamiento de millis().
    if (loginRetryPending && static_cast<int32_t>(now - nextLoginAttempt) < 0) return;
    loginRetryPending = false;
    if (logo.login(LOGO_PASSWORD)) {
      Serial.printf("Sesión mock OK. ESP32=%s\n", WiFi.localIP().toString().c_str());
      lastCycle = now - 1000;
    } else {
      Serial.println("Login falló: " + logo.lastError());
      nextLoginAttempt = millis() + 5000;
      loginRetryPending = true;
    }
    return;
  }

  if (now - lastCycle < 1000) return;
  lastCycle = now;
  int16_t configuracionDesdeWeb = 0;
  if (!logo.readAM(1, configuracionDesdeWeb)) {
    Serial.println("Error leyendo AM1: " + logo.lastError());
    logo.endSession();
    nextLoginAttempt = millis() + 3000;
    loginRetryPending = true;
    return;
  }

  if (!logo.writeAM(2, configuracionDesdeWeb)) {
    Serial.println("Error escribiendo AM2: " + logo.lastError());
    logo.endSession();
    nextLoginAttempt = millis() + 3000;
    loginRetryPending = true;
    return;
  }
  const int16_t distanciaSimuladaMm = static_cast<int16_t>(((now / 1000UL) * 75UL) % 2001UL);
  if (!logo.writeAM(3, distanciaSimuladaMm)) {
    Serial.println("Error escribiendo AM3: " + logo.lastError());
    logo.endSession();
    nextLoginAttempt = millis() + 3000;
    loginRetryPending = true;
    return;
  }

  Serial.printf("AM1(web)=%d | AM2(eco)=%d | AM3(sensor)=%d mm\n",
                configuracionDesdeWeb, configuracionDesdeWeb, distanciaSimuladaMm);
}
