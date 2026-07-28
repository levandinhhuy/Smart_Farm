#include "ap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "_wifi.h"
#include "html/html.h"
#include "main_process.h"
#include "variable.h"

/* Variables -----------------------------------------------------------------*/
static httpd_handle_t server = NULL;

static char          ap_ssid_temp[32]     = "";
bool                 ap_logged_in         = false;
static uint32_t      ap_logged_in_timeout = 0;
uint32_t             ap_timeout           = 0;

bool                 ap_exit_pending      = false;
uint32_t             ap_exit_at           = 0;

/* Helper Functions ----------------------------------------------------------*/
static uint32_t millis(void)
{
  return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

// Helper to extract a value by key from a query/urlencoded string
static bool get_param_value(const char* buf, const char* key, char* val, size_t val_max)
{
  char key_eq[64];
  snprintf(key_eq, sizeof(key_eq), "%s=", key);
  const char* p = strstr(buf, key_eq);
  if (!p)
  {
    if (strncmp(buf, key_eq, strlen(key_eq)) == 0)
    {
      p = buf;
    }
    else
    {
      return false;
    }
  }
  else
  {
    p += strlen(key_eq);
  }
  
  size_t i = 0;
  while (*p && *p != '&' && i < val_max - 1)
  {
    val[i++] = *p++;
  }
  val[i] = '\0';
  
  // URL Decode
  char decoded[256];
  size_t d_idx = 0;
  for (size_t src = 0; val[src] && d_idx < sizeof(decoded) - 1; )
  {
    if (val[src] == '+')
    {
      decoded[d_idx++] = ' ';
      src++;
    }
    else if (val[src] == '%' && val[src+1] && val[src+2])
    {
      char hex[3] = { val[src+1], val[src+2], '\0' };
      decoded[d_idx++] = (char)strtol(hex, NULL, 16);
      src += 3;
    }
    else
    {
      decoded[d_idx++] = val[src++];
    }
  }
  decoded[d_idx] = '\0';
  strncpy(val, decoded, val_max - 1);
  val[val_max - 1] = '\0';
  return true;
}

/* HTTP Handlers -------------------------------------------------------------*/
static esp_err_t root_handler(httpd_req_t *req)
{
  ap_timeout = millis() + AP_TIMEOUT;
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, html_monitor, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t wifi_page_handler(httpd_req_t *req)
{
  ap_timeout = millis() + AP_TIMEOUT;
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, html_setting, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t login_page_handler(httpd_req_t *req)
{
  ap_timeout = millis() + AP_TIMEOUT;
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, html_login, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t admin_page_handler(httpd_req_t *req)
{
  ap_timeout = millis() + AP_TIMEOUT;
  httpd_resp_set_type(req, "text/html");
  if (ap_logged_in)
  {
    httpd_resp_send(req, html_admin, HTTPD_RESP_USE_STRLEN);
  }
  else
  {
    httpd_resp_send(req, html_login, HTTPD_RESP_USE_STRLEN);
  }
  return ESP_OK;
}

static esp_err_t normal_post_handler(httpd_req_t *req)
{
  printf("ap:\t[change to normal mode]\n");
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, html_normal, HTTPD_RESP_USE_STRLEN);
  ap_exit_pending = true;
  ap_exit_at      = millis() + 250;
  return ESP_OK;
}

static esp_err_t data_handler(httpd_req_t *req)
{
  ap_timeout = millis() + AP_TIMEOUT;
  char data[128];
  snprintf(data, sizeof(data), "%s,%s", wifi_ssid, ap_ssid_temp);
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_sendstr(req, data);
  return ESP_OK;
}

static esp_err_t get_admin_handler(httpd_req_t *req)
{
  ap_timeout = millis() + AP_TIMEOUT;
  char data[128];
  snprintf(data, sizeof(data), "%s,%s", _id, station_code);
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_sendstr(req, data);
  return ESP_OK;
}

static esp_err_t scan_wifi_handler(httpd_req_t *req)
{
  if (scan_count == -1 && !is_scanning)
  {
    do_wifi_scan();
    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "[]");
    return ESP_OK;
  }
  if (is_scanning)
  {
    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "[]");
    return ESP_OK;
  }
  
  char* json = (char*)malloc(2048);
  strcpy(json, "[");
  for (int i = 0; i < scan_count; i++)
  {
    if (i > 0) strcat(json, ",");
    char item[128];
    snprintf(item, sizeof(item), "{\"ssid\":\"%s\",\"rssi\":%d}", ap_records[i].ssid, ap_records[i].rssi);
    strcat(json, item);
  }
  strcat(json, "]");
  
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json);
  free(json);
  
  scan_count = -1;
  return ESP_OK;
}

static esp_err_t get_wifi_list_handler(httpd_req_t *req)
{
  ap_timeout = millis() + AP_TIMEOUT;
  char* json = (char*)malloc(1024);
  strcpy(json, "[");
  for (int i = 0; i < wifi_count; i++)
  {
    if (i > 0) strcat(json, ",");
    char item[64];
    snprintf(item, sizeof(item), "{\"ssid\":\"%s\"}", wifi_ssids[i]);
    strcat(json, item);
  }
  strcat(json, "]");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json);
  free(json);
  return ESP_OK;
}

static esp_err_t delete_wifi_handler(httpd_req_t *req)
{
  ap_timeout = millis() + AP_TIMEOUT;
  char buf[128];
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0)
  {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing body");
    return ESP_FAIL;
  }
  buf[ret] = '\0';
  
  char ssid_to_delete[33];
  if (get_param_value(buf, "ssid", ssid_to_delete, sizeof(ssid_to_delete)))
  {
    int delete_index = -1;
    for (int i = 0; i < wifi_count; i++)
    {
      if (strcmp(ssid_to_delete, wifi_ssids[i]) == 0)
      {
        delete_index = i;
        break;
      }
    }
    if (delete_index != -1)
    {
      for (int i = delete_index; i < wifi_count - 1; i++)
      {
        strcpy(wifi_ssids[i], wifi_ssids[i + 1]);
        strcpy(wifi_passwords[i], wifi_passwords[i + 1]);
      }
      wifi_count--;
      save_wifi();
      httpd_resp_sendstr(req, "Deleted WiFi successfully");
    }
    else
    {
      httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "WiFi not found");
    }
  }
  else
  {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ssid parameter");
  }
  return ESP_OK;
}

static esp_err_t update_wifi_handler(httpd_req_t *req)
{
  ap_timeout = millis() + AP_TIMEOUT;
  char buf[256];
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0)
  {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing body");
    return ESP_FAIL;
  }
  buf[ret] = '\0';
  
  char newSSID[33];
  char newPassword[65];
  if (get_param_value(buf, "new_ssid", newSSID, sizeof(newSSID)) &&
      get_param_value(buf, "new_password", newPassword, sizeof(newPassword)))
  {
    bool wifi_exists = false;
    for (int i = 0; i < wifi_count; i++)
    {
      if (strcmp(wifi_ssids[i], newSSID) == 0)
      {
        strncpy(wifi_passwords[i], newPassword, sizeof(wifi_passwords[i]) - 1);
        wifi_passwords[i][sizeof(wifi_passwords[i]) - 1] = '\0';
        wifi_exists                                      = true;
        break;
      }
    }

    if (!wifi_exists)
    {
      if (wifi_count < MAX_WIFI_NETWORKS)
      {
        strncpy(wifi_ssids[wifi_count], newSSID, sizeof(wifi_ssids[wifi_count]) - 1);
        wifi_ssids[wifi_count][sizeof(wifi_ssids[wifi_count]) - 1] = '\0';
        strncpy(wifi_passwords[wifi_count], newPassword, sizeof(wifi_passwords[wifi_count]) - 1);
        wifi_passwords[wifi_count][sizeof(wifi_passwords[wifi_count]) - 1] = '\0';
        wifi_count++;
      }
      else
      {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "WiFi list is full! Please delete some networks first.");
        return ESP_OK;
      }
    }

    save_wifi();
    printf("ap:\t[new wifi added/updated] ssid=%s; count=%d\n", newSSID, wifi_count);
    httpd_resp_sendstr(req, "Update WiFi successful");
  }
  else
  {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing parameters");
  }
  return ESP_OK;
}

static esp_err_t check_login_handler(httpd_req_t *req)
{
  char buf[256];
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0)
  {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing body");
    return ESP_FAIL;
  }
  buf[ret] = '\0';
  
  char username[33];
  char password[65];
  if (get_param_value(buf, "username", username, sizeof(username)) &&
      get_param_value(buf, "password", password, sizeof(password)))
  {
    if (strcmp(username, AP_ADMIN_USERNAME) == 0 && strcmp(password, AP_ADMIN_PASSWORD) == 0)
    {
      printf("ap:\t[check login] OK\n");
      ap_logged_in         = true;
      ap_logged_in_timeout = millis() + (30UL * 60UL * 1000UL);
      httpd_resp_sendstr(req, "Login successful");
    }
    else
    {
      printf("ap:\t[check login] Fail: username=%s; password=%s\n", username, password);
      httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Login failed");
    }
  }
  else
  {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing parameters");
  }
  ap_timeout = millis() + AP_TIMEOUT;
  return ESP_OK;
}

static esp_err_t update_admin_handler(httpd_req_t *req)
{
  if (!ap_logged_in)
  {
    printf("ap:\t[updateAdmin] have not logged in\n");
    httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Have not logged in");
    return ESP_OK;
  }
  
  char buf[256];
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0)
  {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing body");
    return ESP_FAIL;
  }
  buf[ret] = '\0';
  
  char new_id[64];
  char new_station_code[32];
  if (get_param_value(buf, "id", new_id, sizeof(new_id)) &&
      get_param_value(buf, "station_code", new_station_code, sizeof(new_station_code)))
  {
    if (strcmp(new_id, "NULL") != 0 && strlen(new_id) == 10)
    {
      strncpy(_id, new_id, sizeof(_id) - 1);
      _id[sizeof(_id) - 1] = '\0';
      printf("ap:\t[id] %s\n", _id);
      save_id();
    }
    if (strcmp(new_station_code, "NULL") != 0)
    {
      strncpy(station_code, new_station_code, sizeof(station_code) - 1);
      station_code[sizeof(station_code) - 1] = '\0';
      printf("ap:\t[station_code] %s\n", station_code);
      save_station_code();
    }
    httpd_resp_sendstr(req, "Update admin successful");
  }
  else
  {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing parameters");
  }
  ap_timeout = millis() + AP_TIMEOUT;
  return ESP_OK;
}

/* Functions -----------------------------------------------------------------*/
void ap_init(void)
{
  ap_timeout   = millis() + AP_TIMEOUT;
  ap_logged_in = false;

  // Initialize WiFi globally and configure APSTA mode
  wifi_global_init();
  esp_wifi_disconnect();
  esp_wifi_stop();
  vTaskDelay(pdMS_TO_TICKS(10));

  strncpy(ap_ssid, _id, sizeof(ap_ssid) - 1);
  ap_ssid[sizeof(ap_ssid) - 1] = '\0';
  snprintf(ap_ssid_temp, sizeof(ap_ssid_temp), "%.16s %.14s", DEVICE_NAME, _id);

  wifi_config_t ap_config = {};
  strncpy((char*)ap_config.ap.ssid, ap_ssid, sizeof(ap_config.ap.ssid) - 1);
  strncpy((char*)ap_config.ap.password, ap_password, sizeof(ap_config.ap.password) - 1);
  ap_config.ap.ssid_len = strlen((char*)ap_config.ap.ssid);
  ap_config.ap.channel = 1;
  ap_config.ap.authmode = (strlen(ap_password) == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
  ap_config.ap.max_connection = 4;

  esp_wifi_set_mode(WIFI_MODE_APSTA);
  esp_wifi_set_config(WIFI_IF_AP, &ap_config);
  esp_wifi_start();
  
  vTaskDelay(pdMS_TO_TICKS(10));
  printf("ap:\t[init] AP started SSID=%s\n", ap_ssid);

  // Start HTTP Server
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 16;
  
  if (httpd_start(&server, &config) == ESP_OK)
  {
    httpd_uri_t uri_root_get = { .uri = "/", .method = HTTP_GET, .handler = root_handler, .user_ctx = NULL };
    httpd_register_uri_handler(server, &uri_root_get);
    httpd_uri_t uri_root_post = { .uri = "/", .method = HTTP_POST, .handler = root_handler, .user_ctx = NULL };
    httpd_register_uri_handler(server, &uri_root_post);
    
    httpd_uri_t uri_wifi_get = { .uri = "/wifi", .method = HTTP_GET, .handler = wifi_page_handler, .user_ctx = NULL };
    httpd_register_uri_handler(server, &uri_wifi_get);
    httpd_uri_t uri_wifi_post = { .uri = "/wifi", .method = HTTP_POST, .handler = wifi_page_handler, .user_ctx = NULL };
    httpd_register_uri_handler(server, &uri_wifi_post);
    
    httpd_uri_t uri_scan = { .uri = "/scan_wifi", .method = HTTP_GET, .handler = scan_wifi_handler, .user_ctx = NULL };
    httpd_register_uri_handler(server, &uri_scan);
    
    httpd_uri_t uri_list = { .uri = "/getWiFiList", .method = HTTP_GET, .handler = get_wifi_list_handler, .user_ctx = NULL };
    httpd_register_uri_handler(server, &uri_list);
    
    httpd_uri_t uri_del = { .uri = "/deleteWiFi", .method = HTTP_POST, .handler = delete_wifi_handler, .user_ctx = NULL };
    httpd_register_uri_handler(server, &uri_del);
    
    httpd_uri_t uri_login_get = { .uri = "/login", .method = HTTP_GET, .handler = login_page_handler, .user_ctx = NULL };
    httpd_register_uri_handler(server, &uri_login_get);
    httpd_uri_t uri_login_post = { .uri = "/login", .method = HTTP_POST, .handler = login_page_handler, .user_ctx = NULL };
    httpd_register_uri_handler(server, &uri_login_post);
    
    httpd_uri_t uri_admin = { .uri = "/admin", .method = HTTP_GET, .handler = admin_page_handler, .user_ctx = NULL };
    httpd_register_uri_handler(server, &uri_admin);
    
    httpd_uri_t uri_normal = { .uri = "/normal", .method = HTTP_POST, .handler = normal_post_handler, .user_ctx = NULL };
    httpd_register_uri_handler(server, &uri_normal);
    
    httpd_uri_t uri_data = { .uri = "/data", .method = HTTP_GET, .handler = data_handler, .user_ctx = NULL };
    httpd_register_uri_handler(server, &uri_data);
    
    httpd_uri_t uri_getadmin = { .uri = "/getAdmin", .method = HTTP_GET, .handler = get_admin_handler, .user_ctx = NULL };
    httpd_register_uri_handler(server, &uri_getadmin);
    
    httpd_uri_t uri_updatewifi = { .uri = "/updateWiFi", .method = HTTP_POST, .handler = update_wifi_handler, .user_ctx = NULL };
    httpd_register_uri_handler(server, &uri_updatewifi);
    
    httpd_uri_t uri_checklogin = { .uri = "/checkLogin", .method = HTTP_POST, .handler = check_login_handler, .user_ctx = NULL };
    httpd_register_uri_handler(server, &uri_checklogin);
    
    httpd_uri_t uri_updateadmin = { .uri = "/updateAdmin", .method = HTTP_POST, .handler = update_admin_handler, .user_ctx = NULL };
    httpd_register_uri_handler(server, &uri_updateadmin);
  }
  
  vTaskDelay(pdMS_TO_TICKS(10));
}

void ap_stop(void)
{
  if (server)
  {
    httpd_stop(server);
    server = NULL;
  }
  esp_wifi_stop();
  vTaskDelay(pdMS_TO_TICKS(120));
}

void ap_request_exit_to_normal()
{
  ap_exit_pending = true;
  ap_exit_at      = millis() + 250;
}

void ap_process(void)
{
  static uint32_t prev = 0;
  uint32_t        now  = millis();
  if (now - prev < 50)
    return;
  prev = now;

  if (ap_exit_pending && (int32_t)(millis() - ap_exit_at) >= 0)
  {
    ap_exit_pending = false;
    ap_stop();
    main_process_state = NORMAL;
    main_process_reset_state(NORMAL);
    return;
  }

  // Session timeout checking
  if (ap_logged_in && (int32_t)(millis() - ap_logged_in_timeout) >= 0)
  {
    ap_logged_in = false;
    printf("ap:\t[session] login session timeout\n");
  }

  // AP Mode Timeout Checking
  if ((int32_t)(millis() - ap_timeout) >= 0)
  {
    printf("ap:\t[timeout] exit AP mode to Normal mode\n");
    ap_stop();
    main_process_state = NORMAL;
    main_process_reset_state(NORMAL);
  }
}
