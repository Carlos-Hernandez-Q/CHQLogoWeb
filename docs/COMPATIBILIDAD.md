# Compatibilidad por modelo y firmware — 1.1.0

Objetivo: mantener una API común y adaptar variantes del portal web LOGO! 8 con evidencia de cada equipo. Actualmente no se dispone de PLC físico.

## Matriz inicial

| Generación anunciada | Modelo / firmware exactos | Login | Leer M | Escribir M | Leer AM | Escribir AM | HTTP/HTTPS |
| --- | --- | --- | --- | --- | --- | --- | --- |
| LOGO! 8.0 | Pendiente | Sin validar | Sin validar | Sin validar | Sin validar | Sin validar | Sin validar |
| LOGO! 8.1 | Pendiente | Sin validar | Sin validar | Sin validar | Sin validar | Sin validar | Sin validar |
| LOGO! 8.2 | Pendiente | Sin validar | Sin validar | Sin validar | Sin validar | Sin validar | Sin validar |
| LOGO! 8.3 | Pendiente | Sin validar | Sin validar | Sin validar | Sin validar | Sin validar | Sin validar |
| LOGO! 8.4 | Pendiente | Sin validar | Sin validar | Sin validar | Sin validar | Sin validar | Sin validar |
| Otra revisión de LOGO! 8 | Pendiente | Sin validar | Sin validar | Sin validar | Sin validar | Sin validar | Sin validar |

La generación comercial no identifica necesariamente todos los detalles del portal. Se registra la referencia completa y el firmware de cada unidad. Una prueba en una versión no certifica las demás.

Estados a utilizar después de cada prueba: **verificado** (con evidencia), **experimental** (hipótesis), **no soportado** (diferencia identificada) y **sin validar**.

## Parámetros de los perfiles

- Ruta AJAX y nombre del usuario web.
- Código de éxito UAM y longitud del token (1..64 caracteres ASCII alfanuméricos, guion/underscore).
- IDs de solicitud y respuesta. El ID de respuesta no tiene por qué ser igual al enviado.
- Códigos de tipo para marcas M/AM, área y tamaños independientes (`analogSize`, `digitalSize`).
- Base y paso de dirección independientes (`amAddressBase`, `mAddressBase`, `amAddressStride`, `mAddressStride`); por defecto base 0 y paso 1. Se rechaza un perfil cuyo último índice exceda 16 bits.
- Cantidad de marcas dentro del alcance validado; no hay sondeo automático de direcciones.
- Ancho hexadecimal digital (1..4); AM se limita a palabra hexadecimal de 16 bits.
- Habilitación independiente de lectura/escritura digital y analógica.

No hay perfiles nominales “8.0”, “8.1”, etc. que repitan la misma hipótesis y aparenten validación. `mockV1()` mantiene el comportamiento del simulador. `logo8AjaxExperimental()` inicia la prueba física con M; AM debe habilitarse después de capturar su formato. El ejemplo físico deshabilita además la escritura digital por defecto.

Para una variante confirmada:

```cpp
CHQLogoProfile p = CHQLogoProfile::logo8AjaxExperimental();
p.name = "REFERENCIA-FIRMWARE-ENSAYADO";
// Ajustar aquí SOLO lo observado, por ejemplo readResponseId o ajaxPath.
// Habilitar AM cuando se haya confirmado el formato de palabra hexadecimal.
if (!logo.setProfile(p)) Serial.println(logo.lastError());
```

Cambiar el nombre no altera ningún formato. No se usa el texto de modelo/firmware del ejemplo como detector ni como prueba de compatibilidad.

## Diferencias que requieren implementar código adicional

Un firmware con otro algoritmo de autenticación, otro formato de comandos, valores decimales/binarios o respuestas fuera del subconjunto XML admitido necesita una implementación nueva, acompañada de capturas y tests. Los perfiles actuales parametrizan la familia AJAX UAMCHAL/UAMLOGIN; no hacen universal un protocolo desconocido.

El transporte HTTP/HTTPS queda separado mediante `CHQLogoTransport`, para no mezclar cambios de red con la semántica de variables. Ethernet sería un transporte adicional; no está implementado.

## Evidencia que se conserva por equipo

Referencia, firmware, Soft Comfort, HTTP/HTTPS y puerto, perfil exacto, capacidades habilitadas, estado STOP/RUN, comando/resultado sanitizados y lectura de comprobación en el monitor del PLC. Mantener certificados de confianza y contraseñas fuera de los archivos públicos de resultados.

Las referencias públicas son investigación independiente, no certificación de esta librería. La ayuda oficial de Siemens documenta opciones HTTP/HTTPS del servidor, pero no valida por sí misma esta implementación AJAX:

- https://github.com/jankeymeulen/siemens-logo-rest
- https://github.com/j-siderius/Siemens-LOGO-Python-Client
- https://cache.industry.siemens.com/dl/files/807/109974807/att_1299328/v4/Help_en-US_en-US.pdf
