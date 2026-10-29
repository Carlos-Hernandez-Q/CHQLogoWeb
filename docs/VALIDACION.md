# Validación 1.6.0

Ejecutada el 29 de octubre de 2026 en Windows. Los resultados corresponden a las fuentes locales preparadas para revisión; no implican publicación ni carga de firmware.

| Comprobación | Resultado |
| --- | --- |
| C++ nativo, librería y transporte con adaptadores de prueba | 110 casos, 0 fallos |
| Simulador Python y protocolo del mock | 15 pruebas, correctas |
| PlatformIO esp32-s3: ejemplo Simple | Compila |
| PlatformIO analogica: LecturaAnalogica | Compila |
| PlatformIO ultrasonico: UltrasonicoSimulado | Compila |
| Prueba de lectura M1/M2/AM1/AM2 con seleccion automatica de login | Compila para ESP32-S3 |

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

El nuevo login añade vectores SHA-256 (mensaje vacio, abc, un millon de letras y limites de bloque), parser de Session-Id con el espacio observado en el PLC, prueba de acceso con UTF-8, seleccion automatica limitada al reto inicial, conservacion del login anterior, cookie, M simbolicas, AM con signo y verificacion sin reenvios tras perder la sesion. Los rechazos de clave y timeouts no provocan un segundo metodo de autenticacion.

## Hardware

Las lecturas y escrituras físicas previas se describen en [COMPATIBILIDAD.md](COMPATIBILIDAD.md). La segunda unidad tiene login Session-Id y lecturas M1/M2/AM1/AM2 comprobados por el usuario desde PC/Ethernet; los valores fueron 0. La referencia y el firmware siguen pendientes de registrar. La integración C++ 1.6.0 no se ha cargado automáticamente al ESP32; falta comprobar sus cambios de valor y escrituras en esta unidad. Quedan por medir TCP_NODELAY y por probar HTTPS, negativos en PLC y cortes físicos de red.

No se anuncia una tasa máxima garantizada ni compatibilidad universal por versión.
