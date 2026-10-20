#include <WiFi.h>
#include <CHQLogoWeb.h>

const char* WIFI_SSID = "TU_WIFI";
const char* WIFI_PASSWORD = "TU_CLAVE_WIFI";
const char* LOGO_IP = "192.168.1.3";
const char* LOGO_PASSWORD = "TU_CLAVE_WEB";

CHQLogoConfig config(LOGO_PASSWORD, "Web User", &Serial);
CHQLogoWeb logo(LOGO_IP);
int16_t am2 = 0;

void setup() {
  Serial.begin(115200);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) delay(250);
  WiFi.setSleep(false);
  config.profile.readAnalog = true;
  while (!logo.begin(config)) { Serial.println(logo.lastError()); delay(1000); }
  while (!logo.waitUntilReadyAM(0, 2)) { Serial.println(logo.lastError()); delay(1000); }
  Serial.println("LOGO listo. Lectura automatica de AM2.");
}

void loop() {
  logo.loop();
  am2 = logo.readAM(2);
}
