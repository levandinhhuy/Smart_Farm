#pragma once

#ifndef __WIFI_H_
#define __WIFI_H_

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>

/* Define --------------------------------------------------------------------*/
#define WIFI_PERIOD  (2000 / 50)
#define WIFI_CONNECT (10000 / 50)

/* Variables -----------------------------------------------------------------*/
extern uint8_t is_wifi_connected;
extern uint8_t is_internet_connected;
extern uint8_t is_mqtt_connected;

extern bool is_scanning;
extern int scan_count;
#define MAX_SCAN_RECORDS 20
typedef struct {
    char ssid[33];
    int rssi;
} wifi_scan_record_t;
extern wifi_scan_record_t ap_records[MAX_SCAN_RECORDS];

/* Functions -----------------------------------------------------------------*/
void wifi_global_init(void);
void wifi_init(void);
void wifi_handler(void);

void wifi_process(void);
void mqtt_process(void);

void wifi_stop_for_ap(void);
void do_wifi_scan(void);
#endif