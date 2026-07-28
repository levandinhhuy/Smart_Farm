#include "rtc.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"

/* Variables -----------------------------------------------------------------*/
bool rtc_state = false;

uint16_t rtc_year  = 0;
uint8_t  rtc_month = 0, rtc_day = 0;
uint8_t  rtc_hour = 0, rtc_minute = 0, rtc_second = 0;

static const char* TAG = "RTC";

/* Helpers -------------------------------------------------------------------*/
static bool time_is_valid()
{
  time_t now = time(nullptr);
  return (now > 1514764800); // 2018-01-01
}

static void load_localtime()
{
  struct tm t;
  time_t    now = time(nullptr);
  localtime_r(&now, &t);

  rtc_year   = (uint16_t)(t.tm_year + 1900);
  rtc_month  = (uint8_t)(t.tm_mon + 1);
  rtc_day    = (uint8_t)(t.tm_mday);
  rtc_hour   = (uint8_t)(t.tm_hour);
  rtc_minute = (uint8_t)(t.tm_min);
  rtc_second = (uint8_t)(t.tm_sec);
}

/* Task ----------------------------------------------------------------------*/
static void rtc_task(void* pv)
{
  // Set Timezone to VN (UTC+7)
  setenv("TZ", "ICT-7", 1);
  tzset();

  // Configure SNTP
  esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
  esp_netif_sntp_init(&config);

  uint64_t next_print_ms = 0;

  while (1)
  {
    rtc_state = time_is_valid();

    if (rtc_state)
    {
      load_localtime();
    }

    uint64_t now_ms = esp_timer_get_time() / 1000ULL;
    if (now_ms >= next_print_ms)
    {
      next_print_ms = now_ms + 60000UL;
      if (rtc_state) {
        ESP_LOGI(TAG, "Time: %02d/%02d/%04d %02d:%02d:%02d", rtc_day, rtc_month, rtc_year, rtc_hour, rtc_minute, rtc_second);
      } else {
        ESP_LOGI(TAG, "Time not synchronized yet");
      }
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

/* API -----------------------------------------------------------------------*/
bool is_leap_year(uint8_t year)
{
  if (year % 4 != 0)
    return false;
  return true;
}

uint8_t days_in_month(uint8_t year, uint8_t month)
{
  static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && is_leap_year(year))
    return 29;
  return days[month - 1];
}

void rtc_init(void)
{
  xTaskCreate(rtc_task, "RTC Task", 4096, nullptr, 1, nullptr);
  ESP_LOGI(TAG, "Initialized successfully");
}
