import requests
import time
import random

API_URL = "http://localhost:5000/api/temperature"

def send_temperature():
    # Simulate temperature reading (15°C to 45°C)
    temp = round(random.uniform(15, 45), 1)
    
    try:
        response = requests.post(API_URL, json={'temperature': temp})
        if response.status_code == 200:
            print(f"Sent: {temp}°C")
        else:
            print(f"Error: {response.status_code}")
    except Exception as e:
        print(f"Failed to send: {e}")

if __name__ == '__main__':
    print("IoT Device Starting...")
    print(f"Sending to: {API_URL}")
    print("Sending temperature every 10 seconds...\n")
    
    while True:
        send_temperature()
        time.sleep(10)