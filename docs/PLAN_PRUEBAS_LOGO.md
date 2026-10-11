# Matriz de validación para cuando tengamos LOGO! 8

**Objetivo:** comprobar compatibilidad experimental sin modificar elementos conectados a maquinaria ni perder configuración.

## Inventario por cada unidad que se pruebe

| Dato | Registrar |
| --- | --- |
| Modelo/referencia de LOGO! | `6ED...` |
| Generación / revisión anunciada | `8.0 / 8.1 / 8.2 / 8.3 / 8.4 / otra` (sin asumir equivalencia) |
| Firmware exacto | Anotar versión visible en diagnóstico |
| LOGO! Soft Comfort y versión | Anotar |
| Servidor web HTTP habilitado | Sí/No / puerto |
| HTTPS requerido/disponible | Sí/No / puerto / certificados |
| Privilegios del usuario web | Consulta/escritura según configuración |
| Estado de trabajo | STOP/RUN, programa de laboratorio |

## Procedimiento de laboratorio seguro

1. Guardar copia del programa y parámetros, y aislar salidas de equipos reales.
2. Conectar PC y LOGO! en red aislada. Confirmar navegación manual al portal.
3. Abrir herramientas del navegador (Network) y **filtrar peticiones a `/AJAX`**, si la versión las utiliza.
4. Preparar `AM1` destinada a prueba sin escritores lógicos; `M1` reservada, sin conexión a Q física.
5. Intentar desde portal manual: leer M1/AM1 y cambiar valores `0`, `250`, `-120` (si interfaz soporta negativos). Capturar solicitud y respuesta.
6. Verificar comportamiento en STOP y en RUN; comparar con programa que tenga `AM1` solo como fuente de lógica y con bloque que intente sobrescribirla.
7. Usar el entorno `logo-real` / ejemplo `PruebaLOGOReal`: primero `login` y `m`, después habilitar escritura M con lectura posterior. Habilitar AM tras confirmar su formato. Comparar con navegador y Soft Comfort, documentar errores. Ver `PRIMER_LOGO_FISICO.md`.
8. Forzar reconexión Wi-Fi y caducidad de sesión; medir recuperación y consumo de memoria.
9. Si hace falta HTTPS, configurar `useHTTPS(CA, puerto)`, reloj y host acorde al certificado. El transporte está implementado y compilado; validar la negociación y certificados en el PLC físico.
10. Repetir por modelo y firmware, sin asumir que todos los LOGO! 8 se comportan igual.

## Hoja de resultados (copiar por cada PLC)

| Prueba | Resultado | Evidencia |
| --- | --- | --- |
| Web manual permite escribir AM | PENDIENTE | Captura |
| Valor de AM se mantiene en RUN | PENDIENTE | Monitor online |
| GETVARS AM aceptado | PENDIENTE | Tráfico HTTP |
| SETVARS AM aceptado | PENDIENTE | Tráfico HTTP |
| GETVARS M aceptado | PENDIENTE | Tráfico HTTP |
| SETVARS M aceptado | PENDIENTE | Tráfico HTTP |
| Logout/relogin/reconexión | PENDIENTE | Monitor serie |
| Cadencia estable de muestreo | PENDIENTE | Log |
| Versión HTTP/HTTPS compatible | PENDIENTE | Firmware |

**No registrar tokens o contraseñas en capturas que vayas a compartir públicamente.**
