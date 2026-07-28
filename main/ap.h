#pragma once
#ifndef __AP_H_
#define __AP_H_

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>

/* Define --------------------------------------------------------------------*/
#define AP_TIMEOUT (3 * 60 * 1000)  // Set timeout to 3 minutes

/* Variables -----------------------------------------------------------------*/
extern bool          ap_logged_in;
extern uint32_t      ap_timeout;
extern bool          ap_exit_pending;
extern uint32_t      ap_exit_at;

/* Functions -----------------------------------------------------------------*/
void ap_init(void);
void ap_process(void);
void ap_stop(void);
void ap_request_exit_to_normal();

#endif