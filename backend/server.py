import json
import sqlite3
import datetime
import threading
from typing import Optional
from fastapi import FastAPI, Query, HTTPException
from fastapi.middleware.cors import CORSMiddleware
import paho.mqtt.client as mqtt

# ==============================================================================
# CONFIGURATION (Khớp với thông số trong main/variable.h của ESP32)
# ==============================================================================
MQTT_BROKER = "mqtt.abcsolutions.com.vn"
MQTT_PORT = 1883
MQTT_USERNAME = "abcsolution"
MQTT_PASSWORD = "CseLAbC5c6"

# Chú ý:
# - ESP32 Publish lên MQTT_TOPIC_PUB -> Python Server cần SUBSCRIBE vào topic này để nhận dữ liệu
# - ESP32 Subscribe vào MQTT_TOPIC_SUB -> Python Server cần PUBLISH vào topic này để gửi lệnh
MQTT_TOPIC_SUB = "young/smartFarm/publish_test"      # Topic nhận dữ liệu từ ESP32
MQTT_TOPIC_PUB = "young/smartFarm/subscriber_test"   # Topic gửi lệnh xuống ESP32

DB_FILE = "sensor_data.db"

# ==============================================================================
# DATABASE SETUP (SQLite)
# ==============================================================================
def init_db():
    """Khởi tạo bảng cơ sở dữ liệu SQLite nếu chưa tồn tại"""
    conn = sqlite3.connect(DB_FILE)
    cursor = conn.cursor()
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS telemetry (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
            device_id TEXT,
            rtc_time TEXT,
            reset_count INTEGER,
            -- DHT13
            temp_c REAL,
            humidity_pct REAL,
            -- Light
            light_intensity REAL,
            -- Rain
            rain_status INTEGER,
            -- TDS & EC
            tds_ppm REAL,
            ec REAL,
            salinity REAL,
            water_temp_c REAL,
            -- Multi-depth Soil Moisture
            soil_10cm REAL,
            soil_20cm REAL,
            soil_40cm REAL,
            soil_min_thresh REAL,
            soil_max_thresh REAL,
            drought_alert INTEGER,
            waterlog_alert INTEGER,
            -- Smart Drainage
            water_level_cm REAL,
            high_water_alert INTEGER,
            drainage_pump_active INTEGER,
            drainage_auto_mode INTEGER,
            -- Irrigation & ET
            wind_speed_ms REAL,
            et_mm_day REAL,
            irrigation_valve_active INTEGER,
            irrigation_auto_mode INTEGER,
            -- Raw JSON
            raw_payload TEXT
        )
    """)
    conn.commit()
    conn.close()

def save_telemetry_to_db(payload_str: str):
    """Phân tích dữ liệu JSON từ ESP32 và lưu trữ vào SQLite"""
    try:
        data = json.loads(payload_str)
        
        device_id = data.get("id", "UNKNOWN")
        rtc_time = data.get("rtc", "")
        reset_count = data.get("reset", 0)

        dht = data.get("dht", {})
        light = data.get("light", {})
        rain = data.get("rain", {})
        tds = data.get("tds", {})
        soil = data.get("soil", {})
        drainage = data.get("drainage", {})
        irrigation = data.get("irrigation", {})

        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute("""
            INSERT INTO telemetry (
                device_id, rtc_time, reset_count,
                temp_c, humidity_pct,
                light_intensity,
                rain_status,
                tds_ppm, ec, salinity, water_temp_c,
                soil_10cm, soil_20cm, soil_40cm, soil_min_thresh, soil_max_thresh, drought_alert, waterlog_alert,
                water_level_cm, high_water_alert, drainage_pump_active, drainage_auto_mode,
                wind_speed_ms, et_mm_day, irrigation_valve_active, irrigation_auto_mode,
                raw_payload
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """, (
            device_id, rtc_time, reset_count,
            dht.get("temperature_c"), dht.get("humidity_pct"),
            light.get("intensity"),
            rain.get("status"),
            tds.get("tds"), tds.get("ec"), tds.get("salinity"), tds.get("temperature_c"),
            soil.get("moisture_10cm"), soil.get("moisture_20cm"), soil.get("moisture_40cm"),
            soil.get("min_threshold"), soil.get("max_threshold"),
            1 if soil.get("drought_alert") else 0, 1 if soil.get("waterlog_alert") else 0,
            drainage.get("water_level_cm"),
            1 if drainage.get("high_water_alert") else 0,
            1 if drainage.get("pump_active") else 0,
            1 if drainage.get("auto_mode") else 0,
            irrigation.get("wind_speed_ms"), irrigation.get("et_mm_day"),
            1 if irrigation.get("valve_active") else 0,
            1 if irrigation.get("auto_mode") else 0,
            payload_str
        ))
        conn.commit()
        conn.close()
        print(f"[{datetime.datetime.now().isoformat()}] Saved telemetry from Device '{device_id}' to SQLite DB.")
    except json.JSONDecodeError:
        print("Error: Message payload is not valid JSON")
    except Exception as e:
        print(f"Error saving to DB: {e}")

# ==============================================================================
# MQTT CLIENT SERVICE
# ==============================================================================
mqtt_client = mqtt.Client(client_id="SmartFarm_Python_Gateway")

def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        print(f"Connected to MQTT Broker ({MQTT_BROKER}:{MQTT_PORT})")
        client.subscribe(MQTT_TOPIC_SUB)
        print(f"Subscribed to topic: '{MQTT_TOPIC_SUB}'")
    else:
        print(f"Failed to connect to MQTT Broker, return code: {rc}")

def on_message(client, userdata, msg):
    try:
        payload = msg.payload.decode('utf-8')
        print(f"\n[MQTT RECV] Topic '{msg.topic}': {payload}")
        save_telemetry_to_db(payload)
    except Exception as e:
        print(f"Error processing MQTT message: {e}")

def start_mqtt_loop():
    mqtt_client.on_connect = on_connect
    mqtt_client.on_message = on_message
    if MQTT_USERNAME and MQTT_PASSWORD:
        mqtt_client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
    try:
        mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
        mqtt_client.loop_start()
    except Exception as e:
        print(f"MQTT Connection error: {e}")

from fastapi.responses import HTMLResponse, FileResponse
import os

# ==============================================================================
# FASTAPI APP & REST ENDPOINTS
# ==============================================================================
app = FastAPI(
    title="Smart Farm Gateway API & Datalogger",
    description="Python REST API tiếp nhận, lưu trữ và truy vấn dữ liệu IoT cho hệ thống Smart Farm",
    version="1.0.0"
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

@app.on_event("startup")
def startup_event():
    init_db()
    start_mqtt_loop()

@app.get("/", response_class=HTMLResponse)
@app.get("/dashboard", response_class=HTMLResponse)
def get_dashboard():
    """Giao diện Dashboard hiển thị thông số cảm biến thời gian thực"""
    dashboard_path = os.path.join(os.path.dirname(__file__), "dashboard.html")
    if os.path.exists(dashboard_path):
        with open(dashboard_path, "r", encoding="utf-8") as f:
            return f.read()
    return "<h1>Smart Farm Gateway Running</h1>"

@app.get("/api/info")
def get_api_info():
    return {
        "status": "online",
        "service": "Smart Farm Gateway API Python",
        "mqtt_broker": MQTT_BROKER,
        "subscribed_topic": MQTT_TOPIC_SUB,
        "database": DB_FILE
    }

@app.get("/api/telemetry/latest")
def get_latest_telemetry():
    """Lấy bản ghi dữ liệu cảm biến mới nhất từ cơ sở dữ liệu"""
    conn = sqlite3.connect(DB_FILE)
    conn.row_factory = sqlite3.Row
    cursor = conn.cursor()
    cursor.execute("SELECT * FROM telemetry ORDER BY id DESC LIMIT 1")
    row = cursor.fetchone()
    conn.close()
    
    if not row:
        raise HTTPException(status_code=440, detail="Chưa có dữ liệu cảm biến trong cơ sở dữ liệu.")
    
    return dict(row)

@app.get("/api/telemetry/history")
def get_telemetry_history(limit: int = Query(default=50, ge=1, le=500)):
    """Lấy lịch sử dữ liệu cảm biến (mặc định 50 bản ghi gần nhất)"""
    conn = sqlite3.connect(DB_FILE)
    conn.row_factory = sqlite3.Row
    cursor = conn.cursor()
    cursor.execute("SELECT * FROM telemetry ORDER BY id DESC LIMIT ?", (limit,))
    rows = cursor.fetchall()
    conn.close()
    
    return [dict(r) for r in rows]

@app.post("/api/control/drainage_pump")
def control_drainage_pump(state: bool, auto_mode: Optional[bool] = None):
    """Gửi lệnh điều khiển Bơm thoát nước xuống ESP32 qua MQTT"""
    payload = {
        "cmd": "control_drainage_pump",
        "state": state
    }
    if auto_mode is not None:
        payload["auto"] = auto_mode
        
    json_payload = json.dumps(payload)
    mqtt_client.publish(MQTT_TOPIC_PUB, json_payload)
    return {"status": "sent", "topic": MQTT_TOPIC_PUB, "payload": payload}

@app.post("/api/control/irrigation_valve")
def control_irrigation_valve(state: bool, auto_mode: Optional[bool] = None):
    """Gửi lệnh điều khiển Van tưới tiêu xuống ESP32 qua MQTT"""
    payload = {
        "cmd": "control_irrigation_valve",
        "state": state
    }
    if auto_mode is not None:
        payload["auto"] = auto_mode

    json_payload = json.dumps(payload)
    mqtt_client.publish(MQTT_TOPIC_PUB, json_payload)
    return {"status": "sent", "topic": MQTT_TOPIC_PUB, "payload": payload}

@app.post("/api/control/soil_thresholds")
def set_soil_thresholds(min_thresh: float, max_thresh: float):
    """Gửi lệnh cài đặt ngưỡng độ ẩm đất xuống ESP32 qua MQTT"""
    payload = {
        "cmd": "set_soil_thresholds",
        "min": min_thresh,
        "max": max_thresh
    }
    json_payload = json.dumps(payload)
    mqtt_client.publish(MQTT_TOPIC_PUB, json_payload)
    return {"status": "sent", "topic": MQTT_TOPIC_PUB, "payload": payload}

if __name__ == "__main__":
    import uvicorn
    uvicorn.run("server:app", host="0.0.0.0", port=8000, reload=True)
