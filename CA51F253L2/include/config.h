#ifndef CONFIG_H
#define CONFIG_H
/**************************************************************************************************/
/* CONFIG.H - single place for all board/chip settings.                                           */
/*                                                                                                */
/* Section:                                                                                       */
/*   1. Device identity: model string, firmware version, chip id                                  */
/*   2. System clock source and frequency                                                         */
/*   3. UART links: enable switches, baudrates, buffer sizes, pins                                */
/*   4. GPIO map: full pin assignment of this board (reference)                                   */
/*   5. Backlight PWM: channels, polarity, period, startup duty                                   */
/*   6. REG-COM LED matrix: brightness, buffer size                                               */
/*   7. Heating relay                                                                             */
/*   8. NTC external switch                                                                       */
/*   9. Optional code switches (ROM size)                                                         */
/*                                                                                                */
/* The vendor SFR/GPIO bit registers (ca51f2sfr.h, gpiodef_f2.h) stay in the                      */
/* SDK headers; this file only holds project-level choices and the pin map.                       */
/* Include config.h FIRST in every .c file, before module headers.                                */
/**************************************************************************************************/

#define FLASH_TOTAL_SIZE       (32UL * 1024UL)
#define FLASH_DATA_SIZE        512
#define FLASH_CODE_SIZE        32256
#define FLASH_PADRD            0x7E
#if (FLASH_CODE_SIZE + FLASH_DATA_SIZE != FLASH_TOTAL_SIZE) || \
    (FLASH_PADRD * 256 != FLASH_CODE_SIZE)
#error Flash layout constants are inconsistent
#endif

/*** 1. Device identity (reported via LNK_CMD_INFO link handshake) ********************************/
/* Model string is sent without a terminator; keep it <= 16 chars so the                          */
/* Info frame stays within LNK_INFO_MAX_LEN (see link_proto.h).                                   */
#define DEVICE_MODEL        "osrctherm_r0b" /* name, revision suffix _rNN                         */
#define APP_VERSION         0x101           /* firmware version, BCD u16 BE                       */
                                            /* (hi byte = major, lo byte = minor; 0x100 = v1.00)  */
#define CHIP_ID             0x20A1          /* hardware chip ID (u16 BE), CACHIP                  */
                                            /* 0x20A1 = CA51F253L2; exact ID in flash 0x2B area   */

/*** 2. System clock ******************************************************************************/
#define IRCH                0
#define IRCL                1
#define PLL                 2
#define XOSCL               3

#define SYSCLK_SRC          IRCH  /* chip system clock select                 */
                                  /* (IRCH = 0, IRCL = 1, PLL = 2, XOSCL = 3) */
#if (SYSCLK_SRC == PLL)
    #define PLL_Multiple    6                          /* PLL multiplier    */
    #define FOSC            (3686400L * PLL_Multiple)  /* synthesized clock */
#else
    #define FOSC            (3686400L)  /* IRCH clock */
#endif

/*** 3. UART links ********************************************************************************/
/* UART1 = ZT3L/ZR link bus, TX = P6.6, RX = P6.7, 115200 8N1                                     */
#define UART1_EN
#define UART1_BAUTRATE      115200

/* UART0 = debug console (polled TX P3.0/P3.1). UART2 = spare.                                    */
/*#define UART0_EN*/  /* uncomment to enable UART0                                                */
/*#define UART2_EN*/  /* uncomment to enable UART2                                                */
/*#define PRINT_EN*/  /* vendor uart_printf() helper (UART0/1/2)                                  */

#ifdef UART0_EN
    #define UART0_BAUTRATE   115200
#endif
#ifdef UART2_EN
    #define UART2_BAUTRATE   115200
#endif
#if SYSCLK_SRC == PLL
    #define UART1_RELOAD_VALUE     0x03FA
    #define UART0_RELOAD_FALLBACK  0xFFFA
    #define UART2_RELOAD_FALLBACK  0x03FA
#else
    #define UART1_RELOAD_VALUE     0x03FF
    #define UART0_RELOAD_FALLBACK  0xFFFF
    #define UART2_RELOAD_FALLBACK  0x03FF
#endif

/* RX/TX ring buffer sizes (bytes each)                                                           */
#define UART0_TX_BUF_SIZE   20
#define UART0_RX_BUF_SIZE   100
#define UART1_TX_BUF_SIZE   128
#define UART1_RX_BUF_SIZE   100
#define UART2_TX_BUF_SIZE   20
#define UART2_RX_BUF_SIZE   100

/*** 4. GPIO map (this board) *********************************************************************/
/*                                                                                                */
/* LED matrix (REG-COM, internal driver in LED mode):                                             */
/*   COM0..COM7  = P0.0..P0.7 (pins 61..54), high sink current                                    */
/*   SEG12..SEG22 = P6.2..P6.5 + P1.0..P1.6 (pins 34..44)                                         */
/* Backlight PWM:                                                                                 */
/*   P5.0/PWM0  = backlight of 4 touch keys                                                       */
/*   P5.1/PWM1  = red strip                                                                       */
/*   P5.2/PWM2  = white backlight of power (leaf) key                                             */
/*   P5.3/PWM3  = green backlight                                                                 */
/* Touch buttons (front panel, 4 keys):                                                             */
/*   BTN_MENU  = TK6 = P3.2                                                                         */
/*   BTN_POWER = TK5 = P3.3                                                                         */
/*   BTN_UP    = TK9 = P4.5                                                                         */
/*   BTN_DOWN  = TK4 = P3.6                                                                         */
/* NTC sensors (ADC inputs, always powered; read on demand):                                      */
/*   TEMP_INTERNAL = AD_CH[0] = P4.0 (chip pin 21)                                                */
/*   TEMP_EXTERNAL = AD_CH[1] = P4.1 (chip pin 20)                                                */
/* Heating relay:                                                                                 */
/*   RELAY = P3.1 (chip pin 46), high = ON, low = OFF (see section 7)                             */
/* UART0 debug: TX = P3.0, RX = P3.1 (polled)                                                     */
/* UART1 link:  TX = P6.6, RX = P6.7                                                              */
/* UART2 spare: TX = P6.1, RX = P6.0                                                              */

/*** 5. Backlight PWM *****************************************************************************/
#define PW_CH_EN            0x0F               /* bitmask: enable PWM0..PWM3           */
#define PW_DIV              1843               /* PWM period divider                   */
#define PW_TOG              0x0C               /* bitmask: active-low LEDs = PWM2,PWM3 */
#define PW_DUTY             (PW_DIV * 5 / 10)  /* startup duty (50%)                   */

/*** 6. REG-COM LED matrix ************************************************************************/
#define LDRV_DEFAULT        7   /* startup display brightness, 0..7 */
#define LED_BUF_N           36  /* shadow buffer words (SEG lines)  */

/*** 7. Heating relay *****************************************************************************/
/* Fanhar FH14L magnetic-latching relay on the power board (doc_local/FH14L-EN.pdf): each side     */
/* flips on a >= 50 ms coil impulse (set/reset time <= 10 ms), the two sides must NEVER be         */
/* energized together, a constant HIGH only heats the 250 mW coil. Sides measured with the         */
/* relay bench (RELAY_TEST_EN, see status log): P4.4 = CLOSE (ON) side, P4.3 = OPEN (OFF) side,    */
/* the relay powers up OPEN (verified - an impulse into an already held state is silent).          */
/* Relay_Set issues one 100 ms impulse on the matching pin; Relay_Poll ends it from the main loop. */
#define RELAY_ON_PIN         P44       /* CLOSE side: impulse closes the contacts   */
#define RELAY_ON_PIN_MUX     P44F      /* pin mux register (2 = GPIO output)        */
#define RELAY_OFF_PIN        P43       /* OPEN side: impulse opens the contacts     */
#define RELAY_OFF_PIN_MUX    P43F      /* pin mux register (2 = GPIO output)        */
#define RELAY_PULSE_10MS     10        /* coil impulse length, 100 ms (>= 5x <=10ms set/reset) */
/* Control: symmetric hysteresis, heater ON when t <= SP - deadBand, OFF when     */
/* t >= SP + deadBand. OFF is always immediate; the only time restriction is the  */
/* minimum time spent OFF (anti-short-cycle start protection): the ON condition   */
/* is ignored until the relay has been off for RELAY_OFF_WINDOW_SECS.             */
#define RELAY_OFF_WINDOW_SECS 30

/* Emergency cut-off ceiling = maxHeatSetpointLimit + ABS_MAX_DEADBAND + margin   */
/* (c100). The margin is measured from the HIGHEST legal OFF point of the law     */
/* (SP + deadBand <= maxHeatSetpointLimit + ABS_MAX_DEADBAND), so the cut-off     */
/* never fires during normal operation - it only reacts to a genuine runaway.     */
#define RELAY_TRIP_MARGIN_C100 100

/* Wireless climate sensor (WS) freshness window: while the last WS temperature   */
/* report is younger than this, the WS sensor drives BOTH the relay and the big   */
/* digits; after that the thermostat falls back to the configured probes          */
/* (IN/OU/AL) and BOTH WS values count as invalid. Seconds.                       */
#define WS_FRESH_SECS         300

/* AL "Er" alternation: with one probe of the AL pair dead the big digits       */
/* alternate between the survivor's temperature and "Er" so the working value   */
/* stays visible. Phase lengths in 100 ms ticks (main-loop grid).               */
#define TEMP_ER_TEMP_10MS_TICKS 500    /* temperature phase, 5.0 s              */
#define TEMP_ER_WARN_10MS_TICKS 150    /* "Er" phase, 1.5 s                     */

/*** 8. NTC power (none: both dividers permanently powered from VDD) *****************/
/* Owner-verified on this board: one end of BOTH probes (internal and external) is tied  */
/* to VDD permanently - there is no sensor power switch. The r02 "sensor power" stub    */
/* (NTC_EXT_SW_PIN on P4.4 + temp_power() in sensor_ntc.c) is removed: P4.4 is the DOWN */
/* touch key on this board (probed: pin 12 -> R21 -> BTN_DOWN).                          */

/*** 9. Optional code switches (ROM size) ***********************************************************/
/* The linker does no dead-code elimination, so helpers that nothing calls still occupy ROM.        */
#define SYSCLK_EXTRA_FUNCS  0   /* Sys_Clk_Set_IRCL/XOSCL/PLL/TFRC/XOSCH helpers                    */
                                /* (the real crystal wait/fallback lives in RTC_Init, rtc.c)        */
#define SENSOR_LEGACY_READ  0   /* Temp_ReadC100() wrapper, superseded by Temp_ReadC100Checked()    */
#define FACTORY_RESET_TEST_EN 0 /* 1 = OFF+DOWN only BLINKS the NET symbol: no LNK_CMD_FACTORY_     */
                                /* RESET (0x27) is sent and the steady-NET guard in Net_Flash() is  */
                                /* lifted, so the gesture works while the module STAYS joined.      */
                                /* 0 = full provisioning functionality (send + steady guard).       */

/*** 10. LCD mapping bench (r0b glass 16 x 5) ********************************************************/
/* 1 = bench build: after clock/LCD init, main() jumps into LCD_TestMain() (src/hw_test.c) and      */
/* never returns (no WDT, no link, no keys, no UI): empty glass (2 s), full glass (2 s), then an    */
/* endless walk - one INDEX column at a time (0.8 s), then every (cell,row) electrode alone         */
/* (0.4 s). One video of a cycle IS the (cell,row) -> physical position map. Backlight: hardware   */
/* PWM0 on P5.0 at ~25 % brightness - 50 % still bleeds light on video. Default 0 = firmware.   */
#define LCD_TEST_EN     0

/*** 11. Clock measurement bench (logic analyzer on J1) **********************************************/
/* 1 = short self-contained test (src/hw_test.c): main() jumps into Clock_TestMain() and never    */
/* returns. P3.0 (J1 pin 2, GND = J1 pin 1) carries a repeating ~3 s cycle, in this order: 300 ms    */
/* marker, a UART0 TX burst at 115200 (reload 0xFFFF => line rate = f_SYS / 32, the absolute anchor */
/* for f_SYS and for the real link baud), 300 ms marker, a Delay_ms(1) toggle phase (exact Delay_ms  */
/* scale error), 300 ms marker, a raw toggle loop with no delay calls (f_SYS cross-check). The      */
/* >=300 ms markers separate the phases in the trace, so a capture of a few seconds is enough.      */
/* Intended for fx2lafw-class analyzers: keep the sample rate at 1 MS/s.                             */
/* Takes priority over LCD_TEST_EN in main(). Default 0 = firmware.                                  */
#define CLOCK_TEST_EN    0

/*** 12. Brightness sweep bench *********************************************************************/
/* 1 = level-sweep bench (src/hw_test.c): main() jumps into Bright_TestMain() and never returns. */
/* The glass shows the UI level under test as a big digit AND floods every other cell            */
/* (LCD_SEG_ALL) - a single digit was too small to compare levels visually. Bright_Apply walks   */
/* the ladder 1 -> 8 -> 1 (ping-pong), dwelling BRIGHT_TEST_SECS seconds per level and looping   */
/* forever - one video records the whole ladder and lets adjacent levels be compared both ways.  */
/* Backlight = hardware PWM0 on P5.0 (r0b); level 1 is ~9% duty by design (was invisible at 4%). */
/* CLOCK_TEST_EN and LCD_TEST_EN take priority over it in main(). Default 0 = firmware.          */
#define BRIGHT_TEST_EN    0
#define BRIGHT_TEST_SECS  2           /* dwell per level                        */

/*** 13. Photo/documentation mode ***********************************************************/
/* 1 = photo freeze (src/hw_test.c: main() jumps into Photo_ModeMain and never returns): the panel and the */
/* power LED sit at FULL brightness, every segment of the glass is lit, and the device     */
/* freezes in that state until the next reset - for documentation photos. No WDT, no link  */
/* traffic. Priority below CLOCK_TEST_EN / LCD_TEST_EN / BRIGHT_TEST_EN in main().         */
/* Default 0 = firmware.                                                                    */
#define PHOTO_TEST_EN     0

/*** 14. Relay output bench ************************************************************************/
/* 1 = relay output test (src/hw_test.c): main() jumps into Relay_TestMain() and never returns.  */
/* Bench kept for later debugging of the latching relay (doc_local/FH14L-EN.pdf): single-side    */
/* pulses (100 ms HIGH every 2 s) on ONE pin, the other side held LOW (datasheet: never both    */
/* at once). Sides measured with a tester:                                                        */
/*   RELAY_TEST_SIDE = 3 -> pulses on P4.3 (measured: OPEN side, silent when already open)       */
/*   RELAY_TEST_SIDE = 4 -> pulses on P4.4 (measured: CLOSE side, click -> contacts close)       */
/* Pins are latched LOW first, then muxed to GPIO-output - no glitch at startup. No WDT, no      */
/* link, no display. Priority below the other benches in main(). Default 0 = firmware.            */
#define RELAY_TEST_EN     0
#define RELAY_TEST_SIDE   4           /* which pin pulses: 3 = P4.3, 4 = P4.4   */
/****************************************************************************************************/
#endif
