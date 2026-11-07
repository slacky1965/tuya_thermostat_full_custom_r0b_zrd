#include "include/stdint.h"
#include "include/config.h"
#include "include/ca51f2sfr.h"
#include "include/ca51f2xsfr.h"
#include "include/gpiodef_f2.h"
#include "include/system_clock.h"
#include "include/debug.h"
#include "include/mdu.h"
#include "include/intrins.h"
/*******************************************************************************
	Lightweight debug output as a line buffer forwarded to the peer over the
	ZT3L link (LNK_CMD_DEBUG_LOG Report, fire-and-forget). UART0 is NOT used
	on this board: P3.0/P3.1 are the backlight control and the heating relay,
	so the old polled UART0 transport is gone - debug_init() only resets the
	line buffer. Print gating lives in debug.h (BUTTONS_EN/LINK_EN/...): call
	sites use the DEBUG(flag, ...) wrappers which expand to nothing when 0.
********************************************************************************/

#if UART_DEBUG

#include "include/uart.h"
#include "include/link_proto.h"

/* One line per frame (payload <= 64): send on '\n' or when the buffer fills. */
#define DBG_LINE_MAX    48

static uint8_t xdata dbg_line[DBG_LINE_MAX];
static uint8_t dbg_len;
static uint8_t dbg_flushing;    /* 1 while Link_SendLog runs: the frame layer  */
                                /* dumps outgoing frames through debug_puts,    */
                                /* and that nested output must not re-flush.    */

/* Pending line: the ACTUAL frame send is deferred to Debug_Poll() in the main  */
/* loop. Deep handler chains (OnFrame -> h_setting -> settings_save -> print)   */
/* used to send directly from the deepest stack frame: on the 8051's 256-byte   */
/* stack that overflowed into the UART ring state and produced a TX runaway     */
/* (raw flash-code bytes on the wire) followed by a WDT reboot - see the bench  */
/* log of a network brightness write. Appending bytes stays cheap and shallow;  */
/* only the main loop owns the send.                                            */
static uint8_t xdata dbg_pending[DBG_LINE_MAX];
static uint8_t dbg_pending_len;
static uint8_t dbg_pending_valid;

void debug_init(void) {
	/* no UART0 setup on this board - the transport is the link; reset the    */
	/* line buffer (called after UART1 is up, same place as before).            */
	dbg_len = 0;
	dbg_flushing = 0;
	dbg_pending_len = 0;
	dbg_pending_valid = 0;
}

static void debug_flush(void) {
	if(dbg_len == 0 || dbg_flushing) return;
	if(dbg_pending_valid) {
		/* previous line not yet shipped: drop the new one (bench loss is fine) */
		dbg_len = 0;
		return;
	}
	dbg_flushing = 1;
	for(uint8_t i = 0; i < dbg_len; i++) {
		dbg_pending[i] = dbg_line[i];
	}
	dbg_pending_len = dbg_len;
	dbg_pending_valid = 1;
	dbg_len = 0;
	dbg_flushing = 0;
}

/* Ship the pending line - call from the main loop (shallow context).           */
void Debug_Poll(void) {
	if(!dbg_pending_valid) return;
	Link_SendLog(dbg_pending, dbg_pending_len);
	dbg_pending_valid = 0;
}

void debug_putc(uint8_t c) {
	if(dbg_flushing) return;            /* nested dump (send-side trace): drop  */
	if(dbg_len >= DBG_LINE_MAX) {
		debug_flush();                  /* oversized line: ship the prefix      */
	}
	dbg_line[dbg_len++] = c;
	if(c == '\n') {
		debug_flush();
	}
}

/* -------- fixed-format primitives: no varargs, no format strings ----------- */
/* Every DEBUG(flag, ...) call site prints through these. That is what keeps a
   single debug flag inside the 802 B ROM debug budget: Digits are produced by
   subtracting power-of-ten places instead of / and %, so no __divsint or
   __modsint gets linked in. The varargs formatter (ca_printf) was deleted -
   no call site used it and it cost ~1500 B.                                 */

/* The linker pulls the WHOLE debug.rel object in once any symbol of it is
   referenced, so compiling the text/number primitives for a build that only
   needs debug_putc (LOCK_TRACE_EN trace) would cost ~270 B of ROM for nothing.
   Compile them only when a verbose module that actually calls them is ON.    */
#if UART_DEBUG && (BUTTONS_EN || LED_EN || RAW_EN || TEMP_EN || RTC_EN || \
                   LINK_EN || FLASH_EN || SETTINGS_EN || TIMER_EN || \
                   LED_SCAN_EN || FLASHER_EN)
#define DEBUG_PRIMITIVES 1
#endif

#ifdef DEBUG_PRIMITIVES
/* code-qualified pointer: string literals live in code space, so this stays a
   16-bit MOVC read and does not pull the generic __gptrget helper into ROM. */
#if defined(__clang__)
/* the clang-only cast macro in debug.h would rewrite this definition (it
   looks like a call of the macro); drop it here and restore after the body   */
#undef debug_puts
#endif
void debug_puts(const char code *s) {
	while(*s) debug_putc((uint8_t)*s++);
}
#if defined(__clang__)
#define debug_puts(s) debug_puts((const char code *)(s))
#endif

/* Five decimal places (max 99999) cover every value in the firmware:
   temperatures come from an NTC table capped at 6000 c100 (+/- calib),
   setpoints and limits are <= 4500, the WS sensor reports up to 12500 c100
   (100.00..125.00 degC) and the RTC day counter runs past 20000. The fifth
   place (10000) must not be dropped: values >= 10000 would print as
   '0'+10 garbage (it was once removed under ROM pressure and had to come
   back). Only debug builds pay for it - UART_DEBUG=OFF links
   nothing from this file. */
static const uint16_t code debug_place[5] = { 10000, 1000, 100, 10, 1 };

/* decimal, minimal width (no leading zero): replaces %d / %u.
   The zero-padded form is a separate, RTC-only helper (debug_dec2).          */
void debug_dec(uint16_t v) {
	uint8_t i, d, started = 0;

	for(i = 0; i < 5; i++) {
		d = 0;
		while(v >= debug_place[i]) {
			v = (uint16_t)(v - debug_place[i]);
			d++;
		}
		if(d || started || i == 4) {
			debug_putc((uint8_t)('0' + d));
			started = 1;
		}
	}
}

#if UART_DEBUG && RTC_EN
/* zero-padded two digits, replaces %02u (the clock line hh:mm:ss)           */
void debug_dec2(uint8_t v) {
	uint8_t d = 0;

	while(v >= 10) {
		v = (uint8_t)(v - 10);
		d++;
	}
	debug_putc((uint8_t)('0' + d));
	debug_putc((uint8_t)('0' + v));
}
#endif /* RTC_EN */

/* signed decimal: replaces %d (and %u for values below 32768)               */
void debug_i16(int16_t v) {
	if(v < 0) {
		debug_putc('-');
		debug_dec((uint16_t)(0u - (uint16_t)v));
	}
	else {
		debug_dec((uint16_t)v);
	}
}

/* "<tag><decimal>\r\n" in one call: the common dump line ("sp 2100\r\n").
   Three calls at the site cost ~27 B of code, this costs ~9 B plus a ~50 B
   helper once - a net win from the third site onwards.                       */
#if defined(__clang__)
#undef debug_kv
#endif
void debug_kv(const char code *tag, int16_t v) {
	debug_puts(tag);
	debug_i16(v);
	debug_puts("\r\n");
}
#if defined(__clang__)
#define debug_kv(tag, v) debug_kv((const char code *)(tag), (v))
#endif
#endif /* DEBUG_PRIMITIVES */

#if UART_DEBUG && (RAW_EN || LINK_EN || LOCK_TRACE_EN)
/* fixed-width hex, replaces %02X/%04X (raw touch readings and the link dump).
   Every call site passes a constant width (2 or 4), so there is no nibble table
   here - that machinery alone cost 148 B. The `digits == 2` pre-shift IS the
   right-alignment of %02X: the loop always prints the TOP nibbles, so a byte
   (0x0066) must be moved into the high position first or it comes out "00".
   Do NOT drop it as dead code - a LINK_EN bench run showed exactly that
   regression: every dumped byte printed as 00 while the frame lengths were
   right.                                                                 */
void debug_hex(uint16_t v, uint8_t digits) {
	uint8_t i, n;

	if(digits == 2) v = (uint16_t)(v << 8);   /* %02X: show the low byte     */

	for(i = 0; i < digits; i++) {
		n = (uint8_t)(v >> 12);
		v = (uint16_t)(v << 4);
		debug_putc((uint8_t)(n < 10 ? (uint8_t)('0' + n)
		                            : (uint8_t)('A' + n - 10)));
	}
}
#endif /* RAW_EN || LINK_EN */

#if UART_DEBUG && TEMP_EN
/* -------- temperature log: four signed c100 samples, fixed format ---------- */

void debug_temp(int16_t in_live, int16_t in_acc,
                int16_t ext_live, int16_t ext_acc) {
	debug_puts("temp: i=");
	debug_i16(in_live);
	debug_putc('/');
	debug_i16(in_acc);
	debug_puts(" o=");
	debug_i16(ext_live);
	debug_putc('/');
	debug_i16(ext_acc);
	debug_puts("\r\n");
}

#endif /* UART_DEBUG && TEMP_EN */

#endif
