# CHQLogoWeb 1.1.0 — CHQ Labs

Cliente experimental ESP32/ESP32-S3 para el portal web HTTP/HTTPS de LOGO! 8, sin Modbus. Ningún firmware Siemens físico está validado todavía.

La API incluye `login`, `readAM`, `writeAM`, `readM`, `writeM`, `exchangeRaw` y diagnósticos. `CHQLogoProfile` concentra las variantes de ruta, usuario, IDs, tipos, rangos y capacidades; no certifica versiones por su nombre.

El constructor conserva el perfil `mockV1()` para el laboratorio anterior. Para un PLC seleccionar explícitamente `CHQLogoProfile::logo8AjaxExperimental()`: AM está deshabilitada hasta confirmar el formato mediante captura.

HTTPS se configura con `useHTTPS(rootCaPem, 443)` y requiere CA de confianza, host acorde al certificado y reloj correcto. No hay modo inseguro ni fallback automático. Las respuestas están acotadas y no se reintentan escrituras automáticamente.

Instalar esta carpeta en `Documentos/Arduino/libraries/CHQLogoWeb`, o instalar el ZIP desde Arduino IDE. Los ejemplos antiguos usan el mock en puerto 8088. Abrir `PruebaLOGOReal` para la primera unidad física y completar su pestaña `PruebaLOGORealApp.h`. El ejemplo inicia con lecturas digitales manuales y todas las escrituras deshabilitadas.

La licencia sigue pendiente de decisión del autor. Esta versión es de laboratorio.
