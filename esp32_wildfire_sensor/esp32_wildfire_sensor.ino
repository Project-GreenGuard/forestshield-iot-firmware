/*
 * GreenGuard Wildfire Sensor - ESP32 + DHT11
 * Working version - matches successful Python connection
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
#define DHTPIN 15
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

// Fixed client ID
char deviceId[64];
char mqtt_topic[128];

// Timing
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 30000;

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

  // Build JSON payload
  // Each AP entry: {"macAddress":"xx:xx:xx:xx:xx:xx","signalStrength":-70}
  // Google allows up to ~25 APs; we cap at 20 to stay within ArduinoJson limits
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

  // Free scan results
  WiFi.scanDelete();

  // POST to Google Geolocation API
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
    Serial.println("[ERROR] DHT11 sensor not responding! Continuing anyway...");
  } else {
    Serial.printf("[SENSOR] ✓ Sensor working! Test reading: %.1f°C, %.1f%% RH\n", testTemp, testHum);
  }

  // Connect to WiFi
  Serial.printf("\nConnecting to WiFi: %s\n", ssid);
  WiFi.begin(ssid, password);

  int wifi_attempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_attempts < 40) {
    delay(500);
    Serial.print(".");
    wifi_attempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n✗ WiFi connection FAILED! Restarting...");
    while (1) delay(1000);
  }

  Serial.printf("\n✓ WiFi connected! IP: %s  RSSI: %d dBm\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI());

  // Time sync (required for TLS)
  Serial.println("\n[TIME] Syncing with NTP servers...");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  time_t now = time(nullptr);
  int time_attempts = 0;
  while (now < 1672531200 && time_attempts < 60) {
    delay(500);
    now = time(nullptr);
    time_attempts++;
    if (time_attempts % 10 == 0) Serial.print(".");
  }

  if (now < 1672531200) {
    Serial.println("\n✗ TIME SYNC FAILED! Check internet connection.");
    while (1) delay(1000);
  }

  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  char timeStr[64];
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S UTC", &timeinfo);
  Serial.printf("✓ Time synced: %s\n", timeStr);

  // ── Get location once at startup ──────────────
  Serial.println("\n[GEO] Getting device location...");
  if (getLocationFromWiFi(sensorLat, sensorLng)) {
    Serial.printf("✓ Location acquired: %.6f, %.6f\n", sensorLat, sensorLng);
  } else {
    Serial.println("[GEO] ✗ Could not get location. Defaulting to 0.0, 0.0");
    Serial.println("[GEO]   Add GOOGLE_GEO_API_KEY to config.h if you haven't.");
    sensorLat = 0.0;
    sensorLng = 0.0;
  }

  // MQTT config
  snprintf(deviceId,   sizeof(deviceId),   "%s", DEVICE_ID);
  snprintf(mqtt_topic, sizeof(mqtt_topic),  "wildfire/sensors/%s", DEVICE_ID);

  Serial.printf("\n[CONFIG] Client ID: %s\n", deviceId);
  Serial.printf("[CONFIG] Topic:     %s\n", mqtt_topic);
  Serial.printf("[CONFIG] Endpoint:  %s\n", aws_iot_endpoint);

  // TLS
  Serial.println("\n[TLS] Configuring certificates...");
  net.setCACert(root_ca);
  net.setCertificate(device_cert);
  net.setPrivateKey(device_key);
  Serial.println("✓ Certificates loaded");

  mqttClient.setServer(aws_iot_endpoint, aws_iot_port);
  mqttClient.setBufferSize(256);
  mqttClient.setKeepAlive(60);
  mqttClient.setSocketTimeout(15);

  // Test TLS
  Serial.println("\n[TEST] Testing TLS handshake...");
  if (net.connect(aws_iot_endpoint, aws_iot_port)) {
    Serial.println("✓ TLS handshake successful!");
    net.stop();
    delay(1000);
  } else {
    Serial.println("✗ TLS handshake FAILED! Check certificates.");
    while (1) delay(1000);
  }

  Serial.println("\n========================================");
  Serial.println("Starting MQTT connection...");
  Serial.println("========================================\n");

  connectToMqtt();
}

// ──────────────────────────────────────────────
//  Loop
// ──────────────────────────────────────────────
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

// ──────────────────────────────────────────────
//  MQTT Connect
// ──────────────────────────────────────────────
void connectToMqtt() {
  int retries = 0;
  const int MAX_RETRIES = 5;

  while (!mqttClient.connected() && retries < MAX_RETRIES) {
    Serial.printf("\n[MQTT] Connection attempt %d/%d\n", retries + 1, MAX_RETRIES);

    if (mqttClient.connect(deviceId)) {
      Serial.println("\n╔════════════════════════════════════════╗");
      Serial.println("║   ✓ CONNECTED TO AWS IOT CORE!        ║");
      Serial.println("╚════════════════════════════════════════╝\n");
      return;
    }

    int state = mqttClient.state();
    Serial.printf("✗ Connection FAILED - Error code: %d\n", state);

    switch (state) {
      case -4: Serial.println("  MQTT_CONNECTION_TIMEOUT → Check time sync, policy, endpoint"); break;
      case -3: Serial.println("  MQTT_CONNECTION_LOST"); break;
      case -2: Serial.println("  MQTT_CONNECT_FAILED → Network issue or wrong endpoint"); break;
      case -1: Serial.println("  MQTT_DISCONNECTED"); break;
      case  1: Serial.println("  MQTT_CONNECT_BAD_PROTOCOL"); break;
      case  2: Serial.println("  MQTT_CONNECT_BAD_CLIENT_ID"); break;
      case  3: Serial.println("  MQTT_CONNECT_UNAVAILABLE"); break;
      case  4: Serial.println("  MQTT_CONNECT_BAD_CREDENTIALS → Check cert/key match"); break;
      case  5: Serial.println("  MQTT_CONNECT_UNAUTHORIZED → Check policy is attached!"); break;
      default: Serial.println("  Unknown error"); break;
    }

    retries++;
    if (retries < MAX_RETRIES) {
      Serial.println("  Retrying in 5 seconds...");
      delay(5000);
    }
  }

  if (!mqttClient.connected()) {
    Serial.println("\n✗✗✗ FAILED TO CONNECT AFTER ALL RETRIES ✗✗✗");
    Serial.println("Will retry in 30 seconds...\n");
    delay(30000);
  }
}

// ──────────────────────────────────────────────
//  Publish sensor data
// ──────────────────────────────────────────────
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
    Serial.println("\n[ERROR] Failed to read from DHT11 sensor after retries!");
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
  doc["deviceId"]    = DEVICE_ID;
  doc["temperature"] = temperature;
  doc["humidity"]    = humidity;
  doc["lat"]         = sensorLat;
  doc["lng"]         = sensorLng;
  doc["timestamp"]   = timestamp;

  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer);

  Serial.println("\n┌─────────────────────────────────────┐");
  Serial.printf( "│ Temperature: %5.1f°C               │\n", temperature);
  Serial.printf( "│ Humidity:    %5.1f%%                │\n", humidity);
  Serial.printf( "│ Location:    %.4f, %.4f     │\n", sensorLat, sensorLng);
  Serial.println("└─────────────────────────────────────┘");
  Serial.printf("[PUBLISH] %s\n", jsonBuffer);

  bool published = mqttClient.publish(mqtt_topic, jsonBuffer, false);
  if (published) {
    Serial.println("✓ Published successfully to AWS IoT Core!\n");
  } else {
    Serial.printf("✗ Publish FAILED! MQTT State: %d\n", mqttClient.state());
  }
}