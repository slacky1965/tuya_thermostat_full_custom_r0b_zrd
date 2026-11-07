/* T2 backlight carrier ISR (vector 5), hand-written asm.                       */
/*                                                                              */
/* One software phase per base tick (T2 auto-reload, 192 counts at FOSC); P3.0  */
/* is high while phase < level, where phase counts 0..127 inside the carrier     */
/* period and level is the raw duty step (0..128) from Led_SetLevel. The phase  */
/* and level bytes live in FIXED XRAM cells (0x07F0/0x07F1) shared with led.c.  */
/*                                                                              */
/* Clobbers the same context class as the backend-generated ISRs: ACC, PSW,     */
/* DPL/DPH and R0 (bank 0 - this project never switches register banks).        */

.section .mcs51.vector.5, "ax"
ljmp TIMER2_Backlight_ISR

.section .text, "ax"
.globl TIMER2_Backlight_ISR
TIMER2_Backlight_ISR:
	push  0xD0                      /* PSW                                     */
	push  0xE0                      /* ACC                                     */
	push  0x82                      /* DPL                                     */
	push  0x83                      /* DPH                                     */
	push  0x00                      /* R0                                      */

	mov   a, 0xC9                   /* T2MOD                                  */
	jnb   0xE7, .Lrf2               /* ACC.7 = TF2 clear?                     */
	anl   a, #0x1F                  /* W1C TF2, keep CCFG/T2M                 */
	orl   a, #0x80
	mov   0xC9, a

	mov   dptr, #0x07F0             /* phase cell                             */
	movx  a, @dptr
	inc   a                         /* 0xFF sentinel wraps to 0               */
	anl   a, #0x7F                  /* phase = (phase+1) mod 128              */
	movx  @dptr, a
	mov   r0, a
	inc   r0                        /* phase+1 (1..128)                       */

	mov   dptr, #0x07F1             /* level cell                             */
	movx  a, @dptr
	clr   c
	subb  a, r0                     /* C = (level < phase+1)                  */
	jc    .Llow
	orl   0xB0, #0x01               /* P3.0 high                              */
	sjmp  .Lrf2
.Llow:
	anl   0xB0, #0xFE               /* P3.0 low                               */

.Lrf2:
	mov   a, 0xC9                   /* T2MOD                                  */
	jnb   0xE5, .Ldone              /* ACC.5 = RF2 clear?                     */
	anl   a, #0x1F                  /* W1C RF2                                */
	orl   a, #0x20
	mov   0xC9, a

.Ldone:
	pop   0x00
	pop   0x83
	pop   0x82
	pop   0xE0
	pop   0xD0
	reti
