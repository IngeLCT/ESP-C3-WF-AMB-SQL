# ESP32-C3-WF-AMB

Proyecto para **ESP32-C3** (ESP-IDF) orientado a **mediciones ambientales** con **conectividad Wi-Fi** y envío de datos a **Firebase Realtime Database**.  
Incluye un **portal cautivo** con modo **AP+NAT (router)** para asistir en redes con **portal cautivo**, **verificación activa de conectividad a Internet** y soporte **mDNS** (opcional). Licencia **MIT**.

---

## ¿Qué hace?

- **Mide variables ambientales** (p. ej., PM1.0/2.5/4.0/10, VOC, NOx, CO₂, temperatura, humedad) y arma un **payload JSON** con las lecturas.  
- **Configura Wi-Fi** sin hard-coding de credenciales mediante un **portal cautivo** propio (escaneo, selección de SSID y guardado persistente).  
- **Verifica** si hay **salida a Internet**; si no la hay, habilita **AP+NAT** para que el usuario complete el **login del portal cautivo** desde su teléfono/PC.  
- **Envía mediciones a Firebase** por **HTTP/REST**.  
- **Intervalo de envío configurable** y **promedio local configurable**: el dispositivo acumula **N muestras** y **envía solo el promedio** para reducir ruido/uso de red.

---

## Requisitos

- **ESP-IDF 5.x** (recomendado 5.5.1) y **Python 3.11.x** para el toolchain.  
- **Target**: `esp32c3`.  
- **Firebase Realtime Database** operativo (URL y API-Key).  
- (Opcional) **mDNS** habilitado si quieres acceso por `*.local`.

---

## Funcionalidades

### 1) Captive Manager (portal cautivo)

- **SoftAP + portal web** para la provisión inicial (o recuperación).  
- **Escaneo de redes** y lista de SSID; identificación de redes **abiertas** (sin contraseña) y **guardado persistente** en NVS.  
- **Arranque inteligente**: si existen credenciales válidas y hay salida a Internet, **omite** el portal y continúa en **STA**.  
- **Redirección tipo “cautivo”**: mientras está en AP, toda navegación apunta al portal local para configurar.  
- **Reintentos / timeouts**: si la conexión falla, **reabre** el portal para re-provisión.

### 2) Verificación de Internet

- Tras asociarse a la red, realiza **pruebas activas de conectividad** (DNS/HTTP con timeouts).  
- Si **no hay salida** en varios intentos (configurable), cambia a estado de **asistencia** y/o **re-provisión** según corresponda.

### 3) Asistencia para portales cautivos (modo AP+NAT)

- Cuando la red requiere **login** (portal cautivo), el dispositivo mantiene **AP+STA** y habilita **NAT** para que los clientes del AP naveguen “a través” del ESP hasta la red STA.  
- El usuario se conecta al **AP del ESP** y realiza el **login** del portal.  
- Una vez que las **pruebas de Internet** pasan el umbral (configurable), el dispositivo **apaga el AP** y queda **operativo en STA**.

> **Nota:** El dispositivo **no automatiza** el login del portal; solo **facilita** el proceso con el patrón **AP+NAT**.

### 4) Medición y envío de datos (Firebase)

- Lectura de **sensores ambientales** y **serialización JSON**.  
- **Promedio local**: se acumulan **N muestras** (configurable) y se **envía un promedio** a la base para reducir ruido y uso de red.  
- **Intervalo de envío** y **N de muestras** son **configurables** en el código principal de la app.  
- Cliente **REST** ligero para **Firebase Realtime Database**.

### 5) mDNS (opcional)

- Posibilidad de anunciar **hostname** y servicio **HTTP** para acceso vía `http://<hostname>.local` en la LAN (si tu entorno soporta mDNS).  

---

## Licencia

Distribuido bajo **MIT**. Consulta el archivo `LICENSE` en el repositorio.
