/*
 * GreenGuard Wildfire Sensor - ESP32 + DHT11
 * Working version - matches successful Python connection
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <time.h>
#include "config.h"

// DHT11 Sensor Configuration
#define DHTPIN 14
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// Use values from config.h
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;
const char* aws_iot_endpoint = AWS_IOT_ENDPOINT;
const int aws_iot_port = 8883;

// Sensor & Device Info
const float sensorLat = SENSOR_LAT;
const float sensorLng = SENSOR_LNG;

// Fixed client ID
char deviceId[64];
char mqtt_topic[128];

// Timing
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 30000;  // 30 seconds

// WiFi and MQTT clients
WiFiClientSecure net;
PubSubClient mqttClient(net);

// AWS IoT Core Root CA Certificate
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

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n\n========================================");
  Serial.println("GreenGuard Wildfire Sensor - FIXED");
  Serial.println("========================================");
  
  // Initialize DHT sensor
  dht.begin();
  Serial.println("✓ DHT11 sensor initialized");
  
  // Connect to WiFi
  Serial.print("\nConnecting to WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  int wifi_attempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_attempts < 40) {
    delay(500);
    Serial.print(".");
    wifi_attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n✗ WiFi connection FAILED!");
    Serial.println("Check SSID and password, then restart");
    while(1) delay(1000);
  }
  
  Serial.println("\n✓ WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Signal strength: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
  
  // CRITICAL: Time sync - required for TLS
  Serial.println("\n[TIME] Syncing with NTP servers...");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  
  time_t now = time(nullptr);
  int time_attempts = 0;
  while (now < 1672531200 && time_attempts < 60) {  // Wait up to 30 seconds
    delay(500);
    now = time(nullptr);
    time_attempts++;
    if (time_attempts % 10 == 0) Serial.print(".");
  }
  Serial.println();
  
  if (now < 1672531200) {
    Serial.println("✗ TIME SYNC FAILED!");
    Serial.println("Without time sync, TLS will fail.");
    Serial.println("Check your internet connection and restart.");
    while(1) delay(1000);
  }
  
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  char timeStr[64];
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S UTC", &timeinfo);
  Serial.print("✓ Time synced: ");
  Serial.println(timeStr);
  
  // Use FIXED client ID (matching Python script)
  snprintf(deviceId, sizeof(deviceId), "%s", DEVICE_ID);
  Serial.print("\n[CONFIG] Client ID: ");
  Serial.println(deviceId);
  
  // Build MQTT topic
  snprintf(mqtt_topic, sizeof(mqtt_topic), "wildfire/sensors/%s", DEVICE_ID);
  Serial.print("[CONFIG] Topic: ");
  Serial.println(mqtt_topic);
  Serial.print("[CONFIG] Endpoint: ");
  Serial.println(aws_iot_endpoint);
  
  // Configure TLS certificates
  Serial.println("\n[TLS] Configuring certificates...");
  net.setCACert(root_ca);
  net.setCertificate(device_cert);
  net.setPrivateKey(device_key);
  Serial.println("✓ Certificates loaded");
  
  // Configure MQTT Client - CRITICAL SETTINGS
  mqttClient.setServer(aws_iot_endpoint, aws_iot_port);
  mqttClient.setBufferSize(256);      // Smaller buffer (was 512 or 4096)
  mqttClient.setKeepAlive(60);        // 60 second keepalive
  mqttClient.setSocketTimeout(15);    // 15 second timeout
  
  Serial.println("✓ MQTT client configured");
  
  // Test TLS connection first
  Serial.println("\n[TEST] Testing TLS handshake...");
  if (net.connect(aws_iot_endpoint, aws_iot_port)) {
    Serial.println("✓ TLS handshake successful!");
    Serial.println("✓ Certificates are valid!");
    net.stop();
    delay(1000);
  } else {
    Serial.println("✗ TLS handshake FAILED!");
    Serial.println("Check certificates in config.h");
    while(1) delay(1000);
  }
  
  Serial.println("\n========================================");
  Serial.println("Starting MQTT connection...");
  Serial.println("========================================\n");
  
  // Connect to MQTT
  connectToMqtt();
}

void loop() {
  // Reconnect if disconnected
  if (!mqttClient.connected()) {
    Serial.println("\n[WARNING] MQTT disconnected!");
    connectToMqtt();
  }
  
  // Process MQTT messages
  mqttClient.loop();

  // Publish sensor data every 30 seconds
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
    Serial.print("       Connecting to: ");
    Serial.println(aws_iot_endpoint);
    Serial.print("       Client ID: ");
    Serial.println(deviceId);
    
    // Attempt connection with clean session
    bool connected = mqttClient.connect(deviceId);
    
    if (connected) {
      Serial.println("\n╔════════════════════════════════════════╗");
      Serial.println("║   ✓ CONNECTED TO AWS IOT CORE!        ║");
      Serial.println("╚════════════════════════════════════════╝\n");
      Serial.println("[INFO] Sensor will publish every 30 seconds\n");
      return;
    } else {
      int state = mqttClient.state();
      Serial.print("✗ Connection FAILED - Error code: ");
      Serial.println(state);
      
      switch(state) {
        case -4:
          Serial.println("  Error: MQTT_CONNECTION_TIMEOUT");
          Serial.println("  → Check: Time sync, Policy attached, Endpoint");
          break;
        case -3:
          Serial.println("  Error: MQTT_CONNECTION_LOST");
          break;
        case -2:
          Serial.println("  Error: MQTT_CONNECT_FAILED");
          Serial.println("  → Network issue or wrong endpoint");
          break;
        case -1:
          Serial.println("  Error: MQTT_DISCONNECTED");
          break;
        case 1:
          Serial.println("  Error: MQTT_CONNECT_BAD_PROTOCOL");
          break;
        case 2:
          Serial.println("  Error: MQTT_CONNECT_BAD_CLIENT_ID");
          break;
        case 3:
          Serial.println("  Error: MQTT_CONNECT_UNAVAILABLE");
          break;
        case 4:
          Serial.println("  Error: MQTT_CONNECT_BAD_CREDENTIALS");
          Serial.println("  → Check certificate and key match");
          break;
        case 5:
          Serial.println("  Error: MQTT_CONNECT_UNAUTHORIZED");
          Serial.println("  → Check policy is attached to certificate!");
          break;
        default:
          Serial.println("  Unknown error");
      }
      
      retries++;
      if (retries < MAX_RETRIES) {
        Serial.println("\n  Retrying in 5 seconds...");
        delay(5000);
      }
    }
  }
  
  if (!mqttClient.connected()) {
    Serial.println("\n✗✗✗ FAILED TO CONNECT AFTER ALL RETRIES ✗✗✗");
    Serial.println("\nDid you:");
    Serial.println("  1. Attach policy to certificate in AWS IoT Console?");
    Serial.println("  2. Activate the certificate?");
    Serial.println("  3. Use matching certificate and private key?");
    Serial.println("\nWill retry in 30 seconds...\n");
    delay(30000);
  }
}

void publishSensorData() {
  // Read sensor
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  
  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("\n[ERROR] Failed to read from DHT11 sensor!");
    Serial.println("        Check sensor wiring:");
    Serial.println("        - VCC to 3.3V or 5V");
    Serial.println("        - GND to GND");
    Serial.println("        - DATA to GPIO 14");
    return;
  }
  
  // Round to 1 decimal
  temperature = round(temperature * 10.0) / 10.0;
  humidity = round(humidity * 10.0) / 10.0;
  
  // Get timestamp
  time_t now = time(nullptr);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  char timestamp[25];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  
  // Create JSON payload
  StaticJsonDocument<256> doc;  // Smaller doc size
  doc["deviceId"] = DEVICE_ID;
  doc["temperature"] = temperature;
  doc["humidity"] = humidity;
  doc["lat"] = sensorLat;
  doc["lng"] = sensorLng;
  doc["timestamp"] = timestamp;
  
  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer);
  
  // Print to serial
  Serial.println("\n┌─────────────────────────────────────┐");
  Serial.printf("│ Temperature: %5.1f°C               │\n", temperature);
  Serial.printf("│ Humidity:    %5.1f%%                │\n", humidity);
  Serial.println("└─────────────────────────────────────┘");
  Serial.print("[PUBLISH] ");
  Serial.println(jsonBuffer);
  
  // Publish to AWS IoT
  bool published = mqttClient.publish(mqtt_topic, jsonBuffer, false);
  
  if (published) {
    Serial.println("✓ Published successfully to AWS IoT Core!\n");
  } else {
    Serial.println("✗ Publish FAILED!");
    Serial.print("  MQTT State: ");
    Serial.println(mqttClient.state());
  }
}