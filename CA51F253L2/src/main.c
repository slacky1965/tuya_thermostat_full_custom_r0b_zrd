/* Define this TU as THE owner of the XSFR bodies: ca51f2xsfr.h emits the
   XRAM_U8/U16 definitions only when _MAIN_C_ is set, every other TU sees
   externs (this used to come from the file's include guard, which was
   removed - the switch stays). */
#define _MAIN_C_
#include "include/stdint.h"
#include "include/config.h"
#include "include/ca51f2sfr.h"
#include "include/ca51f2xsfr.h"
#include "include/system_clock.h"
#include "include/buttons.h"
#include "include/debug.h"
#include "include/uart.h"
#include "include/link_proto.h"
#include "include/ota.h"
#include "include/led.h"
#include "include/lcd.h"
#include "include/led_format.h"
#include "include/delay.h"
#include "include/chars.h"
#include "include/hw_test.h"
#include "include/sensor_ntc.h"
#include "include/temp_pipeline.h"
#include "include/relay.h"
#include "include/rtc.h"
#include "include/settings.h"
#include "include/modes.h"
/* Boot display window: was the r02 LED boot test; doubles as the link-service */
/* window - the peer sends its Info request ~5 s after power-on, so Uart1_Poll */
/* runs in every delay slice. Boot algorithm:                                  */
/*   early: Lcd_Init + Lcd_Clear, backlight on (Pwm_Backlight_Init)           */
/*   here:  all symbols on the glass 3 s -> clear + firmware version 2 s on    */
/*          clock digits 2..4 (the big digits have no hundreds "1"; BCD:      */
/*          major nibble, minor tens, minor ones) -> clear                      */
/*   after: the normal ON/OFF screen (Thermo_Apply)                             */
static void boot_pump_ms(uint16_t ms) {
	uint16_t t;

	for(t = 0; t < (uint16_t)(ms / 100); t++) {
		WDFLG = 0xA5;
		Delay_ms(100);
		Uart1_Poll();
		Debug_Poll();
	}
}

static void Boot_DisplayWindow(void) {
	uint8_t i;
	uint8_t ver;

	/* all symbols on the glass */
	for(i = 0; i < LCD_CELLS; i++) {
		Lcd_WriteCell(i, LCD_SEG_ALL);
	}
	Lcd_Flush();
	boot_pump_ms(3000);

	/* firmware version on clock digits 2..4 (single-nibble major) */
	Lcd_Clear();
	ver = (uint8_t)(APP_VERSION & 0xFF);
	Lcd_DigitIdx(5, (uint8_t)((APP_VERSION >> 8) & 0x0F)); /* clock digit 2 */
	Lcd_DigitIdx(6, (uint8_t)((ver >> 4) & 0x0F));         /* clock digit 3 */
	Lcd_DigitIdx(7, (uint8_t)(ver & 0x0F));                /* clock digit 4 */
	Lcd_Flush();
	boot_pump_ms(2000);

	/* clear before the normal flow */
	Lcd_Clear();
}

/* Photo/documentation freeze lives in src/hw_test.c (Photo_ModeMain). */

/*********************************************************************************************************************
	TOUCH TEST, modular + events.
	buttons.c/h - touch module, key events (CLICK/HOLD/HOLD_REPEAT/RELEASE),
	slow auto-tracking of idle baselines, per-key thresholds (see buttons.c).
	debug.c/h - UART0 output + per-module debug switches (BUTTONS_EN/LED_EN);
	diagnostics are printed only for modules with *_EN = ON.
	main.c - wiring only: PWM backlight + button event diagnostics.
*********************************************************************************************************************/

#define LVDE(N)        (N<<7)
#define LVDS_reset     (1<<6)
#define LVDF           (1<<5)
#define LVDTH_1p8V     0

#define WDTS(N)        (N<<5)
#define WDRE(N)        (N)
#define WDTS_IRCH      1
#define WDRE_reset     1

/* One-second time base: Timer0, 16-bit, 10 ms x 100. Timer0-as-timer counts  */
/* at sysclk/12 (per user guide 12.1, table 12-1-2-2 CT0=0). With IRCH at     */
/* 3.6864 MHz -> 307200 Hz -> 10 ms = 3072 counts -> reload 0xF400.           */
#define T0_10MS_RELOAD   0xF400      /* 0x10000 - 3072 = 62464   */
#define SEC_TICKS        100         /* 100 x 10 ms = 1 s        */

static bit sec_flag;                      /* set by Timer0 ISR once per second           */
static uint8_t t0_cnt;                    /* 10 ms counter 0..99 (rollover at SOC_TICKS) */

/* Real 10 ms time base exported to buttons.c. One touch conversion takes     */
/* ~20..30 ms, and HOLD/REPEAT must NOT be measured in conversions (500 of    */
/* them = 10..15 s on the bench). Here, in the Timer0 10 ms ISR, we advance a */
/* global that the buttons module reads: 50 ticks = 0.5 s, 6 = 60 ms.         */
volatile uint16_t xdata tick10ms;         /* real 10 ms ticks since boot     */

uint16_t BTN_Tick10ms(void) {
	uint16_t data value;
#if defined(__clang__)
	/* clang rejects address-space-qualified automatics; keep EA in MEMORY    */
	/* (volatile): clang may otherwise leave the saved value in ACC, which    */
	/* the MOVX below clobbers before the restore reads it back.              */
	volatile unsigned char ea;
#else
	bit ea;
#endif

	ea = EA;
	EA = 0;
	value = tick10ms;
	EA = ea;
	return value;
}

/* OFF DOWN hold >= 2 s arms the NET blink; the arm itself stays here (main()  */
/* owns the arms), the animation lives in src/screen.c.                        */
#define NET_HOLD_10MS_TICKS  200             /* 2.0 s DOWN hold while OFF arms the blink */
static uint8_t xdata down_arm;               /* 1 = OFF DOWN hold being measured         */
static uint16_t xdata down_arm_tick;         /* tick10ms when the hold was armed         */

/* Power-transition memory: main() compares these once per loop iteration.      */
static bit prev_power;
static bit prev_ext;

/* Working temperature source; set from settings after settings_restore().     */
/* xdata initialiser dropped: xdata_clear() zeroes it (it is re-read at boot). */
temp_src_t temp_src /* = SENSOR_SRC_IN */;

bit clock_colon;                             /* ':' blink state                   */
#define ADJ_DISPLAY_SECS  7                  /* adjust-display hold, seconds      */
#define HUMID_SHOW_SECS   3                  /* WS humidity window                */
uint16_t xdata adj_remain = 0;               /* seconds left of adjust display    */
static uint8_t xdata set_need_save;          /* arrows changed the setpoint:      */
                                             /* save+report on RELEASE            */

/* Clock-set mode state and constants live in src/clock_mode.c;    */
/* CLK_SET_* and the entry points are declared in include/modes.h. */
/* One MENU hold (3.0 s) feeds both targets: clock-set while ON,    */
/* the settings menu while OFF (entry gesture of the original      */
/* thermostat); the target is picked when the arm fires in the      */
/* main loop.                                                       */
#define CLK_ENTER_10MS_TICKS  300           /* 3.0 s MENU hold to enter         */
static uint8_t xdata clk_arm;               /* 1 = MENU hold being measured     */
static uint16_t xdata clk_arm_tick;         /* tick10ms when the hold was armed */

/* Power (leaf) key: a >= 1.2 s hold toggles the thermostat, so a single tap    */
/* cannot turn it on/off by accident. The constant is the TOTAL hold time from  */
/* the press; the arm fires BTN_HOLD_10MS_TICKS later and the comparison below  */
/* uses the derived remainder (see PWR_ARM_TICKS).                              */
#define PWR_TOGGLE_10MS_TICKS  120          /* 1.2 s total leaf hold to toggle  */
static uint8_t xdata pwr_arm;               /* 1 = POWER hold being measured    */
static uint16_t xdata pwr_arm_tick;         /* tick10ms when the hold was armed */
static bit pwr_arm_power;                   /* power state when the arm was set */

/* Every long-press constant above (CLK_ENTER / NET_HOLD /                        */
/* PWR_TOGGLE) is the TOTAL hold time measured from the initial press. An arm is  */
/* raised when BTN_EVT_HOLD arrives - BTN_HOLD_10MS_TICKS after the press - so   */
/* the arm comparisons use the post-HOLD remainder derived once here instead of  */
/* re-adding or eyeballing the offset at every call site.                        */
#define ARM_TICKS_AFTER_HOLD  BTN_HOLD_10MS_TICKS
#define CLK_ARM_TICKS   (CLK_ENTER_10MS_TICKS - ARM_TICKS_AFTER_HOLD)
#define NET_ARM_TICKS   (NET_HOLD_10MS_TICKS - ARM_TICKS_AFTER_HOLD)
#define PWR_ARM_TICKS   (PWR_TOGGLE_10MS_TICKS - ARM_TICKS_AFTER_HOLD)
#if (CLK_ARM_TICKS < 1) || (NET_ARM_TICKS < 1) || (PWR_ARM_TICKS < 1)
#error Long-press arms must stay longer than the HOLD event itself
#endif

/*********************************************************************************/
static void T0_1s_Init(void) {
	TMOD = (TMOD & 0xF0) | 0x01;       /* Timer0, 16-bit mode 1 */
	TH0 = (uint8_t)(T0_10MS_RELOAD >> 8);
	TL0 = (uint8_t)(T0_10MS_RELOAD);
	TR0 = 1;
	ET0 = 1;
}

void T0_ISR (void) __interrupt (1) {
	TH0 = (uint8_t)(T0_10MS_RELOAD >> 8);
	TL0 = (uint8_t)(T0_10MS_RELOAD);
	tick10ms++;                    /* real 10 ms tick for buttons HOLD/REPEAT */
	if(++t0_cnt >= SEC_TICKS) {
//	    DEBUG(TIMER_EN, "T0_ISR() - adj_remain: %d\r\n", adj_remain);
		t0_cnt = 0;
		sec_flag = 1;
		if (adj_remain) adj_remain--;
	}
}
/*********************************************************************************/
void WDT_Init(void) {
	WDCON  = WDTS(WDTS_IRCH) | WDRE(WDRE_reset);
	WDVTHH = 0x07;
	WDVTHL = 0x08;
	WDFLG  = 0xA5;
}
/*******************************************************************************/
/*** Link protocol (link_proto.c): dispatcher + server-side entity handlers. ***/
/*******************************************************************************/
/* Clear the xdata area SDCC leaves uninitialised (XSEG). An 8051 reset does     */
/* not clear RAM and SDCC's startup only clears IRAM and copies the few bytes    */
/* of initialised xdata (XISEG), so without this every xdata static (mode flags, */
/* link state, ...) would keep its value from the previous run. The loop         */
/* counters are forced into IRAM ('data'): automatic variables live in xdata     */
/* under --model-large and would otherwise be wiped mid-loop.                    */
/*********************************************************************************/
static void xdata_clear(void) {
	uint8_t xdata * data p = (uint8_t xdata *)0x0000;
	uint16_t data n = 0x0800;          /* XRAM size (--xram-size 2048)          */

	while(n) {
		*p = 0;
		p++;
		n--;
	}
}

/********************************************************************************************************************/
/* A scalar setting arrived from the network (h_setting): show its value for ADJ_DISPLAY_SECS exactly like a manual */
/* setpoint edit - "Set" on, thermometer off, the value in the big digits (sensor -> IN/OU/AL letters,              */
/* calibration/deadBand x10 -> x100, small enums as a single digit - the settings-menu render). The existing        */
/* adj_remain expiry (sec_flag block) restores HOME/SET and the measured temperature. Called only while ON with no  */
/* clock/schedule/menu mode owning the display.                                                                     */
/********************************************************************************************************************/
static void Set_Win_Show(uint8_t cmd) {
	int16_t v;

	/* Remote manual/schedule change: NO value window (it used to print 0/1     */
	/* under the Set icon). Only flip the two mode icons with the same mapping  */
	/* as the local MENU click and leave the rest of the screen untouched; the  */
	/* digit-zone cleanup below must not run either.                            */
	if(cmd == LNK_CMD_PROG_MODE) {
		ProgMode_Icons(settings.progMode);
		Lcd_Flush();
		return;
	}

	/* Digit-zone cleanup first: Lcd_MenuValue/Lcd_CharIdx/Lcd_DigitIdx rewrite  */
	/* only the digit segments, so degree C, the dot and the tenths digit       */
	/* (separate symbols/cells) would survive from the measured temperature      */
	/* screen, and the single-digit branches below never touch the tens cell at  */
	/* all (would render brightness 7 as "2.7"). Each branch then draws exactly  */
	/* what it needs; degree C and the tenths come back only for values that ARE */
	/* temperatures (setpoint via Lcd_Temp).                                     */
	Lcd_SymSet(SYM_DEGC, 0);
	Lcd_HalfDot(0);
	Lcd_DigitSmall(0);                   /* no stale tenths (e.g. temp 24.8 -> "8") */
	Lcd_DigitIdx(3, 10);                  /* blank the tens cell (TEMP_DIG_T)      */
	/* %RH belongs to this window only: lit for the humidity window, cleared    */
	/* for every other value window (and by the expiries below).                */
	Lcd_SymSet(SYM_HUMID, (uint8_t)(cmd == LNK_CMD_HUMID_NET));

	switch(cmd) {
	case LNK_CMD_SETPOINT_HEAT:
		Lcd_Temp(settings.occupiedHeatingSetpoint);   /* x100, draws + degree C */
		break;
	case LNK_CMD_SENSOR_SRC: {
		uint8_t s = settings.sensosUsed;
		if(s > SENSOR_SRC_ALL) s = SENSOR_SRC_ALL;
		Lcd_SensorLetters(s);               /* no degree, no '.5'              */
	}
		break;
	case LNK_CMD_CAL_ACTIVE:
		v = (int16_t)settings.localTemperatureCalibration * 10;   /* x10 -> x100 */
		Lcd_MenuValue(v);
		Lcd_SymSet(SYM_DEGC, 1);        /* a temperature value                 */
		break;
	case LNK_CMD_CAL_EXTERNAL:
		v = (int16_t)settings.outTemperatureCalibration * 10;
		Lcd_MenuValue(v);
		Lcd_SymSet(SYM_DEGC, 1);
		break;
	case LNK_CMD_HYSTERESIS:
		v = (int16_t)settings.deadBand * 10;
		Lcd_MenuValue(v);
		Lcd_SymSet(SYM_DEGC, 1);
		break;
	case LNK_CMD_LIMIT_MIN:
		Lcd_MenuValue(settings.minHeatSetpointLimit);  /* already x100        */
		Lcd_SymSet(SYM_DEGC, 1);
		break;
	case LNK_CMD_LIMIT_MAX:
		Lcd_MenuValue(settings.maxHeatSetpointLimit);
		Lcd_SymSet(SYM_DEGC, 1);
		break;
	/* A remote keypad lock has NO value window: the 0/1/2 used to be printed    */
	/* under the Set icon. h_setting raises only the padlock channel for it, so  */
	/* Link_TakeLockChanged() -> Lock_On_Screen() (or Lock_Flash_Start() while   */
	/* OFF) does all the drawing.                                                */
	case LNK_CMD_BRIGHT_DAY:
		Lcd_DigitIdx(2, settings.currentLevel_day);
		break;
	case LNK_CMD_BRIGHT_NIGHT:
		Lcd_DigitIdx(2, settings.currentLevel_night);
		break;
	case LNK_CMD_HUMID_NET:
		/* WS humidity window: the ZT3L rounded the value to whole percents and */
		/* mapped 100 -> 99, so render it as-is. "--" = the sensor is not fresh */
		/* (no trustworthy value). The preamble above already cleared the       */
		/* degree symbol and the '.5' dot.                                      */
		if(Link_WsFresh()) {
			Lcd_MenuValue((int16_t)Link_WsHumid());
		}
		else {
			Lcd_Letters(CH_MINUS, CH_MINUS);
		}
		break;
	default:
		return;                       /* not a scalar setting: no window        */
	}
	adj_remain = (cmd == LNK_CMD_HUMID_NET) ? HUMID_SHOW_SECS : ADJ_DISPLAY_SECS;
	/* The value window replaces the temperature view: the house+thermometer   */
	/* (SYM_THERMO) goes OFF with it and is re-lit after the window expires     */
	/* (the next 1 s tick with adj_remain==0, or Thermo_Apply on a repaint).    */
	Lcd_SymSet(SYM_THERMO, 0);
	Lcd_SymSet(SYM_SET, (uint8_t)(cmd != LNK_CMD_HUMID_NET));
	Bright_MaxHold();                 /* a dimmed screen must show the window   */
	Lcd_Flush();
}

/*********************************************************************************************************************/
/* Leave the schedule-set mode the way POWER/MENU leave it: in the day editor commit the current slot and persist    */
/* only when something actually changed (Settings_Persist skips an identical image); in the day hub there is         */
/* nothing to store. Shared by the key exit and the 20 s idle timeout so both behave identically.                    */
static void Sched_Set_Leave(void) {
	uint8_t day;
	uint8_t sorted = 0;
	uint8_t mask = 0;

	/* Commit the slot being edited FIRST: Sched_Set_Store writes through the   */
	/* sched_slot cursor, and the week-wide sort below can move that index onto */
	/* a different slot - the store would then overwrite an unrelated entry.    */
	/* Only after that normalize the whole week: the editor keeps the storage    */
	/* order the user typed and network days may arrive unsorted - ascending by */
	/* time with empties last is the canonical form for the slot icons and z2m. */
	/* A re-ordered day must be flashed and reported to ZT3L even when this     */
	/* session itself edited nothing (sched_changed = 0), hence the sorted flag */
	/* joining the persist/send condition below.                                */
	if(!sched_hub) {
		Sched_Set_Store();                /* commit the current slot          */
	}
	for(day = 0; day < 7; day++) {
		if(Sched_DaySort(Sched_DayRow(day))) {
			mask |= (uint8_t)(1U << day);
			sorted = 1;
		}
	}
	sched_send_mask |= mask;

	if(sched_changed || sorted) {
		if(Settings_Persist()) Sched_SendPending();
		else sched_send_mask = 0;
	}
	Sched_Set_Exit();
	DEBUG(BUTTONS_EN, { debug_puts("sched leave\r\n"); });
}

/**********************************************************************************************************************/
/* One synchronous probe pass shared by the boot path and the 1 s tick: read both probes and feed the temperature     */
/* pipeline (per-sensor calibration applied). main() runs it BEFORE the first paint, so the restored ON screen never  */
/* shows "Er" while the pipeline is still empty (the first 1 s tick would arrive a second later); the tick calls it   */
/* every second, keeping a single place where the probes are read.                                                    */
static void Temp_MeasureProbes(void) {
	uint8_t v;
	XDATA_TMP(int16_t, c100);

	v = Temp_ReadC100Checked(TEMP_EXTERNAL, &c100);
	Temp_Pipeline_Update(TEMP_EXTERNAL, c100, v,
	                     settings.outTemperatureCalibration);
	v = Temp_ReadC100Checked(TEMP_INTERNAL, &c100);
	Temp_Pipeline_Update(TEMP_INTERNAL, c100, v,
	                     settings.localTemperatureCalibration);
}

/**********************************************************************************************************************/
void main(void) {
	XDATA_TMP(btn_id_t, b);

	b = BTN_MENU;
	xdata_clear();                      /* xdata is not cleared on reset          */
	Temp_Pipeline_Init();

	Sys_Clk_Set_IRCH();

	/* LCD as early as possible: xdata is already clean (the shadow buffer      */
	/* relies on it), the frame clock divides IRCH, and the glass starts blank.  */
	/* Above it only xdata_clear/pipeline/clk - things more critical than the     */
	/* screen. Safe here: ISRs are not enabled yet, so the shared INDEX/LXDAT    */
	/* window cannot be contested.                                               */
	Lcd_Init();
	Lcd_Clear();

	LVDCON = (uint8_t)(LVDE(1) | LVDS_reset | LVDF | LVDTH_1p8V);

#if CLOCK_TEST_EN
	/* Clock bench test: like the LCD test it never reaches the main-loop /      */
	/* button feeds (WDFLG=0xA5), so the armed watchdog (~seconds) would reset    */
	/* the chip in a loop and chop the measured waveform mid-phase.               */
	Clock_TestMain();                  /* clock measurement bench; never returns */
#elif LCD_TEST_EN
	/* No WDT in the LCD bench test: the test never reaches the main-loop /     */
	/* button feeds (WDFLG=0xA5), so the armed watchdog (~seconds) would reset   */
	/* the chip in a loop and chop every marker mid-pulse.                       */
	LCD_TestMain();                    /* LCD bench mapping build; never returns */
#elif BRIGHT_TEST_EN
	/* Brightness bench: like the other tests it never reaches the main-loop /   */
	/* button feeds (WDFLG=0xA5), so the watchdog stays unarmed.                 */
	Bright_TestMain();                /* backlight brightness bench; never returns */
#elif PHOTO_TEST_EN
	/* Photo/documentation freeze: full brightness, all segments, no WDT.       */
	Photo_ModeMain();                 /* never returns */
#elif RELAY_TEST_EN
	/* Relay pulse bench (latching relay): no main-loop / button feeds, so the */
	/* watchdog stays unarmed; the bench drives P4.3/P4.4 impulses itself.      */
	Relay_TestMain();                 /* never returns */
#else
	WDT_Init();
#endif

	Pwm_Backlight_Init();

	/* ZT3L link first: bring UART1 up BEFORE the ~5 s link window so the Info    */
	/* request (sent ~5 s after power-on) is not missed. The RX ISR fills the     */
	/* ring; Boot_LinkWindow() pumps Uart1_Poll() from its delay slices, so       */
	/* frames are parsed AND answered during the wait too.                        */
	Uart1_Initial();                    /* interrupt-driven RX ring              */
	Uart1_Protocol_Init(Link_OnFrame);  /* register the frame callback           */
	Link_Init();                        /* clear link state (xdata not reset)    */
	EA = 1;                             /* enable the interrupt system           */

	/* Ready BEFORE the test: from the first slice Link_OnFrame may run, so a   */
	/* hub write arriving in the window must see a valid settings image, and    */
	/* handler DEBUG lines must go through an initialized debug port.           */
	debug_init();
	PADRD = (uint8_t)FLASH_PADRD;
	settings_persist_pending = !settings_restore();
	temp_src = (temp_src_t)settings.sensosUsed;

	/* Start in the LAST SAVED state: systemMode is the ZCL enum (0x00/0x04). */
	/* Seed the link sentinels FIRST - Boot_LinkWindow() already answers frames  */
	/* from its delay slices, and a STATE_ALL dump must not go out with the   */
	/* stale 0xFF sentinel (which reads back as OFF).                         */
	power_on   = (settings.systemMode != SYS_MODE_OFF);
	prev_power = power_on;
	Link_UpdateStates((uint8_t)power_on, 0);

	Boot_DisplayWindow();

	/* Normal-mode boot is up: announce it so the peer runs a fresh Info        */
	/* handshake (a rebooted CA51F2 would otherwise be invisible to the TLSR).  */
	Link_BootNotify();


    BTN_Init();
	Temp_Init();
	Relay_Init();                        /* P4.3/P4.4 latching relay, OPEN at boot */

	RTC_Init();                          /* real-time clock on the 32.768k crystal */
//	DEBUG(RTC_EN, "rtc %s\r\n", RTC_ClockSrc() ? "ircl" : "xoscl");

	/* First probe pass BEFORE the paint: the pipeline was zeroed at boot and    */
	/* Temp_Draw() shows "Er" for an invalid sample - without this the restored  */
	/* ON screen would flash "Er" until the first 1 s tick.                      */
	Temp_MeasureProbes();

	ext_ok = 0;
	Thermo_Apply();                  /* OFF: standby / ON: the restored live screen */

	T0_1s_Init();                    /* 1 s time base for periodic reports */

	while(1) {
		uint8_t ev;

		WDFLG = 0xA5;
		BTN_Scan();
		Relay_Poll();                  /* end a pending relay coil impulse */
		Uart1_Poll();                  /* decode any pending link frames */
		Sched_NetPreemptPoll();        /* network day: close settings modes
		                                  (deferred out of the UART handler) */
		Debug_Poll();                  /* ship one pending debug line (shallow) */
if(!Ota_IsPending()) Link_Poll();
if(!Ota_IsPending()) Temp_ErPoll();
		Ota_Poll();                    /* OTA handoff to the XRAM flasher */

		/* drain queued key events (CLICK / HOLD / HOLD_REPEAT / RELEASE)  */
		while(1) {
			ev = BTN_GetEvent(&b);

			/* BTN_GetEvent() leaves *b untouched when the queue is empty, so a    */
			/* BTN_EVT_NONE iteration carries a STALE key id. End the drain right  */
			/* here, before any mode branch can commit/exit on that stale key.     */
			if(ev == BTN_EVT_NONE) break;

			/* 0. keypad-lock chord: UP+DOWN held together. Always answers      */
			/* (any power state, even while locked) and toggles the lock on/off */
			/* — it must never be swallowed by the lock filter below.           */
		if(b == BTN_CHORD && ev == BTN_EVT_CHORD) {
			uint8_t prev_lock = settings.keypadLockout;

			/* the chord outranks the OFF NET arm: drop a run that the DOWN   */
			/* hold may have raised before the second arrow arrived.          */
			down_arm = 0;
			settings.keypadLockout = settings.keypadLockout ? 0 : settings.modeKeypadLockout;
			if(!Settings_Persist()) {
				/* Flash refused: roll the toggle back so RAM == flash == peer.  */
				/* A silent RAM-only change would act without any indication     */
				/* (Btn_ActionAllowed reads RAM) and revert by itself after the  */
				/* next reboot.                                                  */
				settings.keypadLockout = prev_lock;
				continue;
			}
				if(Link_PeerAlive() && !Ota_IsPending()) {
					Link_SendU8(LNK_CMD_KEY_LOCK, LNK_T_ENUM, settings.keypadLockout);
				}
				if(power_on) {
					Lock_On_Screen();     /* fresh-ON spike on a lock change */
				}
				else {
					Lock_Flash_Start();    /* blink the padlock on the dark display */
				}
				DEBUG(BUTTONS_EN, { debug_kv("lock ", settings.keypadLockout); });
				continue;
			}

			/* keypad lock consumed: when active, every press is swallowed here. */
			/* FULL ignores everything; EX_OFF lets only the power leaf through  */
			/* (its CLICK/HOLD, see keypad_lock_tristate_design.md).             */
			if(settings.keypadLockout != KEYPAD_LOCKOUT_FREE &&
			   (!Btn_ActionAllowed(b) ||
			    (ev != BTN_EVT_CLICK && ev != BTN_EVT_HOLD))) {
				continue;
			}

			/* Every key event that reaches the mode dispatch restarts the idle  */
			/* timer shared by clock-set / schedule-set / settings menu (20 s    */
			/* without any event -> exit with save, see the main loop).          */
			mode_idle_tick = BTN_Tick10ms();

			/* 0b. clock-set mode: while active it owns the keys.               */
			/* POWER (any event) leaves with save; MENU click walks the steps   */
			/* (weekday -> hours -> minutes -> schedule), UP/DOWN edit.         */
			if(clk_set != CLK_SET_OFF) {
				if(b == BTN_POWER) {
					Clock_Set_Exit(1);       /* leave and keep the edited time */
					continue;
				}
				if(b == BTN_MENU && ev == BTN_EVT_CLICK) {
					if(clk_set == CLK_SET_WDAY) {
						clk_set = CLK_SET_HOUR;    /* weekday -> hours */
						Clock_Set_Draw();
					}
					else if(clk_set == CLK_SET_HOUR) {
						clk_set = CLK_SET_MIN;     /* hours -> minutes */
						Clock_Set_Draw();
					}
					else {
						/* minutes done: apply the time and go to the schedule */
						Clock_Set_Commit();
						clk_set = CLK_SET_OFF;
						Sched_Set_Enter();
					}
					continue;
				}
				if((b == BTN_UP || b == BTN_DOWN) &&
				   (ev == BTN_EVT_CLICK || ev == BTN_EVT_HOLD || ev == BTN_EVT_HOLD_REPEAT)) {
					if(clk_set == CLK_SET_WDAY) {
						if(b == BTN_UP) { if(clk_set_w < 7) clk_set_w++; }
						else            { if(clk_set_w > 1) clk_set_w--; }
					}
					else {
						uint8_t xdata *fld = (clk_set == CLK_SET_HOUR) ? &clk_set_h : &clk_set_m;
						uint8_t  max = (clk_set == CLK_SET_HOUR) ? 23 : 59;

						if(b == BTN_UP) {
							if(*fld < max) *fld = (uint8_t)(*fld + 1);
						}
						else {
							if(*fld > 0) *fld = (uint8_t)(*fld - 1);
						}
					}
					Clock_Set_Draw();
					continue;
				}
				continue;                     /* ignore other events, stay in mode */
			}

			/* 0bb. schedule-set mode: owns the keys while active.              */
			/* POWER leaves (store+save), MENU click walks: day hub -> enter    */
			/* the day -> hours -> minutes -> temperature -> next slot/day.     */
			if(sched_set != SCHED_SET_OFF) {
				/* day-select hub: UP/DOWN pick the weekday, MENU enters it    */
				if(sched_hub) {
					if(b == BTN_POWER) {
						Sched_Set_Leave();
						continue;
					}
					if(b == BTN_MENU && ev == BTN_EVT_CLICK) {
						sched_hub = 0;
						sched_slot = 0;
						sched_field = SCHED_FLD_HOUR;
						Sched_Set_Load();
						Sched_Set_Draw();
						continue;
					}
					if((b == BTN_UP || b == BTN_DOWN) &&
					   (ev == BTN_EVT_CLICK || ev == BTN_EVT_HOLD || ev == BTN_EVT_HOLD_REPEAT)) {
						if(b == BTN_UP) { if(sched_day < 6) sched_day++; }
						else            { if(sched_day > 0) sched_day--; }
						Sched_Set_Draw();
						continue;
					}
					continue;
				}
				if(b == BTN_POWER) {
					Sched_Set_Leave();       /* store + save-if-changed + exit */
					continue;
				}
				if(b == BTN_MENU && ev == BTN_EVT_CLICK) {
					Sched_Set_Next();        /* field/slot/day advance */
					continue;
				}
				if((b == BTN_UP || b == BTN_DOWN) &&
				   (ev == BTN_EVT_CLICK || ev == BTN_EVT_HOLD || ev == BTN_EVT_HOLD_REPEAT)) {
					uint8_t up = (b == BTN_UP) ? 1 : 0;

					sched_dirty   = 1;   /* day changed: report it once          */
					sched_changed = 1;   /* session edited: save flash on exit   */
					/* Only the edited time field becomes "set": the slot is     */
					/* stored as a transition only once BOTH are (sched_mode.c). */
					if(sched_field == SCHED_FLD_HOUR) {
						sched_h_set = 1;
						if(up) { if(sched_h < 23) sched_h++; }
						else   { if(sched_h > 0) sched_h--; }
					}
					else if(sched_field == SCHED_FLD_MIN) {
						sched_m_set = 1;
						if(up) { if(sched_m < 59) sched_m++; }
						else   { if(sched_m > 0) sched_m--; }
					}
					else {
						/* same dual step as the setpoint (owner spec: "везде,    */
						/* где регулируется температура")                          */
						int16_t step;
						int16_t v;

						if(ev == BTN_EVT_HOLD_REPEAT) {
							step = up ? 50 : -50;
						}
						else if(ev == BTN_EVT_HOLD) {
							step = Led_SnapStep(sched_temp, up, 50);
						}
						else {
							step = up ? 10 : -10;
						}
						v = sched_temp + step;
						if(v < settings.minHeatSetpointLimit) v = settings.minHeatSetpointLimit;
						if(v > settings.maxHeatSetpointLimit) v = settings.maxHeatSetpointLimit;
						sched_temp = v;
					}
					Sched_Set_Draw();
					continue;
				}
				continue;                     /* ignore other events, stay in mode */
			}

			/* 0bbb. settings menu: owns the keys while active.                 */
			/* (entry is deferred on r0b; the mode body stays for re-enable.)   */
			/* POWER leaves with save; MENU click advances to the next item.    */
			if(menu_set != MENU_OFF) {
				if(b == BTN_POWER) {
					Menu_Exit();          /* save + exit; power state kept */
					DEBUG(BUTTONS_EN, { debug_puts("menu exit\r\n"); });
					continue;
				}
				if(b == BTN_MENU && ev == BTN_EVT_CLICK) {
					Menu_Advance();       /* save previous item, next item */
					continue;
				}
				if((b == BTN_UP || b == BTN_DOWN) &&
				   (ev == BTN_EVT_CLICK || ev == BTN_EVT_HOLD || ev == BTN_EVT_HOLD_REPEAT)) {
					Menu_Step((b == BTN_UP) ? 1 : 0, (uint8_t)ev);
					Menu_Draw();
					continue;
				}
				continue;
			}

			/* 0c. MENU key held >= 3 s: ON -> clock-set, OFF -> settings menu   */
			/* (the entry gesture of the original thermostat). The first HOLD    */
			/* (0.5 s) only arms the measurement; the target is picked when the   */
			/* arm fires in the main loop, so the hold is armed regardless of the */
			/* power state.                                                       */
			if(b == BTN_MENU && ev == BTN_EVT_HOLD && !clk_arm) {
				clk_arm = 1;
				clk_arm_tick = BTN_Tick10ms();
				continue;
			}

			/* any press event while ON restarts the max-brightness window (CLICK,   */
			/* HOLD, RPT, even RELEASE - all extend it).                             */
			if(power_on) {
				Bright_MaxHold();
			}

			/* 1. leaf key (TK5): a >= 2 s hold toggles the thermostat.        */
			/*    A single TAP shows the WS humidity for HUMID_SHOW_SECS;      */
			/*    the first HOLD (0.5 s) only arms the measurement (see the    */
			/*    main loop, like MENU).                                       */
			if(b == BTN_POWER && ev == BTN_EVT_CLICK && power_on) {
				Set_Win_Show(LNK_CMD_HUMID_NET);
				continue;
			}
			if(b == BTN_POWER && ev == BTN_EVT_HOLD && !pwr_arm) {
				pwr_arm = 1;
				pwr_arm_power = power_on;      /* a remote SystemMode that arrives  */
				pwr_arm_tick = BTN_Tick10ms(); /* during the hold must not be       */
				continue;                      /* toggled back by this arm          */
			}

			/* a) MENU single press (power-on) -> manual <-> schedule mode.      */
			/*    ZCL bit0, no schedule data cleared.                             */
			if(b == BTN_MENU && ev == BTN_EVT_CLICK && power_on) {
				settings.progMode ^= PROG_MODE_SCHEDULE;
				/* the peer must follow a local manual/schedule toggle too, otherwise  */
				/* its ZCL ProgrammingOperationMode keeps the old value until the next */
				/* STATE_ALL resync (same pattern as the arrow setpoint send).         */
				if(Settings_Persist() && Link_PeerAlive() && !Ota_IsPending()) {
					Link_SendU8(LNK_CMD_PROG_MODE, LNK_T_BITMAP8, settings.progMode);
				}
				ProgMode_Icons(settings.progMode);
				Lcd_Flush();
				target_setpoint = Sched_ActiveSetpoint();  /* apply immediately */
                DEBUG(BUTTONS_EN, {
					debug_puts(settings.progMode & PROG_MODE_SCHEDULE ?
					           "mode sched\r\n" : "mode manual\r\n"); });
				continue;
			}
			/* e) UP / DOWN -> setpoint adjust.                                     */
			/*    ON + MANUAL:  CLICK/HOLD/HOLD_REPEAT -> +0.1 / -0.1 degC per step.  */
			/*    ON + SCHEDULE: the running setpoint comes from the weekly           */
			/*      schedule; the arrows belong to the schedule editor only, so       */
			/*      occupiedHeatingSetpoint stays the fallback value.                 */
			/*    OFF: arrows do NOT change the setpoint. Only DOWN long-press        */
			/*         is recognized (logged); UP is fully inert.                     */
			if((b == BTN_UP || b == BTN_DOWN) && power_on &&
			   !(settings.progMode & PROG_MODE_SCHEDULE) &&
			   (ev == BTN_EVT_CLICK || ev == BTN_EVT_HOLD || ev == BTN_EVT_HOLD_REPEAT)) {
					/* Dual-step adjust (owner spec): CLICK = 0.1 degC; the FIRST  */
					/* HOLD step snaps to the 0.5 grid (23.2 -> 23.5), HOLD_REPEAT */
					/* walks the grid (23.5 -> 24.0 ...). DOWN mirrors it.          */
					int16_t d;
					int16_t v;

					if(ev == BTN_EVT_HOLD_REPEAT) {
						d = (b == BTN_UP) ? 50 : -50;
					}
					else if(ev == BTN_EVT_HOLD) {
						d = Led_SnapStep(settings.occupiedHeatingSetpoint,
						                 (uint8_t)(b == BTN_UP), 50);
					}
					else {
						d = (b == BTN_UP) ? 10 : -10;   /* CLICK: 0.1 degC        */
					}
					v = settings.occupiedHeatingSetpoint + d;

					if(v < settings.minHeatSetpointLimit) v = settings.minHeatSetpointLimit;
					if(v > settings.maxHeatSetpointLimit) v = settings.maxHeatSetpointLimit;
					                DEBUG(BUTTONS_EN, { debug_puts("set ");
									debug_i16(v);
									debug_kv(", step ", d); });
					if(v != settings.occupiedHeatingSetpoint) {
					    /* RAM only while the key is held; persisted on RELEASE so a   */
					    /* HOLD/REPEAT burst does not erase the flash sector 16x/s.    */
					                settings.occupiedHeatingSetpoint = v;
					                set_need_save = 1;
					}
					/* value window shared with the network/menu Set window (ROM      */
					/* saving): identical 7 s, SYM_SET/SYM_HOME/digits,               */
					/* and the preamble also clears a lingering %RH icon.             */
					Set_Win_Show(LNK_CMD_SETPOINT_HEAT);
					continue;
			}
			/* OFF: DOWN held >= 2 s -> arm the NET (SYM_NET) 1.5 min blink.      */
			/* The 2 s is measured on the real 10 ms tick: the first HOLD (0.5 s) */
			/* only records the reference point; the loop below fires at 2 s if   */
			/* the key is still down. Setpoint stays intact.                      */
			if(b == BTN_DOWN && !power_on && ev == BTN_EVT_HOLD) {
				if(!down_arm) {
					down_arm = 1;
					down_arm_tick = BTN_Tick10ms();
				}
				continue;
			}
			DEBUG(BUTTONS_EN, {
                    debug_puts((ev == BTN_EVT_CLICK) ? "CLICK" :
                               (ev == BTN_EVT_HOLD) ? "HOLD" :
                               (ev == BTN_EVT_HOLD_REPEAT) ? "RPT" : "RELEAS");
                    debug_puts(" K");
                    debug_i16((int)((uint8_t)b + 3));
                    debug_puts("\r\n"); });
		}

		/* Safety net for the arrow setpoint: if the gesture ended without a    */
		/* RELEASE reaching the drain loop (e.g. the key was released on a scan */
		/* that produced no queued event, or a mode change swallowed it), flush */
		/* the pending value once both arrows are up.                           */
		if(set_need_save && !BTN_IsDown(BTN_UP) && !BTN_IsDown(BTN_DOWN)) {
			set_need_save = 0;
			if(Settings_Persist() && Link_PeerAlive() && !Ota_IsPending()) {
				Link_SendI16(LNK_CMD_SETPOINT_HEAT, settings.occupiedHeatingSetpoint);
			}
		}

		/* link: an incoming System Mode command (from ZT3L) sets the power.   */
		{
			XDATA_TMP(uint8_t, sm);
			if(Link_TakeSysMode(&sm)) {
				/* h_sysmode already restricted this to OFF/HEAT (0x00/0x04). */
				settings.systemMode = (sm == LNK_SYSMODE_OFF) ? SYS_MODE_OFF
				                                              : SYS_MODE_HEAT;
				if(!Settings_Persist()) settings_persist_pending = 1;
				power_on = (uint8_t)(sm != LNK_SYSMODE_OFF);
				DEBUG(BUTTONS_EN, { debug_kv("sysmode ", (uint16_t)sm); });
			}
		}

		/* link: a remote keypad-lock change (0x17). OFF -> padlock blink like the  */
		/* local chord (the dark display would hide it); ON -> fresh-ON             */
		/* brightness spike so the change is seen on the dimmed display.            */
		if(Link_TakeLockChanged()) {
			if(power_on) {
				Lock_On_Screen();
			}
			else {
				Lock_Flash_Start();
			}
		}

		/* link: a scalar setting arrived from the network (0x07..0x1B). Show its */
		/* value for 7 s with the "Set" indicator, like a manual setpoint edit.   */
		/* Only while ON and idle: OFF leaves the dark screen to the lock/NET     */
		/* owners, and a clock/schedule/menu mode owns the display otherwise.     */
		{
			XDATA_TMP(uint8_t, scmd);

			if(Link_TakeSettingChanged(&scmd) && power_on && Screen_ModeFree()) {
				Set_Win_Show(scmd);
			}
		}

		if(power_on != prev_power) {
			prev_power = power_on;
			net_hold = 0;              /* power change ends any steady-display hold */
			/* A settings mode is closed only by the KEY path (any BTN_POWER        */
			/* event inside its branch), so a REMOTE power-off (0x0A from the hub)  */
			/* used to leave clock-set/schedule-set/menu running while OFF:         */
			/* Clock_Set_Tick() repaints with no power_on guard, and the 20 s idle  */
			/* exit repaints the screen at a random moment (e.g. over the padlock   */
			/* blink). Close it exactly like its own POWER key does, BEFORE the     */
			/* power-change repaint below.                                          */
			if(!power_on) {
				if(menu_set != MENU_OFF)            Menu_Exit();
				else if(clk_set != CLK_SET_OFF)     Clock_Set_Exit(1);
				else if(sched_set != SCHED_SET_OFF) Sched_Set_Leave();
			}
			Thermo_Apply();
			if(!power_on) {
				/* OFF: flash the touch keys, then fade to the green leaf only */
				off_hold = STANDBY_KEYS_SECS;
				if(!lock_flash) {       /* a padlock blink owns the display       */
					Standby_KeysApply();
					/* a running NET blink keeps the display ON at max with the  */
					/* keys and green leaf lit (it owns the screen)              */
					if(net_flash) NetBlink_Screen();
				}
			}
		}
		/* FULL lock kills every key INCLUDING the mode's own exits, and the      */
		/* lock filter sits before mode_idle_tick - an open mode would freeze     */
		/* until the 20 s idle timeout. Close it exactly like a remote power-off  */
		/* does (same exits, same saves); catches BOTH sources (remote 0x17 and   */
		/* the local chord) and is idempotent: once closed, nothing matches.      */
		if(settings.keypadLockout == KEYPAD_LOCKOUT_FULL) {
			if(menu_set != MENU_OFF)            Menu_Exit();
			else if(clk_set != CLK_SET_OFF)     Clock_Set_Exit(1);
			else if(sched_set != SCHED_SET_OFF) Sched_Set_Leave();
		}
		/* ext_ok edge: deferred while a mode owns the screen - the condition   */
		/* stays pending (prev_ext untouched) and applies right after the exit. */
		if(power_on && (ext_ok != prev_ext) && Screen_ModeFree()) {
			prev_ext = ext_ok;
			Lcd_SymSet(SYM_HEATER, ext_ok);
			Lcd_Flush();
		}

		/* link: report System Mode / Running State on change (reliable send). */
		if(Link_PeerAlive() && !Ota_IsPending()) {
			Link_UpdateStates((uint8_t)power_on, (uint8_t)Relay_IsOn());
			if(sched_send_mask) Sched_SendPending();
		}

		Lock_Flash();              /* drive the OFF padlock blink, if any */

		/* OFF DOWN hold >= 2 s -> arm the NET blink; release cancels it.        */
		/* The arm is re-authorized here: the lock and the power state may have  */
		/* changed while the key was held (remote 0x17, sysmode, ...). It also   */
		/* yields to the lock chord: while BOTH arrows are down the run is       */
		/* abandoned (UP+DOWN 2 s toggles the lock, DOWN alone 2 s = the net).   */
		if(down_arm) {
			if(!power_on && Btn_ActionAllowed(BTN_DOWN) && BTN_IsDown(BTN_DOWN) &&
			   !(BTN_IsDown(BTN_CHORD_A) && BTN_IsDown(BTN_CHORD_B))) {
				if((uint16_t)(BTN_Tick10ms() - down_arm_tick) >= NET_ARM_TICKS) {
					down_arm = 0;
					Net_Flash_Start();
					DEBUG(BUTTONS_EN, { debug_puts("net flash\r\n"); });
#if !FACTORY_RESET_TEST_EN
					if(Link_PeerAlive() && !Ota_IsPending()) {
						Link_SendFactoryReset();   /* ZT3L: leave + factory-new */
					}
#endif
				}
			}
			else {
				down_arm = 0;      /* released, powered on or chord held: abandon */
			}
		}
		Net_Flash();               /* drive the NET blink if active           */
		Net_StatusApply();         /* joined -> NET steady (follows the link) */
		Clock_Set_Tick();          /* clock-set blink + no-key auto-exit      */

		/* MENU held >= 3 s: while ON -> clock-set, while OFF -> settings menu  */
		/* (the entry gesture of the original thermostat). The OFF entry forces */
		/* the display to maximum brightness for the whole menu session: the 1  */
		/* brightness tick never touches a dark screen and every menu exit path */
		/* ends in Thermo_Apply, which restores the standby picture.            */
		if(clk_arm) {
			if(Btn_ActionAllowed(BTN_MENU) && Screen_ModeFree() &&
			   BTN_IsDown(BTN_MENU)) {
				if((uint16_t)(BTN_Tick10ms() - clk_arm_tick) >= CLK_ARM_TICKS) {
					clk_arm = 0;
					if(power_on) {
						Clock_Set_Enter();
						DEBUG(BUTTONS_EN, { debug_puts("clock set\r\n"); });
					}
					else {
						Bright_Apply(LED_BRIGHTNESS_MAX);
						Menu_Enter();
						DEBUG(BUTTONS_EN, { debug_puts("menu off\r\n"); });
					}
				}
			}
			else {
				clk_arm = 0;       /* released / not applicable: abandon */
			}
		}

		/* clock-set / schedule-set / settings menu: 20 s without any key      */
		/* event returns to the thermostat, saving exactly what the mode's     */
		/* POWER/MENU exit path saves (Settings_Persist skips an unchanged     */
		/* image, so an idle timeout with no edits writes nothing to flash).   */
		if(!Screen_ModeFree()) {
			if((uint16_t)(BTN_Tick10ms() - mode_idle_tick) >= MODE_IDLE_10MS_TICKS) {
				if(menu_set != MENU_OFF) {
					Menu_Exit();
				}
				else if(clk_set != CLK_SET_OFF) {
					Clock_Set_Exit(1);     /* keep the edited time, like POWER */
				}
				else {
					Sched_Set_Leave();     /* hub: plain exit; day: store+save */
				}
			}
		}

		/* leaf held >= PWR_TOGGLE_10MS_TICKS (total, from the press) -> toggle.   */
		/* Re-authorized: the lock AND the power state are re-checked, so neither  */
		/* a remote lock (0x17) nor a remote SystemMode during the hold can make   */
		/* the arm toggle the state back.                                          */
		if(pwr_arm) {
			if(Btn_ActionAllowed(BTN_POWER) && (power_on == pwr_arm_power) &&
			   BTN_IsDown(BTN_POWER)) {
			if((uint16_t)(BTN_Tick10ms() - pwr_arm_tick) >= PWR_ARM_TICKS) {
				pwr_arm = 0;
				/* Persist the ZCL system mode (OFF/HEAT) with every toggle so a  */
				/* reboot restores it. A flash refusal arms the bounded retry -   */
				/* the toggle itself must not depend on data-flash.               */
				settings.systemMode = power_on ? SYS_MODE_OFF : SYS_MODE_HEAT;
				if(!Settings_Persist()) settings_persist_pending = 1;
				power_on = !power_on;
				DEBUG(BUTTONS_EN, { debug_puts(power_on ? "ON\r\n" : "OFF\r\n"); });
			}
			}
			else {
				pwr_arm = 0;       /* released: abandon */
			}
		}

		/* periodic raw dump (reference + per-key raw) for threshold           */
		/* re-calibration; now on a true 1 s time base (Timer0 ISR).           */
		/* NOTE: keep each debug_printf <= ~7 int args (Keil param window)     */
		/* - larger lines print garbage from the tail end of the argument      */
		/*   block (observed: 21-arg line corrupted k4..k7).                   */
		if(sec_flag) {
			const temp_sample_t xdata *in_sample;
			const temp_sample_t xdata *ext_sample;
			const temp_sample_t xdata *display_sample;
			uint8_t ws_fresh;

			sec_flag = 0;
			if(settings_persist_pending && !set_need_save &&
			   menu_set == MENU_OFF && sched_set == SCHED_SET_OFF) {
				(void)Settings_Persist();
			}
			temp_src = (temp_src_t)settings.sensosUsed;  /* follow remote change  */
			/* read both probes each second (external drives SYM_HEATER and is    */
			/* the primary for OU/AL, internal is the fallback and the source for */
			/* IN): shared helper, per-sensor calibration applied inside.         */
			Temp_MeasureProbes();

			in_sample = Temp_Pipeline_Get(TEMP_INTERNAL);
			ext_sample = Temp_Pipeline_Get(TEMP_EXTERNAL);
			ext_ok = ext_sample->live_valid;
			display_sample = Temp_DisplaySample();
			/* read the WS freshness ONCE per second: shared by the relay        */
			/* override and the sun symbol (ROM budget)                          */
			ws_fresh = Link_WsFresh();

			/* active setpoint from MANUAL value or the running schedule        */
			target_setpoint = Sched_ActiveSetpoint();

			/* heating relay: the selection law lives in relay.c - it gets BOTH   */
			/* probes plus temp_src (IN = internal, OU = external, AL = dual      */
			/* AND-start / OR-stop with survivor fallback) + the minimum-OFF      */
			/* window (anti-short-cycle start), 1 Hz. The wireless sensor (WS)    */
			/* rides in as ws_c100/ws_active: while fresh it REPLACES the         */
			/* configured probes in the law; the emergency cut-off inside relay.c */
			/* still sees only the wired probes.                                  */
			Relay_Update(in_sample->accepted_c100, in_sample->accepted_valid,
			             ext_sample->accepted_c100, ext_sample->accepted_valid,
			             temp_src, target_setpoint,
			             (uint16_t)((uint16_t)settings.deadBand * 10),
			             settings.maxHeatSetpointLimit,
			             (uint8_t)(power_on && !Ota_IsPending()),
			             Link_WsTemp(), ws_fresh);
			/* Status icons only while NO mode owns the screen: the modes hide     */
			/* them (menu/schedule redraw with Lcd_Clear) and a foreign           */
			/* Lcd_Flush (NET blink, remote lock) would push a fresh tick write    */
			/* on top of the mode screen. After the exit Thermo_Apply + this tick  */
			/* restore the picture within one second.                              */
			if(Screen_ModeFree()) {
				Lcd_SymSet(SYM_HEAT, (uint8_t)(power_on && Relay_IsOn()));
				/* WS "sun": lit while the wireless sensor is fresh (it drives the     */
				/* relay AND the digits), out when it goes stale. Gated on power_on so */
				/* the dark OFF screens never show it.                                 */
				Lcd_SymSet(SYM_SUNNY, (uint8_t)(power_on && ws_fresh));
			}

			if(power_on) {
				/* brightness: count down the max-brightness window; when it      */
				/* expires, drop to the day/night level from settings (0 = off).  */
				/* While the NET blink or the clock/schedule-set mode runs, full  */
				/* brightness is HELD (the window is re-armed) so the display     */
				/* never drops to the settings level mid-operation.               */
				if(net_flash || !Screen_ModeFree()) {
					Bright_MaxHold();   /* keep it at max */
				}
				else if(bright_hold) {
					if(--bright_hold == 0) {
						Bright_Apply(Bright_LevelBase());
					}
				}
				else {
					/* steady: keep the settings day/night level (picks up a   */
					/* remote brightness change within a second; cached no-op) */
					Bright_Apply(Bright_LevelBase());
				}
				clock_colon = !clock_colon;         /* blink ':' once per second    */
				if(!Screen_ModeFree()) {
					/* clock/schedule/menu owns the display: nothing to redraw   */
					/* here (the field blink is driven by Clock_Set_Tick).       */
				}
				else {
					Lcd_Weekday(RTC_GetWeek());     /* keep day-of-week in sync     */
					Lock_Sym();                     /* remote lock                  */
					Lcd_SymSet(SYM_H, 1);           /* the "h" - always on in ON    */
					Sched_SlotApply();              /* schedule-slot house of "now" */
					Clock_Draw();
					if (!adj_remain) {
						Lcd_SymSet(SYM_THERMO, ON); /* house-with-thermometer on    */
						Lcd_SymSet(SYM_SET,  OFF);  /* "set" symbol off             */
						Lcd_SymSet(SYM_HUMID, 0);   /* %RH window over              */
						Temp_Draw(display_sample);
					}
				}
			}
			else {
				/* standby: an OFF-side mode (the settings menu) owns the        */
				/* display - neither the key-stay countdowns nor the green-leaf  */
				/* repaint may touch it. Without this gate the expiring off_hold */
				/* issued Thermo_Apply (dark panel) a few seconds after the menu  */
				/* entry and Standby_GreenApply dropped the brightness every     */
				/* second after that. The countdowns stay frozen while the menu   */
				/* is open and resume after Menu_Exit -> Thermo_Apply.            */
				if(!Screen_ModeFree()) {
					/* menu owns the display: leave everything alone            */
				}
				else if(lock_flash || net_flash) {
					/* blink in progress: leave the display to its driver */
				}
				else if(net_hold) {
					/* provisioning done: show NET steady a bit longer, then     */
					/* fade to the green-leaf-only standby (same as OFF)         */
					if(--net_hold == 0) Thermo_Apply();
				}
				else if(off_hold) {
					if(--off_hold == 0) Thermo_Apply();
				}
				else {
					/* steady standby: keep the green leaf on the day/night level */
					Standby_GreenApply();
				}
			}
#if UART_DEBUG && LOCK_TRACE_EN
			/* LOCK_TRACE_EN: keep the probes alive for a couple of seconds after   */
			/* the blink, so the post-blink state is visible - every LED probe      */
			/* tests (lock_flash || trace_tail).                                    */
			if(trace_tail) trace_tail--;
#endif
			/* auto-stop the NET blink after NET_FLASH_SECS (1.5 min), in ANY  */
			/* power state (the counter must tick whether ON or OFF).          */
			if(net_flash && (--net_flash_secs == 0)) {
				Net_Flash_Stop();
				/* a running padlock blink owns the display: Lock_Flash() restores
				   the standby (or the NET screen) when its own phases are done. */
				if(!lock_flash) {
					if(power_on) {
						Bright_MaxHold();
						Clock_Draw();
						Temp_Draw(display_sample);
					}
					else {
						Thermo_Apply();  /* restore the standby screen */
					}
				}
			}
			if(!Ota_IsPending()) {
				Link_Tick1s();
				/* Link_Tick1s may call Uart1_SendFrame(), whose compiler-     */
				/* generated temporaries reuse the direct-RAM slots holding    */
				/* these auto pointers - reload them before link traffic and   */
				/* diagnostics dereference them below.                         */
				in_sample = Temp_Pipeline_Get(TEMP_INTERNAL);
				ext_sample = Temp_Pipeline_Get(TEMP_EXTERNAL);
				if(Link_PeerAlive()) {
					Link_ReportTemps(in_sample->accepted_c100, in_sample->accepted_valid,
					                 ext_sample->accepted_c100, ext_sample->accepted_valid);
					/* settings/factory resend cursor: 1 Hz, NOT the main-loop   */
					/* rate - a rewind+replace loop at loop speed would flood    */
					/* the link with fresh SEQs while the peer is silent.        */
					Link_RetryPending();
				}
			}
			DEBUG(RAW_EN, { debug_puts("raw: r="); debug_hex(BTN_Ref(), 4);
                  debug_puts(" k3="); debug_hex(BTN_Raw((btn_id_t)0), 4);
                  debug_puts(" k4="); debug_hex(BTN_Raw((btn_id_t)1), 4);
                  debug_puts(" k5="); debug_hex(BTN_Raw((btn_id_t)2), 4);
                  debug_puts(" k6="); debug_hex(BTN_Raw((btn_id_t)3), 4);
                  debug_puts("\r\n"); });
#if UART_DEBUG && TEMP_EN
			debug_temp(in_sample->live_c100, in_sample->accepted_c100,
			           ext_sample->live_c100, ext_sample->accepted_c100);
#endif
#if UART_DEBUG && RTC_EN
			RTC_Report();
#endif
		}
	}                            /* while(1) */
}                                /* main     */
