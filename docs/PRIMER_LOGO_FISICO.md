# Primera prueba física — CHQLogoWeb 1.1

El firmware ya compila; la comunicación con Siemens sigue pendiente. Este procedimiento comienza con operaciones manuales y una marca reservada sin vínculo a salidas físicas.

## Antes de cargar la ESP32

1. Guardar respaldo del programa. Registrar referencia `6ED...`, firmware exacto, versión de Soft Comfort y modo STOP/RUN.
2. Conectar LOGO! y PC por Ethernet al router; conectar ESP32 por Wi-Fi a la misma red, con comunicación entre ambos segmentos habilitada.
3. Abrir el portal del PLC. Confirmar usuario web, contraseña, HTTP/HTTPS y puerto en la configuración del equipo.
4. Reservar una marca digital libre, sin otro bloque que sobrescriba su valor. No asumir que cualquier marca es escribible en todos los modos o firmwares.
5. En las herramientas del navegador, capturar la secuencia de login y una lectura de la marca. Identificar ruta, cabeceras, cuerpo y respuesta. Capturar escritura desde el portal solo si la unidad la permite.
6. Contrastar con `docs/PROTOCOLO_Y_LIMITES.md`. Si la autenticación o la estructura son diferentes, conservar la evidencia y adaptar el código antes de emitir escrituras.

No publicar HAR ni capturas sin sanitizar: incluyen tokens y pruebas de contraseña. El navegador y la ESP32 pueden reemplazar sus sesiones mutuamente; usar Soft Comfort para la comprobación independiente y detener el polling del portal durante la prueba del cliente.

## Configuración del ejemplo

Editar `lib/CHQLogoWeb/examples/PruebaLOGOReal/PruebaLOGORealApp.h`:

- `WIFI_SSID`, `WIFI_PASSWORD`, `PLC_HOST`, `PLC_PORT`, `PLC_PASSWORD`.
- `PLC_MODEL`, `PLC_FIRMWARE`: identificación para el log, no detección automática.
- `TEST_M`: marca reservada. `TEST_AM` se utilizará más adelante.
- `USE_HTTPS`: seleccionar lo que tenga configurado el portal.
- `ROOT_CA_PEM`: CA de confianza si se utiliza HTTPS. El host debe coincidir con el certificado.
- `NTP_SERVER`: fuente horaria accesible cuando se use HTTPS; alternativamente configurar la hora con otra fuente confiable. El ejemplo no inicia login TLS con reloj sin ajustar.

Dejar `ENABLE_DIGITAL_WRITES`, `ENABLE_ANALOG_READS` y `ENABLE_ANALOG_WRITES` en `false` al comenzar. Ajustar los campos del perfil dentro de `setup()` únicamente con evidencia del firmware.

PlatformIO:

```text
pio run -e logo-real
pio run -e logo-real -t upload
pio device monitor -e logo-real
```

La ESP32-S3 de este proyecto usa USB CDC. Si no aparece el puerto correcto, revisar la placa/puerto seleccionados. En Arduino IDE, abrir el ejemplo `PruebaLOGOReal` y su pestaña `.h`; seleccionar ESP32-S3 y el monitor a 115200 baudios.

## Secuencia de pruebas

El monitor debe enviar fin de línea. La aplicación no escribe ni hace login automáticamente al arrancar.

| Paso | Acción | Resultado que se busca |
| --- | --- | --- |
| 1 | Enviar `login` | HTTP 200 y autenticación aceptada. No se muestran tokens |
| 2 | Enviar `m` | Lectura de la marca reservada; coincide con Soft Comfort |
| 3 | Cambiar el estado desde el programa/portal permitido y repetir `m` | Observar ambos estados 0/1 |
| 4 | Confirmar que el programa no sobrescribe la marca, habilitar escritura digital y cargar de nuevo | Perfil habilitado; todavía no escribe automáticamente |
| 5 | Enviar `m1`, luego `m0` | ACK y lectura posterior coinciden; corroborar en Soft Comfort |
| 6 | Perder/restaurar Wi-Fi, enviar `login` y `m` | Sesión descartada y recuperación manual |
| 7 | Reiniciar el PLC y repetir login/lectura | Manejo de conexión y sesión documentado |
| 8 | Capturar AM del portal; habilitar primero lectura AM | `am` coincide con valor conocido |
| 9 | Confirmar palabra hexadecimal, habilitar lectura y escritura AM | `a0`, `a250`, `a-120`, si firmware admite el rango |
| 10 | Comprobar en RUN y STOP donde el portal lo permita | Identificar restricciones y sobrescrituras del ciclo |

`logout` solo elimina el token local. `login` solicita una nueva sesión aunque ya exista una. `help` muestra comandos y capacidades habilitadas.

## Diagnóstico e interpretación

Cada petición imprime operación, OK/FALLO, HTTP, código del protocolo, código de error, milisegundos y bytes. `login` informa la última de sus dos peticiones. En una escritura aceptada se imprime además valor solicitado, valor leído y coincidencia; hay que comprobar ese valor también en Soft Comfort.

- HTTP 301/302 puede indicar otra ruta o necesidad de HTTPS. No se siguen redirecciones automáticamente.
- HTTP 401/403 o respuesta UAM de rechazo elimina la sesión. Revisar cuenta/permisos y sesiones simultáneas.
- `UnexpectedResponse` significa formato/ID/ancho inesperado o respuesta truncada. Conservar una captura sanitizada; no relajar el parser para aceptar cualquier texto.
- `CapabilityDisabled` significa que esa función está deshabilitada en el perfil.
- `ConnectionFailed` en HTTPS requiere revisar CA, host, reloj, puerto y límites TCP/TLS.
- Tras fallo de escritura, el estado remoto puede ser desconocido. Leer antes de decidir repetir.

Registrar resultados en `docs/PLAN_PRUEBAS_LOGO.md` y añadir una fila identificada a `docs/COMPATIBILIDAD.md`. Esta primera unidad no valida las otras generaciones.
