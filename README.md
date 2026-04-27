# ESP-C3-WF-AMB-SQL

Firmware para **ESP32-C3** con **ESP-IDF** orientado a medición ambiental, provisión Wi-Fi asistida y envío periódico de datos a un backend **Hostinger/PHP/SQL**.

## Qué hace

- Lee sensores ambientales por **I2C**:
  - **SCD41**: CO2, temperatura y humedad
  - **SEN55**: PM1.0, PM2.5, PM4.0, PM10, VOC, NOx, temperatura y humedad
- Calcula **promedios locales** antes de transmitir.
- Se conecta por **Wi-Fi** usando un flujo con portal cautivo propio.
- Puede ayudar con redes que tienen **portal cautivo externo** mediante modo **AP + STA + NAT**.
- Envía las lecturas por **HTTP POST** a un endpoint de ingesta en Hostinger.
- Guarda en NVS:
  - SSID
  - contraseña
  - ubicación textual del equipo

## Flujo general

1. Arranca el **captive manager**.
2. Si hay credenciales guardadas, intenta conectar en modo STA.
3. Si no hay credenciales, levanta un **SoftAP** con portal local para configurar red y ubicación.
4. Si la red requiere login externo, mantiene **AP+STA+NAT** para que el usuario complete el acceso.
5. Cuando detecta internet operativa:
   - sincroniza hora con SNTP
   - inicializa sensores
   - arranca la tarea de medición y envío

## Muestreo y envío

La lógica actual del firmware es:

- **1 muestra cada 5 segundos**
- **60 muestras por lote**
- **1 envío cada 5 minutos**

Cada lote se promedia localmente antes de formar el JSON de salida.

## JSON enviado

El payload incluye, según corresponda:

- `pm1p0`
- `pm2p5`
- `pm4p0`
- `pm10p0`
- `voc`
- `nox`
- `cTe`
- `cHu`
- `co2`
- `hora`
- `fecha` (cuando aplica)
- `inicio` (primer envío tras arranque)
- `ciudad`
- `id`

## Portal cautivo y conectividad

El componente `captive_manager` implementa:

- portal web local para provisión Wi-Fi
- almacenamiento persistente de credenciales y ubicación
- verificación activa de conectividad a internet
- asistencia para redes con portal cautivo usando **AP+STA+NAT**
- soporte de **mDNS** configurable

## mDNS

El hostname mDNS se configura desde `main/Privado.h` mediante:

- `MDNS_HOSTNAME`

Ese valor se pasa al `captive_manager` y se anuncia como:

- `http://<MDNS_HOSTNAME>.local`

(si la red/cliente soporta mDNS)

## Archivo local requerido: `main/Privado.h`

Este archivo no se versiona y debe definir al menos:

```c
#define DEVICE_ID            "ESP-C3-WF-AMB-SQL-01"
#define MDNS_HOSTNAME        "LCTAmbiente02"
#define HOSTINGER_API_KEY    "REEMPLAZAR_API_KEY"
#define HOSTINGER_URL_INGEST "https://example.com/api/ingest.php"
#define HOSTINGER_URL_ADMIN  "https://example.com/api/admin.php"
```

## Notas importantes

- El firmware **ya no usa Firebase**.
- El envío se hace contra backend **Hostinger/PHP/SQL**.
- Se eliminó del flujo principal el uso de:
  - `delete_all`
  - `trim_oldest`
- La ubicación queda fija hasta que el usuario la reconfigure desde el portal.

## Dependencias

- ESP-IDF 5.x
- target: `esp32c3`
- componente `espressif/mdns`

## Licencia

Distribuido bajo licencia **MIT**. Revisa el archivo `LICENSE`.
