#include "sensor.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "buzzer.h"

/* -------------------------
   RS485 / UART pin config
   -------------------------*/
#define DE_PIN 18  // RE/DE của MAX485 (driver enable)
#define TX_PIN 5   // ESP32 TX -> DI
#define RX_PIN 19  // ESP32 RX <- RO

#define MODBUS_SERIAL_BAUD 4800
#define UART_PORT UART_NUM_2
#define BUF_SIZE 256

/* -------------------------
   Actuator GPIO pins
   -------------------------*/
#define DRAINAGE_PUMP_PIN    GPIO_NUM_22 // Máy bơm xả nước rãnh
#define IRRIGATION_VALVE_PIN GPIO_NUM_23 // Van tưới nhỏ giọt / phun mưa

/* -------------------------
   Sensor Modbus defines
   -------------------------*/
#define MODBUS_ID_DHT13       2
#define REG_DHT13_HUMIDITY    0x0000
#define REG_DHT13_TEMPERATURE 0x0001
#define LEN_DHT13             2

#define MODBUS_ID_SOIL        3  // Multi-depth Soil Moisture Sensor (10cm, 20cm, 40cm)
#define REG_SOIL_MOISTURE_10  0x0000
#define REG_SOIL_MOISTURE_20  0x0001
#define REG_SOIL_MOISTURE_40  0x0002
#define LEN_SOIL_MOISTURE     3

#define MODBUS_ID_LIGHT       4
#define REG_LIGHT_INTENSITY_L 0x0003
#define REG_LIGHT_INTENSITY_H 0x0004
#define LEN_LIGHT_INTENSITY   2

#define MODBUS_ID_RAIN  5
#define REG_RAIN_STATUS 0x0000
#define REG_RAIN_LL     0x0031

#define MODBUS_ID_TDS      6
#define REG_TDS_TEMP       0x0000
#define LEN_TDS_TEMP       1
#define REG_TDS_EC_SAL_TDS 0x0002
#define LEN_TDS_EC_SAL_TDS 3
#define REG_TDS_ECRAWAD    0x0008
#define LEN_TDS_ECRAWAD    1

#define MODBUS_ID_WATERLEVEL 7  // Cảm biến mực nước mương rãnh
#define REG_WATER_LEVEL      0x0000

#define MODBUS_ID_WIND       8  // Cảm biến tốc độ gió
#define REG_WIND_SPEED       0x0000

/* Global Sensor & Actuator Data */
DHT13_Data_t      g_dht13       = {0.0f, 0.0f, false};
Rain_Data_t       g_rain        = {0, 0, 0, 0, 0, false};
Light_Data_t      g_light       = {0, false};
TDS_Data_t        g_tds         = {0.0f, 0.0f, 0.0f, 0.0f, 0, false};
Soil_Data_t       g_soil        = {0.0f, 0.0f, 0.0f, 30.0f, 80.0f, false, false, false};
WaterLevel_Data_t g_water_level = {0.0f, 15.0f, false, false};
Wind_Data_t       g_wind        = {0.0f, false};
ET_Data_t         g_et          = {0.0f, false};
Actuator_State_t  g_actuator    = {false, false, true, true}; // Mặc định tự động

static const char* TAG = "SENSOR_FARM";

/* -------------------------
   CRC16 helper
   -------------------------*/
static uint16_t modbus_crc(const uint8_t* buf, int len)
{
  uint16_t crc = 0xFFFF;
  for (int pos = 0; pos < len; pos++)
  {
    crc ^= (uint16_t)buf[pos];
    for (int i = 8; i != 0; i--)
    {
      if ((crc & 0x0001) != 0)
      {
        crc >>= 1;
        crc ^= 0xA001;
      }
      else
      {
        crc >>= 1;
      }
    }
  }
  return crc;
}

/* -------------------------
   RS485 Modbus Query
   -------------------------*/
static bool modbus_query(uint8_t id, uint8_t fc, uint16_t reg, uint16_t count, uint16_t* out_regs)
{
  uint8_t req[8];
  req[0] = id;
  req[1] = fc;
  req[2] = (reg >> 8) & 0xFF;
  req[3] = reg & 0xFF;
  req[4] = (count >> 8) & 0xFF;
  req[5] = count & 0xFF;
  
  uint16_t crc = modbus_crc(req, 6);
  req[6] = crc & 0xFF;
  req[7] = (crc >> 8) & 0xFF;
  
  // Enable TX (DE High)
  gpio_set_level((gpio_num_t)DE_PIN, 1);
  esp_rom_delay_us(50);
  
  uart_write_bytes(UART_PORT, (const char*)req, 8);
  uart_wait_tx_done(UART_PORT, pdMS_TO_TICKS(100));
  
  // Disable TX (DE Low)
  esp_rom_delay_us(50);
  gpio_set_level((gpio_num_t)DE_PIN, 0);
  
  // Read Response
  uint8_t resp[BUF_SIZE];
  int expected_len = 3 + 2 * count + 2; // ID + FC + ByteCount + data + CRC
  
  int len = uart_read_bytes(UART_PORT, resp, expected_len, pdMS_TO_TICKS(500));
  if (len < expected_len)
  {
    return false;
  }
  
  // Verify CRC
  uint16_t rx_crc = modbus_crc(resp, expected_len - 2);
  uint16_t msg_crc = resp[expected_len - 2] | (resp[expected_len - 1] << 8);
  if (rx_crc != msg_crc)
  {
    return false;
  }
  
  // Verify ID & FC
  if (resp[0] != id || resp[1] != fc)
  {
    return false;
  }
  
  // Parse Registers
  for (int i = 0; i < count; i++)
  {
    out_regs[i] = (resp[3 + 2 * i] << 8) | resp[4 + 2 * i];
  }
  
  return true;
}

/* Relay Actuator Control Functions */
void set_drainage_pump(bool enable)
{
  g_actuator.drainage_pump_active = enable;
  gpio_set_level(DRAINAGE_PUMP_PIN, enable ? 1 : 0);
  ESP_LOGI(TAG, "Drainage Pump -> %s", enable ? "ON" : "OFF");
}

void set_irrigation_valve(bool enable)
{
  g_actuator.irrigation_valve_active = enable;
  gpio_set_level(IRRIGATION_VALVE_PIN, enable ? 1 : 0);
  ESP_LOGI(TAG, "Irrigation Valve -> %s", enable ? "ON" : "OFF");
}

/* Evapotranspiration (ET0) Calculation Algorithm */
static void calculate_evapotranspiration(void)
{
  if (!g_dht13.valid || !g_light.valid)
  {
    g_et.valid = false;
    return;
  }

  // Hargreaves-Samani / Penman-Monteith approximation
  float temp = g_dht13.temperature_c;
  float lux = (float)g_light.intensity;
  float wind = g_wind.valid ? g_wind.speed_ms : 1.5f; // Mặc định 1.5 m/s nếu chưa có cảm biến gió

  // Ước tính bức xạ mặt trời Rs (MJ/m2/day) từ Lux: 1 W/m2 ≈ 126 Lux
  float solar_rad_mj = (lux / 126.0f) * 0.0864f; 
  if (solar_rad_mj < 0.1f) solar_rad_mj = 0.1f;

  // Công thức Hargreaves tinh chỉnh theo tốc độ gió
  float et0 = 0.0023f * (temp + 17.8f) * sqrtf(solar_rad_mj) * (1.0f + 0.27f * wind);
  
  g_et.et_mm_day = et0;
  g_et.valid = true;
}

/* Smart Farm Automation Logic */
void update_smart_farm_automation(void)
{
  // 1. Kiểm tra Độ ẩm Đất Đa Tầng (10cm, 20cm, 40cm)
  if (g_soil.valid)
  {
    g_soil.waterlog_alert = (g_soil.moisture_10cm > g_soil.max_threshold || 
                             g_soil.moisture_20cm > g_soil.max_threshold || 
                             g_soil.moisture_40cm > g_soil.max_threshold);
                             
    g_soil.drought_alert  = (g_soil.moisture_10cm < g_soil.min_threshold || 
                             g_soil.moisture_20cm < g_soil.min_threshold || 
                             g_soil.moisture_40cm < g_soil.min_threshold);

    // Kích hoạt còi cảnh báo nếu có nguy cơ úng rễ hoặc héo mè
    if (g_soil.waterlog_alert || g_soil.drought_alert)
    {
      buzzer_beep(300, 500);
    }
  }

  // 2. Tính toán ET
  calculate_evapotranspiration();

  // 3. Tự động Thoát nước Mương Rãnh (Smart Drainage)
  if (g_actuator.auto_drainage_enabled)
  {
    bool need_drainage = false;
    if (g_water_level.valid && g_water_level.level_cm > g_water_level.max_safe_level)
    {
      need_drainage = true;
      g_water_level.high_water_alert = true;
    }
    else
    {
      g_water_level.high_water_alert = false;
    }

    if (g_rain.valid && g_rain.status == 1 && g_soil.waterlog_alert)
    {
      need_drainage = true;
    }

    if (need_drainage && !g_actuator.drainage_pump_active)
    {
      set_drainage_pump(true);
    }
    else if (!need_drainage && (!g_water_level.valid || g_water_level.level_cm < (g_water_level.max_safe_level - 3.0f)) && g_actuator.drainage_pump_active)
    {
      set_drainage_pump(false);
    }
  }

  // 4. Tự động Tưới nhỏ giọt / Phun mưa dựa trên ET & Độ ẩm Đất
  if (g_actuator.auto_irrigation_enabled)
  {
    bool need_irrigation = false;
    
    // Tưới khi bị khô hạn (<30%) hoặc nhu cầu ET cao (>4.0 mm/ngày) & độ ẩm thấp
    if (g_soil.drought_alert || (g_et.valid && g_et.et_mm_day > 4.0f && g_soil.moisture_10cm < 50.0f))
    {
      // An toàn: Không tưới nếu mưa hoặc ngập/úng rễ
      if (!g_soil.waterlog_alert && !g_water_level.high_water_alert && g_rain.status == 0)
      {
        need_irrigation = true;
      }
    }

    if (need_irrigation && !g_actuator.irrigation_valve_active)
    {
      set_irrigation_valve(true);
    }
    else if ((!need_irrigation || g_soil.moisture_10cm > 65.0f || g_rain.status == 1) && g_actuator.irrigation_valve_active)
    {
      set_irrigation_valve(false);
    }
  }
}

/* -------------------------
   Sensor task (RTOS)
   -------------------------*/
static void sensor_task(void* pv)
{
  (void)pv;

  // Initialize DE Pin
  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pin_bit_mask = (1ULL << DE_PIN);
  gpio_config(&io_conf);
  gpio_set_level((gpio_num_t)DE_PIN, 0);

  // Initialize Relay GPIOs (Pump & Valve)
  gpio_config_t relay_conf = {};
  relay_conf.intr_type = GPIO_INTR_DISABLE;
  relay_conf.mode = GPIO_MODE_OUTPUT;
  relay_conf.pin_bit_mask = (1ULL << DRAINAGE_PUMP_PIN) | (1ULL << IRRIGATION_VALVE_PIN);
  gpio_config(&relay_conf);
  gpio_set_level(DRAINAGE_PUMP_PIN, 0);
  gpio_set_level(IRRIGATION_VALVE_PIN, 0);

  // Initialize UART
  uart_config_t uart_config = {};
  uart_config.baud_rate = MODBUS_SERIAL_BAUD;
  uart_config.data_bits = UART_DATA_8_BITS;
  uart_config.parity    = UART_PARITY_DISABLE;
  uart_config.stop_bits = UART_STOP_BITS_1;
  uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  uart_config.source_clk = UART_SCLK_DEFAULT;
  
  uart_driver_install(UART_PORT, BUF_SIZE * 2, 0, 0, NULL, 0);
  uart_param_config(UART_PORT, &uart_config);
  uart_set_pin(UART_PORT, TX_PIN, RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

  vTaskDelay(pdMS_TO_TICKS(100));

  while (1)
  {
    // 1. DHT13
    uint16_t dht_regs[2];
    if (modbus_query(MODBUS_ID_DHT13, 0x04, REG_DHT13_HUMIDITY, LEN_DHT13, dht_regs))
    {
      float hum = (float)dht_regs[0] / 100.0f;
      float temp = (float)dht_regs[1] / 100.0f;
      g_dht13.temperature_c = temp * 10;
      g_dht13.humidity_pct  = hum * 10;
      g_dht13.valid         = true;
    }
    else
    {
      g_dht13.valid = false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    // 2. Multi-depth Soil Moisture (10cm, 20cm, 40cm)
    uint16_t soil_regs[3];
    bool soil_ok = modbus_query(MODBUS_ID_SOIL, 0x04, REG_SOIL_MOISTURE_10, LEN_SOIL_MOISTURE, soil_regs);
    if (!soil_ok)
    {
      soil_ok = modbus_query(MODBUS_ID_SOIL, 0x03, REG_SOIL_MOISTURE_10, LEN_SOIL_MOISTURE, soil_regs);
    }
    if (soil_ok)
    {
      g_soil.moisture_10cm = (float)soil_regs[0] / 10.0f;
      g_soil.moisture_20cm = (float)soil_regs[1] / 10.0f;
      g_soil.moisture_40cm = (float)soil_regs[2] / 10.0f;
      g_soil.valid         = true;
    }
    else
    {
      g_soil.valid = false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    // 3. Light
    uint16_t light_regs[2];
    if (modbus_query(MODBUS_ID_LIGHT, 0x04, REG_LIGHT_INTENSITY_L, LEN_LIGHT_INTENSITY, light_regs))
    {
      g_light.intensity = (((uint32_t)light_regs[0]) << 16) | light_regs[1];
      g_light.valid     = true;
    }
    else
    {
      g_light.valid = false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    // 4. Rain
    uint16_t rain_regs[1];
    if (modbus_query(MODBUS_ID_RAIN, 0x04, REG_RAIN_STATUS, 1, rain_regs))
    {
      g_rain.status = rain_regs[0];
      uint16_t extra_regs[4];
      if (modbus_query(MODBUS_ID_RAIN, 0x04, REG_RAIN_LL, 4, extra_regs))
      {
        g_rain.lower_limit = extra_regs[0];
        g_rain.hysteresis  = extra_regs[1];
        g_rain.reset_delay = extra_regs[2];
        g_rain.sensitivity = extra_regs[3];
      }
      else
      {
        g_rain.lower_limit = g_rain.hysteresis = g_rain.reset_delay = g_rain.sensitivity = 0xFFFF;
      }
      g_rain.valid = true;
    }
    else
    {
      g_rain.valid = false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    // 5. TDS/EC
    uint16_t tds_regs[5];
    bool ok = modbus_query(MODBUS_ID_TDS, 0x03, 0x0000, 5, tds_regs);
    if (!ok)
    {
      vTaskDelay(pdMS_TO_TICKS(10));
      ok = modbus_query(MODBUS_ID_TDS, 0x04, 0x0000, 5, tds_regs);
    }
    
    if (ok)
    {
      int16_t t_raw = (int16_t)tds_regs[0];
      g_tds.temperature_c = (float)t_raw / 10.0f;
      g_tds.ec            = (float)tds_regs[2];
      g_tds.salinity      = (float)tds_regs[3];
      g_tds.tds           = (float)tds_regs[4];
      
      vTaskDelay(pdMS_TO_TICKS(20));
      uint16_t raw_regs[1];
      bool raw_ok = modbus_query(MODBUS_ID_TDS, 0x03, REG_TDS_ECRAWAD, 1, raw_regs);
      if (!raw_ok)
      {
        raw_ok = modbus_query(MODBUS_ID_TDS, 0x04, REG_TDS_ECRAWAD, 1, raw_regs);
      }
      
      if (raw_ok)
      {
        g_tds.ec_raw = raw_regs[0];
      }
      else
      {
        g_tds.ec_raw = tds_regs[2];
      }
      g_tds.valid = true;
    }
    else
    {
      g_tds.valid = false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    // 6. Water Level Sensor
    uint16_t wl_regs[1];
    if (modbus_query(MODBUS_ID_WATERLEVEL, 0x04, REG_WATER_LEVEL, 1, wl_regs) ||
        modbus_query(MODBUS_ID_WATERLEVEL, 0x03, REG_WATER_LEVEL, 1, wl_regs))
    {
      g_water_level.level_cm = (float)wl_regs[0] / 10.0f;
      g_water_level.valid    = true;
    }
    else
    {
      g_water_level.valid = false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    // 7. Wind Speed Sensor
    uint16_t wind_regs[1];
    if (modbus_query(MODBUS_ID_WIND, 0x04, REG_WIND_SPEED, 1, wind_regs) ||
        modbus_query(MODBUS_ID_WIND, 0x03, REG_WIND_SPEED, 1, wind_regs))
    {
      g_wind.speed_ms = (float)wind_regs[0] / 10.0f;
      g_wind.valid    = true;
    }
    else
    {
      g_wind.valid = false;
    }

    // 8. Tự động hóa Smart Farm & Tính toán ET
    update_smart_farm_automation();

    printf("SOIL[10cm=%.1f%% 20cm=%.1f%% 40cm=%.1f%% %s] | WATER_LVL[%.1fcm %s] | ET[%.2f mm/day] | PUMP[%s] | VALVE[%s]\n",
           g_soil.moisture_10cm, g_soil.moisture_20cm, g_soil.moisture_40cm, g_soil.valid ? "OK" : "ERR",
           g_water_level.level_cm, g_water_level.valid ? "OK" : "ERR", g_et.et_mm_day,
           g_actuator.drainage_pump_active ? "ON" : "OFF", g_actuator.irrigation_valve_active ? "ON" : "OFF");

    vTaskDelay(pdMS_TO_TICKS(3000 - 6 * 100));
  }
}

/* -------------------------
   Public init
   -------------------------*/
void sensor_init(void)
{
  xTaskCreatePinnedToCore(sensor_task, "sensor_task", 4096, NULL, 1, NULL, 1);
  ESP_LOGI(TAG, "Initialized Smart FarmLab sensors & automation successfully");
}

