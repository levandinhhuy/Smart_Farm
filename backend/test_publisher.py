import json
import time
import random
import paho.mqtt.client as mqtt

MQTT_BROKER = "mqtt.abcsolutions.com.vn"
MQTT_PORT = 1883
MQTT_USERNAME = "abcsolution"
MQTT_PASSWORD = "CseLAbC5c6"
MQTT_TOPIC_PUB = "young/smartFarm/publish_test"

client = mqtt.Client(client_id="ESP32_SmartFarm_Simulator")
if MQTT_USERNAME and MQTT_PASSWORD:
    client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
client.connect(MQTT_BROKER, MQTT_PORT, 60)

print(f"Đã kết nối Broker '{MQTT_BROKER}'. Đang gửi dữ liệu mô phỏng ESP32 lên topic '{MQTT_TOPIC_PUB}'...")

try:
    while True:
        simulated_data = {
            "reset": 1,
            "id": "ESP32_FARM",
            "rtc": time.strftime("%Y-%m-%dT%H:%M:%S"),
            "dht": {
                "valid": True,
                "temperature_c": round(random.uniform(25.0, 32.0), 2),
                "humidity_pct": round(random.uniform(60.0, 85.0), 2)
            },
            "light": {
                "valid": True,
                "intensity": round(random.uniform(1000.0, 8000.0), 1)
            },
            "rain": {
                "valid": True,
                "status": 0,
                "lower_limit": 10.0,
                "hysteresis": 2.0,
                "reset_delay": 5.0,
                "sensitivity": 80.0
            },
            "tds": {
                "valid": True,
                "temperature_c": round(random.uniform(24.0, 28.0), 2),
                "ec": round(random.uniform(1.2, 2.5), 2),
                "salinity": round(random.uniform(0.5, 1.2), 2),
                "tds": round(random.uniform(600.0, 1200.0), 1),
                "ec_raw": round(random.uniform(1.0, 2.0), 2)
            },
            "soil": {
                "valid": True,
                "moisture_10cm": round(random.uniform(30.0, 50.0), 2),
                "moisture_20cm": round(random.uniform(35.0, 55.0), 2),
                "moisture_40cm": round(random.uniform(40.0, 60.0), 2),
                "min_threshold": 25.0,
                "max_threshold": 70.0,
                "waterlog_alert": False,
                "drought_alert": False
            },
            "drainage": {
                "valid": True,
                "water_level_cm": round(random.uniform(5.0, 15.0), 2),
                "high_water_alert": False,
                "pump_active": False,
                "auto_mode": True
            },
            "irrigation": {
                "wind_valid": True,
                "wind_speed_ms": round(random.uniform(0.5, 3.5), 2),
                "et_valid": True,
                "et_mm_day": round(random.uniform(3.0, 6.0), 2),
                "valve_active": False,
                "auto_mode": True
            }
        }
        
        payload_str = json.dumps(simulated_data)
        client.publish(MQTT_TOPIC_PUB, payload_str)
        print(f"[{time.strftime('%H:%M:%S')}] Published: {payload_str[:90]}...")
        time.sleep(10)
except KeyboardInterrupt:
        print("\nĐã dừng giả lập.")
        client.disconnect()
