# Protocolo HTTP/HTTPS experimental — CHQLogoWeb 1.1

## Flujo actual

1. POST a la ruta del perfil, por defecto `/AJAX`, con `Security-Hint: p` y `UAMCHAL:3,4,a,b,c,d`.
2. Exigir tres campos: `700,<hint_temporal>,<desafío_uint32>`. Código y longitud del hint configurables por perfil.
3. CRC32 IEEE de `password + "+" + desafío_decimal`, truncado a 32 bytes UTF-8; XOR con el desafío.
4. XOR de las cuatro claves con el desafío.
5. Publicar `UAMLOGIN:<usuario>,<prueba_password>,<prueba_desafío>` con el hint temporal.
6. Exigir dos campos `700,<hint_sesión>` y un token apto para cabecera HTTP.
7. Utilizar el hint para GETVARS/SETVARS.

El mock reproduce esta hipótesis. Las pruebas C++ comprueban la implementación y vectores CRC independientes, pero ningún firmware físico está validado. No hay algoritmo alternativo de autenticación ni detección automática de firmware.

## Mensajes del perfil MockV1

| Acción | Cuerpo |
| --- | --- |
| Leer AM1 | `GETVARS:v1,20,0,0,1,1` |
| Escribir AM1=250 | `SETVARS:v0,20,0,0,1,1,00FA` |
| Leer M1 | `GETVARS:v1,131,0,0,1,1` |
| Escribir M1=1 | `SETVARS:v0,131,0,0,1,1,01` |

Los perfiles iniciales conservan tipo 20/131, área 0, índices desde cero, tamaño 1 y cantidad 1. Ruta, usuario, IDs, tipos, área, tamaños, bases/pasos de dirección, rangos y ancho digital se concentran en `CHQLogoProfile`. `analogSize` y `digitalSize` son independientes; las bases/pasos se validan para evitar direcciones truncadas. Estos parámetros se ajustan solo con capturas reales. AM sigue siendo hipótesis de una palabra hexadecimal de 16 bits. El perfil físico inicial deshabilita lectura/escritura AM.

## Interpretación de respuestas

El parser admite el subconjunto `<rs><r .../></rs>`, comillas simples/dobles, espacios en atributos, filas vacías con cierre explícito, declaración XML y comentarios. Comprueba el documento completo, atributos duplicados, presencia de `i/e` y un único resultado con el ID esperado. No acepta DTD, entidades ni elementos desconocidos. Una variante que los utilice requiere adaptar el parser con una captura real.

`e` debe pertenecer a la misma fila que el valor. Se exigen cuatro dígitos hexadecimales para AM y el ancho digital del perfil. Un ACK de escritura puede omitir `v`; su éxito confirma aceptación del mensaje, no persistencia del valor en el ciclo del PLC.

Las lecturas conservan el argumento de salida en caso de error. Se rechazan marcas digitales diferentes de 0/1. Los errores XML guardan su código en diagnóstico. Una respuesta UAM 7xx diferente del éxito del perfil, incluso con HTTP 200, descarta la sesión; una respuesta XML malformada o una página inesperada también la descarta. Esto es una política conservadora, pendiente de contrastar con errores reales.

## Transporte

- POST HTTP/1.1, `Connection: close`, `Accept-Encoding: identity`.
- Content-Length, chunked o cuerpo delimitado por cierre; sin redirecciones ni compresión.
- Límite de cuerpo predeterminado 2048 bytes, configurable 64..16384. Cabeceras: máximo 512 bytes por línea y 8192 bytes en total, incluidos trailers.
- Los cuerpos incompletos se descartan. Los cuerpos de HTTP diferente de 200 no se leen ni se incluyen en errores.
- `setTimeout(ms)` establece por separado el timeout de conexión y el plazo total de envío/lectura tras conectar. Dos etapas, no un único plazo de pared.
- Nuestros bucles controlan el tiempo total con resta unsigned de millis(), incluso frente a servidor que envía lentamente. DNS, TCP, TLS y una llamada individual a write() dependen del core; en Arduino 2.x algunos límites TLS/socket se expresan en segundos. No prometer precisión de milisegundos ni tiempo real estricto.
- HTTPS exige CA PEM propia, reloj correcto y host acorde al certificado. No hay `setInsecure()`, fallback ni aceptación automática de certificados.
- `CHQLogoTransport` es sustituible; un transporte externo debe devolver cuerpo completo, HTTP y error coherentes. La vida del transporte debe exceder la del cliente.

## Sesiones y uso

No se guarda la contraseña ni se reautentica automáticamente. 401/403 y pérdida de Wi-Fi descartan el token. El cambio de perfil, transporte HTTP/HTTPS o opciones de conexión descarta la sesión; cambiar únicamente el timeout no lo hace.

No se reintentan escrituras. Una petición puede haberse aplicado antes de perder su respuesta. El programa debe leer la variable y decidir cómo recuperarse; repetir una orden a ciegas no es una confirmación.

Una instancia no es segura entre tareas simultáneas. Los rangos configurados son alcance del cliente, no capacidades descubiertas del PLC. No hay API VM, teclas, reloj, RUN/STOP ni cambios de usuarios/configuración. `exchangeRaw` conserva una puerta de ingeniería inversa, por lo que su usuario debe comprobar semántica y permisos.
