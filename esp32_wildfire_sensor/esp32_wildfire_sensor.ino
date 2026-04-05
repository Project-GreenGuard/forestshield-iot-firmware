/*
 * GreenGuard Wildfire Sensor - ESP32 + DHT11
 * WITH Photo Simulation Feature
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

// Location
float sensorLat = 0.0;
float sensorLng = 0.0;

// Unique client ID and topic
char deviceId[64];
char mqtt_topic[128];

// Timing
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 30000;

// ── Photo Simulation ──────────────────────────
const char* photoUrls[] = {
  "https://cdn.britannica.com/42/188142-050-4D4D9D19/wildfire-Stanislaus-National-Forest-California-2013.jpg",
  "https://www.metroparks.net/wp-content/uploads/2017/06/1080p_HBK_autumn-morning_GI-1024x686.jpg"
};

const int numPhotos = sizeof(photoUrls) / sizeof(photoUrls[0]);

unsigned long lastPhotoTime = 0;
const unsigned long photoCooldown = 10 * 60 * 1000; // 10 minutes

// WiFi and MQTT clients
WiFiClientSecure net;
PubSubClient mqttClient(net);

// Root CA
const char* root_ca = \ "-----BEGIN CERTIFICATE-----\n" \ "MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF\n" \ "ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6\n" \ "b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL\n" \ "MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv\n" \ "b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj\n" \ "ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM\n" \ "9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw\n" \ "IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6\n" \ "VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L\n" \ "93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm\n" \ "jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC\n" \ "AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA\n" \ "A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI\n" \ "U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs\n" \ "N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv\n" \ "o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU\n" \ "5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy\n" \ "rqXRfboQnoZsG4q5WTP468SQvvG5\n" \ "-----END CERTIFICATE-----\n";

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
  int networkCount = WiFi.scanNetworks();
  if (networkCount <= 0) return false;

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

  int httpCode = http.POST(requestBody);
  if (httpCode != 200) {
    http.end();
    return false;
  }

  String response = http.getString();
  http.end();

  DynamicJsonDocument resDoc(512);
  if (deserializeJson(resDoc, response)) return false;

  if (!resDoc.containsKey("location")) return false;

  lat = resDoc["location"]["lat"];
  lng = resDoc["location"]["lng"];
  return true;
}

// ──────────────────────────────────────────────
//  Setup
// ──────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(2000);

  dht.begin();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  time_t now = time(nullptr);
  while (now < 1672531200) {
    delay(500);
    now = time(nullptr);
  }

  getLocationFromWiFi(sensorLat, sensorLng);

  String mac = WiFi.macAddress();
  mac.replace(":", "");
  snprintf(deviceId, sizeof(deviceId), "esp32-%s", mac.c_str());
  snprintf(mqtt_topic, sizeof(mqtt_topic), "wildfire/sensors/%s", deviceId);

  net.setCACert(root_ca);
  net.setCertificate(device_cert);
  net.setPrivateKey(device_key);

  mqttClient.setServer(aws_iot_endpoint, aws_iot_port);

  connectToMqtt();

  // Seed random for photo selection
  randomSeed(micros());
}

// ──────────────────────────────────────────────
//  Loop
// ──────────────────────────────────────────────
void loop() {
  if (!mqttClient.connected()) {
    connectToMqtt();
  }

  mqttClient.loop();

  if (millis() - lastSendTime >= sendInterval) {
    lastSendTime = millis();
    publishSensorData();
  }
}

// ──────────────────────────────────────────────
//  MQTT Connect
// ──────────────────────────────────────────────
void connectToMqtt() {
  while (!mqttClient.connected()) {
    mqttClient.connect(deviceId);
    delay(2000);
  }
}

// ──────────────────────────────────────────────
//  Publish sensor data
// ──────────────────────────────────────────────
void publishSensorData() {
  float temperature = dht.readTemperature();
  float humidity    = dht.readHumidity();

  if (isnan(temperature) || isnan(humidity)) return;

  time_t now = time(nullptr);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);

  char timestamp[25];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);

  // ── Photo trigger logic ───────────────────────
  bool sendPhoto = false;
  String selectedPhoto = "";

  if (temperature > 35.0) {
    unsigned long currentTime = millis();

    if (currentTime - lastPhotoTime >= photoCooldown) {
      int index = random(0, numPhotos);
      selectedPhoto = String(photoUrls[index]);

      sendPhoto = true;
      lastPhotoTime = currentTime;

      Serial.println("[ALERT] High temp! Photo triggered 📸");
    }
  }

  StaticJsonDocument<384> doc;

  doc["deviceId"]    = deviceId;
  doc["temperature"] = temperature;
  doc["humidity"]    = humidity;
  doc["lat"]         = sensorLat;
  doc["lng"]         = sensorLng;
  doc["timestamp"]   = timestamp;

  if (sendPhoto) {
    doc["photo_url"] = selectedPhoto;
    doc["alert"] = true;
  }

  char jsonBuffer[384];
  serializeJson(doc, jsonBuffer);

  mqttClient.publish(mqtt_topic, jsonBuffer);
}