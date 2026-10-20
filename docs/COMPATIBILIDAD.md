# Compatibilidad

Versión preparada: 1.5.0. Actualización: 20 de octubre de 2026.

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

Los perfiles permiten ajustar variantes del mismo protocolo AJAX. No se anuncia compatibilidad certificada con todas las versiones LOGO! 8. Un cambio de autenticación o formato fundamental puede requerir código adicional.
