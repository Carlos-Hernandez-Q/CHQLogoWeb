#include <WiFi.h>
#include <CHQLogoWeb.h>

// EJEMPLO SIN PROBAR EN HARDWARE: sensor HC-SR04 con nivel lógico de ECHO
// reducido a 3.3V mediante divisor de tensión o level shifter.
const int PIN_TRIG = 5;
const int PIN_ECHO = 6;
const char* SSID = "TU_WIFI";
const char* PASS = "TU_PASSWORD";
CHQLogoWeb logo("192.168.1.100", 8088);  // IP de PC con mock

int16_t distanciaMilimetros() {
  digitalWrite(PIN_TRIG, LOW); delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  unsigned long microsEco = pulseIn(PIN_ECHO, HIGH, 30000);
  if (microsEco == 0) return -1;  // Sin eco; NO publicar como distancia válida
  float cm = (microsEco * 0.0343f) / 2.0f;
  return static_cast<int16_t>(cm * 10.0f);  // mm
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  WiFi.begin(SSID, PASS);
  while (WiFi.status() != WL_CONNECTED) delay(200);
}

void loop() {
  if (!logo.isAuthenticated() && !logo.login("demo123")) {
    Serial.println(logo.lastError()); delay(3000); return;
  }
  int16_t mm = distanciaMilimetros();
  if (mm >= 0) {
    if (logo.writeAM(1, mm)) Serial.printf("AM1=%d mm\n", mm);
    else Serial.println(logo.lastError());
  } else {
    Serial.println("Ultrasonico sin eco; no se actualiza AM1");
  }
  delay(500);
}
