/*
 * GreenGuard Wildfire Sensor - ESP32 + DHT11
 * 
 * This firmware reads temperature and humidity from DHT11 sensor
 * and publishes data to AWS IoT Core via MQTT over TLS.
 * 
 * Hardware:
 * - ESP32 Dev Module
 * - DHT11 sensor
 * 
 * Wiring:
 * - DHT11 VCC -> ESP32 3V3
 * - DHT11 DATA -> ESP32 GPIO14
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
 * 3. Update certificates and endpoint in config.h
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <AsyncMqttClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <time.h>
#include "config.h"  // Configuration file (not committed to git)

// DHT11 Sensor Configuration
#define DHTPIN 14         // GPIO pin connected to DHT11 DATA pin
#define DHTTYPE DHT11     // DHT11 sensor type
DHT dht(DHTPIN, DHTTYPE);

// Use values from config.h
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;
const char* aws_iot_endpoint = AWS_IOT_ENDPOINT;
const int aws_iot_port = 8883;
const char* deviceId = DEVICE_ID;
const float sensorLat = SENSOR_LAT;
const float sensorLng = SENSOR_LNG;

// MQTT Topic (uses deviceId from config.h)
char mqtt_topic[50];

// Timing
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 30000; // 30 seconds

// WiFi and MQTT clients
WiFiClientSecure net;
AsyncMqttClient mqttClient;

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

// Device Certificate (loaded from config.h)
extern const char* DEVICE_CERT;
const char* device_cert = DEVICE_CERT;

// Device Private Key (loaded from config.h)
extern const char* DEVICE_KEY;
const char* device_key = DEVICE_KEY;

// Forward declarations
void publishSensorData();
void connectToMqtt();

void onMqttConnect(bool sessionPresent) {
  Serial.println("\n✓ Connected to AWS IoT Core!");
  Serial.print("Session present: ");
  Serial.println(sessionPresent);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.print("\n✗ Disconnected from AWS IoT Core. Reason: ");
  Serial.println((uint8_t)reason);
  
  if (WiFi.isConnected()) {
    Serial.println("Reconnecting to MQTT...");
    connectToMqtt();
  }
}

void onMqttPublish(uint16_t packetId) {
  Serial.println(" - ✓ Published to AWS IoT");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== GreenGuard Wildfire Sensor ===");
  Serial.println("Initializing...");
  
  // Initialize DHT sensor
  dht.begin();
  Serial.println("✓ DHT11 sensor initialized");
  
  // Connect to WiFi
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\n✓ WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  // Configure time (required for AWS IoT Core)
  configTime(0, 0, "pool.ntp.org");
  Serial.print("Waiting for time sync...");
  time_t now = time(nullptr);
  int timeout = 0;
  while (now < 1000000000 && timeout < 20) {
    delay(500);
    now = time(nullptr);
    timeout++;
    Serial.print(".");
  }
  Serial.println();
  if (now < 1000000000) {
    Serial.println("✗ Time sync failed - TLS will fail");
  } else {
    Serial.print("✓ Time configured: ");
    Serial.println(now);
  }
  
  // Build MQTT topic from device ID
  snprintf(mqtt_topic, sizeof(mqtt_topic), "wildfire/sensors/%s", deviceId);
  
  // Configure AWS IoT Core connection
  Serial.println("Configuring TLS certificates...");
  net.setCACert(root_ca);
  net.setCertificate(device_cert);
  net.setPrivateKey(device_key);
  net.setTimeout(30);
  Serial.println("✓ Certificates configured");
  
  // Configure AsyncMQTTClient
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onPublish(onMqttPublish);
  mqttClient.setServer(aws_iot_endpoint, aws_iot_port);
  mqttClient.setClientId(deviceId);
  mqttClient.setKeepAlive(30);
  mqttClient.setCleanSession(true);
  mqttClient.setSecure(true);
  mqttClient.setSecureClient(&net);
  
  Serial.print("Connecting to AWS IoT Core: ");
  Serial.println(aws_iot_endpoint);
  Serial.print("Port: ");
  Serial.println(aws_iot_port);
  Serial.print("Client ID: ");
  Serial.println(deviceId);
  
  // Connect to MQTT
  connectToMqtt();
  
  Serial.println("=== Sensor Ready ===");
  Serial.print("Device ID: ");
  Serial.println(deviceId);
  Serial.print("Location: ");
  Serial.print(sensorLat, 6);
  Serial.print(", ");
  Serial.println(sensorLng, 6);
  Serial.print("Publishing to: ");
  Serial.println(mqtt_topic);
  Serial.println("Reading sensor every 30 seconds...\n");
}

void loop() {
  // AsyncMQTTClient handles connection in background
  // Just publish sensor data at intervals
  unsigned long currentTime = millis();
  
  if (currentTime - lastSendTime >= sendInterval) {
    lastSendTime = currentTime;
    publishSensorData();
  }
  
  delay(100); // Small delay to prevent watchdog issues
}

void connectToMqtt() {
  Serial.print("Connecting to MQTT...");
  mqttClient.connect();
}

void publishSensorData() {
  // Read temperature and humidity
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  
  // Check if readings are valid
  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("✗ Failed to read from DHT11 sensor!");
    return;
  }
  
  // Round to 1 decimal place
  temperature = round(temperature * 10.0) / 10.0;
  humidity = round(humidity * 10.0) / 10.0;
  
  // Always print sensor readings to Serial (for debugging)
  Serial.print("[SENSOR] ");
  Serial.print(deviceId);
  Serial.print(" - Temperature: ");
  Serial.print(temperature, 1);
  Serial.print("°C, Humidity: ");
  Serial.print(humidity, 1);
  Serial.print("% RH");
  
  // Get current timestamp
  time_t now = time(nullptr);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  char timestamp[25];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  
  // Create JSON payload
  StaticJsonDocument<300> doc;
  doc["deviceId"] = deviceId;
  doc["temperature"] = temperature;
  doc["humidity"] = humidity;
  doc["lat"] = sensorLat;
  doc["lng"] = sensorLng;
  doc["timestamp"] = timestamp;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  // Publish to MQTT (only if connected)
  if (mqttClient.connected()) {
    uint16_t packetId = mqttClient.publish(mqtt_topic, 0, false, jsonString.c_str());
    if (packetId == 0) {
      Serial.println(" - ✗ MQTT publish failed");
    }
    // onMqttPublish callback will print success message
  } else {
    Serial.println(" - ✗ Not connected to AWS IoT (MQTT connection failed)");
    // Try to reconnect
    connectToMqtt();
  }
}
