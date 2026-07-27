#pragma once

#ifndef __SENSOR_H_
#define __SENSOR_H_

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

/* Define --------------------------------------------------------------------*/
typedef struct
{
  float temperature_c;  // °C
  float humidity_pct;   // %RH
  bool  valid;          // true nếu đọc thành công
} DHT13_Data_t;

// Rain sensor
typedef struct
{
  uint16_t status;       // 0 = không mưa, 1 = có mưa
  uint16_t lower_limit;  // ngưỡng dưới (°C)
  uint16_t hysteresis;   // độ trễ (°C)
  uint16_t reset_delay;  // thời gian reset (s)
  uint16_t sensitivity;  // độ nhạy
  bool     valid;
} Rain_Data_t;

// Light sensor
typedef struct
{
  uint32_t intensity;  // 0 – 4,294,967,295 (Lux)
  bool     valid;
} Light_Data_t;

// TDS/EC sensor
typedef struct
{
  float    temperature_c;  // °C
  float    ec;             // µS/cm
  float    salinity;       // ppt
  float    tds;            // mg/L
  uint16_t ec_raw;         // raw ADC
  bool     valid;
} TDS_Data_t;

// Multi-depth Soil Moisture sensor (10cm, 20cm, 40cm)
typedef struct
{
  float moisture_10cm;  // %RH độ ẩm đất tầng 10cm
  float moisture_20cm;  // %RH độ ẩm đất tầng 20cm
  float moisture_40cm;  // %RH độ ẩm đất tầng 40cm
  float min_threshold;  // Ngưỡng héo (Mặc định: 30.0%)
  float max_threshold;  // Ngưỡng úng rễ (Mặc định: 80.0%)
  bool  waterlog_alert; // Độ ẩm > 80% (nguy cơ úng rễ)
  bool  drought_alert;  // Độ ẩm < 30% (nguy cơ héo)
  bool  valid;
} Soil_Data_t;

// Water level sensor for mương rãnh
typedef struct
{
  float level_cm;        // Mực nước mương rãnh (cm)
  float max_safe_level;  // Mực nước tối đa an toàn (Mặc định: 15.0 cm)
  bool  high_water_alert;// Cảnh báo nước dâng cao
  bool  valid;
} WaterLevel_Data_t;

// Wind speed sensor
typedef struct
{
  float speed_ms;  // Tốc độ gió (m/s)
  bool  valid;
} Wind_Data_t;

// Evapotranspiration (ET) data
typedef struct
{
  float et_mm_day; // Chỉ số bốc thoát hơi nước (mm/ngày)
  bool  valid;
} ET_Data_t;

// Actuators status (Bơm thoát nước & Van tưới nhỏ giọt)
typedef struct
{
  bool drainage_pump_active;  // Trạng thái máy bơm xả nước
  bool irrigation_valve_active;// Trạng thái van tưới nhỏ giọt / phun mưa
  bool auto_drainage_enabled;  // Chế độ thoát nước tự động
  bool auto_irrigation_enabled;// Chế độ tưới tự động dựa trên ET & độ ẩm
} Actuator_State_t;

/* Variables -----------------------------------------------------------------*/
extern DHT13_Data_t      g_dht13;
extern Rain_Data_t       g_rain;
extern Light_Data_t      g_light;
extern TDS_Data_t        g_tds;
extern Soil_Data_t       g_soil;
extern WaterLevel_Data_t g_water_level;
extern Wind_Data_t       g_wind;
extern ET_Data_t         g_et;
extern Actuator_State_t  g_actuator;

/* Functions -----------------------------------------------------------------*/
void sensor_init(void);
void set_drainage_pump(bool enable);
void set_irrigation_valve(bool enable);
void update_smart_farm_automation(void);

#endif