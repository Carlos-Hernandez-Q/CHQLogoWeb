# Changelog

## 1.1.0 — Base adaptable, primera validación física pendiente
- Perfiles configurables de protocolo y capacidades, sin anunciar firmwares como validados.
- Transporte separado HTTP/HTTPS; TLS con CA de confianza y sin modo inseguro.
- Lectura HTTP acotada: Content-Length/chunked/cierre, límites de cuerpo/cabeceras y plazo total de E/S.
- Parser XML del resultado exacto, sin falsos ACK por coincidencias de texto; rechazo de datos parciales/duplicados.
- Sesiones descartadas ante 401/403, Wi-Fi perdido y errores UAM con HTTP 200.
- Diagnósticos estructurados sin incluir cuerpos, tokens ni pruebas de contraseña.
- Índices/valores de entrada ampliados a int32_t para validarlos antes de convertir.
- Corrección de plazos antiguos de reautenticación en la demo.
- Ejemplo manual PruebaLOGOReal compartido por Arduino IDE/PlatformIO; entorno logo-real separado.
- Simulador con hexadecimal estricto, ocultación de autenticación, timeout de lectura y caducidad de sesión/desafío.
- Tests que compilan y ejecutan el C++ real con adaptadores de plataforma; matriz por firmware y guía de primera prueba.
- PlatformIO fijado a espressif32 6.12.0. Se mantienen credenciales de demostración y licencia pendiente.

Las llamadas habituales conservan su forma. Código que utilice punteros a miembros con firmas uint8_t/int16_t debe actualizarse a las nuevas firmas int32_t. No se declara estabilidad ABI.

## V1 (1.0.0, laboratorio; no release estable para PLC real)
- Marca CHQ Labs, nombre `CHQLogoWeb`, clases y metadatos PlatformIO/Arduino.
- Migración de prototipo HTTP/AJAX V0.3 al esquema `lib/CHQLogoWeb`.
- API `login`, `readAM`, `writeAM`, `readM`, `writeM`, `exchangeRaw`.
- Mejoras de control local de sesión, validación básica de respuestas, timeout y errores.
- Ejemplo ESP32-S3 bidireccional (AM1 manual, AM2 eco, AM3 sensor simulado).
- Ejemplos Arduino y laboratorio de ultrasónico; este último NO está probado en hardware.
- Simulador y pruebas heredados de V0.3, documentación de límites y matriz multi-firmware.

**Sin verificar:** compilación PlatformIO ESP32-S3 en este entorno, nuevos ejemplos en placa y LOGO! físico.
