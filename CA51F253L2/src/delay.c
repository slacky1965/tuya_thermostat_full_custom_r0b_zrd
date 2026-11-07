#include "include/stdint.h"
#include "include/config.h"
#include "include/intrins.h"

/* Calibration, not a formula: one tick (DELAY_50US_PER_MS of them per ms) must  */
/* burn 1000/DELAY_50US_PER_MS worth of clocks, i.e. 1000/21 us = 175 clocks at  */
/* FOSC 3686400. The number below is what the loop as emitted by this toolchain   */
/* actually needs - the surrounding code (call, 16-bit counter, DJNZ body) costs  */
/* cycles that no expression here accounts for. Re-verify with a marker on a      */
/* logic analyzer after changing either constant, the compiler or the flags:      */
/* DELAY_50US_INNER 56 measured 1.392x too long, 39 measures 1.000x.              */
#define DELAY_50US_INNER 39
#define DELAY_50US_PER_MS 21

void Delay_50us(register uint16_t n) {
	register uint8_t i;

	if(!n) return;
#if (SYSCLK_SRC == PLL)
	{
		uint8_t repeats = PLL_Multiple;
		uint16_t count = n;
		uint16_t part;
		while(repeats--) {
			part = count;
			do {
				_nop_();
				i = DELAY_50US_INNER;
				do {
					_nop_();
				} while(--i);
			} while(--part);
		}
	}
#else
	do {
		_nop_();
		i = DELAY_50US_INNER;
		do {
			_nop_();
		} while(--i);
	} while(--n);
#endif
}
void Delay_ms(register uint16_t n) {
	if(!n) return;
	while(n--) {
		Delay_50us(DELAY_50US_PER_MS);
	}
}
