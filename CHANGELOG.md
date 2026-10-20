# Changelog

## 1.5.0 - Ejemplos y manual
- Tres ejemplos: lectura digital y mando por serie, lectura analogica y sensor simulado con verificacion.
- waitUntilReadyAM permite esperar una lectura analogica nueva al arrancar, sin registrar marcas digitales.
- Configuracion de ejemplo sin datos personales; Wi-Fi sigue a cargo de la aplicacion.
- Manual PDF, referencia de uso y documentacion de compatibilidad actualizados.
- 97 casos nativos, 15 pruebas del simulador y los tres ejemplos compilados para ESP32-S3.

## 1.4.3 — TCP_NODELAY y tiempos por fase
- `CHQLogoConnectionOptions.tcpNoDelay` activo por defecto: configura el socket tras conectar para evitar agrupar pequenos envios HTTP; false permite comparar. Se conserva el envio acotado a 128 bytes y la gestion de escrituras sin reintentos.
- Diagnosticos `connectMs`, `sendMs`, `receiveMs`, `closeMs` y `tcpNoDelayApplied`; elapsedMs incluye ahora el cierre. Se informa si la opcion TCP pudo aplicarse, sin cambiar la validez de HTTP ante un fallo de esa opcion.
- 93 pruebas nativas, incluidas fases de tiempo, rollover, fallos TCP, timeouts, HTTP/HTTPS y TCP_NODELAY opcional. Compilacion del proyecto ESP32-S3. Rendimiento fisico con TCP_NODELAY pendiente de medir.

## 1.4.2 — Comprobacion de codigo AM observado por equipo
- `profile.analogWriteReadbackCode` permite verificar por una lectura nueva un codigo de respuesta observado en escrituras AM simbolicas. Predeterminado 0 (deshabilitado); no es un ACK valido ni se aplica a M, AM numericas, API directa o lecturas.
- La primera unidad devuelve e=16 y aplica AM2: captura 30 -> SET 40 -> e=16 -> lectura 40. Su aplicacion habilita explicitamente codigo 16, sin reenvios ni confirmacion desde cache previa. Los demas codigos mantienen rechazo.
- Se conserva diagnostico y se imprime el codigo observado. El plazo puede terminar en discrepancia o resultado desconocido. 87 pruebas nativas y compilacion del proyecto ESP32-S3.

## 1.4.1 — Formato AM observado en el portal del PLC
- `symbolicAnalog` permite GETVARS/SETVARS con `_local_=ID,AM..4:numero-1`, como construye `js/variable.js` del PLC. El perfil fisico usa base 1/paso 1 y mantiene AM deshabilitada hasta validar. MockV1 y las peticiones M conservan el formato numerico.
- Los ensayos numericos de AM devolvieron un byte con selectores 1/2 o `0000` con selector 4, sin coincidir con los 700/800 mostrados por el portal. Se retira la hipotesis de paso AM 2 para este portal.
- El parser conserva XML estricto, cuatro digitos AM y rechazo de errores. Tests de comandos simbolicos, IDs automaticos, valores signed y verificacion de escritura. Usuario confirma lectura fisica AM2=800/700/600/1/10000/200/32767; escritura AM aun pendiente.
- Hexadecimal de escritura simbolica en minusculas como el portal; numerico conserva mayusculas. Captura AM e=16 permanece Rejected y no se convierte en exito por coincidir con un valor previo. Falta validar la escritura fisica con transicion observable.

## 1.4.0 — Deteccion de identificadores y espera de arranque
- autoReadId por defecto en CHQLogoConfig: dos lecturas estrictas de la misma variable distinguen ID eco o fijo, sin escrituras de prueba.
- El ID se aprende solo ante XML completo de una fila valida y se exige durante el resto de la sesion; perdida de sesion obliga a corroborar de nuevo.
- waitUntilReady espera login y muestra nueva antes de salir de setup; plazo opcional, espera indefinida por defecto y sin despachar escrituras pendientes.
- Wi-Fi sigue siendo externo; ejemplo Simple espera primero la red y luego la lectura del PLC.
- Se mantienen perfil manual con autoReadId=false y begin(password) anterior.

## 1.3.0 — Inicializacion simple y estados deseados
- CHQLogoConfig y begin(config) concentran perfil, sesion LOGO y sondeo; Wi-Fi se administra externamente con WiFi.h.
- setM/setAM permiten solicitar un valor en una linea; loop coalesce, envia una vez y verifica por lectura.
- writeStateM/AM separan aceptacion local, envio, lectura coincidente, rechazo, discrepancia y resultado desconocido.
- Las llamadas repetidas al mismo estado no repiten escrituras, tampoco al recuperar una conexion fallida.
- Mensajes opcionales mediante Print, sin cuerpos HTTP ni credenciales; ejemplo Simple con inicializacion LOGO de una linea y WiFi.h por separado.
- Se conservan begin(password), callbacks y API directa avanzada. Ningun cambio en el significado global de e='1' ni relajacion del XML.

## 1.2.0 — Sesion gestionada y cache de variables
- begin/loop/end mantienen el login y recuperan la sesion con espera entre fallos.
- Hasta 16 variables M/AM con sondeo por variable, una lectura por llamada a loop.
- readM(numero) y readAM(numero) consultan cache local; availableM/AM indica muestra disponible.
- Callbacks de primera muestra, resincronizacion y cambio; sin repetir valores estables.
- La cache se invalida al perder sesion, cambiar perfil o escribir; nunca se repite una escritura.
- Un ACK de escritura no reconocido sigue fallando, pero en modo gestionado deja que la siguiente lectura compruebe sesion y estado sin provocar inmediatamente otro login.
- Se conserva la API directa de dos argumentos y el transporte HTTP por peticion.
- Primera unidad fisica: usuario confirma login y lectura M, con resultado i='v1'. Referencia/firmware por registrar; ACK de escritura pendiente.

## 1.1.0 — Base adaptable, primera validación física pendiente
- Perfiles configurables de protocolo y capacidades, sin anunciar firmwares como validados.
- Transporte separado HTTP/HTTPS; TLS con CA de confianza y sin modo inseguro.
- Lectura HTTP acotada: Content-Length/chunked/cierre, límites de cuerpo/cabeceras y plazo total de E/S.
- Parser XML del resultado exacto, sin falsos ACK por coincidencias de texto; rechazo de datos parciales/duplicados.
- Sesiones descartadas ante 401/403, Wi-Fi perdido y errores UAM con HTTP 200.
- Diagnósticos estructurados sin incluir cuerpos, tokens ni pruebas de contraseña.
- Índices/valores de entrada ampliados a int32_t para validarlos antes de convertir.
- Corrección de plazos antiguos de reautenticación en la demo.
- Ejemplo manual PruebaLOGOReal compartido por Arduino IDE/PlatformIO; entorno logo-real separado.
- Simulador con hexadecimal estricto, ocultación de autenticación, timeout de lectura y caducidad de sesión/desafío.
- Tests que compilan y ejecutan el C++ real con adaptadores de plataforma; matriz por firmware y guía de primera prueba.
- PlatformIO fijado a espressif32 6.12.0. Se mantienen credenciales de demostración y licencia pendiente.

Las llamadas habituales conservan su forma. Código que utilice punteros a miembros con firmas uint8_t/int16_t debe actualizarse a las nuevas firmas int32_t. No se declara estabilidad ABI.

## V1 (1.0.0, laboratorio; no release estable para PLC real)
- Marca CHQ Labs, nombre `CHQLogoWeb`, clases y metadatos PlatformIO/Arduino.
- Migración de prototipo HTTP/AJAX V0.3 al esquema `lib/CHQLogoWeb`.
- API `login`, `readAM`, `writeAM`, `readM`, `writeM`, `exchangeRaw`.
- Mejoras de control local de sesión, validación básica de respuestas, timeout y errores.
- Ejemplo ESP32-S3 bidireccional (AM1 manual, AM2 eco, AM3 sensor simulado).
- Ejemplos Arduino y laboratorio de ultrasónico; este último NO está probado en hardware.
- Simulador y pruebas heredados de V0.3, documentación de límites y matriz multi-firmware.

**Sin verificar:** compilación PlatformIO ESP32-S3 en este entorno, nuevos ejemplos en placa y LOGO! físico.
