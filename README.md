# CHQLogoWeb 1.1 — CHQ Labs

Librería experimental para ESP32/ESP32-S3 que lee y escribe mediante el portal web HTTP/HTTPS de LOGO! 8. Sin Modbus, OPC ni intermediario en producción.

**Estado:** compila para ESP32-S3; pruebas nativas del C++ y pruebas HTTP del mock pasan. Todavía no hay ningún LOGO! físico validado. Se prepara la primera prueba con un equipo real.

## Preparación para distintas versiones

Se conserva la API `login/readAM/writeAM/readM/writeM`. Los detalles del servidor se concentran en `CHQLogoProfile`: ruta AJAX, usuario web, IDs de solicitud/respuesta, códigos de tipo, rangos, longitud del token y capacidades. HTTP/HTTPS se configura por separado.

| Perfil | Alcance actual | Evidencia |
| --- | --- | --- |
| `CHQLogoProfile::mockV1()` | AM1–AM8 y M1–M32; conserva el laboratorio anterior | C++ con sockets simulados y mock Python |
| `CHQLogoProfile::logo8AjaxExperimental()` | Hipótesis UAMCHAL/UAMLOGIN y marcas digitales; AM deshabilitada por defecto | Código compilado; hardware pendiente |
| Perfil personalizado | Variantes del mismo protocolo AJAX según capturas de un firmware | Validar cada variante |

Estos perfiles NO equivalen a “LOGO! 8.0 a 8.4 certificados”. La matriz por versión está en [docs/COMPATIBILIDAD.md](docs/COMPATIBILIDAD.md). No se adivina la versión ni se amplían direcciones automáticamente. Si cambia el algoritmo de autenticación o la codificación fundamental, hará falta implementar ese protocolo; cambiar una etiqueta de perfil no lo resuelve.

## Laboratorio existente

1. Ejecutar `py -3 -B simulador/logo_mock.py` (o `python -B simulador/logo_mock.py`).
2. Abrir `http://localhost:8088` en la PC.
3. Configurar Wi-Fi e IP LAN de la PC en `src/main.cpp`; puerto 8088 y contraseña de demostración `demo123`.
4. Compilar/subir con `pio run -e esp32-s3 -t upload`.
5. Abrir el monitor a 115200 baudios.
6. Escribir 250, 500 y -120 en AM1 desde el panel. La placa lee AM1, copia a AM2 y publica el sensor simulado en AM3.

`/panel/am` existe solo en el simulador; permite escribir AM1 sin contraseña en la LAN de laboratorio. El mock caduca sesiones a los 300 segundos y desafíos a los 30; estos tiempos no describen un PLC Siemens.

## Primer PLC físico

El ejemplo [PruebaLOGORealApp.h](lib/CHQLogoWeb/examples/PruebaLOGOReal/PruebaLOGORealApp.h) contiene toda la configuración. PlatformIO y Arduino IDE comparten ese archivo.

1. Identificar modelo y firmware; capturar login y lectura del portal.
2. Completar Wi-Fi, IP, puerto, contraseña web y datos del PLC.
3. Mantener deshabilitadas las escrituras al comenzar.
4. Compilar `pio run -e logo-real`; subir con `pio run -e logo-real -t upload`.
5. Enviar `login` y luego `m` desde el monitor serie, con fin de línea.
6. Seguir [docs/PRIMER_LOGO_FISICO.md](docs/PRIMER_LOGO_FISICO.md) para escritura, lectura posterior, AM y recuperación.

Los dos entornos tienen filtros de fuentes separados; no se cambia el ejemplo del simulador para probar el PLC.

## API y perfiles

```cpp
#include <WiFi.h>
#include <CHQLogoWeb.h>

CHQLogoWeb logo("192.168.1.100", 80);

void setup() {
  // Inicializar Wi-Fi antes de login.
  CHQLogoProfile p = CHQLogoProfile::logo8AjaxExperimental();
  p.writeDigital = false;  // Primera prueba: lectura.
  if (!logo.setProfile(p)) Serial.println(logo.lastError());
  logo.setTimeout(4000);
}

void loop() {
  if (!logo.isAuthenticated() && !logo.login("CONFIGURAR")) {
    Serial.println(logo.lastError());
    delay(3000);
    return;
  }
  bool estado = false;
  if (logo.readM(1, estado)) Serial.println(estado);
  else Serial.println(logo.lastError());
  delay(1000);
}
```

Para AM en el PLC: habilitar `p.readAnalog` y/o `p.writeAnalog` SOLO después de confirmar el formato en el portal y aplicar de nuevo `setProfile(p)`. Los rangos del perfil son el alcance configurado del cliente, no límites oficiales del dispositivo.

`writeAM` admite entrada de 32 bits y rechaza valores fuera de -32768..32767. Los índices también se validan antes de convertirlos, para impedir que 257 termine escribiendo en la marca 1.

## HTTPS

`logo.useHTTPS(rootCaPem, 443)` copia el certificado raíz y descarta la sesión previa. No existe modo inseguro ni cambio automático a HTTP. Se requiere un PEM de confianza, reloj correcto y host coincidente con el certificado. El ejemplo permite configurar un servidor NTP accesible. La validación TLS física todavía está pendiente.

## Errores y límites

- `lastError()`: mensaje sin contraseña, token ni cuerpo HTTP.
- `diagnostics()`: código de error, HTTP, código de protocolo, duración y bytes de la última petición; el login realiza dos peticiones.
- XML completo, resultado identificado y atributos asociados. Se rechazan truncamientos, duplicados y formatos inesperados.
- Cuerpos limitados a 2048 bytes por defecto; configurable entre 64 y 16384.
- HTTP con Content-Length, chunked o cierre. No se siguen redirecciones ni se aceptan cuerpos comprimidos.
- Conexión y E/S tienen presupuestos independientes. La E/S incluye envío, cabeceras y cuerpo con un plazo total; DNS, llamadas TCP/TLS y sus unidades dependen del core. No es una garantía de tiempo real estricto.
- No hay reintentos automáticos de escritura. Tras un fallo de red, el resultado remoto puede ser desconocido; comprobar el valor antes de decidir repetir.
- `isAuthenticated()` solo indica token local; `endSession()` no hace logout remoto.
- Una instancia debe pertenecer a una sola tarea; no es una API concurrente.
- Sin memoria VM, RUN/STOP, reloj del PLC, usuarios ni cambios de configuración.

El protocolo y su alcance se detallan en [docs/PROTOCOLO_Y_LIMITES.md](docs/PROTOCOLO_Y_LIMITES.md).

## Pruebas sin PLC

```text
py -3 -B -m unittest discover -s pruebas -v
py -3 -B pruebas/run_native.py
pio run -e esp32-s3 -e logo-real
```

Las pruebas nativas necesitan g++/clang++: aceptan `--compiler RUTA` o `CXX`, usan también MinGW si está en `C:/mingw32`, y no instalan nada. Compilan el C++ de la librería con adaptadores Arduino/socket de prueba; no sustituyen Wi-Fi, TLS ni firmware físico. Evidencia: [docs/VALIDACION_V1.md](docs/VALIDACION_V1.md).

## Instalación Arduino IDE

Copiar **solo `lib/CHQLogoWeb`** a `Documentos/Arduino/libraries/CHQLogoWeb`. Reiniciar el IDE y abrir un ejemplo. Seleccionar la placa ESP32 y el monitor a 115200 baudios. Para el primer PLC usar `PruebaLOGOReal`.

## Fuentes y licencia

Investigaciones independientes, no dependencias de código:
- https://github.com/jankeymeulen/siemens-logo-rest
- https://github.com/j-siderius/Siemens-LOGO-Python-Client

Configuración HTTP/HTTPS oficial: https://cache.industry.siemens.com/dl/files/807/109974807/att_1299328/v4/Help_en-US_en-US.pdf

HTTP sin cifrado queda restringido a la LAN de laboratorio. Esta versión no está validada para maquinaria productiva ni seguridad funcional. La licencia sigue pendiente de decisión del autor; ver [LICENSE_PENDIENTE.md](LICENSE_PENDIENTE.md).
