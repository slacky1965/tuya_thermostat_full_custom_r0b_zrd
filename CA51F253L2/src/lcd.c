/******************************************************************************/
#include "include/stdint.h"
#include "include/ca51f2sfr.h"
#include "include/ca51f2xsfr.h"
#include "include/lcd.h"
#include "include/led.h"
#include "include/led_format.h"
#include "include/chars.h"
#include "include/debug.h"
#include "include/modes.h"          /* lock_flash/net_flash/trace_tail: LOCK_TRACE_EN */
/******************************************************************************/
/* Map status: the sym_* tables below still carry the r01 24 x 4 map - not    */
/* valid for the r0b glass (16 x 5) until the LCD_TEST_EN walk re-measures it. */
/*                                                                              */
/* The shadow buffer holds one byte per cell; drawing sets (cell,row) bits,     */
/* Lcd_Flush() pushes the buffer through INDEX/LXDAT. Never touch INDEX from    */
/* an ISR (shared with MDU and touch scan).                                     */

/* Frame clock = IRCH / ((LXDIV + 1) * 512): 3686400 / (101 * 512) ~ 71 Hz.   */
/* (LXDIV 0x64 = the stock r0b value recovered from the original firmware.)    */
#define LCD_LXCON       0x40        /* LEN=010 (IRCH), LMOD=0 (LCD)              */
#define LCD_LXDIVL      0x64
#define LCD_LXDIVH      0x00
#define LCD_LXCFG       0xE7        /* stock: DMOD=130uA, BIAS=1/3, LDRV=max     */

/* r0b pin set (from the original firmware dump): P0.0..P0.4 = COM0..COM4,     */
/* P04F = 3 selects COM4 (the 5th row); 16 SEG pins follow, all value 3.        */

/* r02 index-style positions (kept for Lcd_DigitIdx/Lcd_CharIdx).              */
#define LCD_DIG_T       3           /* big tens  = big digit pos 0               */
#define LCD_DIG_U       2           /* big units = big digit pos 1               */

/* Master switch for the screen output: 0 = no glyphs may reach the glass.      */
/* Boot sequence is then exactly Lcd_Init + Lcd_Clear + backlight; the shadow   */
/* buffer never changes after the boot clear, so every Lcd_Flush() just         */
/* rewrites zeros. Lcd_Init/Lcd_Clear/Lcd_Flush/Lcd_SetBrightness stay live.    */
/* 1 = normal UI output (composition draws; verify zone by zone on the glass).  */
#define LCD_DRAW_ENABLE 1

typedef struct {
	uint8_t cell;
	uint8_t mask;
} lcd_pair_t;

static xdata uint8_t lcd_buf[LCD_CELLS];

#if LCD_DRAW_ENABLE
/* SYM_* -> (cell, mask) on the r0b glass (16 x 5); 0xFF = no such glyph here   */
/* (no-op so the composition keeps its ids). Mapping source:                    */
/* doc_local/lcd_cell_map_r0b.md (bench video). Weekday circles: Mon..Fri =     */
/* cell8 rows4..0, Sat/Sun = cell7 rows0/1. The fractional area is a FIXED      */
/* ".5" electrode (cell5 row1): driven only by Lcd_HalfDot (r02 policy: light   */
/* when hundredths >= 50). Icons absent on this glass (H/AM/PM/MOON/ON/OFF/     */
/* HOME/HUMID) map to 0xFF. Fan and AUTO are not driven by this firmware.       */
static const uint8_t code sym_cell[SYM_COUNT] = {
	[SYM_SUNNY]  = 1,   [SYM_CLOUD]  = 1,   [SYM_RAIN]   = 2,   [SYM_SNOW]   = 3,
	[SYM_HEAT]   = 4,   /* red flame (coloured segment)                          */
	[SYM_DEGC]   = 5,   [SYM_DP5]    = 5,   [SYM_MON]    = 8,   [SYM_TUE]    = 8,
	[SYM_WED]    = 8,   [SYM_THU]    = 8,   [SYM_FRI]    = 8,   [SYM_SAT]    = 7,
	[SYM_SUN]    = 7,   [SYM_NET]    = 0,   [SYM_SP1]    = 9,   [SYM_SP2]    = 9,
	[SYM_SP3]    = 9,   [SYM_SP4]    = 9,   [SYM_SP5]    = 9,   [SYM_SP6]    = 6,
	[SYM_PM]     = 0xFF,[SYM_AM]     = 0xFF,[SYM_MOON]   = 0xFF,[SYM_CLOCK]  = 11,
	[SYM_HAND]   = 10,  [SYM_HEATER] = 11,  [SYM_SET]    = 11,  [SYM_HOME]   = 0xFF,
	[SYM_LOCK]   = 10,  [SYM_HUMID]  = 0xFF,[SYM_H]      = 0xFF,
	[SYM_ON]     = 0xFF,[SYM_OFF]    = 0xFF,[SYM_THERMO] = 10,
};

static const uint8_t code sym_mask[SYM_COUNT] = {
	[SYM_SUNNY]  = 0x01, [SYM_CLOUD]  = 0x02, [SYM_RAIN]   = 0x01,
	[SYM_SNOW]   = 0x01, [SYM_HEAT]   = 0x01,
	[SYM_DEGC]   = 0x01, [SYM_DP5]    = 0x02, [SYM_MON]    = 0x10,
	[SYM_TUE]    = 0x08, [SYM_WED]    = 0x04, [SYM_THU]    = 0x02,
	[SYM_FRI]    = 0x01, [SYM_SAT]    = 0x01, [SYM_SUN]    = 0x02,
	[SYM_NET]    = 0x01, [SYM_SP1]    = 0x10, [SYM_SP2]    = 0x08,
	[SYM_SP3]    = 0x04, [SYM_SP4]    = 0x02, [SYM_SP5]    = 0x01,
	[SYM_SP6]    = 0x01, [SYM_PM]     = 0x00, [SYM_AM]     = 0x00,
	[SYM_MOON]   = 0x00, [SYM_CLOCK]  = 0x04, [SYM_HAND]   = 0x08,
	[SYM_HEATER] = 0x01, [SYM_SET]    = 0x02, [SYM_HOME]   = 0x00,
	[SYM_LOCK]   = 0x02, [SYM_HUMID]  = 0x00,
	[SYM_H]      = 0x00, [SYM_ON]     = 0x00, [SYM_OFF]    = 0x00,
	[SYM_THERMO] = 0x04,
};

/* SYM_HOME has no dedicated glyph on r0b (schedule mode = the CLOCK icon).     */
/* Inert pairs keep Lcd_SymSet's 0xFE branch inside the map.                    */
static const lcd_pair_t code sym_home_pairs[] = {
	{ 0, 0x00 }, { 0, 0x00 }, { 0, 0x00 }
};
#define SYM_HOME_N     (sizeof(sym_home_pairs) / sizeof(sym_home_pairs[0]))
#endif /* LCD_DRAW_ENABLE */

/* Per-segment (cell,row) pairs, order a,b,c,d,e,f,g (7-seg pattern bits).     */
/* r0b big temperature digits (positions from the map walk, orientation        */
/* normalized): pos 0 tens = cell13 (A,B,C,D) + cell12 (E,F,G);                */
/* pos 1 units = cell15 (A,B,C,D) + cell14 (E,F,G).                            */
static const lcd_pair_t code big_pairs[2][7] = {
	/* pos 0: tens                                                              */
	{ { 13, 0x08 }, { 13, 0x04 }, { 13, 0x02 }, { 13, 0x01 },
	  { 12, 0x02 }, { 12, 0x08 }, { 12, 0x04 } },
	/* pos 1: units                                                             */
	{ { 15, 0x08 }, { 15, 0x04 }, { 15, 0x02 }, { 15, 0x01 },
	  { 14, 0x02 }, { 14, 0x08 }, { 14, 0x04 } },
};

/* No fractional digit on r0b: one fixed ".5" electrode (cell5/row1) toggled    */
/* by Lcd_HalfDot only - table kept inert for linkage.                          */
static const lcd_pair_t code small_pairs[7] = {
	{ 0, 0x00 }, { 0, 0x00 }, { 0, 0x00 }, { 0, 0x00 },
	{ 0, 0x00 }, { 0, 0x00 }, { 0, 0x00 },
};

/* Clock digits (r0b): digit1 = cells 6+7, digit2 = 4+5, digit3 = 2+3,        */
/* digit4 = 0+1; rows from the map walk video.                                  */
static const lcd_pair_t code clk_pairs[4][7] = {
	/* digit 1 (H10): cell6 = A,B,C,D; cell7 = E,F,G                            */
	{ { 6, 0x02 }, { 6, 0x04 }, { 6, 0x08 }, { 6, 0x10 },
	  { 7, 0x10 }, { 7, 0x04 }, { 7, 0x08 } },
	/* digit 2 (H1): cell4 + cell5                                              */
	{ { 4, 0x02 }, { 4, 0x04 }, { 4, 0x08 }, { 4, 0x10 },
	  { 5, 0x10 }, { 5, 0x04 }, { 5, 0x08 } },
	/* digit 3 (M10): cell2 + cell3                                              */
	{ { 2, 0x02 }, { 2, 0x04 }, { 2, 0x08 }, { 2, 0x10 },
	  { 3, 0x10 }, { 3, 0x04 }, { 3, 0x08 } },
	/* digit 4 (M1): cell0 + cell1                                               */
	{ { 0, 0x02 }, { 0, 0x04 }, { 0, 0x08 }, { 0, 0x10 },
	  { 1, 0x10 }, { 1, 0x04 }, { 1, 0x08 } },
};

/* ':' = a single electrode carrying both dots: cell3 row1.                     */
static const lcd_pair_t code colon_pair = { 3, 0x02 };

/* Weekdays Mon..Sun: cell8 rows4..0 then cell7 rows0/1.                        */
static const lcd_pair_t code week_pairs[7] = {
	{ 8, 0x10 },   /* Mon */
	{ 8, 0x08 },   /* Tue */
	{ 8, 0x04 },   /* Wed */
	{ 8, 0x02 },   /* Thu */
	{ 8, 0x01 },   /* Fri */
	{ 7, 0x01 },   /* Sat */
	{ 7, 0x02 },   /* Sun */
};

/* 7-seg digit patterns (a=bit0 .. g=bit6) - same font as the stock dump.      */
static const uint8_t code lcd_seg7[10] = {
	0x3F, 0x06, 0x5B, 0x4F, 0x66,   /* 0 1 2 3 4 */
	0x6D, 0x7D, 0x07, 0x7F, 0x6F    /* 5 6 7 8 9 */
};

static void lcd_pair_set(const lcd_pair_t code *p, uint8_t on) {
#if LCD_DRAW_ENABLE
	if(on) {
		lcd_buf[p->cell] |= p->mask;
	} else {
		lcd_buf[p->cell] &= (uint8_t)~p->mask;
	}
#else
	(void)p;
	(void)on;
#endif
}

static void lcd_draw7(const lcd_pair_t code (*pairs)[7], uint8_t seg7) {
	uint8_t i;

	for(i = 0; i < 7; i++) {
		lcd_pair_set(&(*pairs)[i], (uint8_t)((seg7 >> i) & 0x01));
	}
}

void Lcd_Init(void) {
	/* r0b pin set: 5 COM (P0.0..P0.4, P04F = 3 = COM4) + 16 SEG. P2.0..P2.3  */
	/* and P5.x stay out of the LCD function (P2.3 = GPIO out, P5.0 = PWM0).   */
	P00F = 3; P01F = 3; P02F = 3; P03F = 3; P04F = 3;
	P10F = 3; P11F = 3; P12F = 3; P13F = 3; P16F = 3; P17F = 3;
	P24F = 3; P25F = 3; P26F = 3; P27F = 3;
	P60F = 3; P61F = 3; P62F = 3; P63F = 3; P64F = 3; P65F = 3;

	LXCON  = LCD_LXCON;
	LXDIVL = LCD_LXDIVL;
	LXDIVH = LCD_LXDIVH;
	LXCFG  = LCD_LXCFG;
}

void Lcd_Flush(void) {
	uint8_t i;

	DEBUG(LOCK_TRACE_EN, { if(lock_flash || net_flash || trace_tail) debug_putc('.'); });
	for(i = 0; i < LCD_CELLS; i++) {
		INDEX = i;
		LXDAT = (uint8_t)(lcd_buf[i] & LCD_SEG_ALL);
	}
}

void Lcd_Clear(void) {
	uint8_t i;

	DEBUG(LOCK_TRACE_EN, { if(lock_flash || net_flash || trace_tail) debug_putc('='); });
	for(i = 0; i < LCD_CELLS; i++) {
		lcd_buf[i] = 0;
	}
	Lcd_Flush();
}

/* Zero the shadow buffer WITHOUT pushing it to the panel. A caller that draws  */
/* a whole screen right after must use this instead of Lcd_Clear(): the latter */
/* flushes the empty frame itself, and in a periodic repaint (the schedule     */
/* editor blinks its field twice a second) that shows up as the whole screen   */
/* blanking for a frame - "cleared and redrawn" flicker.                      */
void Lcd_ClearBuf(void) {
	uint8_t i;

	for(i = 0; i < LCD_CELLS; i++) {
		lcd_buf[i] = 0;
	}
}

void Lcd_WriteCell(uint8_t cell, uint8_t val) {
#if LCD_DRAW_ENABLE
	lcd_buf[cell] = (uint8_t)(val & LCD_SEG_ALL);
#else
	(void)cell;
	(void)val;
#endif
}

void Lcd_SetBrightness(uint8_t level) {
	if(level > LCD_BRIGHTNESS_MAX) {
		level = LCD_BRIGHTNESS_MAX;
	}
	DEBUG(LOCK_TRACE_EN, { if(lock_flash || net_flash || trace_tail) debug_putc((uint8_t)('0' + level)); });

	/* Brightness policy: the CONTENT stays visible - the panel keeps the       */
	/* constant drive set up by Lcd_Init (LXCON/LXCFG are not touched here: no  */
	/* washout on a level change, the buffer is never affected). The configured  */
	/* level dims ONLY the backlight through the edge-PWM in led.c: UI levels    */
	/* 0..8 map onto raw steps 0..128 (level 1 = the dimmest VISIBLE step,       */
	/* level 8 = full duty, 0 = off). The hold-expiry drops the backlight to     */
	/* the day/night level while the clock stays on screen.                      */
	{
		/* Map: evened out in PERCEIVED brightness (owner's bench iterations).   */
		/* Level 1 is the anchor: minimum duty that is still VISIBLE (12/128 ~   */
		/* 9% - the original 5/128 ~ 4% was measured invisible on this glass).   */
		/* Levels follow duty = 12 + 116*(n^2.5 - 1)/180 (n = level): steps      */
		/* grow 3,6,11,15,21,27,33 - a big 1->2 jump and a flat top were both    */
		/* rejected by eye (12->27 too coarse, 12->21 too coarse at the bottom). */
		/* The 5/128 floor rationale: below it the once-per-second LCD refresh   */
		/* (clock draw / colon blink) couples an increasingly visible impulse    */
		/* into the backlight spark - pure hardware, no firmware modulation      */
		/* (the level write path is cached, the relay pin is a bit write).       */
		/* Calibration ladder: 1/128 and 2/128 clearly pulsed, 4/128 borderline,  */
		/* 5/128 the accepted minimum; 12/128 sits well above the spark region.   */
		/* The `code` qualifier is load-bearing: without it a function-local       */
		/* static lands in .mcs51.xdata.init, whose load image this project never  */
		/* copies (xdata_clear() zeroes XRAM) - the map would read as all zeros.   */
		static const uint8_t code bright_map[9] = {0, 12, 15, 21, 32, 47, 68, 95, 128};

		Led_SetLevel(bright_map[level]);
	}
}

void Lcd_SymSet(uint8_t id, uint8_t on) {
#if LCD_DRAW_ENABLE
	uint8_t i;

	if(id >= SYM_COUNT) {
		return;
	}
	if(sym_cell[id] == 0xFE) {          /* SYM_HOME: three pairs                */
		for(i = 0; i < (uint8_t)SYM_HOME_N; i++) {
			lcd_pair_set(&sym_home_pairs[i], on);
		}
		return;
	}
	if(sym_cell[id] == 0xFF) {          /* no such glyph on this glass          */
		return;
	}
	if(on) {
		lcd_buf[sym_cell[id]] |= sym_mask[id];
	} else {
		lcd_buf[sym_cell[id]] &= (uint8_t)~sym_mask[id];
	}
#else
	(void)id;
	(void)on;
#endif
}

void Lcd_DigitBig(uint8_t pos, uint8_t seg7) {
	if(pos > 1) {
		return;
	}
	lcd_draw7(&big_pairs[pos], seg7);
}

void Lcd_DigitSmall(uint8_t seg7) {
	lcd_draw7(&small_pairs, seg7);
}

void Lcd_ClockDigit(uint8_t pos, uint8_t seg7) {
	if(pos < 1 || pos > 4) {
		return;
	}
	lcd_draw7(&clk_pairs[pos - 1], seg7);
}

void Lcd_Colon(uint8_t on) {
	lcd_pair_set(&colon_pair, on);
}

void Lcd_Weekday(uint8_t week) {
	uint8_t i;

	for(i = 0; i < 7; i++) {
		lcd_pair_set(&week_pairs[i], (uint8_t)(week == (uint8_t)(i + 1)));
	}
}

/*******************************************************************************/
/* r02 index-style helpers: idx 3/2 = big tens/units, 4..7 = clock H10..M1.   */
void Lcd_DigitIdx(uint8_t idx, uint8_t digit) {
	uint8_t seg7 = 0x00;

	if(digit <= 9) {
		seg7 = lcd_seg7[digit];
	}
	switch(idx) {
	case LCD_DIG_T: Lcd_DigitBig(0, seg7);  break;
	case LCD_DIG_U: Lcd_DigitBig(1, seg7);  break;
	case 4:         Lcd_ClockDigit(1, seg7); break;
	case 5:         Lcd_ClockDigit(2, seg7); break;
	case 6:         Lcd_ClockDigit(3, seg7); break;
	case 7:         Lcd_ClockDigit(4, seg7); break;
	default: break;
	}
}

void Lcd_CharIdx(uint8_t idx, uint8_t seg7) {
	seg7 &= 0x7F;
	switch(idx) {
	case LCD_DIG_T: Lcd_DigitBig(0, seg7);  break;
	case LCD_DIG_U: Lcd_DigitBig(1, seg7);  break;
	case 4:         Lcd_ClockDigit(1, seg7); break;
	case 5:         Lcd_ClockDigit(2, seg7); break;
	case 6:         Lcd_ClockDigit(3, seg7); break;
	case 7:         Lcd_ClockDigit(4, seg7); break;
	default: break;
	}
}

void Lcd_Letters(uint8_t tens_seg7, uint8_t units_seg7) {
	Lcd_CharIdx(LCD_DIG_T, tens_seg7);
	Lcd_CharIdx(LCD_DIG_U, units_seg7);
}

/* IN / OU / AL in the big digits (shared table, one definition).              */
void Lcd_SensorLetters(uint8_t src) {
	static const uint8_t code letters[3][2] = {
		{ CH_I, CH_N }, { CH_O, CH_U }, { CH_A, CH_L }
	};

	Lcd_Letters(letters[src][0], letters[src][1]);
}

void Lcd_HalfDot(uint8_t on) {
	Lcd_SymSet(SYM_DP5, on);
}

void Lcd_Clock(uint8_t hour, uint8_t min, uint8_t colon_on) {
	uint8_t h_t;
	uint8_t h_o;
	uint8_t m_t;
	uint8_t m_o;

	h_t = (uint8_t)(hour / 10);
	h_o = (uint8_t)(hour % 10);
	m_t = (uint8_t)(min / 10);
	m_o = (uint8_t)(min % 10);

	if(h_t) {
		Lcd_ClockDigit(1, lcd_seg7[h_t]);
	}
	else {
		Lcd_ClockDigit(1, 0);            /* blank leading zero                   */
	}
	Lcd_ClockDigit(2, lcd_seg7[h_o]);
	Lcd_ClockDigit(3, lcd_seg7[m_t]);
	Lcd_ClockDigit(4, lcd_seg7[m_o]);

	Lcd_Colon(colon_on);
}

void Lcd_Clock_Blank(uint8_t field) {
	if(field == 0) {
		Lcd_ClockDigit(1, 0);
		Lcd_ClockDigit(2, 0);
	}
	else {
		Lcd_ClockDigit(3, 0);
		Lcd_ClockDigit(4, 0);
	}
}

/* menu=0 also lights the °C symbol and flushes; menu=1 leaves the unit and    */
/* the flush to the caller (the network windows light °C themselves, the local  */
/* menu draws a bare number). The glass has a full small 7-seg digit after the  */
/* decimal point (cells 8/9) - richer than the r02 half-dot: the tenths of the  */
/* absolute value are shown whenever present, while the value is in range.      */
static void lcd_draw_value(int16_t value_c100, uint8_t menu) {
	XDATA_TMP(led_value_parts_t, parts);
	uint16_t av;

	Led_FormatC100(value_c100, &parts);
	Lcd_DigitSmall(0);                 /* tenths blank unless in range          */
	if(parts.range) {
		/* outside the displayable range: say so, never show a wrong number     */
		if(parts.range == LED_RANGE_UP) Lcd_Letters(CH_U, CH_P);
		else                            Lcd_Letters(CH_L, CH_O);
		Lcd_HalfDot(0);
	}
	else {
		if(parts.negative) {
			Lcd_CharIdx(LCD_DIG_T, CH_MINUS);
		}
		else if(parts.tens) {
			Lcd_DigitIdx(LCD_DIG_T, parts.tens);
		}
		else {
			Lcd_DigitIdx(LCD_DIG_T, 10); /* blank the tens cell                  */
		}
		Lcd_DigitIdx(LCD_DIG_U, parts.units);
		av = (uint16_t)(value_c100 < 0 ? -value_c100 : value_c100);
		av = (av % 100) / 10;            /* tenths                                */
		/* The fraction follows the value, not the caller: a dot + the small      */
		/* tenths digit whenever tenths are present - menu tenths values          */
		/* (calibration, hysteresis, min/max limits) show them like the           */
		/* temperature does, an x.0 value shows just the big digits, and a        */
		/* whole-percent value (humidity, zero tail) never shows a fraction.      */
		Lcd_HalfDot(av ? 1 : 0);
		if(av) {
			/* Lcd_DigitSmall takes a 7-seg PATTERN, not a digit value           */
			Lcd_DigitSmall(lcd_seg7[av]);
		}
	}
	if(!menu) {
		Lcd_SymSet(SYM_DEGC, 1);        /* MenuValue callers light it themselves */
	}
}

void Lcd_Temp(int value_c100) {
	lcd_draw_value((int16_t)value_c100, 0);
	Lcd_Flush();
}

void Lcd_MenuValue(int16_t value_c100) {
	lcd_draw_value(value_c100, 1);
}

void Lcd_Temp_Blank(void) {
	Lcd_DigitBig(0, 0);
	Lcd_DigitBig(1, 0);
}

/* Draw "Er" in the big temperature digits (sensor error / no reading).        */
void Lcd_TempErr(void) {
	Lcd_Letters(CH_E, CH_R);
	Lcd_SymSet(SYM_DEGC, 1);             /* the unit must show even on a cold   */
	                                     /* buffer, not only survive a warm one */
	Lcd_HalfDot(0);                      /* no stale dot                        */
	Lcd_DigitSmall(0);                   /* no stale tenths digit              */
	Lcd_Flush();
}
