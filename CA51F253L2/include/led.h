#ifndef LED_H
#define LED_H
#include "include/stdint.h"
/********************************************************************************/
/* Power-key LED / backlight module (board 002_WF1).                           */
/*                                                                              */
/*   P3.0 -> R25 -> Q3 -> R26 -> backlight anode (K = +5V); active-HIGH,        */
/*   P30F = 2 (push-pull).                                                      */
/*                                                                              */
/* The r02 PWM channels (P5.0..P5.3) do NOT exist on this board: P5.0..P5.2    */
/* are LCD SEG4..SEG6 of the glass.                                             */
/********************************************************************************/
/* Unified brightness: level 0 = off, 1-8 = on (single backlight here)          */
#define LED_BRIGHTNESS_OFF  0
#define LED_BRIGHTNESS_MIN  1
#define LED_BRIGHTNESS_MAX  8

/* Channel ids kept from the r02 PWM API so the composition does not change;    */
/* every channel drives the single backlight.                                    */
#define BL_KEYS          0
#define BL_RED           1
#define BL_WHITE         2
#define BL_GREEN         3

void Pwm_Backlight_Init(void);                 /* P30F = 2, T2 carrier + PWM7      */
                                               /* power LED, full at boot           */
void Led_SetLevel(uint8_t level);              /* raw duty 0..128 (1/128 steps):    */
                                               /* phase-PWM (the screen backlight;  */
                                               /* UI levels 0..8 map linearly in    */
                                               /* Lcd_SetBrightness)                */
void PowerLed_SetLevel(uint8_t level);         /* power-key LED (PWM7): UI 0..8,    */
                                               /* follows the screen level          */
void Led_Backlight(uint8_t ch, uint8_t level); /* r02 compat: no-op                  */

#endif
