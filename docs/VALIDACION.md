# Validación 1.5.0

Ejecutada el 20 de octubre de 2026 en Windows. Los resultados corresponden a las fuentes locales preparadas para revisión; no implican publicación ni carga de firmware.

| Comprobación | Resultado |
| --- | --- |
| C++ nativo, librería y transporte con adaptadores de prueba | 97 casos, 0 fallos |
| Simulador Python y protocolo del mock | 15 pruebas, correctas |
| PlatformIO esp32-s3: ejemplo Simple | Compila |
| PlatformIO analogica: LecturaAnalogica | Compila |
| PlatformIO ultrasonico: UltrasonicoSimulado | Compila |

PlatformIO: espressif32 6.12.0, Arduino ESP32 2.0.17, placa esp32s3usbotg. Los ejemplos no requieren bibliotecas externas a CHQLogoWeb y el core ESP32.

## Repetir

```text
py -3 -B pruebas/run_native.py
py -3 -B -m unittest discover -s pruebas -p "test_*.py"
pio run -e esp32-s3 -e analogica -e ultrasonico
```

El ejecutor nativo requiere g++ o clang++; admite --compiler RUTA y la variable CXX. No instala compiladores.

## Cobertura

Parser XML/hexadecimal, autenticación, perfiles, límites HTTP, Content-Length/chunked/cierre, errores de red, plazos y rollover, TLS con adaptadores, TCP_NODELAY y diagnósticos por fase, cache, detección de IDs, cola de escritura sin reenvíos y comprobación por lectura.

El arranque analógico añade pruebas de solo AM, muestra nueva, valores con signo, suspensión de escrituras durante la espera, errores de configuración y pérdida de Wi-Fi con timeout. Las pruebas de transporte usan sockets simulados: no miden radio, certificados ni latencia del PLC.

## Hardware

Las lecturas y escrituras físicas previas se describen en [COMPATIBILIDAD.md](COMPATIBILIDAD.md). La referencia y el firmware siguen pendientes de registrar. La versión 1.5.0 no se ha cargado automáticamente al ESP32. Quedan por medir TCP_NODELAY y por probar HTTPS, negativos en PLC y cortes físicos de red.

No se anuncia una tasa máxima garantizada ni compatibilidad universal por versión.
