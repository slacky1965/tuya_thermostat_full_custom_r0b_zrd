#ifndef _HW_TEST_H_
#define _HW_TEST_H_
#include "include/config.h"

/* Consolidated hardware bench tests (src/hw_test.c). Each entry point runs    */
/* only while its config.h gate is ON; main() diverts into it and it never     */
/* returns (no WDT, no normal UI).                                             */

#if LCD_TEST_EN
void LCD_TestMain(void);              /* LCD bench sequence; never returns     */
#endif

#if CLOCK_TEST_EN
void Clock_TestMain(void);            /* clock measurement bench; never returns */
#endif

#if BRIGHT_TEST_EN
void Bright_TestMain(void);           /* brightness sweep; never returns       */
#endif

#if PHOTO_TEST_EN
void Photo_ModeMain(void);            /* photo/documentation freeze; never returns */
#endif

#if RELAY_TEST_EN
void Relay_TestMain(void);            /* relay pulse bench (latching); never returns */
#endif

#endif
