#pragma once
#ifndef __BUTTON_H_
#define __BUTTON_H_

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

/* Define --------------------------------------------------------------------*/
#define BUTTON_PIN 14

/* Variables -----------------------------------------------------------------*/
extern uint32_t key_code;
extern uint32_t key_code_before_releasing;

/* Functions -----------------------------------------------------------------*/
void button_init();

#endif