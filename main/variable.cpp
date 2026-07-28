/* Includes ------------------------------------------------------------------*/
#include "variable.h"
#include "memory.h"
#include <stdio.h>
#include <string.h>

/* Define --------------------------------------------------------------------*/

/* Variables -----------------------------------------------------------------*/
// address
uint16_t address_version     = 0;
uint16_t address_id          = 0;
uint16_t address_reset_count = 0;

uint16_t address_station_code = 0;

uint16_t address_wifi_count     = 0;
uint16_t address_wifi_ssids     = 0;
uint16_t address_wifi_passwords = 0;

uint16_t address_ap_ssid     = 0;
uint16_t address_ap_password = 0;

uint16_t address_mqtt_host      = 0;
uint16_t address_mqtt_username  = 0;
uint16_t address_mqtt_password  = 0;
uint16_t address_mqtt_topic_sub = 0;
uint16_t address_mqtt_topic_pub = 0;
uint16_t address_mqtt_port      = 0;

// variables
char     _id[64]     = ID_DEFAULT;
uint16_t _version    = VERSION_DEFAULT;
uint16_t reset_count = 0;

char station_code[17] = STATION_CODE;

uint8_t wifi_ssid_length     = strlen(WIFI_SSID_DEFAULT);
uint8_t wifi_password_length = strlen(WIFI_PASSWORD_DEFAULT);
char    wifi_ssid[33]        = WIFI_SSID_DEFAULT;
char    wifi_password[65]    = WIFI_PASSWORD_DEFAULT;

uint8_t wifi_count = 0;
char wifi_ssids[MAX_WIFI_NETWORKS][33]       = { "" };
char wifi_passwords[MAX_WIFI_NETWORKS][65]   = { "" };

char ap_ssid[33]     = AP_SSID_DEFAULT;
char ap_password[65] = AP_PASSWORD_DEFAULT;

char     mqtt_host[65]      = MQTT_HOST;
char     mqtt_username[65]  = MQTT_USERNAME;
char     mqtt_password[65]  = MQTT_PASSWORD;
char     mqtt_topic_sub[65] = MQTT_TOPIC_SUB;
char     mqtt_topic_pub[65] = MQTT_TOPIC_PUB;
uint32_t mqtt_port          = MQTT_PORT;

/* Functions -----------------------------------------------------------------*/
void set_address(uint16_t* startAddr, uint16_t* sourceAddr, uint16_t numOfAddr, uint16_t sizeOfPara)
{
  uint8_t  _i;
  uint16_t _addr = *startAddr;

  for (_i = 0; _i < numOfAddr; _i++)
  {
    *(sourceAddr + _i) = _addr;
    _addr              = _addr + sizeOfPara;
  }
  *startAddr = _addr;
}

void variable_init(void)
{
  uint16_t _addr = 0x0000;

  set_address(&_addr, &address_version, 1, sizeof(_version));
  set_address(&_addr, &address_id, 1, sizeof(_id));

  set_address(&_addr, &address_reset_count, 1, sizeof(reset_count));

  set_address(&_addr, &address_station_code, 1, sizeof(station_code));

  set_address(&_addr, &address_wifi_count, 1, sizeof(wifi_count));
  set_address(&_addr, &address_wifi_ssids, MAX_WIFI_NETWORKS, sizeof(wifi_ssids[0]));
  set_address(&_addr, &address_wifi_passwords, MAX_WIFI_NETWORKS, sizeof(wifi_passwords[0]));

  set_address(&_addr, &address_ap_ssid, 1, sizeof(ap_ssid));
  set_address(&_addr, &address_ap_password, 1, sizeof(ap_password));

  set_address(&_addr, &address_mqtt_host, 1, sizeof(mqtt_host));
  set_address(&_addr, &address_mqtt_username, 1, sizeof(mqtt_username));
  set_address(&_addr, &address_mqtt_password, 1, sizeof(mqtt_password));
  set_address(&_addr, &address_mqtt_topic_sub, 1, sizeof(mqtt_topic_sub));
  set_address(&_addr, &address_mqtt_topic_pub, 1, sizeof(mqtt_topic_pub));
  set_address(&_addr, &address_mqtt_port, 1, sizeof(mqtt_port));
}

void save_wifi(void)
{
  EepromWrite8b(address_wifi_count, wifi_count);
  for (int i = 0; i < MAX_WIFI_NETWORKS; i++)
  {
    EepromWriteString(address_wifi_ssids + i * 33, wifi_ssids[i], 33);
    EepromWriteString(address_wifi_passwords + i * 65, wifi_passwords[i], 65);
  }
}

void save_ap(void)
{
  EepromWriteString(address_ap_password, ap_password, sizeof(ap_password));
}

void save_mqtt(void)
{
  EepromWriteString(address_mqtt_host, mqtt_host, sizeof(mqtt_host));
  EepromWriteString(address_mqtt_username, mqtt_username, sizeof(mqtt_username));
  EepromWriteString(address_mqtt_password, mqtt_password, sizeof(mqtt_password));
  EepromWriteString(address_mqtt_topic_sub, mqtt_topic_sub, sizeof(mqtt_topic_sub));
  EepromWriteString(address_mqtt_topic_pub, mqtt_topic_pub, sizeof(mqtt_topic_pub));
  EepromWrite32b(address_mqtt_port, mqtt_port);
}

void save_id(void)
{
  EepromWriteString(address_id, _id, 11);
}

void save_station_code(void)
{
  EepromWriteString(address_station_code, station_code, sizeof(station_code));
}

void save_reset_count(void)
{
  EepromWrite16b(address_reset_count, reset_count);
}

void update_version(void)
{
  EepromWrite16b(address_version, _version);
}

void restore_wifi(void)
{
  wifi_count = EepromRead8b(address_wifi_count);
  if (wifi_count == 0 || wifi_count > MAX_WIFI_NETWORKS)
  {
    wifi_count = 1;
    strcpy(wifi_ssids[0], WIFI_SSID_DEFAULT);
    strcpy(wifi_passwords[0], WIFI_PASSWORD_DEFAULT);
    save_wifi();
  }
  else
  {
    for (int i = 0; i < MAX_WIFI_NETWORKS; i++)
    {
      EepromReadString(address_wifi_ssids + i * 33, wifi_ssids[i], 33);
      EepromReadString(address_wifi_passwords + i * 65, wifi_passwords[i], 65);
    } 
  }

  strcpy(wifi_ssid, wifi_ssids[0]);
  strcpy(wifi_password, wifi_passwords[0]);
  wifi_ssid_length = strlen(wifi_ssid);
  wifi_password_length = strlen(wifi_password);

  printf("res: \t [wifi-list-count] %d\n", wifi_count);
  for (int i = 0; i < wifi_count; i++)
  {
    printf("res: \t [SSID: %s\n", wifi_ssids[i]);
  }
}

void restore_mqtt(void)
{
  char read_mqtt_host[65];
  EepromReadString(address_mqtt_host, read_mqtt_host, 65);

  char read_mqtt_username[65];
  EepromReadString(address_mqtt_username, read_mqtt_username, 65);

  char read_mqtt_password[65];
  EepromReadString(address_mqtt_password, read_mqtt_password, 65);

  char read_mqtt_topic_sub[65];
  EepromReadString(address_mqtt_topic_sub, read_mqtt_topic_sub, 65);

  char read_mqtt_topic_pub[65];
  EepromReadString(address_mqtt_topic_pub, read_mqtt_topic_pub, 65);

  strcpy(mqtt_host, read_mqtt_host);
  strcpy(mqtt_username, read_mqtt_username);
  strcpy(mqtt_password, read_mqtt_password);
  strcpy(mqtt_topic_sub, read_mqtt_topic_sub);
  strcpy(mqtt_topic_pub, read_mqtt_topic_pub);
  mqtt_port = EepromRead32b(address_mqtt_port);

  printf("res: \t [mqtt] %s - %s - %s - %s - %s - %lu\n",
         mqtt_host, mqtt_username, mqtt_password, mqtt_topic_sub, mqtt_topic_pub, (unsigned long)mqtt_port);
}

void restore_id(void)
{
  char read_id[11];
  EepromReadString(address_id, read_id, 11);

  strcpy(_id, read_id);

  printf("res: \t [id] %s (%d)\n", _id, (int)strlen(_id));
}

void restore_reset_count(void)
{
  reset_count = EepromRead16b(address_reset_count);
  printf("res: \t [reset] %d\n", reset_count);
}

void restore_station_code(void)
{
  char read_station_code[17];
  EepromReadString(address_station_code, read_station_code, 17);

  strcpy(station_code, read_station_code);

  printf("res: \t [station code] %s (%d)\n", station_code, (int)strlen(station_code));
}

void restore_ap(void)
{
  char read_ap_password[65];
  EepromReadString(address_ap_password, read_ap_password, 65);

  strcpy(ap_password, read_ap_password);

  printf("res: \t [ap password] %s\n", ap_password);
}
