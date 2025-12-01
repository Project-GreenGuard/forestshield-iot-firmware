# ForestShield IoT Firmware

ESP32 firmware for wildfire sensor devices using DHT11 temperature/humidity sensors.

## Overview

This repository contains the Arduino firmware for ESP32 devices that collect environmental data and transmit it to AWS IoT Core via MQTT over TLS.

## Hardware Requirements

- ESP32 Dev Module (USB-C)
- DHT11 temperature/humidity sensor

## Wiring

- DHT11 VCC → ESP32 3V3
- DHT11 DATA → ESP32 GPIO14
- DHT11 GND → ESP32 GND

## Setup

### 1. Install Arduino Libraries

Required libraries (install via Arduino Library Manager):
- **ArduinoJson** by Benoit Blanchon (v6.x+)
- **DHT sensor library** by Adafruit
- **Adafruit Unified Sensor** (dependency)
- **PubSubClient** by Nick O'Leary (v2.8.x+)

### 2. Configure Firmware

Edit `esp32_wildfire_sensor.ino`:

1. **WiFi Credentials**
   ```cpp
   const char* ssid = "YOUR_WIFI_SSID";
   const char* password = "YOUR_WIFI_PASSWORD";
   ```

2. **AWS IoT Core**
   - Get endpoint from `forestshield-infrastructure` Terraform output
   - Download device certificate and private key from AWS IoT Core
   - Update `aws_iot_endpoint`, `device_cert`, and `device_key`

3. **Device Information**
   ```cpp
   const char* deviceId = "esp32-01";  // Must match IoT Thing name
   const float sensorLat = 43.467;     // Your GPS coordinates
   const float sensorLng = -79.699;
   ```

### 3. Upload to ESP32

1. Select board: **Tools → Board → ESP32 Dev Module**
2. Select port: **Tools → Port → [Your ESP32 Port]**
3. Upload

## Testing

Monitor serial output at 115200 baud to verify:
- WiFi connection
- AWS IoT Core connection
- Sensor readings
- MQTT publishing

## Mock Sensor

For testing without hardware:
```bash
python mock_sensor.py
```

## MQTT Topic

Publishes to: `wildfire/sensors/{deviceId}`

## Payload Format

```json
{
  "deviceId": "esp32-01",
  "temperature": 23.4,
  "humidity": 40.2,
  "lat": 43.467,
  "lng": -79.699,
  "timestamp": "2025-12-01T16:20:00Z"
}
```

## Related Repositories

- **forestshield-backend** - Lambda functions that process this data
- **forestshield-infrastructure** - AWS IoT Core setup
- **forestshield-frontend** - Dashboard visualization

## License

See LICENSE file
