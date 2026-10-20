# CHQLogoWeb 1.5.0

Cliente Arduino para leer y escribir marcas M/AM de LOGO! 8 desde ESP32 mediante HTTP/HTTPS.

Instala esta carpeta en `lib/CHQLogoWeb` de PlatformIO o en `Documentos/Arduino/libraries/CHQLogoWeb`. El [manual PDF](extras/Manual_CHQLogoWeb.pdf) contiene la guía completa.

## Arranque

```cpp
#include <WiFi.h>
#include <CHQLogoWeb.h>

CHQLogoConfig config("TU_CLAVE_WEB", "Web User", &Serial);
CHQLogoWeb logo("192.168.1.3");

void setup() {
  Serial.begin(115200);
  WiFi.setAutoReconnect(true);
  WiFi.begin("TU_WIFI", "TU_CLAVE_WIFI");
  while (WiFi.status() != WL_CONNECTED) delay(250);
  WiFi.setSleep(false);
  while (!logo.begin(config)) { Serial.println(logo.lastError()); delay(1000); }
  while (!logo.waitUntilReady()) { Serial.println(logo.lastError()); delay(1000); }
}

void loop() {
  logo.loop();
  bool m1 = logo.readM(1);
}
```

El Wi-Fi pertenece a la aplicación. `begin(config)` configura el LOGO y conserva la contraseña en RAM para recuperar la sesión. `waitUntilReady()` espera login y una lectura nueva de M1; durante esa espera no despacha escrituras pendientes. `waitUntilReady(10000, 2)` comprueba M2 con plazo nominal de 10 s. Una operación HTTP en curso puede exceder ese plazo.

Omitir `&Serial` silencia los mensajes de la librería. Con él se imprimen conexión, cambios de valor y resultados de escritura, sin credenciales ni cuerpos HTTP.

## Lectura y escritura

| Llamada | Resultado |
| --- | --- |
| `logo.loop()` | Atiende sesión, consultas y escrituras en la tarea llamante. |
| `logo.readM(1)` | Último bool de M1, sin HTTP. |
| `logo.readAM(2)` | Último int16_t de AM2, sin HTTP. |
| `logo.availableM(1)` / `availableAM(2)` | Hay una muestra válida. |
| `logo.setM(2, true)` | Solicita M2=1. El bool indica aceptación local. |
| `logo.setAM(2, 120)` | Solicita AM2=120. Rango: -32768..32767. |
| `logo.writeStateM(2)` / `writeStateAM(2)` | Estado de la solicitud. |
| `logo.connected()` | Hay Wi-Fi y token local; no prueba salud remota continua. |
| `logo.lastError()` / `diagnostics()` | Error y detalles de la última petición. |
| `logo.end()` | Detiene el modo gestionado y descarta sesión y órdenes. No desconecta Wi-Fi. |

Las lecturas de un argumento registran la marca automáticamente. Antes de la primera muestra devuelven 0; tras perder sesión conservan el último valor, con `available=false`. Consultar repetidamente una marca no crea nuevas entradas ni consultas inmediatas. Se admiten 16 marcas distintas M/AM combinadas, también las solicitadas con set. No hay baja individual de marcas.

El primer set solicita una escritura aunque el valor leído ya coincida. Antes del envío se conserva el último estado solicitado; mientras se verifica una orden, la siguiente espera. Repetir el mismo valor no reenvía, ni tras errores, reconexiones o cambios de otro escritor. Esto es un mando por cambios, no un regulador continuo.

Una orden aún no intentada puede esperar sin red. Una orden ya intentada no se repite automáticamente. Un nuevo begin, end o cambio de perfil/transporte cancela las órdenes anteriores.

| Estado | Significado |
| --- | --- |
| `None` | Sin solicitud. |
| `Pending` | Espera el envío. |
| `Verifying` | Se intentó enviar; espera una lectura nueva. |
| `Matched` | Lectura posterior coincidente dentro del plazo. |
| `Mismatch` | Al vencer el plazo hay lectura posterior distinta. |
| `Unknown` | Al vencer el plazo no hay lectura posterior disponible. |
| `Rejected` | Respuesta válida de rechazo del protocolo. |

Matched no convierte una respuesta irregular en ACK ni demuestra causalidad si había otro escritor o el valor ya era igual. Una orden puede haberse aplicado aunque se pierda su respuesta. Comprobar antes de decidir repetir.

## Analógicas

Antes de begin:

```cpp
config.profile.readAnalog = true;
config.profile.writeAnalog = true;  // Omitir para solo lectura.
```

Para arrancar solo con AM2, usa `waitUntilReadyAM(0, 2)` en lugar de `waitUntilReady()`. El cero indica espera indefinida.

El perfil de CHQLogoConfig usa AM simbólicas del portal, por ejemplo `AM..4:2-1`. Las AM están deshabilitadas por defecto. El perfil limita M1..M32 y AM1..AM8; no representa la capacidad global del PLC.

La unidad física ensayada responde e=16 al escribir AM, aunque aplica el cambio. Solo para una unidad con ese comportamiento comprobado:

```cpp
config.profile.analogWriteReadbackCode = 16;  // Predeterminado: 0, deshabilitado.
```

Esta excepción solo permite pasar a verificación con `setAM`, formato simbólico, lectura AM habilitada y una respuesta íntegra del código esperado. No afecta a M, AM numéricas, lecturas ni `writeAM` directo. Otros rechazos se mantienen. Nunca reenvía el SET.

## Configuración

| Campo de CHQLogoConfig | Predeterminado |
| --- | --- |
| `pollIntervalMs` | 500 ms por variable |
| `reconnectIntervalMs` | 3000 ms entre recuperaciones fallidas |
| `writeTimeoutMs` | 5000 ms para verificar, desde el final del intento |
| `requestTimeoutMs` | 4000 ms para conexión y presupuesto de E/S |
| `autoReadId` | true |
| `debug` | nullptr; el constructor admite &Serial |

La detección consulta dos veces la misma marca con IDs distintos y aprende si el portal devuelve eco o un ID fijo. Luego exige ese ID exacto hasta perder sesión. No detecta firmware, algoritmo de login ni direcciones. Configuración manual:

```cpp
config.autoReadId = false;
config.profile.readResponseId = "v0";  // Usar el ID observado en ese equipo.
```

`CHQLogoProfile` concentra ruta AJAX, usuario, identificadores, tipos, direcciones y capacidades. `mockV1()` conserva el simulador numérico. El constructor sin begin(config) conserva ese perfil anterior; para la API directa seleccionar explícitamente `logo8AjaxExperimental()` cuando corresponda.

## Tiempos y red

`loop()` es síncrono: una llamada puede esperar HTTP o las dos peticiones de login/detección. No hay tarea de fondo ni concurrencia. Llamarlo con frecuencia y usar millis para temporizar. Un delay largo también detiene las consultas.

Se reutiliza la sesión, no el socket: cada petición abre/cierra TCP. Más marcas distintas requieren más consultas; no hay lectura por lote. El periodo configurado no garantiza la frecuencia real.

TCP_NODELAY está activo por defecto. Su mejora física sigue pendiente de medición. `diagnostics()` informa `connectMs`, `sendMs`, `receiveMs`, `closeMs`, `elapsedMs` y `tcpNoDelayApplied`. La recepción incluye servidor y red; no mide solo el tiempo del PLC.

HTTP usa por defecto puerto 80. HTTPS se selecciona antes de begin mediante `useHTTPS(rootCaPem, 443)`: requiere CA válida, reloj correcto y host acorde al certificado. No hay modo inseguro ni cambio automático a HTTP. La validación TLS en PLC físico sigue pendiente.

## API avanzada

`watchM/watchAM` registran marcas sin pedir su valor. `onChangeM/onChangeAM` reciben primera muestra, resincronización y cambios posteriores.

`readM(numero, salida)`, `readAM(numero, salida)`, `writeM` y `writeAM` hacen HTTP directo. No equivalen a las lecturas locales ni a la cola de estados deseados. `login`, `endSession`, `setProfile`, `setConnectionOptions` y `exchangeRaw` están disponibles para integraciones específicas. No mezclar llamadas concurrentes ni operaciones de red desde callbacks.

Los ejemplos públicos son Simple, LecturaAnalogica y UltrasonicoSimulado. Las pruebas físicas corresponden a una sola unidad; referencia y firmware pendientes de registrar. No se certifican todas las versiones de LOGO! 8.

CHQ Labs. Licencia pendiente de elección por el autor.
