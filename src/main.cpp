/*
 * Nodo Sensor Invernadero - Heltec WiFi LoRa 32 V2
 * =================================================
 * 
 * Sistema de monitoreo autonomo para invernaderos con capacidad de:
 *   - Lectura de temperatura y humedad (DHT11)
 *   - Lectura de intensidad luminosa (BH1750)
 *   - Calculo de Deficit de Presion de Vapor (VPD)
 *   - Transmision de datos via LoRa (banda 915MHz)
 *   - Sincronizacion horaria bidireccional con Gateway
 *   - Datalogger en tarjeta SD con formato inteligente
 *   - Gestión de energia con Light Sleep y pulsos WiFi para Power Bank
 *
 * Hardware:
 *   - Placa: Heltec WiFi LoRa 32 V2 (ESP32 + SX1276 + OLED)
 *   - Sensor: DHT11 en GPIO17
 *   - Sensor: BH1750 en bus I2C (SDA=GPIO4, SCL=GPIO15)
 *   - Tarjeta SD en bus SPI dedicado (SCK=21, MISO=22, MOSI=23, CS=13)
 *
 * Estrategia de energia:
 *   - Light Sleep de 12 segundos entre ciclos (~1.5mA)
 *   - Transmision LoRa cada 15 ciclos (3 minutos)
 *   - Pulso WiFi en modo SoftAP (~130mA) durante ventana de escucha LoRa
 *     para mantener activa la power bank
 */

#include <Heltec.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <BH1750.h>
#include <DHT.h>
#include <WiFi.h>
#include <math.h>

#define BAND    915E6
#define DHTPIN  17
#define DHTTYPE DHT11
#define SD_CS   13

#define SLEEP_TIME_SEC          12
#define CICLOS_PARA_TRANSMITIR  15
#define LORA_LISTEN_WINDOW_MS   2000
#define MAX_LORA_PAYLOAD_CHARS  30

DHT dht(DHTPIN, DHTTYPE);
BH1750 lightMeter;
SPIClass spiSD(HSPI);

bool bh1750Ok = false;
bool sdOk = false;

unsigned long numeroCiclo = 0;
RTC_DATA_ATTR int ciclosAcumulados = 0;
unsigned long totalMsTranscurridos = 0;
unsigned long horasRelativas = 0;
unsigned long minutosRelativos = 0;
unsigned long segundosRelativos = 0;

// ------------------------------------------------------------------
//  FUNCIONES AUXILIARES
// ------------------------------------------------------------------

void actualizarTiempoRelativo(unsigned long msDinamicos) {
  totalMsTranscurridos += msDinamicos;
  unsigned long totalSegundos = totalMsTranscurridos / 1000;
  horasRelativas = (totalSegundos / 3600) % 24;
  minutosRelativos = (totalSegundos % 3600) / 60;
  segundosRelativos = totalSegundos % 60;
}

float calcularVPD(float temp, float hum) {
  if (isnan(temp) || isnan(hum) || temp < -40 || temp > 85) {
    return 0.0;
  }
  float svp = 0.61078 * exp((17.27 * temp) / (temp + 237.3));
  float vpd = svp * (1.0 - (hum / 100.0));
  return (vpd < 0.0) ? 0.0 : vpd;
}

String escucharRespuestaGateway() {
  String respuesta = "";
  unsigned long tiempoInicio = millis();

  while (millis() - tiempoInicio < LORA_LISTEN_WINDOW_MS) {
    if (LoRa.parsePacket()) {
      while (LoRa.available() && respuesta.length() < MAX_LORA_PAYLOAD_CHARS) {
        respuesta += (char)LoRa.read();
      }
      if (respuesta.length() > 0) {
        break;
      }
    }
    yield();
  }

  LoRa.idle();
  return respuesta;
}

void escribirDatalog(float temp, float hum, float lux, float vpd,
                     String fechaHoraGateway, bool gatewayRespondio) {
  if (!sdOk) {
    sdOk = SD.begin(SD_CS, spiSD);
  }
  if (!sdOk) {
    return;
  }

  File dataFile = SD.open("/datalog.txt", FILE_APPEND);
  if (!dataFile) {
    return;
  }

  String linea;
  if (gatewayRespondio && fechaHoraGateway.length() > 0) {
    linea = "[" + fechaHoraGateway + "] "
          + String(temp) + "," + String(hum) + "," + String(lux) + ","
          + String(vpd);
  } else {
    linea = "[CICLO:" + String(numeroCiclo)
          + " - TEMP:" + String(horasRelativas) + "h"
          + String(minutosRelativos) + "m" + String(segundosRelativos) + "s] "
          + String(temp) + "," + String(hum) + "," + String(lux) + ","
          + String(vpd);
  }

  dataFile.println(linea);
  dataFile.close();
}

void initiarVentanaDeEscuchaConWiFi() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("Pulso_PowerBank");
}

void finalizarVentanaDeEscuchaYApagarWiFi() {
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
}

void setup() {
  Heltec.begin(true, true, true, true, BAND);

  Wire.begin(4, 15);
  delay(100);

  bh1750Ok = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
  WiFi.mode(WIFI_OFF);
  dht.begin();

  spiSD.begin(21, 22, 23, SD_CS);
  sdOk = SD.begin(SD_CS, spiSD);

  delay(100);

  Heltec.display->clear();
  Heltec.display->drawString(0, 0, "Inicializando...");
  Heltec.display->drawString(0, 15, "Nodo Emisor LoRa");
  Heltec.display->drawString(0, 30, "Autonomo v2.0");
  Heltec.display->display();
  delay(2000);
}

void loop() {
  // ------------------------------------------------------------------
  // FASE 0: ESTABILIZACION POST-SLEEP Y CONTEO DE CICLOS
  // ------------------------------------------------------------------
  delay(50);

  unsigned long tiempoDespiertoInicio = millis();
  numeroCiclo++;
  ciclosAcumulados++;

  bool esTransmision = (ciclosAcumulados >= CICLOS_PARA_TRANSMITIR);

  if (!esTransmision) {
    // Ciclo de ahorro: solo pulso WiFi para power bank, sin sensores ni LoRa
    Heltec.display->clear();
    Heltec.display->drawString(0, 0, "Modo Ahorro");
    Heltec.display->drawString(0, 15, "Ciclo: " + String(ciclosAcumulados)
                               + "/" + String(CICLOS_PARA_TRANSMITIR));
    Heltec.display->drawString(0, 30, "Tx en: "
                               + String(CICLOS_PARA_TRANSMITIR - ciclosAcumulados)
                               + " ciclos");
    Heltec.display->drawString(0, 45, "Pulso PowerBank...");
    Heltec.display->display();

    initiarVentanaDeEscuchaConWiFi();
    delay(100);
    finalizarVentanaDeEscuchaYApagarWiFi();

    unsigned long tiempoDespiertoMs = millis() - tiempoDespiertoInicio;
    unsigned long msTotalesAadir = (SLEEP_TIME_SEC * 1000) + tiempoDespiertoMs;
    actualizarTiempoRelativo(msTotalesAadir);

    esp_sleep_enable_timer_wakeup(SLEEP_TIME_SEC * 1000000ULL);
    esp_light_sleep_start();
    return;
  }

  ciclosAcumulados = 0;

  // ------------------------------------------------------------------
  // FASE 1: LECTURA DE SENSORES
  // ------------------------------------------------------------------
  String errLog = "";

  float temp = dht.readTemperature();
  float hum = dht.readHumidity();
  if (isnan(temp) || isnan(hum)) {
    errLog += "DHT11: Error\n";
    temp = 0.0;
    hum = 0.0;
  }

  float lux = 0.0;
  if (!bh1750Ok) {
    errLog += "BH1750: Error\n";
    bh1750Ok = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
  } else {
    lux = lightMeter.readLightLevel();
  }

  if (!sdOk) {
    errLog += "SD: Error\n";
    sdOk = SD.begin(SD_CS, spiSD);
  }

  float vpd = calcularVPD(temp, hum);

  // ------------------------------------------------------------------
  // FASE 2: PANTALLA OLED - DIAGNOSTICO
  // ------------------------------------------------------------------
  Heltec.display->clear();
  if (errLog == "") {
    Heltec.display->drawString(0, 0, "Diagnostico: OK");
    Heltec.display->drawString(0, 15, "T:" + String(temp) + "C  H:"
                               + String(hum) + "%");
    Heltec.display->drawString(0, 25, "Luz:" + String(lux) + "lx VPD:"
                               + String(vpd) + "kPa");
  } else {
    Heltec.display->drawString(0, 0, "FALLO DETECTADO:");

    int y = 12;
    int index = 0;
    while (errLog.indexOf('\n', index) != -1 && y < 50) {
      int next = errLog.indexOf('\n', index);
      Heltec.display->drawString(0, y, errLog.substring(index, next));
      index = next + 1;
      y += 10;
    }
  }
  Heltec.display->drawString(0, 52, "Enviando LoRa...");
  Heltec.display->display();

  // ------------------------------------------------------------------
  // FASE 3: TRANSMISION LORA
  // ------------------------------------------------------------------
  String payload = String(temp) + "," + String(hum) + ","
                 + String(lux) + "," + String(vpd);

  LoRa.beginPacket();
  LoRa.setTxPower(14, RF_PACONFIG_PASELECT_PABOOST);
  LoRa.print(payload);
  LoRa.endPacket();

  // ------------------------------------------------------------------
  // FASE 4: VENTANA BIDIRECCIONAL LORA + PULSO WIFI
  // ------------------------------------------------------------------
  initiarVentanaDeEscuchaConWiFi();
  delay(50);
  LoRa.receive();
  String respuestaGateway = escucharRespuestaGateway();
  finalizarVentanaDeEscuchaYApagarWiFi();

  bool gatewayRespondio = (respuestaGateway.length() > 0)
                       && (respuestaGateway.indexOf("No_Sync") == -1);

  // ------------------------------------------------------------------
  // FASE 5: ACTUALIZACION DE RELOJ RELATIVO
  // ------------------------------------------------------------------
  unsigned long tiempoDespiertoMs = millis() - tiempoDespiertoInicio;
  unsigned long msTotalesAadir = (SLEEP_TIME_SEC * 1000) + tiempoDespiertoMs;
  actualizarTiempoRelativo(msTotalesAadir);

  // ------------------------------------------------------------------
  // FASE 6: DATALOGGER EN SD + PANTALLA FINAL
  // ------------------------------------------------------------------
  escribirDatalog(temp, hum, lux, vpd, respuestaGateway, gatewayRespondio);

  Heltec.display->clear();
  if (gatewayRespondio) {
    Heltec.display->drawString(0, 0, "Gateway: ONLINE");
    Heltec.display->drawString(0, 15, "Fecha/Hora: " + respuestaGateway);
  } else {
    Heltec.display->drawString(0, 0, "Gateway: OFFLINE");
    Heltec.display->drawString(0, 15, "Tiempo Relativo:");
    Heltec.display->drawString(0, 25, String(horasRelativas) + "h "
                               + String(minutosRelativos) + "m "
                               + String(segundosRelativos) + "s");
  }
  Heltec.display->drawString(0, 40, "Tx Completado");
  Heltec.display->drawString(0, 52, "Durmiendo " + String(SLEEP_TIME_SEC) + "s...");
  Heltec.display->display();

  // ------------------------------------------------------------------
  // FASE 7: RETORNO A LIGHT SLEEP
  // ------------------------------------------------------------------
  esp_sleep_enable_timer_wakeup(SLEEP_TIME_SEC * 1000000ULL);
  esp_light_sleep_start();
}