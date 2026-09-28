// Solar EMS - sensor firmware
// Reads:
//   - Battery INA226
//   - PV/Solar INA226
//   - SCT-013 current sensor
//   - DS18B20 temperature sensor
//
// Publishes readings as JSON to an MQTT broker every 5 minutes.
// Built for extended soak testing.
//
// Libraries required (Arduino IDE Library Manager):
//   INA226            by Rob Tillaart
//   OneWire
//   DallasTemperature by Miles Burton
//   PubSubClient      by Nick O'Leary
//   ArduinoJson       by Benoit Blanchon

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <INA226.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define MQTT_MAX_PACKET_SIZE 512
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "time.h"
#include <math.h>

// ============================================================
// WiFi / MQTT CONFIGURATION
// Replace placeholders with your network and broker parameters.
// ============================================================
#define WIFI_SSID            "YOUR_WIFI_SSID"
#define WIFI_PASSWORD        "YOUR_WIFI_PASSWORD"
#define MQTT_BROKER          "YOUR_MQTT_BROKER"
#define MQTT_PORT            8883
#define MQTT_TOPIC           "solar_ems/telemetry"
#define MQTT_PUBLISHER_USER  "YOUR_MQTT_USERNAME"
#define MQTT_PUBLISHER_PASS  "YOUR_MQTT_PASSWORD"

#define DEVICE_ID "solar_ems_001"

// ============================================================
// PIN CONFIGURATION
// ============================================================
#define I2C_SDA     21
#define I2C_SCL     22
#define ONEWIRE_PIN 5 
#define SCT_PIN     35

// ============================================================
// READING INTERVAL
// ============================================================
const unsigned long INTERVAL_S  = 300;  // 5 minutes, for soak testing
const unsigned long INTERVAL_MS = INTERVAL_S * 1000UL;

// ============================================================
// SCT-013 CONFIGURATION
// ============================================================
const float SCT_RATED_AMPS  = 50.0;
const float SCT_RATED_VOLTS = 1.0;
const int   SCT_SAMPLES     = 3000;

// ============================================================
// INA226
//   Battery INA226  = 0x41
//   Solar/PV INA226 = 0x40
// ============================================================
INA226 inaBattery(0x41, &Wire);
INA226 inaSolar(0x40, &Wire);

// ============================================================
// DS18B20
// ============================================================
OneWire oneWire(ONEWIRE_PIN);
DallasTemperature tempSensor(&oneWire);

// ============================================================
// NETWORK / MQTT
// ============================================================
WiFiClientSecure secureClient;
PubSubClient mqttClient(secureClient);

unsigned long lastReadingTime = 0;

// ============================================================
// CONNECT TO WIFI
// ============================================================
void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Configure NTP time
  configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
  Serial.print("Syncing time");

  time_t now = time(nullptr);
  int attempts = 0;

  while (now < 100000 && attempts < 20) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
    attempts++;
  }
  Serial.println();

  if (now < 100000) {
    Serial.println("Time sync failed, continuing without accurate time");
  } else {
    Serial.println("Time synced.");
  }
}

// ============================================================
// CONNECT TO MQTT
// ============================================================
void connectMQTT() {
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setBufferSize(512);

  while (!mqttClient.connected()) {
    Serial.print("Connecting to MQTT broker...");
    String clientId = "esp32-" + String(DEVICE_ID);

    if (mqttClient.connect(clientId.c_str(), MQTT_PUBLISHER_USER, MQTT_PUBLISHER_PASS)) {
      Serial.println(" connected.");
    } else {
      Serial.print(" failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(", retrying in 3 seconds");
      delay(3000);
    }
  }
}

// ============================================================
// GET ISO 8601 UTC TIMESTAMP
//   Example: 2026-08-23T19:00:00Z
// ============================================================
String getIsoTimestamp() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);

  char buf[25];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(buf);
}

// ============================================================
// READ SCT-013 RMS CURRENT
//   Subtracts the sample mean as the bias reference, so it
//   self-corrects for drift instead of assuming a fixed midpoint.
// ============================================================
float readLoadCurrentRMS() {
  long sum = 0;
  static int samples[SCT_SAMPLES];

  for (int i = 0; i < SCT_SAMPLES; i++) {
    samples[i] = analogRead(SCT_PIN);
    sum += samples[i];
    delayMicroseconds(100);
  }

  float mean = (float)sum / SCT_SAMPLES;

  double sumSquares = 0;
  for (int i = 0; i < SCT_SAMPLES; i++) {
    float centered = samples[i] - mean;
    sumSquares += centered * centered;
  }

  float rmsCounts = sqrt(sumSquares / SCT_SAMPLES);
  float rmsVolts   = rmsCounts * (3.3 / 4095.0);
  float amps       = rmsVolts * (SCT_RATED_AMPS / SCT_RATED_VOLTS);

  return amps;
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("==============================");
  Serial.println(" Solar EMS Starting");
  Serial.println("==============================");

  Wire.begin(I2C_SDA, I2C_SCL);
  Serial.println("I2C initialized.");

  Serial.println("Checking Battery INA226 at 0x41...");
  if (!inaBattery.begin()) {
    Serial.println("ERROR: Battery INA226 (0x41) not found.");
  } else {
    Serial.println("Battery INA226 detected.");
  }
  inaBattery.setMaxCurrentShunt(50.0, 0.0015);

  Serial.println("Checking Solar INA226 at 0x40...");
  if (!inaSolar.begin()) {
    Serial.println("ERROR: Solar INA226 (0x40) not found.");
  } else {
    Serial.println("Solar INA226 detected.");
  }
  inaSolar.setMaxCurrentShunt(50.0, 0.0015);

  tempSensor.begin();
  Serial.println("DS18B20 initialized.");

  analogReadResolution(12);
  pinMode(SCT_PIN, INPUT);
  Serial.println("ADC initialized.");

  connectWiFi();

  // WARNING: setInsecure() disables certificate verification.
  // Suitable for initial bench and prototype testing; configure proper
  // CA root certificate validation for production deployments.
  secureClient.setInsecure();

  connectMQTT();

  Serial.println();
  Serial.println("==============================");
  Serial.println(" Solar EMS Ready");
  Serial.println("==============================");
}

// ============================================================
// MAIN LOOP
// ============================================================
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Reconnecting...");
    connectWiFi();
  }

  if (!mqttClient.connected()) {
    Serial.println("MQTT disconnected. Reconnecting...");
    connectMQTT();
  }

  mqttClient.loop();

  unsigned long nowMillis = millis();
  if (nowMillis - lastReadingTime < INTERVAL_MS && lastReadingTime != 0) {
    return;
  }
  lastReadingTime = nowMillis;

  // ---- Solar / PV INA226 ----
  float solarVoltage = inaSolar.getBusVoltage();
  float solarCurrent = inaSolar.getCurrent_mA() / 1000.0;
  float solarPower   = inaSolar.getPower_mW() / 1000.0;
  float solarShuntV  = inaSolar.getShuntVoltage_mV();

  // ---- Battery INA226 ----
  float batteryVoltage = inaBattery.getBusVoltage();
  float batteryCurrent = inaBattery.getCurrent_mA() / 1000.0;
  float batteryPower   = inaBattery.getPower_mW() / 1000.0;
  float batteryShuntV  = inaBattery.getShuntVoltage_mV();
  bool  charging       = (batteryCurrent > 0.05);

  // ---- Load current (SCT-013) ----
  float loadCurrent = readLoadCurrentRMS();

  // ---- Temperature (DS18B20) ----
  tempSensor.requestTemperatures();
  float temperature = tempSensor.getTempCByIndex(0);

  // ---- Build JSON payload ----
  StaticJsonDocument<512> doc;
  doc["device_id"] = DEVICE_ID;
  doc["timestamp"] = getIsoTimestamp();

  JsonObject solar = doc.createNestedObject("solar");
  solar["voltage_v"] = solarVoltage;
  solar["current_a"] = solarCurrent;
  solar["power_w"]   = solarPower;
  solar["shunt_mv"]  = solarShuntV;

  JsonObject battery = doc.createNestedObject("battery");
  battery["voltage_v"] = batteryVoltage;
  battery["current_a"] = batteryCurrent;
  battery["power_w"]   = batteryPower;
  battery["shunt_mv"]  = batteryShuntV;
  battery["charging"]  = charging;

  JsonObject load = doc.createNestedObject("load");
  load["current_a"] = loadCurrent;

  doc["temperature_c"] = temperature;
  doc["interval_s"]    = INTERVAL_S;

  char payload[512];
  size_t len = serializeJson(doc, payload, sizeof(payload));

  // ---- Serial output ----
  Serial.println();
  Serial.println("------------------------------");
  Serial.print("Solar Voltage: ");   Serial.print(solarVoltage, 3);   Serial.println(" V");
  Serial.print("Solar Current: ");   Serial.print(solarCurrent, 3);   Serial.println(" A");
  Serial.print("Solar Power: ");     Serial.print(solarPower, 3);     Serial.println(" W");
  Serial.print("Battery Voltage: "); Serial.print(batteryVoltage, 3); Serial.println(" V");
  Serial.print("Battery Current: "); Serial.print(batteryCurrent, 3); Serial.println(" A");
  Serial.print("Battery Power: ");   Serial.print(batteryPower, 3);   Serial.println(" W");
  Serial.print("Battery Charging: ");Serial.println(charging ? "YES" : "NO");
  Serial.print("Load Current: ");    Serial.print(loadCurrent, 3);    Serial.println(" A");
  Serial.print("Temperature: ");     Serial.print(temperature, 2);    Serial.println(" C");
  Serial.print("Timestamp: ");       Serial.println(getIsoTimestamp());
  Serial.println("------------------------------");

  // ---- MQTT publish ----
  Serial.print("Publishing to ");
  Serial.print(MQTT_TOPIC);
  Serial.print(": ");
  Serial.println(payload);

  bool sent = mqttClient.publish(MQTT_TOPIC, payload, len);

  if (sent) {
    Serial.println("MQTT publish successful.");
  } else {
    Serial.println("MQTT publish FAILED.");
  }
}
