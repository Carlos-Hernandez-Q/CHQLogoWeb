#include <WiFi.h>
#include <CHQLogoWeb.h>

const char* WIFI_SSID = "TU_WIFI";
const char* WIFI_PASSWORD = "TU_CLAVE_WIFI";
const char* LOGO_IP = "192.168.1.3";
const char* LOGO_PASSWORD = "TU_CLAVE_WEB";

CHQLogoConfig config(LOGO_PASSWORD, "Web User", &Serial);
CHQLogoWeb logo(LOGO_IP);
int16_t distanciaCm = 30;
int16_t pasoCm = 10;
uint32_t ultimoCambio = 0;
const uint32_t INTERVALO_MS = 1000;

// La unidad probada aplica la escritura AM aunque responde e=16.
// Usa 0 si tu equipo no presenta ese comportamiento.
const uint16_t CODIGO_AM_POR_LECTURA = 16;

void setup() {
  Serial.begin(115200);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) delay(250);
  WiFi.setSleep(false);
  config.profile.readAnalog = true;
  config.profile.writeAnalog = true;
  config.profile.analogWriteReadbackCode = CODIGO_AM_POR_LECTURA;
  while (!logo.begin(config)) { Serial.println(logo.lastError()); delay(1000); }
  while (!logo.waitUntilReadyAM(0, 2)) { Serial.println(logo.lastError()); delay(1000); }
  Serial.println("Sensor simulado: 30 a 200 cm en AM2. Reserva esa marca para el ESP32.");
}

void loop() {
  logo.loop();
  if (!logo.availableAM(2)) return;

  if (logo.writeStateAM(2) == CHQLogoWriteState::Matched &&
      static_cast<uint32_t>(millis() - ultimoCambio) >= INTERVALO_MS) {
    ultimoCambio = millis();
    distanciaCm += pasoCm;
    if (distanciaCm == 200 || distanciaCm == 30) pasoCm = -pasoCm;
  }
  logo.setAM(2, distanciaCm);
}
