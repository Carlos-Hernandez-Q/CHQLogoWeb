#include <WiFi.h>
#include <CHQLogoWeb.h>

const char* ssid = "TU_WIFI";
const char* wifiPass = "TU_PASSWORD";
const char* serverIP = "192.168.1.100";
CHQLogoWeb logo(serverIP, 8088);  // Servidor mock CHQLogoWeb, no PLC físico

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, wifiPass);
  while (WiFi.status() != WL_CONNECTED) delay(200);
  if (!logo.login("demo123")) Serial.println(logo.lastError());
}

void loop() {
  if (!logo.isAuthenticated() && !logo.login("demo123")) {
    Serial.println(logo.lastError()); delay(3000); return;
  }
  int16_t dato = 0;
  bool marca = false;
  if (!logo.writeAM(1, 250) || !logo.readAM(1, dato) ||
      !logo.writeM(1, true) || !logo.readM(1, marca)) {
    Serial.println("Error: " + logo.lastError());
  } else {
    Serial.printf("AM1=%d | M1=%d\n", dato, marca);
  }
  delay(1000);
}
