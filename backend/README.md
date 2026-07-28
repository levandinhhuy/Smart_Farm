# Smart Farm Backend Server (API Python & MQTT Datalogger)

Thư mục này chứa mã nguồn Python tiếp nhận dữ liệu MQTT từ ESP32, lưu trữ vào cơ sở dữ liệu SQLite và cung cấp RESTful API cho ứng dụng/giao diện người dùng.

## 📂 Cấu trúc mã nguồn

* `server.py`: Server chính (FastAPI + Paho-MQTT Client + SQLite Database).
* `test_publisher.py`: Script giả lập thiết bị ESP32 gửi dữ liệu MQTT mẫu (dùng để test khi không cắm phần cứng).
* `requirements.txt`: Các thư viện Python cần thiết.
* `sensor_data.db`: File CSDL SQLite (tự động khởi tạo khi chạy `server.py`).

## 🚀 Hướng dẫn cài đặt & Chạy demo

### Bước 1: Cài đặt thư viện Python
Mở Terminal trong thư mục `backend` và chạy:
```bash
pip install -r requirements.txt
```

### Bước 2: Khởi chạy Python API Server & MQTT Subscriber
```bash
python server.py
```
* Server sẽ khởi chạy tại: `http://localhost:8000`
* Tài liệu API tương tác (Swagger UI): `http://localhost:8000/docs`

### Bước 3: Chạy script giả lập ESP32 (Nếu muốn test không cần phần cứng)
Mở một cửa sổ Terminal mới trong thư mục `backend`:
```bash
python test_publisher.py
```

## 📡 Các API Endpoint chính

* **`GET /api/telemetry/latest`**: Lấy bản ghi cảm biến mới nhất.
* **`GET /api/telemetry/history?limit=50`**: Lấy 50 bản ghi cảm biến gần nhất.
* **`POST /api/control/drainage_pump`**: Bật/tắt hoặc chuyển chế độ tự động cho Bơm thoát nước.
* **`POST /api/control/irrigation_valve`**: Bật/tắt hoặc chuyển chế độ tự động cho Van tưới.
* **`POST /api/control/soil_thresholds`**: Cấu hình ngưỡng độ ẩm đất min/max.
