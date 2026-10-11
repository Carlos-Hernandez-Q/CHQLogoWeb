# Evidencia y estado de CHQLogoWeb 1.1

Actualización: 2026-10-10. Esta versión prepara una primera prueba física; todavía no se dispone de LOGO! Siemens.

## Resultados

| Evidencia | Resultado | Limitación |
| --- | --- | --- |
| Prueba manual previa del usuario, ESP32-S3 -> mock v0.2 | AM simulada cambió a 1200 y 250 | No valida esta versión ni Siemens |
| Python unittest discover | **15 pruebas OK** | Ejecutan el servidor mock por HTTP local |
| pruebas/run_native.py | **33 casos C++ OK**, incluyendo 10.000 entradas inválidas | C++ real con Arduino/Wi-Fi/socket de prueba |
| PlatformIO esp32-s3 | **Compila y enlaza** | No se cargó ni ejecutó en placa |
| PlatformIO logo-real | **Compila y enlaza** | No se conectó a PLC |
| LecturaEscritura, UltrasonicoAM1 y PruebaLOGOReal | **Compilan para ESP32-S3** mediante conversión .ino de PlatformIO | No se ejecutaron; no se utilizó el GUI de Arduino IDE |
| Negociación HTTPS/certificados | Ruta TLS y configuración comprobadas con dobles de prueba; código compilado con WiFiClientSecure real | TLS físico pendiente |
| Compatibilidad LOGO! 8 por firmware | **PENDIENTE** | No hay ninguna unidad validada |
| Persistencia de M/AM en RUN | **PENDIENTE** | Requiere programa y monitor del PLC |

Toolchain utilizada: PlatformIO Espressif32 **6.12.0**, Arduino ESP32 **2.0.17** (paquete 3.20017), Xtensa ESP32-S3 GCC **8.4.0**. Tests nativos: MinGW GCC **15.2.0**, C++11, -Wall -Wextra -Werror -pedantic.

## Cobertura nativa de C++

Se compilan y ejecutan CHQLogoWeb.cpp, CHQLogoProtocol.cpp y CHQLogoEsp32Transport.cpp. Los stubs reemplazan Arduino String, reloj, Wi-Fi, socket y aleatoriedad; no reemplazan el código de producción del cliente/parser/lector HTTP.

- CRC32 con vector estándar y pruebas de autenticación calculadas independientemente con Python zlib.
- Contraseña larga truncada por bytes y contraseña UTF-8.
- Campos extra, tokens inválidos, desbordamiento uint32 y salida sin alterar cuando falla.
- XML: atributos, comillas, espacios, comentarios, declaraciones, IDs, errores por fila, duplicados, truncamientos y hexadecimal.
- AM negativas/extremos, índices amplios sin conversión prematura y M no binaria.
- Perfiles, capacidades, variante de ruta/IDs/rango y parámetros UAM configurables.
- Sesiones inválidas dentro de HTTP 200, 401/403, pérdida de Wi-Fi y reautenticación.
- Transporte inyectado y su contrato; ninguna repetición automática de escritura.
- HTTP con longitud/chunked/cierre, escrituras parciales, cuerpos/cabeceras incompletos.
- Plazo de E/S ante servidor detenido o lento y rollover de millis().
- Límites por cuerpo, suma de chunks, línea y cabeceras acumuladas.
- Framing ambiguo, codificación no soportada y reserva/concatenación fallida del transporte.
- Configuración CA/puerto/timeouts TLS. El stub NO negocia TLS ni comprueba certificados.

## Cobertura Python

Autenticación, contraseña incorrecta, rechazo de sesiones, reemplazo y caducidad, desafío caducado, marcas digitales, analógicas negativas/extremos, direcciones y hexadecimal inválidos, panel bidireccional, tipos/rangos/origen del panel, ocultación de pruebas de contraseña y timeout de petición incompleta.

Los formatos de las respuestas nativas son fixtures sintéticos y los del mock son hipótesis. No son capturas de un LOGO! físico.

## Pendientes

Primer login, permisos/errores reales, formatos M/AM, tamaños y rangos por modelo, efecto del ciclo RUN/STOP, sesiones simultáneas, HTTPS con certificado real, pérdida de red física, consumo dinámico de heap y estabilidad prolongada. La compilación no mide estos comportamientos.

Registrar evidencia por referencia y firmware en COMPATIBILIDAD.md y seguir PRIMER_LOGO_FISICO.md. No confundir el objetivo multiversión con compatibilidad ya certificada.
