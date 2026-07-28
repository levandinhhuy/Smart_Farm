#include "led.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

typedef enum
{
  LED_ON,
  LED_OFF,
  LED_TOGGLE_1S
} LED_STATE;

static LED_STATE led_state = LED_OFF;
static TaskHandle_t ledTaskHandle = NULL;
static const char* TAG = "LED";

void led_on()
{
  led_state = LED_ON;
}

void led_off()
{
  led_state = LED_OFF;
}

void led_toggle_1s()
{
  led_state = LED_TOGGLE_1S;
}

static void led_task(void* pvParameters)
{
  int led2_level = 1; // 1 = OFF (active low)
  uint64_t last_toggle_us = esp_timer_get_time();
  
  while (1)
  {
    switch (led_state)
    {
      case LED_ON:
        led2_level = 0; // ON
        gpio_set_level((gpio_num_t)LED_2, led2_level);
        break;

      case LED_OFF:
        led2_level = 1; // OFF
        gpio_set_level((gpio_num_t)LED_2, led2_level);
        break;

      case LED_TOGGLE_1S:
        {
          uint64_t now = esp_timer_get_time();
          if (now - last_toggle_us >= 1000000ULL)
          {
            last_toggle_us = now;
            led2_level = !led2_level;
            gpio_set_level((gpio_num_t)LED_2, led2_level);
          }
        }
        break;

      default:
        led_state = LED_OFF;
        break;
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void led_init()
{
  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pin_bit_mask = (1ULL << LED_1) | (1ULL << LED_2);
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  gpio_config(&io_conf);

  gpio_set_level((gpio_num_t)LED_1, 0); // LED_1 ON (active low)
  gpio_set_level((gpio_num_t)LED_2, 1); // LED_2 OFF

  led_off();

  xTaskCreate(led_task, "LED Task", 2048, NULL, 2, &ledTaskHandle);
  ESP_LOGI(TAG, "Initialized successfully");
}
