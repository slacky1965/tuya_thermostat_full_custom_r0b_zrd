/* Define the XSFRs (volatile __xdata __at) in this translation unit: the SDK
   header only emits definitions when _MAIN_C_ is set (main.c does the same). */
#define _MAIN_C_
/***********************************************************************************/
/* XRAM-resident OTA writer for the CA51F2.                                        */
/* Built separately, linked at code 0x8000; the app copies this image to XRAM      */
/* 0x0000 and sets MECON.REMAP=1, so it runs from RAM while flash is written.      */
/* No interrupts: the vector table lives in the flash being erased.                */
/*                                                                                 */
/* Host (TLSR) protocol, raw bytes:                                                */
/*   block   TLSR -> flasher: SYNC OFF_HI OFF_LO LEN DATA[LEN] CRC8                */
/*   ACK/NAK flasher -> TLSR (0x06 / 0x15 + OFF_HI + OFF_LO)                       */
/*   READY   flasher -> TLSR after entry and after RESTART                         */
/*   DONE    flasher -> TLSR after the whole image is written                      */
/*   RESTART TLSR -> flasher (reset the write offset to 0)                         */
/* bin_size (u16 LE) is published by the app at XRAM 0x07F0.                       */
/***********************************************************************************/
#include "include/stdint.h"
#include "include/ca51f2sfr.h"
#include "include/ca51f2xsfr.h"
#include "include/gpiodef_f2.h"
#include "include/config.h"
#include "include/chars.h"
#include "include/debug.h"
#include "../include_common/link_proto.h"
/***********************************************************************************/

#define SIZE_CELL   ((uint16_t xdata *)0x07F0)
#define FLASHER_RX_TIMEOUT_INNER 0xFFFFu
#if LNK_OTA_RSP_LEN != 3
#error OTA status length mismatch
#endif

static uint8_t xdata blk[LNK_OTA_BLOCK_MAX];

/* Paint two big-digit characters on the r0b glass and force the display on.
   LCD mode (the app's frame setup is re-asserted so show() is self-contained).
   r0b pairs: tens = cells 12 (E,G,F) + 13 (D,C,B,A), units = 14 + 15
   (doc_local/lcd_cell_map_r0b.md): left part E/G/F = row1/row2/row3,
   right part D/C/B/A = row0..row3. The 7-seg packing is chars.h.             */
static void draw7(uint8_t cell_left, uint8_t seg7) {
	uint8_t l;
	uint8_t r;

	l = 0;
	if(seg7 & 0x10) l |= 0x02;         /* E = row1 */
	if(seg7 & 0x40) l |= 0x04;         /* G = row2 */
	if(seg7 & 0x20) l |= 0x08;         /* F = row3 */
	INDEX = cell_left; LXDAT = l;

	r = 0;
	if(seg7 & 0x08) r |= 0x01;         /* D = row0 */
	if(seg7 & 0x04) r |= 0x02;         /* C = row1 */
	if(seg7 & 0x02) r |= 0x04;         /* B = row2 */
	if(seg7 & 0x01) r |= 0x08;         /* A = row3 */
	INDEX = (uint8_t)(cell_left + 1); LXDAT = r;
}

static void show(uint8_t t, uint8_t u) {
	/* r0b pin set: 5 COM (P0.0..P0.4 = 3, P04F selects COM4) + 16 SEG.        */
	P00F = 3; P01F = 3; P02F = 3; P03F = 3; P04F = 3;
	P10F = 3; P11F = 3; P12F = 3; P13F = 3; P16F = 3; P17F = 3;
	P24F = 3; P25F = 3; P26F = 3; P27F = 3;
	P60F = 3; P61F = 3; P62F = 3; P63F = 3; P64F = 3; P65F = 3;
	LXCON  = 0x40;                      /* LEN=IRCH, LMOD=LCD                    */
	LXDIVL = 0x64;                      /* ~71 Hz frame (stock r0b value)        */
	LXDIVH = 0x00;
	LXCFG  = 0xE7;                      /* DMOD=130uA, BIAS=1/3, drive max       */
	draw7(12, t);
	draw7(14, u);
}

/* Receive one byte with the bounded 100 ms contract; returns -1 on timeout. */
static int16_t rx_byte_timed(void) {
	uint16_t i;
	for(i = 0; i < FLASHER_RX_TIMEOUT_INNER; i++) {
		if(S1CON & 0x01) {
			S1CON = (S1CON & 0xFC) | 0x01;
			return (int16_t)S1BUF;
		}
	}
	return -1;
}

/* CRC-8/MAXIM (reflected 0x8C), same convention as uart.c. The block CRC covers
   OFF_HI, OFF_LO, LEN and DATA (matches the TLSR sender). */
static uint8_t crc8_byte(uint8_t c, uint8_t b) {
	uint8_t i;
	c ^= b;
	for(i = 0; i < 8; i++) {
		c = (c & 0x01) ? (uint8_t)((c >> 1) ^ 0x8C) : (uint8_t)(c >> 1);
	}
	return c;
}

static uint8_t crc8_buf(uint8_t c, const uint8_t xdata *p, uint8_t n) {
	while(n--) c = crc8_byte(c, *p++);
	return c;
}

static void uart_init(void) {
	uint16_t reload = (uint16_t)(0x400UL - (FOSC / ((uint32_t)UART1_BAUTRATE * 32UL)));

	GPIO_Init(P67F, P67_UART1_RX_SETTING);
	GPIO_Init(P66F, P66_UART1_TX_SETTING);
	S1RELH = (uint8_t)(reload >> 8);
	S1RELL = (uint8_t)reload;
	S1CON  = 0xD0;                     /* mode 1, RX enable, interrupts off */
}

static void tx_byte(uint8_t b) {
	S1BUF = b;
	while(!(S1CON & 0x02)) { }
	S1CON = (S1CON & 0xFC) | 0x02;
}

static void tx_status(uint8_t status, uint16_t off) {
	tx_byte(status);
	tx_byte((uint8_t)(off >> 8));
	tx_byte((uint8_t)off);
}

static void hold_done(uint8_t t) {
    uint8_t o;
    t *= 10;
    for(o = 0; o < t; o++) {
        tx_byte(LNK_OTA_DONE);
        rx_byte_timed();
    }
}

/* The flasher log is gated on FLASHER_EN ALONE: the flasher owns its own
   polled UART0 (log_init below) and does not use debug.c, so requiring
   UART_DEBUG as well would make the log unbuildable on SDCC builds whose
   application no longer fits the extra debug code. */
#if FLASHER_EN
static void log_init(void) {
	uint16_t reload = (uint16_t)(0x10000UL - (FOSC / (115200UL * 32UL)));

	P31F = P31_UART0_RX_SETTING;
	P30F = P30_UART0_TX_SETTING;
	T2CON = 0x24;
	T2CH = (uint8_t)(reload >> 8);
	T2CL = (uint8_t)reload;
	TH2 = (uint8_t)(reload >> 8);
	TL2 = (uint8_t)reload;
	TR2 = 1;
	S0CON = 0x50;
}

static void log_c(uint8_t c) {
	S0BUF = c;
	while(!TI0) { }
	TI0 = 0;
}
#else
/* debug.h UART_DEBUG/FLASHER_EN off: no UART0 text log (the TLSR side prints    */
/* every ACK/NAK it receives, and the log needs a USB-UART on P3.0/P3.1 anyway). */
#define log_init()       /* no flasher log */
#define log_c(c)         /* no flasher log */
#endif

static uint8_t flash_cmd_done(void) {
	uint16_t timeout = 0xFFFFu;

	while((uint8_t)FSCMD && timeout) timeout--;
	return (uint8_t)(FSCMD == 0);
}

static uint8_t write_sector_checked(uint16_t off, uint8_t len) {
	uint8_t i;
	uint8_t readback;
	uint8_t ok = 1;

	FSCMD = 0;
	LOCK  = 0x29;
	PTSH  = (uint8_t)(off >> 8);
	PTSL  = (uint8_t)off;
	FSCMD = 7;
	if(!flash_cmd_done()) {
		FSCMD = 0;
		LOCK  = 0xAA;
		return 0;
	}
	LOCK  = 0x29;
	PTSH  = (uint8_t)(off >> 8);
	PTSL  = (uint8_t)off;
	FSCMD = 6;
	if((uint8_t)FSCMD != 6) {
		FSCMD = 0;
		LOCK  = 0xAA;
		return 0;
	}
	for(i = 0; i < len; i++) {
		FSDAT = blk[i];
	}
	FSCMD = 0;
	LOCK  = 0x29;
	PTSH  = (uint8_t)(off >> 8);
	PTSL  = (uint8_t)off;
	FSCMD = 5;
	if((uint8_t)FSCMD != 5) {
		FSCMD = 0;
		LOCK  = 0xAA;
		return 0;
	}
	for(i = 0; i < len; i++) {
		readback = FSDAT;
		if(readback != blk[i]) ok = 0;
	}
	FSCMD = 0;
	LOCK  = 0xAA;
	return ok;
}

void main(void) {
	uint16_t total;
	uint16_t expected_off;
	uint8_t  b, len, crc;
	uint16_t off, i, c;
	int16_t  rb;

	WDCON = 0;
	uart_init();
	log_init();
	log_c('F'); log_c('\r'); log_c('\n');

	total = *SIZE_CELL;
	expected_off = 0;
	if(total < LNK_OTA_BLOCK_MAX || total > LNK_OTA_PROGRAM_MAX) {
		show(CH_E, CH_R);
		for(;;) { }
	}

	while(S1CON & 0x01) {
		(void)S1BUF;
		S1CON = (S1CON & 0xFC) | 0x01;
	}

	show(CH_U, CH_P);
	tx_byte(LNK_OTA_READY);

	for(;;) {
		rb = rx_byte_timed();
		if(rb < 0) {
			log_c('E'); log_c('\r'); log_c('\n');
			show(CH_E, CH_R);
			continue;
		}
		b = (uint8_t)rb;
		if(b == LNK_OTA_RESTART) {
			expected_off = 0;
			log_c('R'); log_c('\r'); log_c('\n');
			show(CH_U, CH_P);
			tx_byte(LNK_OTA_READY);
			continue;
		}
		if(b != LNK_OTA_SYNC) continue;

		rb = rx_byte_timed();
		if(rb < 0) goto frame_timeout;
		off = (uint16_t)((uint16_t)rb << 8);
		rb = rx_byte_timed();
		if(rb < 0) goto frame_timeout;
		off |= (uint16_t)rb;
		rb = rx_byte_timed();
		if(rb < 0) goto frame_timeout;
		len = (uint8_t)rb;
		if(len == 0 || len > LNK_OTA_BLOCK_MAX) {
			tx_status(LNK_OTA_NAK, off);
			continue;
		}
		for(i = 0; i < (uint16_t)len; i++) {
			rb = rx_byte_timed();
			if(rb < 0) goto frame_timeout;
			blk[i] = (uint8_t)rb;
		}
		rb = rx_byte_timed();
		if(rb < 0) goto frame_timeout;
		c = (uint16_t)rb;
		crc = 0x00;
		crc = crc8_byte(crc, (uint8_t)(off >> 8));
		crc = crc8_byte(crc, (uint8_t)off);
		crc = crc8_byte(crc, len);
		crc = crc8_buf(crc, blk, len);
		if(c != crc) {
			tx_status(LNK_OTA_NAK, off);
			continue;
		}

		if(off > expected_off || (off % 128u) != 0 ||
		   off + (uint16_t)len > total ||
		   off + (uint16_t)len > LNK_OTA_PROGRAM_MAX) {
			tx_status(LNK_OTA_NAK, off);
		}
		else if(off < expected_off) {
			tx_status(LNK_OTA_ACK, off);
		}
		else if(!write_sector_checked(off, len)) {
			show(CH_E, CH_R);
			tx_status(LNK_OTA_NAK, off);
		}
		else {
			expected_off += len;
			tx_status(LNK_OTA_ACK, off);
		}

		if(expected_off == total) {
			tx_byte(LNK_OTA_DONE);
			log_c('D'); log_c('\r'); log_c('\n');
			show(CH_A, CH_L);
		    /* Hold 3 sec */
			hold_done(3);
#if defined(__clang__)
			/* An unconditional assembly LJMP to the flash
			 * reset vector is the honest spelling.
			 */
			__asm__ volatile ("ljmp 0x0000");
#else
			((void (code *)(void))0x0000)();   /* never returns                */
#endif
			for(;;) { }
		}
		continue;

frame_timeout:
		log_c('E'); log_c('\r'); log_c('\n');
		show(CH_E, CH_R);
	}
}
