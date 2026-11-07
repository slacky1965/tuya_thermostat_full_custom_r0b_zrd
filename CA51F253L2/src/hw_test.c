/******************************************************************************/
/* Hardware bench tests: every *_TEST_EN mode in one file, grouped in the     */
/* order of the config.h sections:                                            */
/*   10. LCD bring-up     (LCD_TEST_EN)      - LCD_TestMain()                  */
/*   11. Clock bench      (CLOCK_TEST_EN)    - Clock_TestMain()                */
/*   12. Brightness sweep (BRIGHT_TEST_EN)  - Bright_TestMain()               */
/*   13. Photo freeze     (PHOTO_TEST_EN)    - Photo_ModeMain()                */
/*   14. Relay output     (RELAY_TEST_EN)    - Relay_TestMain()                */
/* Each mode is compiled only while its gate is ON; main() diverts into the   */
/* matching Xxx_TestMain() and never returns (no WDT, no normal UI).          */
/******************************************************************************/
#include "include/stdint.h"
#include "include/config.h"

#if LCD_TEST_EN || CLOCK_TEST_EN || BRIGHT_TEST_EN || PHOTO_TEST_EN || RELAY_TEST_EN

#include "include/ca51f2sfr.h"
#include "include/ca51f2xsfr.h"
#include "include/gpiodef_f2.h"
#include "include/lcd.h"
#include "include/led.h"
#include "include/modes.h"
#include "include/delay.h"
#include "include/hw_test.h"

#if LCD_TEST_EN
/******************************************************************************/
/* 10. LCD mapping bench - r0b glass (16 cells x 5 rows).                      */
/* Sequence, endless until reset (one video of a cycle IS the map):            */
/*                                                                              */
/*   phase 1  glass empty (2 s)                                                 */
/*   phase 2  glass full - every cell, all 5 rows (2 s)                         */
/*   phase 3  column walk: one INDEX column at a time, all rows, 0.8 s each    */
/*   phase 4  electrode walk: every (cell,row) alone, 0.4 s each               */
/*   phases 3+4 repeat forever                                                  */
/*                                                                              */
/* INDEX order follows the datasheet renumbering: INDEX counts the ENABLED     */
/* SEG pins in ascending order, so INDEX 0..15 = SEG10..SEG19, SEG22..SEG27    */
/* (the r0b pin set - see hw_pinout doc for CA51F223L3_TZE204_8byfmxdv).       */
/*                                                                              */
/* Frame clock = source / ((LXDIV + 1) * 512); this test runs from IRCH       */
/* (3.6864 MHz), LEN=010 (IRCH), LMOD=0 (LCD) -> LXCON = 0x40. The stock      */
/* r0b firmware uses LXDIV = 100 (0x64) -> ~71 Hz: copy it.                    */
#define LXCON_LCD       0x40
#define LCD_LXDIV_L     0x64
#define LCD_LXDIV_H     0x00

/* LXCFG (LCD mode, datasheet table 20-3-2): DMOD[7:6] current               */
/* (00=5uA 01=40uA 10=80uA 11=130uA), BIAS[5:4] (01=1/2, 10=1/3, else 1/4),   */
/* LDRV[2:0] drive strength. Reset value 0x00 = 5uA/weak - far too dim.        */
/* 0xE7 = the stock configuration, decoded from its three init helper calls     */
/* (args 3 -> DMOD=130uA, 2 -> BIAS=1/3, 7 -> LDRV=max): stock screen is       */
/* bright on this glass, so copy it verbatim.                                   */
#define LXCFG_STOCK      0xE7

static void lcd_write(uint8_t cell, uint8_t val) {
	INDEX = cell;
	LXDAT = (uint8_t)(val & LCD_SEG_ALL);
}

static void lcd_init(void) {
	/* r0b pin set (stock firmware): 5 COM on P0.0..P0.4 - P04F = 3 selects    */
	/* COM4, the 5th row - and 16 SEG: P6.0..P6.5, P1.0..P1.3 + P1.6/P1.7,     */
	/* P2.4..P2.7 (all value 3). P5.0 is NOT SEG on r0b: it is the backlight.  */
	P00F = 3; P01F = 3; P02F = 3; P03F = 3; P04F = 3;
	P10F = 3; P11F = 3; P12F = 3; P13F = 3; P16F = 3; P17F = 3;
	P24F = 3; P25F = 3; P26F = 3; P27F = 3;
	P60F = 3; P61F = 3; P62F = 3; P63F = 3; P64F = 3; P65F = 3;

	LXCON  = LXCON_LCD;
	LXDIVL = LCD_LXDIV_L;
	LXDIVH = LCD_LXDIV_H;
	LXCFG  = LXCFG_STOCK;                /* 130 uA, 1/3 bias, drive max         */

	/* Backlight: P5.0 = hardware PWM0 (P50F = 5), exactly like the stock      */
	/* firmware. PWMCKS = 001 (IRCH), edge mode, period 122 -> ~30 kHz,        */
	/* duty ~25 % (30 of 122) - even 50 % bleeds light on video. The PWM      */
	/* registers are INDEX-indexed: save the LCD window cursor and restore it.  */
	{
		uint8_t save = INDEX;
		P50F = 5;
		INDEX = 0;                        /* PWM channel 0                      */
		PWMCON = 0x01;                    /* edge mode, clock = IRCH            */
		PWMCFG = 0x00;                    /* no phase toggle, no prescale       */
		PWMDIVL = 122; PWMDIVH = 0x00;    /* period: 3686400 / 122 ~ 30 kHz    */
		PWMDUTL = 30;  PWMDUTH = 0x00;    /* duty ~25 % (30 of 122)             */
		PWMUPD = 0x01;                    /* latch into the channel             */
		while(PWMUPD) { }
		PWMEN = 0x01;                     /* enable PWM0 output                 */
		INDEX = save;
	}
}

/* Measured per-icon (cell, COM-row mask) pairs from the mapping videos - the   */
/* map work data. NOT referenced by the current one-shot sequence; kept as the  */
/* only copy of the measurements in this tree.                                  */
const uint8_t code sym_groups[][9] = {
	{  4, 0x07,  5, 0x0F,  6, 0x07,  7, 0x0F, 0xFF }, /* big digits "88"        */
	{  8, 0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	{ 13, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	{ 12, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	{ 12, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	{ 11, 0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	{  1, 0x04, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	{ 12, 0x04, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	{  1, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	{  1, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	{ 23, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	{ 23, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	{ 23, 0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	{ 22, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	/* second pass over the same glass, measured with a tighter frame inset    */
	{  6, 0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	{ 11, 0x06, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	{  2, 0x0E,  3, 0x08,  4, 0x08, 0xFF, 0xFF, 0xFF },
	{  8, 0x07,  9, 0x06, 10, 0x02, 0xFF, 0xFF, 0xFF },
	{ 13, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	{  2, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	{ 13, 0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	{ 10, 0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	{ 12, 0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	{ 11, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	{  0, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	{  0, 0x04, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	{  0, 0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	{ 14, 0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	{  0, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	{ 10, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	{ 23, 0x04, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }
};

static void lcd_fill_n(uint8_t ncells, uint8_t val) {
	uint8_t i;

	for(i = 0; i < ncells; i++) {
		lcd_write(i, val);
	}
}

static void lcd_clear_all(void) {
	lcd_fill_n(LCD_CELLS, 0);
}

void LCD_TestMain(void) {
	uint8_t cell;
	uint8_t row;

	/* Stock boot state for the pins the bench does not use: UART0 (ISP on  */
	/* J3/COM4) back to its UART function with the TX latch parked HIGH; the    */
	/* relay drive pins P4.3/P4.4 as GPIO outputs LOW, as the stock firmware    */
	/* parks them at boot. Do NOT drive P3.0/P3.1 as GPIO - they are the ISP    */
	/* lines on r0b.                                                            */
	P3 |= 0x01;
	P30F = P30_UART0_TX_SETTING;
	P31F = P31_UART0_RX_SETTING;
	P43F = OUTPUT; P4 &= (uint8_t)~0x08;
	P44F = OUTPUT; P4 &= (uint8_t)~0x10;

	lcd_init();                           /* glass + backlight at full          */

	/* Phase 1: glass empty - nothing may stay lit after a clear.                */
	lcd_clear_all();
	Delay_ms(2000);

	/* Phase 2: glass full - every (cell,row) electrode lit.                     */
	lcd_fill_n(LCD_CELLS, LCD_SEG_ALL);
	Delay_ms(2000);

	for(;;) {
		/* Phase 3: column walk - one INDEX column at a time, all rows.          */
		for(cell = 0; cell < LCD_CELLS; cell++) {
			lcd_write(cell, LCD_SEG_ALL);
			Delay_ms(800);
			lcd_write(cell, 0x00);
		}
		Delay_ms(500);

		/* Phase 4: electrode walk - one (cell,row) at a time.                   */
		for(cell = 0; cell < LCD_CELLS; cell++) {
			for(row = 0; row < 5; row++) {
				lcd_write(cell, (uint8_t)(1u << row));
				Delay_ms(400);
				lcd_write(cell, 0x00);
			}
		}
		Delay_ms(1000);
	}
}
#endif /* LCD_TEST_EN */

#if CLOCK_TEST_EN
/******************************************************************************/
/* 11. Clock measurement bench (config.h section 11).                          */
/* For a logic analyzer on J1: P3.0 = J1 pin 2, GND = J1 pin 1 (J1 pin 4 is  */
/* +5V - do not clip the analyzer onto it).                                     */
/*                                                                              */
/* The whole cycle is ~3 s so that even a short capture gets every phase, and   */
/* the UART burst - the absolute timing anchor - comes FIRST. Segmentation of a */
/* trace needs nothing but the >=100 ms static markers between the phases:       */
/*                                                                              */
/*   [HIGH 300 ms]  phase C  UART0 TX burst  - Timer2 baud generator, reload     */
/*               0xFFFF, so the line rate is exactly f_SYS / 32: measure one     */
/*               bit (every run of 0x55 is exactly one bit) -> f_SYS. Also shows */
/*               the real baud the firmware would put on the wire.              */
/*   [LOW 300 ms]   phase B  toggle via Delay_ms(1) - measured half period /     */
/*               1 ms is the exact Delay_ms scale error                          */
/*   [HIGH 300 ms]  phase A  raw toggle loop, no delay calls - f_SYS cross-check */
/*               against the loop's instruction count (needs the core timing     */
/*               model, so it only corroborates phase C)                        */
/*                                                                              */
/* P3.0 also feeds the backlight chain (R25 -> Q3), so phases A/B just strobe it */
/* - it looks dimmer, harmless.                                                  */
/*                                                                              */
/* NOTE: phase A inverts the SFR bit directly - `P30 = !P30;` lowers to the     */
/* 2-byte `cpl P30`. An older toolchain build turned bit inversion into a plain  */
/* copy (load + store, no CPL): a static level instead of a square wave in the   */
/* trace means suspect the compiler, not the board.                              */

/* Phase A: no time base, only an iteration counter - a few hundred ms at a      */
/* nominal 3.6864 MHz clock (2 x 0xFFFF iterations; the 16-bit loop counter      */
/* dominates the per-iteration cost, the toggle itself is a single CPL).         */
#define CT_RAW_OUTER   2

/* Phase B: 300 periods of 2 ms nominal = 600 measured half periods (~0.84 s).   */
#define CT_DELAY_LOOPS 300

/* Phase C: 1000 bytes of 0x55 = 10000 bit intervals, ~0.12 s of traffic.       */
#define CT_UART_BYTES   1000

/* Markers: 300 ms nominal (x1.4 real) - far above the 100 ms the trace          */
/* segmentation treats as static, and far below any logic-analyzer buffer.       */
#define CT_MARKER_MS    300

/* Bounded wait for the TX flag: a stuck UART must not stall the whole bench.    */
#define CT_TI_TIMEOUT   0xFFFFU

/* Same reload formula as src/debug.c: baud = FOSC / (32 * (0x10000 - reload)). */
/* At FOSC 3686400 and 115200 it folds to 0xFFFF, so baud = FOSC / 32 and        */
/* f_SYS = 32 * (measured baud).                                                 */
#define CT_UART_BAUD    115200UL
#define CT_UART_RELOAD  ((uint16_t)(0x10000UL - (FOSC / (CT_UART_BAUD * 32UL))))

static void ct_phase_raw(void) {
	uint16_t outer;
	uint16_t inner;

	for(outer = 0; outer < CT_RAW_OUTER; outer++) {
		for(inner = 0; inner < 0xFFFF; inner++) {
			P30 = !P30;
		}
	}
}

static void ct_phase_delay(void) {
	uint16_t i;

	for(i = 0; i < CT_DELAY_LOOPS; i++) {
		P30 = 1;
		Delay_ms(1);
		P30 = 0;
		Delay_ms(1);
	}
}

static void ct_uart_putc(uint8_t c) {
	uint16_t t = CT_TI_TIMEOUT;

	S0BUF = c;
	while(!TI0) {
		if(--t == 0) {
			break;
		}
	}
	TI0 = 0;
}

static void ct_phase_uart(void) {
	uint16_t i;

	/* UART0 on P3.0 (J1-2), Timer2 as baud generator (T2CON: reload mode0 +   */
	/* UCKS=1 -> UART0 clock from Timer2 overflow, T2P=00 -> Timer2 counts the   */
	/* system clock directly), REN left clear so nothing listens on the relay    */
	/* pin.                                                                       */
	P30F = P30_UART0_TX_SETTING;
	T2CON = 0x24;
	T2CH  = (uint8_t)(CT_UART_RELOAD >> 8);
	T2CL  = (uint8_t)(CT_UART_RELOAD);
	TH2   = (uint8_t)(CT_UART_RELOAD >> 8);
	TL2   = (uint8_t)(CT_UART_RELOAD);
	TR2   = 1;
	S0CON = 0x40;                     /* mode 1 (8-bit UART), receiver disabled */

	for(i = 0; i < CT_UART_BYTES; i++) {
		ct_uart_putc(0x55);           /* start + 101010101 + stop: every run = 1 bit */
	}

	TR2   = 0;
	S0CON = 0x40;
	P30F  = 2;                        /* hand the pin back to GPIO (latch = 1 here) */
}

void Clock_TestMain(void) {
	uint8_t i;

	/* Push-pull output (datasheet ch.15.3: PxF = 2): the reset default (0) is   */
	/* not a strong drive, which would round the edges off for the analyzer.      */
	P30F = 2;
	P32F = 2;

	/* Marker: the POWER LED (P3.2) blinks twice - execution reached this point. */
	for(i = 0; i < 2; i++) {
		P32 = 1;
		Delay_ms(300);
		P32 = 0;
		Delay_ms(300);
	}

	for(;;) {
		P30 = 1;                       /* park HIGH: the UART takes the pin from   */
		Delay_ms(CT_MARKER_MS);        /* this level without an edge                */
		ct_phase_uart();
		P30 = 0;
		Delay_ms(CT_MARKER_MS);
		ct_phase_delay();
		P30 = 1;
		Delay_ms(CT_MARKER_MS);
		ct_phase_raw();
	}
}
#endif /* CLOCK_TEST_EN */

#if BRIGHT_TEST_EN
/******************************************************************************/
/* 12. Brightness sweep bench (config.h section 12).                           */
/* The glass shows the UI level under test as a big digit AND floods every     */
/* other cell (LCD_SEG_ALL), so brightness can be judged on a large area -     */
/* a single digit alone was too small to compare levels visually (owner).     */
/* The digit keeps its own cells (14/15 = the big units pair); everything     */
/* else is fully lit: worst-case segment load on the backlight, which is      */
/* exactly what a brightness comparison should measure.                        */
/* Bright_Apply walks the ladder 1 -> 8 -> 1 (ping-pong, so adjacent levels    */
/* can be compared in both directions), dwelling BRIGHT_TEST_SECS seconds     */
/* per level and looping forever - one video records the whole ladder.        */
/* The LED's ceiling is the R18/LED chain (A/B vs GPIO-HIGH measured equal),  */
/* so the ladder tops out at whatever the hardware can give.                   */
/* Note on level 1: bright_map[1] = 12/128 ~ 9% (was 5/128 = invisible);     */
/* that is what this bench lets the owner judge.                               */
/* main() diverts here BEFORE Uart1/EA, so this function brings up the pieces */
/* it needs itself: Pwm_Backlight_Init (hardware PWM0 on P5.0, r0b) and EA    */
/* (kept for parity with the other benches; no ISR drives the backlight).     */
/* Nothing else runs.                                                          */
void Bright_TestMain(void) {
	int8_t level = 1;
	int8_t dir = 1;                    /* +1 climbing 1..8, -1 descending     */
	uint8_t i;

	Lcd_Init();
	Lcd_Clear();
	Pwm_Backlight_Init();             /* P5.0 -> PWM0, full duty until applied */
	EA = 1;

	for(;;) {
		Lcd_Clear();
		/* flood everything except the big-digit pairs: the units pair (14/15)  */
		/* carries the level digit, the tens pair (12/13) stays DARK - a flooded */
		/* tens digit reads as a phantom "8" in front of the level (81..88) and  */
		/* confuses the enumeration (owner).                                      */
		for(i = 0; i < LCD_CELLS; i++) {
			if(i == 12 || i == 13 || i == 14 || i == 15) {
				continue;
			}
			Lcd_WriteCell(i, LCD_SEG_ALL);
		}
		Lcd_DigitIdx(2, (uint8_t)level); /* big units digit = level under test */
		Lcd_Flush();
		Bright_Apply((uint8_t)level);    /* screen + power LED, production path */
		Delay_ms((uint16_t)BRIGHT_TEST_SECS * 1000u);
		/* ping-pong: turn around at the ends without repeating the end level */
		if(level >= 8) {
			dir = -1;
		}
		else if(level <= 1) {
			dir = 1;
		}
		level = (int8_t)(level + dir);
	}
}
#endif /* BRIGHT_TEST_EN */

#if PHOTO_TEST_EN
/******************************************************************************/
/* 13. Photo/documentation freeze (config.h section 13).                       */
/* The panel and the Power LED sit at FULL brightness, every segment of the   */
/* glass is lit, and the device freezes in that state until the next reset -  */
/* for documentation photos. The divert runs before WDT_Init and before       */
/* Uart1/EA, so this function brings up the carriers itself (Pwm_Backlight   */
/* Init arms ET2 + PWM7; EA lets the T2 ISR drive P3.0). No watchdog, no      */
/* link traffic.                                                               */
void Photo_ModeMain(void) {
	uint8_t i;

	Pwm_Backlight_Init();             /* T2 carrier + PWM7, both at full        */
	EA = 1;                           /* the carrier ISR may drive P3.0 now     */
	Lcd_SetBrightness(LED_BRIGHTNESS_MAX);
	PowerLed_SetLevel(LED_BRIGHTNESS_MAX);

	/* every segment of the glass (same fill as the boot window's photo phase) */
	for(i = 0; i < LCD_CELLS; i++) {
		Lcd_WriteCell(i, LCD_SEG_ALL);
	}
	Lcd_Flush();

	for(;;) {
		/* frozen until reset */
	}
}
#endif /* PHOTO_TEST_EN */

#if RELAY_TEST_EN
/******************************************************************************/
/* 14. Relay output bench (config.h section 14).                               */
/* The power board has a Fanhar FH14L MAGNETIC LATCHING relay (datasheet in    */
/* doc_local/FH14L-EN.pdf): a SET/RESET pulse (>= 50 ms) flips it, the state   */
/* persists magnetically afterwards; the datasheet forbids energizing both     */
/* sides simultaneously.                                                      */
/*                                                                            */
/* This variant pulses ONE side (RELAY_TEST_SIDE in config.h): a single        */
/* 100 ms HIGH every 2 s, the other side held LOW as an output. Latching means */
/* only the first pulse that flips the state produces a click - every later     */
/* pulse into the same state is silent. The owner watches a tester:            */
/*   click on the first pulse  -> the contacts changed state                  */
/*   no click at all           -> they were already in that state             */
/* Measured so far: P4.3 pulses are SILENT while the contacts are open, so     */
/* P4.3 = the OPEN side (and the relay starts open).                           */
/* Setup order mirrors the original firmware: latch LOW first, then mux the    */
/* pins to GPIO outputs - no glitch at startup. No WDT, no link, no display.   */
void Relay_TestMain(void) {
	uint8_t lvl;

	/* LOW while the pins are still in their reset function, then GPIO out    */
	P43 = 0;
	P44 = 0;
	P43F = 2;                          /* mux P4.3 to GPIO output              */
	P44F = 2;                          /* mux P4.4 to GPIO output              */

	for(;;) {
		/* llc (mcs51) cannot select a CONSTANT sbit store (SETB) inside a     */
		/* loop - it crashes the ISel; a runtime value selects fine.           */
		lvl = 1;
#if RELAY_TEST_SIDE == 4
		P44 = lvl;                      /* pulse the P4.4 (CLOSE) side          */
#else
		P43 = lvl;                      /* pulse the P4.3 (OPEN) side           */
#endif
		Delay_ms(100);                  /* >= 50 ms per datasheet, 2x margin    */
		lvl = 0;
		P43 = lvl;
		P44 = lvl;                      /* both LOW: never energized together   */
		Delay_ms(2000);                 /* pause to read the tester             */
	}
}
#endif /* RELAY_TEST_EN */

#endif /* any test gate */
