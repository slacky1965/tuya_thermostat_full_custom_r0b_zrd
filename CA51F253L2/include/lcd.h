#ifndef LCD_H
#define LCD_H
#include "include/stdint.h"
/********************************************************************************/
/* LCD glass driver, r0b glass (16 x 5).                                        */
/*                                                                              */
/* Glass: 16 cells (SEG columns, INDEX 0..15) x 5 rows (COM0..4); a cell byte   */
/* holds 5 row bits (LCD_SEG_ALL = 0x1F). One (cell,row) pair = one electrode,  */
/* so a symbol is a (cell, row-mask) pair; icons that span several pairs are    */
/* special-cased (see sym_cell[] / Lcd_SymSet).                                 */
/*                                                                              */
/* Data flow: draw into the shadow buffer lcd_buf[] -> Lcd_Flush() pushes it    */
/* through the INDEX/LXDAT window of the LCD controller, which scans the glass  */
/* on its own (~71 Hz). INDEX is a shared window (MDU / touch / PWM): only main */
/* context may use it - never from an ISR.                                      */
/*                                                                              */
/* WARNING: the sym_cell/sym_mask/pairs tables in lcd.c still carry the r01    */
/* 24 x 4 map - invalid for this glass until the LCD_TEST_EN walk re-measures  */
/* it. The bench build does not draw through them.                              */
/*                                                                              */
/* The SYM_* ids and the Lcd_DigitIdx/Lcd_CharIdx index style follow the r02   */
/* LED composition (word indexes): 3/2 = big tens/units, 4..7 = clock          */
/* H10/H1/M10/M1 - so the composition ports as a rename.                        */
/********************************************************************************/
#define LCD_CELLS       16
#define LCD_SEG_ALL     0x1F        /* COM0..COM4: the glass has 5 rows          */

/* 7-seg glyph pattern bits, same packing as the r02 font: a=bit0 .. g=bit6.    */
#define LCD_SEG_A       0x01
#define LCD_SEG_B       0x02
#define LCD_SEG_C       0x04
#define LCD_SEG_D       0x08
#define LCD_SEG_E       0x10
#define LCD_SEG_F       0x20
#define LCD_SEG_G       0x40

/* Display brightness: level 0 = off, 1-8 = LDRV 0-7 (LXCFG low bits).          */
#define LCD_BRIGHTNESS_OFF  0
#define LCD_BRIGHTNESS_MAX  8

/* On-screen symbols (ids inherited from the r02 LED map; unmapped ids are      */
/* no-ops so the composition keeps its names through the port).                  */
enum
{
	SYM_SUNNY   = 0,  /* sunny pictogram    cell12/COM1                            */
	SYM_CLOUD   = 1,  /* not on this glass                                            */
	SYM_RAIN    = 2,  /* not on this glass                                            */
	SYM_SNOW    = 3,  /* snowflake: cool mode (not used)     cell12/COM0             */
	SYM_HEAT   = 4,  /* heating active: smoke above the pipe  cell2/COM1-3       */
	SYM_DEGC   = 5,  /* degree-c unit      cell10/COM3                               */
	SYM_DP5    = 6,  /* decimal dot        cell10/COM0                               */
	SYM_MON    = 7,  /* weekday Mon        cell1/COM3                                */
	SYM_TUE    = 8,  /* weekday Tue        cell1/COM2                                */
	SYM_WED    = 9,  /* weekday Wed        cell1/COM1                                */
	SYM_THU    = 10, /* weekday Thu        cell1/COM0                                */
	SYM_FRI    = 11, /* weekday Fri        cell23/COM0                               */
	SYM_SAT    = 12, /* weekday Sat        cell23/COM1                               */
	SYM_SUN    = 13, /* weekday Sun        cell22/COM0                               */
	SYM_NET    = 14, /* network            cell11/COM3                               */
	SYM_SP1    = 15, /* schedule slot 1 (house) cell0/COM3                           */
	SYM_SP2    = 16, /* schedule slot 2    cell0/COM2                                */
	SYM_SP3    = 17, /* schedule slot 3    cell0/COM1                                */
	SYM_SP4    = 18, /* schedule slot 4    cell0/COM0                                */
	SYM_SP5    = 19, /* schedule slot 5    cell23/COM2                               */
	SYM_SP6    = 20, /* schedule slot 6    cell23/COM3                               */
	SYM_PM     = 21, /* PM (above clock digit 2)  cell20/COM0 (solved)            */
	SYM_AM     = 22, /* AM (above clock digit 3)  cell16/COM0 (solved)            */
	SYM_MOON   = 23, /* not on this glass                                            */
	SYM_CLOCK  = 24, /* schedule mode     cell13/COM3                                */
	SYM_HAND   = 25, /* manual setpoint   cell13/COM2                                */
	SYM_HEATER = 26, /* external probe    cell11/COM2                                */
	SYM_SET    = 27, /* Set               cell11/COM1                                */
	SYM_HOME   = 28, /* big house: roof(6/COM3)+lower(13/COM1)+thermometer(8/COM3)   */
	SYM_LOCK   = 29, /* padlock           cell12/COM3                                */
	SYM_HUMID  = 31, /* %RH               cell10/COM1                                */
	SYM_H      = 32, /* "h" of the8888-hour counter, always on in ON: cell14/COM3     */
	SYM_ON     = 33, /* "ON" line of "h ON/OFF"   cell14/COM1                         */
	SYM_OFF    = 34, /* "OFF" line of "h ON/OFF"  cell14/COM2                         */
	SYM_THERMO = 35, /* thermometer in the house: temperature view only  cell8/COM3   */
	SYM_COUNT  = 36
};

void Lcd_Init(void);                            /* pins + controller (LCD mode, ~64 Hz)   */
void Lcd_Clear(void);                           /* zero shadow buffer + flush              */
void Lcd_Flush(void);                           /* shadow buffer -> LCD RAM                */
void Lcd_WriteCell(uint8_t cell, uint8_t val);  /* one cell of the shadow buffer (no flush)*/
void Lcd_SetBrightness(uint8_t level);          /* 0 = off, 1..8 = UI level           */
                                                /* (map to duty 0..128, evened in    */
                                                /* perceived brightness)             */
void Lcd_SymSet(uint8_t id, uint8_t on);        /* SYM_* id on/off (no flush)              */
void Lcd_DigitBig(uint8_t pos, uint8_t seg7);   /* big "88": pos 0 = left digit            */
void Lcd_DigitSmall(uint8_t seg7);              /* digit after the decimal dot             */
void Lcd_ClockDigit(uint8_t pos, uint8_t seg7); /* clock digit 1..4 (walk numbering)       */
void Lcd_Colon(uint8_t on);                     /* ':' between clock digits 2 and 3        */
void Lcd_Weekday(uint8_t week);                 /* weekday 1..7 (Mon=1), 0 = none          */

/* r02-composition renames: index style 3/2 = big tens/units, 4..7 = clock      */
/* H10/H1/M10/M1.                                                                */
void Lcd_DigitIdx(uint8_t idx, uint8_t digit);  /* digit 0..9, >9 = blank                  */
void Lcd_CharIdx(uint8_t idx, uint8_t seg7);    /* raw 7-seg pattern                       */
void Lcd_Letters(uint8_t tens_seg7, uint8_t units_seg7);
void Lcd_SensorLetters(uint8_t src);            /* IN / OU / AL in the big digits          */
void Lcd_HalfDot(uint8_t on);                   /* decimal dot (SYM_DP5)                   */
void Lcd_Clock(uint8_t hour, uint8_t min, uint8_t colon_on);        /* 24 h format          */
void Lcd_Clock_Blank(uint8_t field);            /* field 0 = hours, 1 = minutes            */
void Lcd_Temp(int value_c100);                  /* two big digits + dot + degC (+ flush)   */
void Lcd_MenuValue(int16_t value_c100);         /* value in the big digits, no unit        */
void Lcd_Temp_Blank(void);                      /* blank the big digits                    */
void Lcd_TempErr(void);                         /* "Er" in the big digits (+ flush)        */

#endif
