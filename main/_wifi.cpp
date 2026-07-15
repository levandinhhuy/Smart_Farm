#include "_wifi.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "mqtt_client.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "cJSON.h"

#include "led.h"
#include "main_process.h"
#include "rtc.h"
#include "sensor.h"
#include "variable.h"

/* Config --------------------------------------------------------------------*/
#define CHECK_INTERNET_CONNECTION_PERIOD 500   // ms
#define NETCHECK_INTERVAL_MS             3000  // ms
#define WIFI_RETRY_COOLDOWN_MS           3000  // ms
#define MQTT_BACKOFF_MAX_S               30

/* FSM Enum ------------------------------------------------------------------*/
typedef enum
{
  WIFI_INIT,
  SCANNING_WIFI,
  CONNECT_WIFI,
  CONNECT_INTERNET,
  CONNECT_MQTT,
  MQTT_LOOP
} E_WIFI_PROCESS_STATE;

/* State/IO ------------------------------------------------------------------*/
uint8_t is_wifi_connected     = 0;
uint8_t is_internet_connected = 0;
uint8_t is_mqtt_connected     = 0;

static bool wifi_global_initialized = false;
static bool wifi_started            = false;
static esp_mqtt_client_handle_t mqtt_client = NULL;
static const char* TAG = "WIFI_MQTT";

/* Throttle & backoff --------------------------------------------------------*/
static uint32_t next_wifi_try_ms = 0;
static uint32_t next_mqtt_try_ms = 0;
static uint32_t next_netcheck_ms = 0;
static uint8_t  mqtt_backoff_s   = 2;

/* Scanning variables --------------------------------------------------------*/
bool is_scanning = false;
int scan_count = -1;
wifi_scan_record_t ap_records[MAX_SCAN_RECORDS];

/* Protos --------------------------------------------------------------------*/
static bool check_internet_connection(void);
static void mqtt_publish_process(void);
static void mqtt_callback(const std::string& topic, const std::string& payload);
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static void mqtt_process_reset_state(E_WIFI_PROCESS_STATE state);

/* Helper Functions ----------------------------------------------------------*/
static uint32_t millis(void)
{
  return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

/* WiFi Event Handler --------------------------------------------------------*/
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
  {
    esp_wifi_connect();
  }
  else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
  {
    is_wifi_connected = 0;
    is_internet_connected = 0;
    is_mqtt_connected = 0;
    ESP_LOGI(TAG, "wifi: STA disconnected");
  }
  else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
  {
    is_wifi_connected = 1;
    ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
    ESP_LOGI(TAG, "wifi: got ip: " IPSTR, IP2STR(&event->ip_info.ip));
  }
  else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE)
  {
    uint16_t ap_count = MAX_SCAN_RECORDS;
    wifi_ap_record_t raw_records[MAX_SCAN_RECORDS];
    esp_wifi_scan_get_ap_records(&ap_count, raw_records);
    scan_count = ap_count;
    is_scanning = false;
    for (int i = 0; i < scan_count; i++)
    {
      strncpy(ap_records[i].ssid, (char*)raw_records[i].ssid, 32);
      ap_records[i].ssid[32] = '\0';
      ap_records[i].rssi = raw_records[i].rssi;
    }
    ESP_LOGI(TAG, "wifi: scan done, found %d APs", scan_count);
  }
}

void wifi_global_init(void)
{
  if (wifi_global_initialized)
    return;
    
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();
  esp_netif_create_default_wifi_ap();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

  wifi_global_initialized = true;
  ESP_LOGI(TAG, "wifi: global init ok");
}

/* WiFi Scan Helper ----------------------------------------------------------*/
void do_wifi_scan(void)
{
  if (is_scanning) return;
  is_scanning = true;
  scan_count = -1;
  wifi_scan_config_t scan_config = {};
  scan_config.show_hidden = false;
  scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  
  esp_wifi_scan_start(&scan_config, false); // non-blocking
}

/* Internet probe ------------------------------------------------------------*/
static bool check_internet_connection(void)
{
  esp_http_client_config_t config = {};
  config.url = "http://www.google.com/generate_204";
  config.timeout_ms = CHECK_INTERNET_CONNECTION_PERIOD;
  
  esp_http_client_handle_t client = esp_http_client_init(&config);
  esp_err_t err = esp_http_client_perform(client);
  bool ok = false;
  
  if (err == ESP_OK)
  {
    int status = esp_http_client_get_status_code(client);
    if (status == 204)
    {
      ok = true;
    }
  }
  esp_http_client_cleanup(client);
  is_internet_connected = ok ? 1 : 0;
  
  if (ok)
  {
    printf("wifi:\tInternet OK\n");
  }
  else
  {
    printf("wifi:\tInternet NOT OK\n");
  }
  return ok;
}

/* Bring-up STA cleanly ------------------------------------------------------*/
void wifi_init(void)
{
  if (wifi_started)
  {
    printf("wifi:\t[init skipped]\n");
    return;
  }
  wifi_started = true;

  printf("wifi:\t[init] %s (%d) - %s (%d)\n", wifi_ssid, (int)wifi_ssid_length, wifi_password, (int)wifi_password_length);

  wifi_global_init();
  esp_wifi_disconnect();
  esp_wifi_stop();
  vTaskDelay(pdMS_TO_TICKS(150));

  wifi_config_t wifi_config = {};
  strncpy((char*)wifi_config.sta.ssid, wifi_ssid, sizeof(wifi_config.sta.ssid) - 1);
  strncpy((char*)wifi_config.sta.password, wifi_password, sizeof(wifi_config.sta.password) - 1);
  wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
  esp_wifi_start();

  printf("wifi:\tconnect to wifi\n");
}

/* Periodic flags ------------------------------------------------------------*/
void wifi_handler(void)
{
  static uint32_t prev = 0;
  uint32_t        now  = millis();
  if (now - prev < 1000)
    return;
  prev = now;

  wifi_ap_record_t ap_info;
  is_wifi_connected = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) ? 1 : 0;

  if (!is_wifi_connected)
  {
    led_toggle_1s();
    is_internet_connected = 0;
    is_mqtt_connected     = 0;
  }
  else
  {
    led_on();
  }
}

void wifi_stop_for_ap(void)
{
  if (mqtt_client)
  {
    esp_mqtt_client_disconnect(mqtt_client);
    esp_mqtt_client_destroy(mqtt_client);
    mqtt_client = NULL;
  }
  is_mqtt_connected = 0;
}

/* FSM helpers ---------------------------------------------------------------*/
static void mqtt_process_reset_state(E_WIFI_PROCESS_STATE state)
{
  switch (state)
  {
    case CONNECT_WIFI:
      {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK && millis() >= next_wifi_try_ms)
        {
          esp_wifi_disconnect();
          esp_wifi_connect();
          next_wifi_try_ms = millis() + WIFI_RETRY_COOLDOWN_MS;
          printf("wifi:\tconnect to wifi\n");
        }
      }
      break;

    case SCANNING_WIFI:
      printf("wifi:\tscanning surrounding networks...\n");
      esp_wifi_disconnect();
      do_wifi_scan();
      next_wifi_try_ms = millis() + 10000; // 10s scan timeout
      break;

    case CONNECT_INTERNET:
      next_netcheck_ms = millis();
      printf("wifi:\tconnect to internet\n");
      break;

    case CONNECT_MQTT:
      printf("wifi:\tprepare mqtt %s : %d\n", mqtt_host, (int)mqtt_port);
      break;

    case MQTT_LOOP:
      if (mqtt_client)
      {
        esp_mqtt_client_subscribe(mqtt_client, mqtt_topic_sub, 0);
      }
      printf("wifi:\tmqtt loop\n");
      break;

    default:
      break;
  }
}

/* FSM main ------------------------------------------------------------------*/
void mqtt_process(void)
{
  static E_WIFI_PROCESS_STATE wifi_state = WIFI_INIT;

  static uint32_t prev = 0;
  uint32_t        now  = millis();
  if (now - prev < 50)
    return;
  prev = now;

  switch (wifi_state)
  {
    case WIFI_INIT:
      printf("wifi:\t[init]\n");
      wifi_state = SCANNING_WIFI;
      mqtt_process_reset_state(SCANNING_WIFI);
      break;

    case SCANNING_WIFI:
      {
        if (scan_count >= 0)
        {
          int best_index = -1;
          int best_rssi  = -100;

          for (int i = 0; i < scan_count; i++)
          {
            std::string scanned_ssid(ap_records[i].ssid);
            for (int j = 0; j < wifi_count; j++)
            {
              if (scanned_ssid == std::string(wifi_ssids[j]))
              {
                int rssi = ap_records[i].rssi;
                if (rssi > best_rssi)
                {
                  best_rssi  = rssi;
                  best_index = j;
                }
              }
            }
          }

          if (best_index != -1)
          {
            printf("wifi:\tfound best SSID: %s (RSSI=%d)\n", wifi_ssids[best_index], best_rssi);

            strncpy(wifi_ssid, wifi_ssids[best_index], sizeof(wifi_ssid) - 1);
            wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';
            strncpy(wifi_password, wifi_passwords[best_index], sizeof(wifi_password) - 1);
            wifi_password[sizeof(wifi_password) - 1] = '\0';
            wifi_ssid_length     = strlen(wifi_ssid);
            wifi_password_length = strlen(wifi_password);

            wifi_config_t wifi_config = {};
            strncpy((char*)wifi_config.sta.ssid, wifi_ssid, sizeof(wifi_config.sta.ssid) - 1);
            strncpy((char*)wifi_config.sta.password, wifi_password, sizeof(wifi_config.sta.password) - 1);
            
            esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
            esp_wifi_connect();

            wifi_state = CONNECT_WIFI;
            next_wifi_try_ms = millis() + 15000; // 15s connection timeout
          }
          else
          {
            printf("wifi:\tno known SSID found, waiting cooldown...\n");
            next_wifi_try_ms = millis() + WIFI_RETRY_COOLDOWN_MS;
            scan_count = -1;
          }
        }
        else if (millis() >= next_wifi_try_ms)
        {
          printf("wifi:\tscan timeout or cooldown finished, rescanning...\n");
          mqtt_process_reset_state(SCANNING_WIFI);
        }
      }
      break;
    
    case CONNECT_WIFI:
      {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
        {
          printf("wifi:\twifi connected\n");
          wifi_state = CONNECT_INTERNET;
          mqtt_process_reset_state(CONNECT_INTERNET);
        }
        else if (millis() >= next_wifi_try_ms)
        {
          printf("wifi:\tconnect timeout, scanning again...\n");
          wifi_state = SCANNING_WIFI;
          mqtt_process_reset_state(SCANNING_WIFI);
        }
      }
      break;

    case CONNECT_INTERNET:
      {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK)
        {
          printf("wifi:\twifi disconnected\n");
          wifi_state = SCANNING_WIFI;
          mqtt_process_reset_state(SCANNING_WIFI);
          break;
        }

        if (millis() >= next_netcheck_ms)
        {
          if (check_internet_connection())
          {
            printf("wifi:\tinternet connected\n");
            mqtt_backoff_s   = 2;
            next_mqtt_try_ms = millis();
            wifi_state = CONNECT_MQTT;
            mqtt_process_reset_state(CONNECT_MQTT);
          }
          else
          {
            printf("wifi:\tinternet NOT connected\n");
            next_netcheck_ms = millis() + NETCHECK_INTERVAL_MS;
          }
        }
      }
      break;

    case CONNECT_MQTT:
      {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK)
        {
          printf("wifi:\twifi disconnected\n");
          wifi_state = SCANNING_WIFI;
          mqtt_process_reset_state(SCANNING_WIFI);
          break;
        }

        if (!is_mqtt_connected && millis() >= next_mqtt_try_ms)
        {
          printf("wifi:\tconnect to mqtt %s : %d\n", mqtt_host, (int)mqtt_port);
          if (mqtt_client)
          {
            esp_mqtt_client_destroy(mqtt_client);
          }
          char uri[128];
          snprintf(uri, sizeof(uri), "mqtt://%s:%lu", mqtt_host, (unsigned long)mqtt_port);
          
          esp_mqtt_client_config_t mqtt_cfg = {};
          mqtt_cfg.broker.address.uri = uri;
          mqtt_cfg.credentials.client_id = _id;
          mqtt_cfg.credentials.username = mqtt_username;
          mqtt_cfg.credentials.authentication.password = mqtt_password;
          
          mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
          esp_mqtt_client_register_event(mqtt_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);
          esp_mqtt_client_start(mqtt_client);
          
          next_mqtt_try_ms = millis() + 5000; // 5s connection check timeout
        }

        if (is_mqtt_connected)
        {
          printf("wifi:\tmqtt connected (%s)\n", mqtt_username);
          wifi_state = MQTT_LOOP;
          mqtt_process_reset_state(MQTT_LOOP);
        }
      }
      break;

    case MQTT_LOOP:
      {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK)
        {
          printf("wifi:\twifi disconnected\n");
          wifi_state = SCANNING_WIFI;
          mqtt_process_reset_state(SCANNING_WIFI);
          break;
        }
        if (!is_mqtt_connected)
        {
          printf("wifi:\tmqtt is not connected\n");
          wifi_state = CONNECT_MQTT;
          break;
        }

        mqtt_publish_process();
      }
      break;

    default:
      wifi_state = WIFI_INIT;
      printf("wifi:\tunknown state\n");
      break;
  }
}

/* MQTT Event Handler --------------------------------------------------------*/
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
  switch ((esp_mqtt_event_id_t)event_id)
  {
    case MQTT_EVENT_CONNECTED:
      is_mqtt_connected = 1;
      esp_mqtt_client_subscribe(mqtt_client, mqtt_topic_sub, 0);
      break;
    case MQTT_EVENT_DISCONNECTED:
      is_mqtt_connected = 0;
      break;
    case MQTT_EVENT_DATA:
      {
        std::string topic(event->topic, event->topic_len);
        std::string payload(event->data, event->data_len);
        mqtt_callback(topic, payload);
      }
      break;
    default:
      break;
  }
}

/* MQTT callbacks/publish ----------------------------------------------------*/
static void mqtt_callback(const std::string& topic, const std::string& payload)
{
  printf("mqtt:\t[subscribed] %s:\n%s\n", topic.c_str(), payload.c_str());

  cJSON *root = cJSON_Parse(payload.c_str());
  if (!root)
  {
    printf("mqtt:\tJSON parse failed\n");
    return;
  }

  cJSON *cmd_item = cJSON_GetObjectItem(root, "cmd");
  if (cmd_item && strcmp(cmd_item->valuestring, "update_config") == 0)
  {
    bool has_changed = false;

    // 1. Configure Wi-Fi list
    cJSON *wifi = cJSON_GetObjectItem(root, "wifi");
    if (wifi)
    {
      cJSON *ssid_item = cJSON_GetObjectItem(wifi, "ssid");
      cJSON *pass_item = cJSON_GetObjectItem(wifi, "pass");

      if (ssid_item && strlen(ssid_item->valuestring) > 0)
      {
        bool wifi_exists = false;
        for (int i = 0; i < wifi_count; i++)
        {
          if (strcmp(wifi_ssids[i], ssid_item->valuestring) == 0)
          {
            if (pass_item)
            {
              strncpy(wifi_passwords[i], pass_item->valuestring, sizeof(wifi_passwords[i]) - 1);
              wifi_passwords[i][sizeof(wifi_passwords[i]) - 1] = '\0';
            }
            wifi_exists = true;
            break;
          }
        }
        if (!wifi_exists)
        {
          if (wifi_count < MAX_WIFI_NETWORKS)
          {
            strncpy(wifi_ssids[wifi_count], ssid_item->valuestring, sizeof(wifi_ssids[wifi_count]) - 1);
            wifi_ssids[wifi_count][sizeof(wifi_ssids[wifi_count]) - 1] = '\0';
            if (pass_item)
            {
              strncpy(wifi_passwords[wifi_count], pass_item->valuestring, sizeof(wifi_passwords[wifi_count]) - 1);
              wifi_passwords[wifi_count][sizeof(wifi_passwords[wifi_count]) - 1] = '\0';
            }
            else
            {
              wifi_passwords[wifi_count][0] = '\0';
            }
            wifi_count++;
          }
          else
          {
            printf("mqtt:\tWiFi list is full!\n");
          }
        }
        has_changed = true;
      }
    }

    // 2. Configure MQTT Settings
    cJSON *mqtt = cJSON_GetObjectItem(root, "mqtt");
    if (mqtt)
    {
      cJSON *m_host = cJSON_GetObjectItem(mqtt, "host");
      cJSON *m_port = cJSON_GetObjectItem(mqtt, "port");
      cJSON *m_user = cJSON_GetObjectItem(mqtt, "user");
      cJSON *m_pass = cJSON_GetObjectItem(mqtt, "pass");

      if (m_host)
      {
        strncpy(mqtt_host, m_host->valuestring, sizeof(mqtt_host) - 1);
        mqtt_host[sizeof(mqtt_host) - 1] = '\0';
        has_changed = true;
      }
      if (m_port)
      {
        mqtt_port   = m_port->valueint;
        has_changed = true;
      }
      if (m_user)
      {
        strncpy(mqtt_username, m_user->valuestring, sizeof(mqtt_username) - 1);
        mqtt_username[sizeof(mqtt_username) - 1] = '\0';
        has_changed = true;
      }
      if (m_pass)
      {
        strncpy(mqtt_password, m_pass->valuestring, sizeof(mqtt_password) - 1);
        mqtt_password[sizeof(mqtt_password) - 1] = '\0';
        has_changed = true;
      }
    }

    // 3. Configure General Device Settings
    cJSON *id_item = cJSON_GetObjectItem(root, "id");
    if (id_item && strlen(id_item->valuestring) == 10)
    {
      strncpy(_id, id_item->valuestring, sizeof(_id) - 1);
      _id[sizeof(_id) - 1] = '\0';
      save_id();
      has_changed = true;
    }
    
    cJSON *sc_item = cJSON_GetObjectItem(root, "station_code");
    if (sc_item && strlen(sc_item->valuestring) > 0)
    {
      strncpy(station_code, sc_item->valuestring, sizeof(station_code) - 1);
      station_code[sizeof(station_code) - 1] = '\0';
      save_station_code();
      has_changed = true;
    }

    // 4. Save changes and reply
    if (has_changed)
    {
      save_wifi();
      save_mqtt();
      printf("mqtt:\tConfiguration changed and saved to EEPROM.\n");

      cJSON *reply = cJSON_CreateObject();
      cJSON_AddStringToObject(reply, "status", "success");
      cJSON_AddStringToObject(reply, "message", "Configuration updated successfully");
      char *replyStr = cJSON_PrintUnformatted(reply);
      esp_mqtt_client_publish(mqtt_client, mqtt_topic_pub, replyStr, 0, 1, 0);
      free(replyStr);
      cJSON_Delete(reply);

      cJSON *reboot_item = cJSON_GetObjectItem(root, "reboot");
      bool reboot = (reboot_item) ? cJSON_IsTrue(reboot_item) : true;
      if (reboot)
      {
        printf("mqtt:\tRebooting in 1 second to apply changes...\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
      }
    }
  }
  cJSON_Delete(root);
}

void mqtt_pub_test()
{
  cJSON *json_doc = cJSON_CreateObject();
  cJSON_AddNumberToObject(json_doc, "reset", reset_count);
  cJSON_AddStringToObject(json_doc, "id", _id);

  char buf[32];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d", (int)rtc_year, (int)rtc_month, (int)rtc_day, (int)rtc_hour, (int)rtc_minute, (int)rtc_second);
  cJSON_AddStringToObject(json_doc, "rtc", buf);

  // --- DHT13 block ---
  cJSON *dht = cJSON_AddObjectToObject(json_doc, "dht");
  cJSON_AddBoolToObject(dht, "valid", g_dht13.valid);
  cJSON_AddNumberToObject(dht, "temperature_c", g_dht13.temperature_c);
  cJSON_AddNumberToObject(dht, "humidity_pct", g_dht13.humidity_pct);

  // --- Light block ---
  cJSON *light = cJSON_AddObjectToObject(json_doc, "light");
  cJSON_AddBoolToObject(light, "valid", g_light.valid);
  cJSON_AddNumberToObject(light, "intensity", g_light.intensity);

  // --- Rain block ---
  cJSON *rain = cJSON_AddObjectToObject(json_doc, "rain");
  cJSON_AddBoolToObject(rain, "valid", g_rain.valid);
  cJSON_AddNumberToObject(rain, "status", g_rain.status);
  cJSON_AddNumberToObject(rain, "lower_limit", g_rain.lower_limit);
  cJSON_AddNumberToObject(rain, "hysteresis", g_rain.hysteresis);
  cJSON_AddNumberToObject(rain, "reset_delay", g_rain.reset_delay);
  cJSON_AddNumberToObject(rain, "sensitivity", g_rain.sensitivity);

  // --- TDS block ---
  cJSON *tds = cJSON_AddObjectToObject(json_doc, "tds");
  cJSON_AddBoolToObject(tds, "valid", g_tds.valid);
  cJSON_AddNumberToObject(tds, "temperature_c", g_tds.temperature_c);
  cJSON_AddNumberToObject(tds, "ec", g_tds.ec);
  cJSON_AddNumberToObject(tds, "salinity", g_tds.salinity);
  cJSON_AddNumberToObject(tds, "tds", g_tds.tds);
  cJSON_AddNumberToObject(tds, "ec_raw", g_tds.ec_raw);

  char *json_string = cJSON_PrintUnformatted(json_doc);

  if (is_mqtt_connected && mqtt_client)
  {
    int msg_id = esp_mqtt_client_publish(mqtt_client, mqtt_topic_pub, json_string, 0, 1, 0);
    printf("mqtt:\t[publish] result=%d\n", msg_id >= 0);
  }
  else
  {
    printf("mqtt:\t[publish fail] not connected\n");
  }

  free(json_string);
  cJSON_Delete(json_doc);
}

static void mqtt_publish_process(void)
{
  if (!is_mqtt_connected || !mqtt_client)
    return;

  static uint32_t last_pub = 0;
  uint32_t        now      = millis();

  if (now - last_pub >= 10000)
  {
    last_pub = now;
    mqtt_pub_test();
  }
}