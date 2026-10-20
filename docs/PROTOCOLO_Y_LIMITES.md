# Protocolo y límites

## Sesión

El login usa UAMCHAL/UAMLOGIN. La sesión se conserva en RAM y se recupera desde loop cuando corresponde. Cada solicitud usa POST en /AJAX y abre/cierra TCP. endSession descarta el token local; no confirma logout remoto. end detiene además el modo gestionado, sin desconectar Wi-Fi.

CHQLogoConfig usa el perfil logo8AjaxExperimental. M están habilitadas; AM se habilitan por separado. El perfil mockV1 conserva el laboratorio anterior. Las capacidades de un perfil describen al cliente, no certifican un firmware.

## Comandos observados

```text
GETVARS:v1,131,0,0,1,1
SETVARS:v0,131,0,1,1,1,01
GETVARS:_local_=v1,AM..4:2-1
SETVARS:_local_=v0,AM..4:2-1,0028
```

Corresponden a leer M1, escribir M2=1, leer AM2 y escribir AM2=40. La AM simbólica usa selector word=4, base 1 y paso 1; el número 2 representa AM2. El hexadecimal simbólico de escritura se envía en minúsculas, como el portal observado.

Las respuestas M tienen dos dígitos hexadecimales, con valor solo 0/1; AM tienen cuatro y se interpretan como int16_t. Se rechazan índices y valores fuera de rango antes de convertirlos.

## Validación

XML completo con un único resultado, ID exacto, atributos asociados, hexadecimal del ancho requerido y ausencia de duplicados o datos sobrantes. Se rechazan cuerpos truncados o malformados. La detección de IDs solo publica el resultado tras dos respuestas válidas que corroboran eco o ID fijo.

El rechazo de una lectura invalida la sesión cuando corresponde. Una escritura con respuesta irregular puede haberse aplicado; el modo gestionado permite comprobar por lectura sin volver a enviar. Un rechazo XML válido permanece Rejected, salvo el código AM configurado expresamente para el equipo. No se cambia el significado global de e=16 ni de e=1.

## Transporte

| Límite | Valor |
| --- | --- |
| Cuerpo predeterminado | 2048 bytes |
| Cuerpo configurable | 64..16384 bytes |
| Cabeceras totales | 8192 bytes |
| Línea HTTP | 512 bytes |
| Envío por fragmento | Hasta 128 bytes |
| Variables vigiladas | 16 M/AM combinadas |

Se admiten Content-Length, chunked y cuerpo terminado por cierre. No se siguen redirecciones ni se admiten cuerpos comprimidos. Conexión y E/S tienen presupuestos independientes; la E/S comparte un plazo para envío, cabeceras y cuerpo. DNS/TCP/TLS dependen también del core ESP32 y no ofrecen tiempo real estricto.

TCP_NODELAY se solicita tras conectar; diagnostics indica si pudo aplicarse. Sus tiempos por fase distinguen conexión, envío, recepción y cierre. elapsedMs incluye cierre. receiveMs incluye espera del servidor y red.

HTTPS exige CA de confianza, host correcto y reloj válido. Sin modo inseguro ni fallback automático a HTTP. Ensayo físico pendiente.

## Interfaz gestionada

begin configura, loop atiende y end detiene. readM/AM de un argumento devuelven cache local; available distingue una muestra válida del 0 inicial o un dato anterior a la desconexión. setM/AM acepta una intención local, conserva el último valor aún no enviado y verifica el intento mediante GET nuevo. Un intento nunca se reproduce automáticamente.

Las marcas se registran con watch, read o set; hasta 16 únicas, sin baja individual. La configuración inicial limita M1..M32 y AM1..AM8. Más variables implican más HTTP, sin lotes.

No hay suscripción push, tarea de fondo, acceso concurrente, VM, RUN/STOP, reloj del PLC, usuarios ni configuración del programa. La red y sus credenciales pertenecen a la aplicación.

## Referencias

El formato AM se contrastó con el JavaScript servido por el propio portal del equipo ensayado. El simulador de este repositorio sirve para regresión local y no representa todos los servidores Siemens.

Proyectos consultados durante el desarrollo: [siemens-logo-rest](https://github.com/jankeymeulen/siemens-logo-rest) y [Siemens-LOGO-Python-Client](https://github.com/j-siderius/Siemens-LOGO-Python-Client). Son referencias independientes, no dependencias distribuidas con la librería.
