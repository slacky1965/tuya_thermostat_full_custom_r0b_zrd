/********************************************************************************/
/* Backlight module (r0b): hardware PWM0 on P5.0 (P50F = 5).                    */
/*                                                                              */
/* This board has no power-key LED (P3.2 is the Menu touch pad) and P3.0 is the */
/* ISP UART line - neither is driven here; the old timer-ISR soft PWM on P3.0   */
/* is gone (timer2_backlight_isr.s stays linked but is never armed: no ET2).    */
/*                                                                              */
/* Brightness chain: UI level 0..8 -> Lcd_SetBrightness's level map -> raw     */
/* 0..128 -> duty = raw * BACKLIGHT_PERIOD >> 7 (period 122 = FOSC/30 kHz,      */
/* edge mode, clock IRCH, exactly like the stock dump). Duty 0 = off            */
/* (PWMDIV != 0 && PWMDUT == 0 drives the pin low); boot = full duty until      */
/* Bright_Apply applies the configured level.                                    */
/*                                                                              */
/* PWM registers are INDEX-indexed: every access saves and restores INDEX        */
/* (shared with the LCD window and the touch scan; main context only).           */
/********************************************************************************/
#include "include/stdint.h"
#include "include/config.h"
#include "include/ca51f2sfr.h"
#include "include/ca51f2xsfr.h"
#include "include/gpiodef_f2.h"
#include "include/led.h"

#define PWM_DUTY_MAX       128        /* raw step range for Led_SetLevel          */
#define BACKLIGHT_PERIOD   122        /* FOSC/30k, fits in one byte               */

/* Raw duty 0..PWM_DUTY_MAX: 0 = off. Called from Lcd_SetBrightness (UI levels   */
/* 0..8 via the level map) and from the brightness bench test. This is the       */
/* ONLY place that decides how bright the backlight is on this board.            */
void Led_SetLevel(uint8_t level) {
	uint8_t save;
	uint16_t duty;

	if(level > PWM_DUTY_MAX) {
		level = PWM_DUTY_MAX;
	}
	duty = ((uint16_t)level * BACKLIGHT_PERIOD) >> 7;  /* 122 at full scale      */
	save = INDEX;
	INDEX = 0;                           /* PWM channel 0 register window         */
	PWMDUTH = 0;
	PWMDUTL = (uint8_t)duty;
	PWMUPD |= 0x01;                      /* latch at counter overflow              */
	while(PWMUPD) {
	}
	INDEX = save;
}

void Pwm_Backlight_Init(void) {
	uint8_t save;

	/* P5.0 -> PWM0; boot sequence = LCD init -> clear -> backlight ON at full    */
	/* duty until the UI applies the configured level.                            */
	P50F = P50_PWM0_SETTING;             /* = 5                                  */
	save = INDEX;
	INDEX = 0;
	PWMCON = 0x01;                       /* edge mode, clock = IRCH, no IRQs      */
	PWMCFG = 0x00;                       /* no phase toggle, no prescale          */
	PWMDIVH = 0x00;
	PWMDIVL = BACKLIGHT_PERIOD;          /* ~30 kHz                               */
	PWMDUTH = 0x00;
	PWMDUTL = BACKLIGHT_PERIOD;          /* 100 % until Bright_Apply              */
	PWMUPD |= 0x01;
	while(PWMUPD) {
	}
	PWMEN |= 0x01;                       /* enable PWM0 output                    */
	INDEX = save;
}

/* No power-key LED on r0b: P3.2 is the Menu touch pad. The call stays a no-op   */
/* so the composition keeps its shape.                                            */
void PowerLed_SetLevel(uint8_t level) {
	(void)level;
}

/* Channel API kept for the composition; this board has one backlight whose      */
/* level comes from Led_SetLevel - channel writes are ignored.                   */
void Led_Backlight(uint8_t ch, uint8_t level) {
	(void)ch;
	(void)level;
}
