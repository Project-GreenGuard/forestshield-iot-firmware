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
- **AsyncMQTTClient** by Marvin ROGER (v0.9.0+)
- **AsyncTCP** by dvarrel (ESP32 version, dependency for AsyncMQTTClient)

### 2. Configure Firmware

**Create configuration file:**

```bash
cp config.h.example config.h
```

Edit `config.h` with your values:

1. **WiFi Credentials**

   ```cpp
   const char* WIFI_SSID = "YOUR_WIFI_SSID";
   const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
   ```

2. **AWS IoT Core**

   - Get endpoint from `forestshield-infrastructure` Terraform output
   - Update `AWS_IOT_ENDPOINT` with your IoT Core endpoint

3. **Device Information**
   ```cpp
   const char* DEVICE_ID = "esp32-01";  // Must match IoT Thing name
   const float SENSOR_LAT = 43.467;     // Your GPS coordinates
   const float SENSOR_LNG = -79.699;
   ```

**Note:** `config.h` is in `.gitignore` and will not be committed. Share the IoT endpoint with team members separately.

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

The sensor sends the following JSON payload to AWS IoT Core:

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

### Data Fields

- **deviceId**: Unique identifier for the sensor (matches IoT Thing name)
- **temperature**: Temperature reading from DHT11 sensor (°C, rounded to 1 decimal)
- **humidity**: Humidity reading from DHT11 sensor (%, rounded to 1 decimal)
- **lat**: Sensor latitude coordinate (from `config.h`)
- **lng**: Sensor longitude coordinate (from `config.h`)
- **timestamp**: UTC timestamp in ISO 8601 format

### Data Processing

After the sensor sends this data, the backend Lambda function (`wildfire-process-sensor-data`) enriches it by:

1. Fetching active wildfire data from NASA FIRMS API
2. Calculating distance to nearest fire
3. Computing risk score based on temperature, humidity, and fire proximity
4. Storing enriched data in DynamoDB

The enriched data stored in DynamoDB includes:

- All original sensor data
- `riskScore`: Calculated risk (0-100)
- `nearestFireDistance`: Distance to nearest fire in km
- `nearestFireData`: JSON data about the nearest fire

## Related Repositories

- **forestshield-backend** - Lambda functions that process this data
- **forestshield-infrastructure** - AWS IoT Core setup
- **forestshield-frontend** - Dashboard visualization

## License

See LICENSE file
