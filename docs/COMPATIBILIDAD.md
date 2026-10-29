# Compatibilidad

Versión preparada: 1.6.0. Actualización: 29 de octubre de 2026.

## Unidad física ensayada

| Elemento | Evidencia |
| --- | --- |
| Cliente | ESP32-S3, placa PlatformIO esp32s3usbotg, Arduino ESP32 2.0.17 |
| Transporte | HTTP, puerto 80, LAN con Wi-Fi del ESP32 |
| LOGO | Familia LOGO! 8; referencia exacta y firmware aún sin registrar |
| Autenticación | Login y recuperación de sesión observados |
| M | M1/M2 leídas; M2 en ambos sentidos y relé Q1 confirmado por el usuario |
| AM | AM2 simbólica leída con valores 1, 200, 600, 700, 800, 10000 y 32767 |
| Escritura AM | Valores entre 30 y 200 observados por lectura posterior; respuesta e=16 |
| HTTPS | Implementación y pruebas locales; falta ensayo en PLC físico |

La lectura digital devuelve un ID eco, v1 al solicitar v1. La biblioteca lo corrobora con dos consultas por sesión. El aprendizaje no identifica la versión del equipo.

## Segunda unidad: portal con Session-Id

El portal servido por la segunda unidad usa un login distinto: POST `/login/guest/challenge`, respuesta `ok: <token>` de 64 caracteres, y POST `/login/guest/login` con SHA-256 de usuario + SHA-256 de contraseña + token temporal. La sesión se transmite como cookie Session-Id. Sus archivos JavaScript comentan la transición de portal 8.4; esto no sustituye el registro de la referencia y firmware del PLC.

El reto UAMCHAL anterior enviado a `/AJAX` devuelve HTTP 403. Agregar la cookie Security-Hint y Origin/Referer no resolvió ese rechazo. El atributo de cifrado del canal vale 0.

El usuario comprobó el 29 de octubre login HTTP 200 y lecturas repetidas de M1/M2/AM1/AM2 desde una prueba Python en PC conectada por Ethernet. Todas las muestras compartidas devolvieron 0 válido. Aún falta observar cambios de valor, probar escrituras en esta unidad y cargar la integración C++ al ESP32. No se habilita por defecto la excepción AM e=16 de la primera unidad.

## Respuestas de escritura observadas

- M2 se aplica, pero el cuerpo recibido no es XML válido: `<rs><r i='v0' e='1'/>/<rs>`. Se conserva el error y se comprueba por una lectura nueva; no se acepta como ACK ni se reenvía.
- AM2 se aplica y devuelve XML válido con e=16. Se habilita expresamente `analogWriteReadbackCode=16` para esa unidad, solo en setAM simbólico. Su valor predeterminado es 0. La API directa sigue informando rechazo.
- Matched solo informa coincidencia por lectura posterior. Si el valor ya era igual, no prueba que se haya escrito.

## Rendimiento observado

Mediciones anteriores a la incorporación de TCP_NODELAY:

| Ensayo | Resultado aproximado |
| --- | --- |
| M1, M2 y AM2 con verificación | 0.82 escrituras AM confirmadas/s |
| Solo AM2 con verificación | 1.94 confirmadas/s |
| Solo AM2, Wi-Fi sin ahorro | 2.00 confirmadas/s; HTTP medio 249 ms |

Son resultados de esa red y esa unidad, no límites garantizados. Un objetivo de generación de 10 ms no produjo una actualización cada 10 ms. Cada cambio confirmado necesita SET y GET; más marcas añaden consultas individuales. La mejora de TCP_NODELAY aún requiere medición física.

## Pendiente de ampliar

Registrar referencia y firmware; probar otras unidades y marcas, valores negativos en PLC, HTTPS y recuperación tras un corte físico de red/alimentación. Los nuevos ejemplos y el arranque analógico 1.5.0 compilan y tienen pruebas locales; no se han cargado automáticamente al ESP32.

Los perfiles permiten ajustar variantes AJAX y seleccionar login clasico o Session-Id/SHA-256. No se anuncia compatibilidad certificada con todas las versiones LOGO! 8. Otros cambios de autenticación o formato pueden requerir código adicional.
