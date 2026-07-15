#include "buzzer.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

/* Define --------------------------------------------------------------------*/
#define BUZZER_PIN 32

/* Variables -----------------------------------------------------------------*/
static const char* TAG = "BUZZER";

static bool     buzzer_active         = false;
static uint32_t buzzer_beep_length    = 0;
static uint32_t buzzer_beep_deadline  = 0;
static uint32_t buzzer_beep_cooldown  = 0;
static uint32_t buzzer_alert_cooldown = 0;

static uint32_t buzzer_alert_deadline = 0;
static uint16_t buzzer_alert_freq     = 1000;
static uint8_t  buzzer_alert_step     = 0;

/* Helper Functions ----------------------------------------------------------*/
static uint32_t millis(void)
{
  return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

/* RTOS ----------------------------------------------------------------------*/
static void buzzer_task(void*);

/* Functions -----------------------------------------------------------------*/
void tone(int pin, int frequency)
{
  if (pin != BUZZER_PIN)
    return;

  ledc_timer_config_t ledc_timer = {};
  ledc_timer.speed_mode      = LEDC_LOW_SPEED_MODE;
  ledc_timer.duty_resolution = LEDC_TIMER_10_BIT;
  ledc_timer.timer_num       = LEDC_TIMER_0;
  ledc_timer.freq_hz         = (uint32_t)frequency;
  ledc_timer.clk_cfg         = LEDC_AUTO_CLK;
  
  ledc_timer_config(&ledc_timer);

  ledc_channel_config_t ledc_channel = {};
  ledc_channel.gpio_num   = BUZZER_PIN;
  ledc_channel.speed_mode = LEDC_LOW_SPEED_MODE;
  ledc_channel.channel    = LEDC_CHANNEL_0;
  ledc_channel.intr_type  = LEDC_INTR_DISABLE;
  ledc_channel.timer_sel  = LEDC_TIMER_0;
  ledc_channel.duty       = 512; // 50% duty
  ledc_channel.hpoint     = 0;
  
  ledc_channel_config(&ledc_channel);
}

void noTone(int pin)
{
  if (pin != BUZZER_PIN)
    return;
  ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
}

void buzzer_set_state(bool state)
{
  buzzer_active = state;
}

void buzzer_beep(uint32_t length, uint32_t cooldown)
{
  if (millis() >= buzzer_beep_cooldown)
  {
    buzzer_beep_length   = length;
    buzzer_beep_deadline = millis() + length;
    buzzer_beep_cooldown = millis() + length + cooldown;
  }
}

void buzzer_beep_tick()
{
  if (buzzer_beep_length > 0)
  {
    if ((int32_t)(millis() - buzzer_beep_deadline) < 0)
    {
      tone(BUZZER_PIN, 2000);
    }
    else
    {
      noTone(BUZZER_PIN);
      buzzer_beep_length = 0;
    }
  }
}

void buzzer_alert(uint16_t freq_start, uint16_t freq_end, uint16_t step, uint32_t step_length, uint32_t cooldown)
{
  if (millis() >= buzzer_alert_cooldown)
  {
    if (buzzer_alert_step == 0)
    {
      buzzer_alert_freq     = freq_start;
      buzzer_alert_step     = step;
      buzzer_alert_deadline = millis() + step_length;
      tone(BUZZER_PIN, buzzer_alert_freq);
    }
    else if ((int32_t)(millis() - buzzer_alert_deadline) >= 0)
    {
      buzzer_alert_deadline = millis() + step_length;
      buzzer_alert_freq += step;
      if (buzzer_alert_freq >= freq_end)
      {
        noTone(BUZZER_PIN);
        buzzer_alert_step     = 0;
        buzzer_alert_cooldown = millis() + cooldown;
      }
      else
      {
        tone(BUZZER_PIN, buzzer_alert_freq);
      }
    }
  }
}

static void buzzer_task(void* pv)
{
  (void)pv;

  // Initialize Buzzer GPIO
  gpio_config_t io_conf = {};
  io_conf.intr_type    = GPIO_INTR_DISABLE;
  io_conf.mode         = GPIO_MODE_OUTPUT;
  io_conf.pin_bit_mask = (1ULL << BUZZER_PIN);
  gpio_config(&io_conf);
  gpio_set_level((gpio_num_t)BUZZER_PIN, 0);

  while (1)
  {
    if (buzzer_active)
    {
      buzzer_beep_tick();
    }
    else
    {
      noTone(BUZZER_PIN);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void buzzer_init(void)
{
  xTaskCreate(buzzer_task, "Buzzer Task", 2048, nullptr, 1, nullptr);
  ESP_LOGI(TAG, "Initialized successfully");
}