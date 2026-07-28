#include "button.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

/* Variables -----------------------------------------------------------------*/
uint32_t key_code                  = 0;
uint32_t key_code_before_releasing = 0;

/* Task handles */
static TaskHandle_t buttonTaskHandle = NULL;
static const char* TAG = "BUTTON";

/* Functions -----------------------------------------------------------------*/
static void button_task(void* pvParameters)
{
  while (1)
  {
    if (gpio_get_level((gpio_num_t)BUTTON_PIN) == 1)
    {
      key_code_before_releasing = 0;
      key_code++;

      if (key_code == 1000)
      {
        key_code = 1000;
      }
    }
    else
    {
      key_code_before_releasing = key_code;
      key_code                  = 0;
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void button_init()
{
  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pin_bit_mask = (1ULL << BUTTON_PIN);
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&io_conf);

  key_code                  = 0;
  key_code_before_releasing = 0;

  // Create the button task
  xTaskCreate(button_task, "Button Task", 2048, NULL, 2, &buttonTaskHandle);

  ESP_LOGI(TAG, "Initialized successfully");
}