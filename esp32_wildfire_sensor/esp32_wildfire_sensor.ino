/*
 * GreenGuard Wildfire Sensor - ESP32 + DHT11
 * Working version - matches successful Python connection
 *
 * This firmware reads temperature and humidity from DHT11 sensor
 * and publishes data to AWS IoT Core via MQTT over TLS.
 * Location is resolved at startup via Google Geolocation API (WiFi scan).
 *
 * Hardware:
 * - ESP32 Dev Module
 * - DHT11 sensor
 *
 * Wiring:
 * - DHT11 VCC -> ESP32 3V3
 * - DHT11 DATA -> ESP32 GPIO15 (D15)
 * - DHT11 GND -> ESP32 GND
 *
 * Requirements:
 * - Install ArduinoJson library
 * - Install DHT sensor library
 * - Install AsyncMQTT_ESP32 library (Library Manager: "AsyncMQTT_ESP32" by Marvin ROGER)
 * - Install AsyncTCP library (dependency, by dvarrel)
 *
 * AWS IoT Core Setup:
 * 1. Create IoT Thing in AWS IoT Core
 * 2. Download device certificate, private key, and root CA
 * 3. Update certificates, endpoint, and GOOGLE_GEO_API_KEY in config.h
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <time.h>
#include "config.h"

// DHT11 Sensor Configuration
#define DHTPIN 15  // GPIO 15 (D15)
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// Use values from config.h
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;
const char* aws_iot_endpoint = AWS_IOT_ENDPOINT;
const int aws_iot_port = 8883;

// Location — populated at startup via Google Geolocation API
float sensorLat = 0.0;
float sensorLng = 0.0;

// Unique client ID and topic
char deviceId[64];
char mqtt_topic[128];

// Timing
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 30000;  // 30 seconds

// WiFi and MQTT clients
WiFiClientSecure net;
PubSubClient mqttClient(net);

const char* root_ca = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF\n" \
"ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6\n" \
"b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL\n" \
"MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv\n" \
"b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj\n" \
"ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM\n" \
"9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw\n" \
"IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6\n" \
"VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L\n" \
"93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm\n" \
"jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC\n" \
"AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA\n" \
"A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI\n" \
"U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs\n" \
"N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv\n" \
"o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU\n" \
"5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy\n" \
"rqXRfboQnoZsG4q5WTP468SQvvG5\n" \
"-----END CERTIFICATE-----\n";

extern const char* DEVICE_CERT;
const char* device_cert = DEVICE_CERT;

extern const char* DEVICE_KEY;
const char* device_key = DEVICE_KEY;

void publishSensorData();
void connectToMqtt();

// ──────────────────────────────────────────────
//  Google Geolocation via WiFi scan
// ──────────────────────────────────────────────
bool getLocationFromWiFi(float &lat, float &lng) {
  Serial.println("[GEO] Scanning nearby WiFi networks...");

  int networkCount = WiFi.scanNetworks();
  if (networkCount <= 0) {
    Serial.println("[GEO] No networks found during scan.");
    return false;
  }

  Serial.printf("[GEO] Found %d networks\n", networkCount);

  int apCount = min(networkCount, 20);

  DynamicJsonDocument reqDoc(2048);
  JsonArray aps = reqDoc.createNestedArray("wifiAccessPoints");

  for (int i = 0; i < apCount; i++) {
    JsonObject ap = aps.createNestedObject();
    ap["macAddress"]    = WiFi.BSSIDstr(i);
    ap["signalStrength"] = WiFi.RSSI(i);
  }

  String requestBody;
  serializeJson(reqDoc, requestBody);

  WiFi.scanDelete();

  String url = String("https://www.googleapis.com/geolocation/v1/geolocate?key=") + GOOGLE_GEO_API_KEY;

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  Serial.println("[GEO] Posting to Google Geolocation API...");
  int httpCode = http.POST(requestBody);

  if (httpCode != 200) {
    Serial.printf("[GEO] HTTP error: %d\n", httpCode);
    Serial.println(http.getString());
    http.end();
    return false;
  }

  String response = http.getString();
  http.end();

  DynamicJsonDocument resDoc(512);
  DeserializationError err = deserializeJson(resDoc, response);
  if (err) {
    Serial.println("[GEO] JSON parse error");
    return false;
  }

  if (!resDoc.containsKey("location")) {
    Serial.println("[GEO] No location in response");
    return false;
  }

  lat = resDoc["location"]["lat"].as<float>();
  lng = resDoc["location"]["lng"].as<float>();
  return true;
}

// ──────────────────────────────────────────────
//  Setup
// ──────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("\n\n========================================");
  Serial.println("GreenGuard Wildfire Sensor");
  Serial.println("========================================");

  dht.begin();
  delay(2000);

  Serial.println("[SENSOR] Testing DHT11 sensor...");
  float testTemp = dht.readTemperature();
  float testHum  = dht.readHumidity();

  if (isnan(testTemp) || isnan(testHum)) {
    Serial.println("[ERROR] DHT11 sensor not responding!");
    Serial.println("        Troubleshooting:");
    Serial.println("        1. Check wiring: VCC->3.3V, GND->GND, DATA->GPIO15 (D15)");
    Serial.println("        2. Add 4.7k-10k pull-up: DATA to VCC");
    Serial.println("        3. Ensure sensor is powered");
    Serial.println("        Continuing anyway - sensor may work after warm-up...");
  } else {
    Serial.printf("[SENSOR] OK — test reading: %.1f C, %.1f %% RH\n", testTemp, testHum);
  }

  Serial.printf("\nConnecting to WiFi: %s\n", ssid);
  WiFi.begin(ssid, password);

  int wifi_attempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_attempts < 40) {
    delay(500);
    Serial.print(".");
    wifi_attempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi connection FAILED. Check SSID/password, then restart.");
    while (1) delay(1000);
  }

  Serial.printf("\nWiFi connected. IP: %s  RSSI: %d dBm\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI());

  Serial.println("\n[TIME] Syncing with NTP servers...");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  time_t now = time(nullptr);
  int time_attempts = 0;
  while (now < 1672531200 && time_attempts < 60) {
    delay(500);
    now = time(nullptr);
    if (time_attempts % 10 == 0) Serial.print(".");
    time_attempts++;
  }

  if (now < 1672531200) {
    Serial.println("\nTIME SYNC FAILED. TLS will fail. Check internet, then restart.");
    while (1) delay(1000);
  }

  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  char timeStr[64];
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S UTC", &timeinfo);
  Serial.printf("Time synced: %s\n", timeStr);

  Serial.println("\n[GEO] Getting device location...");
  if (getLocationFromWiFi(sensorLat, sensorLng)) {
    Serial.printf("Location acquired: %.6f, %.6f\n", sensorLat, sensorLng);
  } else {
    Serial.println("[GEO] ✗ Could not get location. Defaulting to 0.0, 0.0");
    Serial.println("[GEO] Could not get location. Defaulting to 0.0, 0.0");
    Serial.println("[GEO] Set GOOGLE_GEO_API_KEY in config.h if needed.");
    sensorLat = 0.0;
    sensorLng = 0.0;
  }

  // ── Generate UNIQUE device ID using MAC address ──────────────
  String mac = WiFi.macAddress();
  mac.replace(":", "");  // remove colons
  snprintf(deviceId, sizeof(deviceId), "esp32-%s", mac.c_str());
  snprintf(mqtt_topic, sizeof(mqtt_topic), "wildfire/sensors/%s", deviceId);
  snprintf(deviceId,   sizeof(deviceId),   "%s", DEVICE_ID);
  snprintf(mqtt_topic, sizeof(mqtt_topic),  "wildfire/sensors/%s", DEVICE_ID);

  Serial.printf("\n[CONFIG] Client ID: %s\n", deviceId);
  Serial.printf("[CONFIG] Topic:     %s\n", mqtt_topic);
  Serial.printf("[CONFIG] Endpoint:  %s\n", aws_iot_endpoint);

  Serial.println("\n[TLS] Configuring certificates...");
  net.setCACert(root_ca);
  net.setCertificate(device_cert);
  net.setPrivateKey(device_key);
  Serial.println("Certificates loaded");

  mqttClient.setServer(aws_iot_endpoint, aws_iot_port);
  /* 512 bytes: headroom for slightly larger JSON (e.g. extra fields) and MQTT overhead */
  mqttClient.setBufferSize(512);
  mqttClient.setKeepAlive(60);
  mqttClient.setSocketTimeout(15);

  Serial.println("\n[TEST] Testing TLS handshake...");
  if (net.connect(aws_iot_endpoint, aws_iot_port)) {
    Serial.println("TLS handshake successful.");
    net.stop();
    delay(1000);
  } else {
    Serial.println("TLS handshake FAILED. Check certificates in config.h.");
    while (1) delay(1000);
  }

  Serial.println("\n========================================");
  Serial.println("Starting MQTT connection...");
  Serial.println("========================================\n");

  connectToMqtt();
}

void loop() {
  if (!mqttClient.connected()) {
    Serial.println("\n[WARNING] MQTT disconnected!");
    connectToMqtt();
  }

  mqttClient.loop();

  unsigned long currentTime = millis();
  if (currentTime - lastSendTime >= sendInterval) {
    lastSendTime = currentTime;
    publishSensorData();
  }
}

void connectToMqtt() {
  int retries = 0;
  const int MAX_RETRIES = 5;

  while (!mqttClient.connected() && retries < MAX_RETRIES) {
    Serial.printf("\n[MQTT] Connection attempt %d/%d\n", retries + 1, MAX_RETRIES);
    Serial.print("       Endpoint: ");
    Serial.println(aws_iot_endpoint);
    Serial.print("       Client ID: ");
    Serial.println(deviceId);

    if (mqttClient.connect(deviceId)) {
      Serial.println("\nCONNECTED TO AWS IOT CORE");
      Serial.println("[INFO] Publishing every 30 seconds\n");
      return;
    }

    int state = mqttClient.state();
    Serial.printf("Connection FAILED - state: %d\n", state);

    switch (state) {
      case -4: Serial.println("  TIMEOUT - time sync, policy, endpoint"); break;
      case -3: Serial.println("  CONNECTION_LOST"); break;
      case -2: Serial.println("  CONNECT_FAILED - network / endpoint"); break;
      case -1: Serial.println("  DISCONNECTED"); break;
      case  1: Serial.println("  BAD_PROTOCOL"); break;
      case  2: Serial.println("  BAD_CLIENT_ID"); break;
      case  3: Serial.println("  UNAVAILABLE"); break;
      case  4: Serial.println("  BAD_CREDENTIALS - cert/key mismatch"); break;
      case  5: Serial.println("  UNAUTHORIZED - attach policy to certificate"); break;
      default: Serial.println("  Unknown"); break;
    }

    retries++;
    if (retries < MAX_RETRIES) {
      Serial.println("  Retrying in 5 seconds...");
      delay(5000);
    }
  }

  if (!mqttClient.connected()) {
    Serial.println("\nFAILED TO CONNECT after all retries.");
    Serial.println("Check: policy attached, certificate active, cert matches key.");
    Serial.println("Retry in 30 seconds...\n");
    delay(30000);
  }
}

void publishSensorData() {
  float temperature = NAN;
  float humidity    = NAN;
  int retries = 0;
  const int maxRetries = 3;

  while ((isnan(temperature) || isnan(humidity)) && retries < maxRetries) {
    delay(2000);
    temperature = dht.readTemperature();
    humidity    = dht.readHumidity();
    retries++;
    if (isnan(temperature) || isnan(humidity)) {
      Serial.printf("[SENSOR] Read attempt %d/%d failed, retrying...\n", retries, maxRetries);
    }
  }

  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("\n[ERROR] DHT11 read failed after retries.");
    Serial.println("  Check DATA on GPIO15, pull-up, power.");
    return;
  }

  temperature = round(temperature * 10.0) / 10.0;
  humidity    = round(humidity    * 10.0) / 10.0;

  time_t now = time(nullptr);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  char timestamp[25];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);

  StaticJsonDocument<256> doc;
  doc["deviceId"]    = deviceId;
  doc["temperature"] = temperature;
  doc["humidity"]    = humidity;
  doc["lat"]         = sensorLat;
  doc["lng"]         = sensorLng;
  doc["timestamp"]   = timestamp;

  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer);

  Serial.println("\n--- Publish ---");
  Serial.printf("Temperature: %.1f C  Humidity: %.1f %%\n", temperature, humidity);
  Serial.printf("Location:    %.4f, %.4f\n", sensorLat, sensorLng);
  Serial.printf("[PUBLISH] %s\n", jsonBuffer);

  bool published = mqttClient.publish(mqtt_topic, jsonBuffer, false);
  if (published) {
    Serial.println("Published OK.\n");
  } else {
    Serial.printf("Publish FAILED. MQTT state: %d\n", mqttClient.state());
  }
}
