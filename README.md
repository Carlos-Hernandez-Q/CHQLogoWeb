# CHQLogoWeb 1.5.0

Librería de CHQ Labs para leer y escribir marcas de un Siemens LOGO! 8 desde ESP32 mediante su portal web.

El programa administra el Wi-Fi. La librería mantiene la sesión del LOGO, consulta las marcas y comprueba las escrituras por lectura. No necesita Modbus ni un servidor intermedio.

## Uso

Después de conectar el Wi-Fi, estas son las dos líneas de arranque:

```cpp
CHQLogoConfig config("TU_CLAVE_WEB", "Web User", &Serial);
CHQLogoWeb logo("192.168.1.3");

// En setup(), con Wi-Fi conectado:
while (!logo.begin(config)) { Serial.println(logo.lastError()); delay(1000); }
while (!logo.waitUntilReady()) { Serial.println(logo.lastError()); delay(1000); }

// En loop():
logo.loop();
bool m1 = logo.readM(1);
logo.setM(2, salidaDeseada);  // bool definido por tu programa.
```

`waitUntilReady()` espera el login y una lectura nueva de M1. La espera es indefinida por defecto. `readM()` devuelve el último valor guardado; `availableM()` indica si hay una muestra válida. `setM()` solicita un valor y `logo.loop()` lo envía una vez y verifica por lectura.

Ejecuta `logo.loop()` con frecuencia. Las lecturas locales son rápidas, pero una consulta HTTP dentro de `loop()` puede bloquear. Evita `delay()` largos en tu programa.

## Instalación

- **PlatformIO:** copia la carpeta `lib/CHQLogoWeb` completa dentro de `lib/` de tu proyecto.
- **Arduino IDE:** instala el ZIP de la librería desde **Programa > Incluir librería > Añadir biblioteca .ZIP**, o copia `lib/CHQLogoWeb` a `Documentos/Arduino/libraries/CHQLogoWeb`.

Edita el nombre y la clave Wi-Fi, la IP del LOGO y su contraseña web en el ejemplo. El LOGO debe tener el servidor web habilitado y ser accesible desde esa red.

## Tres ejemplos

| Ejemplo | Uso |
| --- | --- |
| [Simple](lib/CHQLogoWeb/examples/Simple/Simple.ino) | Lee M1/M2. Envía 1 o 0 por el monitor serie para cambiar M2. |
| [LecturaAnalogica](lib/CHQLogoWeb/examples/LecturaAnalogica/LecturaAnalogica.ino) | Lee AM2, sin escrituras. |
| [UltrasonicoSimulado](lib/CHQLogoWeb/examples/UltrasonicoSimulado/UltrasonicoSimulado.ino) | Envía de 30 a 200 cm a AM2 y comprueba cada valor por lectura. |

Monitor serie: **115200 baudios**. En el proyecto de este repositorio:

```text
pio run -e esp32-s3
pio run -e analogica
pio run -e ultrasonico
```

Los entornos usan ESP32-S3 USB-OTG con adaptador USB-serie. Selecciona tu placa si es distinta.

## Marcas analógicas

```cpp
// Antes de begin():
config.profile.readAnalog = true;
config.profile.writeAnalog = true;  // Solo si vas a escribir.

// Segunda línea de arranque para un programa que solo usa AM2:
while (!logo.waitUntilReadyAM(0, 2)) { Serial.println(logo.lastError()); delay(1000); }

// En loop(), después de logo.loop():
int16_t am2 = logo.readAM(2);
logo.setAM(2, distanciaCm);
```

AM usa enteros con signo de 16 bits: -32768 a 32767. Las unidades las define tu aplicación. El ejemplo de sensor genera centímetros; no incluye un sensor físico.

La unidad ensayada aplica AM y responde `e=16`. El ejemplo de escritura habilita explícitamente `analogWriteReadbackCode = 16` para comprobar ese caso por lectura. La librería lo deja deshabilitado por defecto. No se debe aplicar esa excepción a otro equipo sin comprobar su respuesta.

## Alcance

- Hasta **16 marcas distintas M/AM en total**, incluyendo las que se escriben.
- Perfil inicial: M1..M32 y AM1..AM8; estos son límites configurados del cliente.
- M habilitadas; AM se habilitan por separado.
- Sondeo predeterminado: 500 ms por variable. Cada consulta usa una petición HTTP.
- Sesión reutilizada, conexión TCP nueva por petición. Sin notificaciones push.
- Repetir `setM/setAM` con el mismo valor no repite el envío, incluso tras un fallo.
- Una escritura intentada no se reenvía al reconectar. Consulta `writeStateM/AM` para conocer su resultado.
- `Matched` significa coincidencia en una lectura posterior; no demuestra una transición si ya tenía ese valor.
- Una instancia debe usarse desde una sola tarea.

Se comprobó login, lectura M1/M2, accionamiento de Q1 mediante M2 y lectura/escritura de AM2 en una unidad física. Aún falta registrar su referencia y firmware; no se anuncia compatibilidad certificada con todas las versiones de LOGO! 8. TCP_NODELAY está habilitado, con mejora de velocidad pendiente de medición física.

## Manual y documentación

- [Manual de uso en PDF](lib/CHQLogoWeb/extras/Manual_CHQLogoWeb.pdf)
- [Referencia de la librería](lib/CHQLogoWeb/README.md)
- [Compatibilidad](docs/COMPATIBILIDAD.md)
- [Protocolo y límites](docs/PROTOCOLO_Y_LIMITES.md)
- [Validación](docs/VALIDACION.md)
- [Cambios](CHANGELOG.md)

## Desarrollo

```text
py -3 -B pruebas/run_native.py
py -3 -B -m unittest discover -s pruebas -p "test_*.py"
pio run -e esp32-s3 -e analogica -e ultrasonico
```

El simulador sigue disponible en `simulador/logo_mock.py`; su formato AM numérico utiliza `CHQLogoProfile::mockV1()`. Los tres ejemplos públicos están preparados para el portal del PLC, no para ese mock.

La licencia está [pendiente de elección](LICENSE_PENDIENTE.md).
