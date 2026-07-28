#pragma once
#ifndef __LED_H_
#define __LED_H_

/* Define --------------------------------------------------------------------*/
#define LED_1 32
#define LED_2 33

/* Functions -----------------------------------------------------------------*/
void led_init();
void led_off();
void led_on();
void led_toggle_1s();

#endif