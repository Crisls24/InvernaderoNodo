# Nodo Sensor Invernadero — ESP32 LoRa

Firmware (PlatformIO / Arduino) del **nodo sensor autónomo** del sistema de monitoreo de invernaderos: lectura de sensores, cálculo de VPD y transmisión por LoRa con energía optimizada.

El nodo lee temperatura, humedad y luminosidad, calcula el **VPD (Déficit de Presión de Vapor)**, guarda un datalog en microSD y sincroniza su reloj de forma bidireccional con el gateway. Corre sobre una **Heltec WiFi LoRa 32 V2** (ESP32 + SX1276 + OLED) en banda de 915 MHz, con una estrategia de ahorro de energía diseñada para operar con power bank.

## Características

- Lectura de **temperatura y humedad** (DHT11, GPIO17).
- Lectura de **intensidad luminosa** (BH1750, I2C).
- **Cálculo de VPD** por sensor para decisiones de riego.
- **Transmisión LoRa (915 MHz)** con payload CSV `temp,hum,lux,vpd`.
- **Sincronización de reloj bidireccional** con el gateway (fecha/hora NTP).
- **Datalogger en microSD** (`datalog.txt`) con marca de tiempo real o relativa.
- **Gestión de energía:** Light Sleep de 12 s (~1.5 mA) + pulso WiFi en modo SoftAP para mantener viva la power bank.
- **Diagnóstico en OLED** (estado de sensores, errores y gateway online/offline).

## Hardware

| Componente | Conexión |
| --- | --- |
| Heltec WiFi LoRa 32 V2 | — |
| DHT11 (temperatura/humedad) | GPIO17 |
| BH1750 (luxómetro) | I2C: SDA=GPIO4, SCL=GPIO15 |
| MicroSD (datalogger) | SPI dedicado: SCK=21, MISO=22, MOSI=23, CS=13 |

## Estrategia de energía

- `Light Sleep` de 12 s entre ciclos (~1.5 mA).
- Transmisión LoRa cada 15 ciclos (~3 min).
- Pulso WiFi en modo SoftAP (~130 mA) durante la ventana de escucha LoRa para mantener activa la power bank.

## Configuración

`platformio.ini` — entorno `heltec_wifi_lora_32_V2`, región `US915`. Dependencias: Heltec ESP32 Dev-Boards, DHT sensor library, Adafruit Unified Sensor y BH1750.

## Build y flash

1. Clona el repo e instala PlatformIO (extensión de VS Code o CLI).
2. Compila y sube el firmware:
   ```
   pio run -t upload
   ```
3. Monitorea por serie (115200 baud):
   ```
   pio device monitor
   ```

## Payload LoRa

Formato CSV de 4 campos enviado en cada transmisión:

```
<temperatura>,<humedad>,<luminosidad>,<vpd>
```

## Proyectos relacionados

| Repo | Descripción |
| --- | --- |
| [`InvernaderoGateway`](https://github.com/Crisls24/InvernaderoGateway) | Gateway que recibe los paquetes LoRa y los publica en Firebase |
| [`ServicioBioSensor`](https://github.com/Crisls24/ServicioBioSensor) | App Flutter que consume los datos en Firebase |