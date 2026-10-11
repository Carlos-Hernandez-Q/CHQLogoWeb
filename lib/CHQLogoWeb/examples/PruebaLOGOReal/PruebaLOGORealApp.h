#pragma once

#include <WiFi.h>
#include <CHQLogoWeb.h>
#include <string.h>
#include <time.h>

// Completar cuando llegue el PLC. No colocar contraseñas reales en capturas/logs.
static const char* WIFI_SSID = "TU_RED_WIFI";
static const char* WIFI_PASSWORD = "TU_PASSWORD_WIFI";
static const char* PLC_HOST = "192.168.1.100";
static const char* PLC_PASSWORD = "CONFIGURAR_PASSWORD_WEB";
static const char* PLC_MODEL = "PENDIENTE";
static const char* PLC_FIRMWARE = "PENDIENTE";

static const bool USE_HTTPS = false;
static const uint16_t PLC_PORT = USE_HTTPS ? 443 : 80;
static const char* ROOT_CA_PEM = "";  // PEM de confianza requerido para HTTPS.
static const char* NTP_SERVER = "";   // Servidor horario accesible, preferentemente LAN.

// Habilitar solo tras confirmar la marca reservada y las capturas del portal.
static const bool ENABLE_DIGITAL_WRITES = false;
static const bool ENABLE_ANALOG_READS = false;
static const bool ENABLE_ANALOG_WRITES = false;
static const int32_t TEST_M = 1;
static const int32_t TEST_AM = 1;

static CHQLogoWeb logo(PLC_HOST, PLC_PORT);
static bool configurationReady = false;
static bool wifiWasConnected = false;
static char serialCommand[32];
static size_t serialLength = 0;
static bool serialOverflow = false;

static void printResult(const char* operation, bool ok) {
  const CHQLogoDiagnostics& d = logo.diagnostics();
  Serial.printf("%s: %s | HTTP=%d | protocolo=%d | error=%d | %lu ms | %u bytes\n",
      operation, ok ? "OK" : "FALLO", d.httpStatus, d.protocolStatus,
      static_cast<int>(d.error), static_cast<unsigned long>(d.elapsedMs),
      static_cast<unsigned>(d.responseBytes));
  if (!ok) Serial.println(logo.lastError());
}

static void help() {
  Serial.println("Comandos: login, m, am, m0, m1, a0, a250, a-120, logout, help");
  Serial.println("m/am leen la marca reservada. Cada escritura se solicita manualmente.");
  Serial.printf("Habilitado: escribir M=%d | leer AM=%d | escribir AM=%d\n",
      ENABLE_DIGITAL_WRITES, ENABLE_ANALOG_READS, ENABLE_ANALOG_WRITES);
}

static bool ensureLogin() {
  if (!configurationReady) { Serial.println("Revisa la configuración del ejemplo"); return false; }
  if (WiFi.status() != WL_CONNECTED) { Serial.println("Esperando Wi-Fi"); return false; }
  if (USE_HTTPS && time(nullptr) < 1700000000) {
    Serial.println("HTTPS: ajusta el reloj mediante NTP accesible o una fuente de hora confiable");
    return false;
  }
  if (logo.isAuthenticated()) return true;
  const bool ok = logo.login(PLC_PASSWORD);
  printResult("login", ok);
  return ok;
}

static void readDigital() {
  bool state = false;
  const bool ok = logo.readM(TEST_M, state);
  printResult("readM", ok);
  if (ok) Serial.printf("M%ld=%d\n", static_cast<long>(TEST_M), state);
}

static void readAnalog() {
  int16_t value = 0;
  const bool ok = logo.readAM(TEST_AM, value);
  printResult("readAM", ok);
  if (ok) Serial.printf("AM%ld=%d\n", static_cast<long>(TEST_AM), value);
}

static void execute(const char* command) {
  if (!strcmp(command, "help")) { help(); return; }
  if (!strcmp(command, "logout")) { logo.endSession(); Serial.println("Token local descartado"); return; }
  const bool writeDigital = !strcmp(command, "m0") || !strcmp(command, "m1");
  const bool writeAnalog = !strcmp(command, "a0") || !strcmp(command, "a250") || !strcmp(command, "a-120");
  if (strcmp(command, "login") && strcmp(command, "m") && strcmp(command, "am") &&
      !writeDigital && !writeAnalog) { help(); return; }
  if (!strcmp(command, "login")) logo.endSession();
  if (!ensureLogin()) return;
  if (!strcmp(command, "login")) return;
  if (!strcmp(command, "m")) { readDigital(); return; }
  if (!strcmp(command, "am")) { readAnalog(); return; }
  if (writeDigital) {
    const bool requested = !strcmp(command, "m1");
    const bool ok = logo.writeM(TEST_M, requested);
    printResult("writeM", ok);
    if (ok) {
      bool observed = false;
      const bool readOK = logo.readM(TEST_M, observed);
      printResult("readM posterior", readOK);
      if (readOK) Serial.printf("M%ld: solicitado=%d leído=%d coincidencia=%d\n",
          static_cast<long>(TEST_M), requested, observed, requested == observed);
    }
  } else {
    const int16_t requested = !strcmp(command, "a250") ? 250 : (!strcmp(command, "a-120") ? -120 : 0);
    const bool ok = logo.writeAM(TEST_AM, requested);
    printResult("writeAM", ok);
    if (ok) {
      int16_t observed = 0;
      const bool readOK = logo.readAM(TEST_AM, observed);
      printResult("readAM posterior", readOK);
      if (readOK) Serial.printf("AM%ld: solicitado=%d leído=%d coincidencia=%d\n",
          static_cast<long>(TEST_AM), requested, observed, requested == observed);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("CHQLogoWeb 1.1 | Primer LOGO físico | Compatibilidad experimental");
  Serial.printf("Modelo=%s | Firmware=%s | Destino=%s:%u | HTTPS=%d\n",
      PLC_MODEL, PLC_FIRMWARE, PLC_HOST, PLC_PORT, USE_HTTPS);
  CHQLogoProfile profile = CHQLogoProfile::logo8AjaxExperimental();
  profile.writeDigital = ENABLE_DIGITAL_WRITES;
  profile.readAnalog = ENABLE_ANALOG_READS;
  profile.writeAnalog = ENABLE_ANALOG_WRITES;
  // Ajustar aquí IDs, tipos, longitudes o ruta SOLO según capturas del firmware.
  configurationReady = logo.setProfile(profile);
  if (configurationReady && USE_HTTPS) configurationReady = logo.useHTTPS(ROOT_CA_PEM, PLC_PORT);
  if (!configurationReady) Serial.println(logo.lastError());
  Serial.printf("Perfil=%s | M de prueba=%ld | AM de prueba=%ld\n",
      logo.profile().name.c_str(), static_cast<long>(TEST_M), static_cast<long>(TEST_AM));
  logo.setTimeout(4000);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  if (USE_HTTPS && NTP_SERVER[0]) configTime(0, 0, NTP_SERVER);
  help();
}

void loop() {
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected != wifiWasConnected) {
    wifiWasConnected = connected;
    logo.endSession();
    Serial.println(connected ? "Wi-Fi conectado; envía login" : "Wi-Fi perdido; token descartado");
  }
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      if (serialOverflow) Serial.println("Comando demasiado largo");
      else if (serialLength) { serialCommand[serialLength] = '\0'; execute(serialCommand); }
      serialLength = 0; serialOverflow = false;
    } else if (serialLength < sizeof(serialCommand) - 1) serialCommand[serialLength++] = c;
    else serialOverflow = true;
  }
  delay(2);
}
