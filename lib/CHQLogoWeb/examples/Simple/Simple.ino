#include <WiFi.h>
#include <CHQLogoWeb.h>

const char* WIFI_SSID = "TU_WIFI";
const char* WIFI_PASSWORD = "TU_CLAVE_WIFI";
const char* LOGO_IP = "192.168.1.3";
const char* LOGO_PASSWORD = "TU_CLAVE_WEB";

CHQLogoConfig config(LOGO_PASSWORD, "Web User", &Serial);
CHQLogoWeb logo(LOGO_IP);
bool m1 = false, m2 = false;

void setup() {
  Serial.begin(115200);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) delay(250);
  WiFi.setSleep(false);
  while (!logo.begin(config)) { Serial.println(logo.lastError()); delay(1000); }
  while (!logo.waitUntilReady()) { Serial.println(logo.lastError()); delay(1000); }
  Serial.println("LOGO listo. Envia 1 para activar M2 o 0 para apagarla.");
}

void loop() {
  logo.loop();
  m1 = logo.readM(1);
  m2 = logo.readM(2);

  if (Serial.available()) {
    const char tecla = static_cast<char>(Serial.read());
    if (tecla == '1' || tecla == '0') logo.setM(2, tecla == '1');
  }
}
