# Device operations (ESP32 wildfire sensor)

## Provisioning checklist

1. **AWS IoT Core:** Create a **Thing** (or reuse team naming like `esp32-wildfire-sensor-N`), attach an **X.509 certificate**, activate it, and attach **WildfireSensorPolicy** (or equivalent) so the device can connect and publish to `wildfire/sensors/*`.
2. **Secrets on device:** Copy **device certificate**, **private key**, and **Amazon Root CA 1** into `config.h` (use `config.h.example` as a template). Never commit real keys.
3. **Network:** Set **WIFI_SSID** / **WIFI_PASSWORD**. For geolocation, set **GOOGLE_GEO_API_KEY** or accept `0,0` lat/lng until configured.
4. **Identity:** **DEVICE_ID** must match the MQTT topic segment: `wildfire/sensors/<DEVICE_ID>`.

## Runtime behavior

- Publishes every **30 seconds** after a successful DHT read.
- **MQTT buffer** is set to **512 bytes** to reduce risk of oversized payload failures if JSON grows.
- On **publish failure**, the firmware logs and retries on the next interval; there is **no on-device queue** for offline storage (add SPIFFS/LittleFS ring buffer only if course scope requires it).

## OTA and field updates

- **Arduino IDE / `arduino-cli`:** Flash over USB as usual. For OTA, add an OTA partition and library only if you explicitly need over-the-air updates; default sketch is USB-flash.

## Operations / debugging

- **Serial monitor (115200):** Watch `[MQTT]`, `[PUBLISH]`, and TLS errors. Code **5 UNAUTHORIZED** usually means policy not attached to the certificate.
- **AWS:** If the pipeline drops messages, check **SQS DLQ** `wildfire-sensor-pipeline-dlq` for failed Lambda or **IoT rule error action** payloads.

## Buffering note

For intermittent Wi-Fi, a small **RAM ring buffer** of JSON strings could be added before `publish()`; current firmware prioritizes simplicity and matches the capstone “light device ops” scope.
