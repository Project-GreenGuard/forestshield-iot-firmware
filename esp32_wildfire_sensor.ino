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
 * - Install WiFiClientSecure library (built-in)
 * - Install PubSubClient library
 * 
 * AWS IoT Core Setup:
 * 1. Create IoT Thing in AWS IoT Core
 * 2. Download device certificate, private key, and root CA
 * 3. Update certificates and endpoint below
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
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

// MQTT Topic
const char* mqtt_topic = "wildfire/sensors/esp32-01";

// Timing
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 30000; // 30 seconds (adjust as needed)

// WiFi and MQTT clients
WiFiClientSecure net;
PubSubClient client(net);

// AWS IoT Core Root CA Certificate
// Download from: https://www.amazontrust.com/repository/AmazonRootCA1.pem
const char* root_ca = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF\n" \
"ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6\n" \
"b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL\n" \
"MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAwwQQW1hem9uIFJv\n" \
"b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj\n" \
"ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBmk3mVk0ue8Pp\n" \
"y+Fwo9TZ6F3M4zYynsg6NljsIkY7xICIImHj0uVjx3Q3iYcYz6RYrAMhsnjlsl1a\n" \
"xY4Ix9ukY6umzn3xzk9pP6jUzn6wiTb1YRkcsnH5XVlbFcofenf9T3RjS4asAV8P\n" \
"EXODw0TbiGECYx2VapGmpwsuYm8zt3VM2Saz3JlcXuRFx8iYdC8oVhE2vN/Jewww6\n" \
"b62u1K91S0s9G2weQ+w5n0ullN/KtB8c6xU5lbK4Rj3BTJKnLq85BxEe5P5aHX9Y\n" \
"jJxChXW2Esh5XvA0G0H3rY1xY8j2L4L5S5r5y5r5y5r5y5r5y5r5y5r5y5r5y5r5\n" \
"y5r5y5r5y5r5y5r5y5r5y5r5y5r5y5r5y5r5y5r5y5r5y5r5y5r5y5r5y5r5y5r5\n" \
"y5r5y5r5y5r5y5r5y5r5y5r5y5r5y5r5y5r5y5r5y5r5y5r5y5r5y5r5y5r5y5r5\n" \
"-----END CERTIFICATE-----\n";

// Device Certificate (replace with your certificate from AWS IoT Core)
const char* device_cert = \
"-----BEGIN CERTIFICATE-----\n" \
"REPLACE_WITH_YOUR_DEVICE_CERTIFICATE\n" \
"-----END CERTIFICATE-----\n";

// Device Private Key (replace with your private key from AWS IoT Core)
const char* device_key = \
"-----BEGIN RSA PRIVATE KEY-----\n" \
"REPLACE_WITH_YOUR_PRIVATE_KEY\n" \
"-----END RSA PRIVATE KEY-----\n";

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
  Serial.println("✓ Time configured");
  
  // Configure AWS IoT Core connection
  net.setCACert(root_ca);
  net.setCertificate(device_cert);
  net.setPrivateKey(device_key);
  
  client.setServer(aws_iot_endpoint, aws_iot_port);
  client.setCallback(mqttCallback);
  
  Serial.print("Connecting to AWS IoT Core: ");
  Serial.println(aws_iot_endpoint);
  
  // Connect to MQTT broker
  while (!client.connected()) {
    if (client.connect(deviceId)) {
      Serial.println("✓ Connected to AWS IoT Core");
    } else {
      Serial.print("✗ Connection failed, rc=");
      Serial.print(client.state());
      Serial.println(" Retrying in 5 seconds...");
      delay(5000);
    }
  }
  
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
  // Maintain MQTT connection
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  
  // Send sensor data at intervals
  unsigned long currentTime = millis();
  if (currentTime - lastSendTime >= sendInterval) {
    lastSendTime = currentTime;
    publishSensorData();
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect(deviceId)) {
      Serial.println("✓ Connected");
    } else {
      Serial.print("✗ Failed, rc=");
      Serial.print(client.state());
      Serial.println(" Retrying in 5 seconds...");
      delay(5000);
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Handle incoming MQTT messages (if needed)
  Serial.print("Message received on topic: ");
  Serial.println(topic);
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
  
  // Publish to MQTT
  if (client.publish(mqtt_topic, jsonString.c_str())) {
    Serial.print("[");
    Serial.print(deviceId);
    Serial.print("] ");
    Serial.print(temperature, 1);
    Serial.print("°C, ");
    Serial.print(humidity, 1);
    Serial.print("% RH - ✓ Published");
    Serial.println();
  } else {
    Serial.println("✗ Failed to publish");
  }
}
