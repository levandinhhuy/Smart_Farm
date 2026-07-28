/* Includes ------------------------------------------------------------------*/
#include "memory.h"
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#define EEPROM_SIZE 4096
static uint8_t eeprom_buffer[EEPROM_SIZE];
static bool eeprom_initialized = false;
static const char* TAG = "EEPROM";

union FloatUIntConvertor
{
  float    _float;
  uint32_t _uint;
};

void eeprom_init(void)
{
  // Initialize NVS
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  // Fill buffer with 0xFF (uninitialized state)
  memset(eeprom_buffer, 0xFF, EEPROM_SIZE);

  nvs_handle_t my_handle;
  err = nvs_open("storage", NVS_READWRITE, &my_handle);
  if (err == ESP_OK) {
    size_t required_size = EEPROM_SIZE;
    err = nvs_get_blob(my_handle, "eeprom", eeprom_buffer, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
      ESP_LOGI(TAG, "EEPROM blob not found in NVS, using default 0xFF");
    } else if (err != ESP_OK) {
      ESP_LOGE(TAG, "Error reading EEPROM blob from NVS: %d", err);
    }
    nvs_close(my_handle);
  } else {
    ESP_LOGE(TAG, "Error opening NVS handle: %d", err);
  }
  eeprom_initialized = true;
  ESP_LOGI(TAG, "Initialized successfully");
}

void EepromCommit(void)
{
  if (!eeprom_initialized) return;
  nvs_handle_t my_handle;
  esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
  if (err == ESP_OK) {
    err = nvs_set_blob(my_handle, "eeprom", eeprom_buffer, EEPROM_SIZE);
    if (err == ESP_OK) {
      err = nvs_commit(my_handle);
      if (err == ESP_OK) {
        ESP_LOGI(TAG, "Committed successfully to NVS");
      }
    }
    nvs_close(my_handle);
  }
}

uint8_t EepromRead8b(uint16_t _address)
{
  if (_address >= EEPROM_SIZE) return 0;
  return ~eeprom_buffer[_address];
}

uint16_t EepromRead16b(uint16_t _address)
{
  uint16_t _data;
  _data = EepromRead8b(_address);
  _data = (_data << 8) | EepromRead8b(_address + 1);
  return _data;
}

uint32_t EepromRead32b(uint16_t _address)
{
  uint32_t _data;
  _data = EepromRead16b(_address);
  _data = (_data << 16) | EepromRead16b(_address + 2);
  return _data;
}

float EepromReadFloat(uint16_t _address)
{
  union FloatUIntConvertor _fic;
  _fic._uint = EepromRead32b(_address);
  return _fic._float;
}

void EepromReadString(uint16_t _addr, char* _dest, uint16_t _maxSize)
{
  uint16_t _i;
  if (_maxSize == 0) return;
  for (_i = 0; _i < _maxSize; _i++) {
    _dest[_i] = EepromRead8b(_addr + _i);
    if (_dest[_i] == '\0') break;
  }
  if (_i == _maxSize) _dest[_i - 1] = '\0';
}

void EepromWrite8b(uint16_t _address, uint8_t _data)
{
  if (_address >= EEPROM_SIZE) return;
  eeprom_buffer[_address] = ~_data;
  EepromCommit();
}

void EepromWrite16b(uint16_t _address, uint16_t _data)
{
  uint16_t val = ~_data;
  if (_address + 1 >= EEPROM_SIZE) return;
  eeprom_buffer[_address]     = (val & 0xFF00) >> 8;
  eeprom_buffer[_address + 1] = val & 0x00FF;
  EepromCommit();
}

void EepromWrite32b(uint16_t _address, uint32_t _data)
{
  uint32_t val = ~_data;
  if (_address + 3 >= EEPROM_SIZE) return;
  eeprom_buffer[_address]     = (val & 0xFF000000) >> 24;
  eeprom_buffer[_address + 1] = (val & 0x00FF0000) >> 16;
  eeprom_buffer[_address + 2] = (val & 0x0000FF00) >> 8;
  eeprom_buffer[_address + 3] = val & 0x000000FF;
  EepromCommit();
}

void EepromWriteFloat(uint16_t _address, float _data)
{
  union FloatUIntConvertor _fic;
  _fic._float = _data;
  EepromWrite32b(_address, _fic._uint);
}

void EepromWriteString(uint16_t _addr, char* _source, uint16_t _maxSize)
{
  uint16_t _i;
  if (_maxSize == 0) return;
  for (_i = 0; _i < _maxSize; _i++) {
    if (_addr + _i >= EEPROM_SIZE) break;
    eeprom_buffer[_addr + _i] = ~_source[_i];
    if (_source[_i] == '\0') break;
  }
  if (_i == _maxSize) {
    if (_addr + _i - 1 < EEPROM_SIZE) {
      eeprom_buffer[_addr + _i - 1] = ~'\0';
    }
  }
  EepromCommit();
}

uint8_t CompareStringInEeprom(uint16_t _addr, char* _term)
{
  char _cont[100];
  EepromReadString(_addr, _cont, sizeof(_cont));
  if (strcmp(_cont, _term) == 0) return 1;
  return 0;
}
