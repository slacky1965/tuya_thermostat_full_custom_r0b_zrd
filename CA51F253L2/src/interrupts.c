
/* Timer0 ISR (vector 1) lives in main.c - 1 s time base */
/* Timer2 ISR (vector 5) lives in led.c - backlight carrier (T2-PWM); T1 is    */
/* free but its clock is prescaled by 256 on this chip (level.sr capture).     */
void isr_vect_uart0(void) __interrupt (4) { }
/* UART1 ISR (vector 6) lives in uart.c - the ZT3L link */
void isr_vect_uart2_int3(void) __interrupt (8) { }