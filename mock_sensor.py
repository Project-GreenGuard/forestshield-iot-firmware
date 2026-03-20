import requests
import time
import random

# API URL - Docker maps container port 5000 to host port 5001
API_URL = "http://localhost:5001/api/temperature"

# Sensor configuration (matches config.h)
DEVICE_ID = "esp32-wildfire-sensor-1"
SENSOR_LAT = 43.467
SENSOR_LNG = -79.699

def send_sensor_data():
    # Simulate sensor readings
    temp = round(random.uniform(15, 45), 1)
    humidity = round(random.uniform(20, 80), 1)
    
    payload = {
        'deviceId': DEVICE_ID,
        'temperature': temp,
        'humidity': humidity,
        'lat': SENSOR_LAT,
        'lng': SENSOR_LNG
    }
    
    try:
        response = requests.post(API_URL, json=payload)
        if response.status_code == 200:
            result = response.json()
            risk_score = result.get('riskScore', 'N/A')
            risk_level = result.get('riskLevel', 'N/A')
            spread_rate = result.get('spreadRateKmh', 'N/A')
            print(f"✓ Sent: {temp}°C, {humidity}% RH | Risk Score: {risk_score} | Risk Level: {risk_level} | Spread: {spread_rate} km/h")
        else:
            print(f"✗ Error: {response.status_code} - {response.text}")
    except Exception as e:
        print(f"✗ Failed to send: {e}")

if __name__ == '__main__':
    print("=" * 60)
    print("Mock IoT Sensor Simulator (LOCAL DEVELOPMENT ONLY)")
    print("=" * 60)
    print("NOTE: This uses HTTP POST for local testing.")
    print("In production, ESP32 devices use MQTT → AWS IoT Core.")
    print("=" * 60)
    print(f"Device ID: {DEVICE_ID}")
    print(f"Location: {SENSOR_LAT}, {SENSOR_LNG}")
    print(f"API Endpoint: {API_URL}")
    print("Sending sensor data every 10 seconds...")
    print("=" * 60)
    print()
    
    while True:
        send_sensor_data()
        time.sleep(10)