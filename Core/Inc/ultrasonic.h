/**
 * @file    ultrasonic.h
 * @brief   Ultrasonic transceiver configuration and API for STM32F030F4P6
 *
 * Pin assignment:
 *   PA9 - TIM1_CH2  - Continuous TX frequency output
 *   PA6 - TIM3_CH1  - TX burst gate (high for N pulses)
 *   PA2 - GPIO       - TX enable, inverted from PA6 (default HIGH, LOW during burst)
 *   PB1 - TIM14_CH1 - Echo input capture (rising edge)
 *   PA4 - GPIO       - Buzzer output (HIGH = on)
 */

#ifndef ULTRASONIC_H
#define ULTRASONIC_H

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/* ---------------------------------------------------------------------------
 * Clock source selection
 * Set exactly one of these. All options target 48 MHz SYSCLK.
 *   CLOCK_HSE_24MHZ : 24 MHz crystal, PLL x2
 *   CLOCK_HSE_8MHZ  : 8 MHz crystal,  PLL x6
 *   CLOCK_HSI_8MHZ  : Internal 8 MHz,  PLL /2 x12  (HSI/2 * 12 = 48)
 * -------------------------------------------------------------------------*/
#define CLOCK_HSE_24MHZ   0
#define CLOCK_HSE_8MHZ    1
#define CLOCK_HSI_8MHZ    2

#ifndef CLOCK_SOURCE
#define CLOCK_SOURCE      CLOCK_HSE_8MHZ
#endif

/* System clock after PLL - always 48 MHz regardless of source */
#define SYSCLK_HZ         48000000U

/* ---------------------------------------------------------------------------
 * Transmitter parameters
 * -------------------------------------------------------------------------*/

/* TX frequency on PA9 in Hz (valid range: 60 000 - 230 000) */
#ifndef TX_FREQ_HZ
#define TX_FREQ_HZ        66000U
#endif

/* Number of TX pulses per burst on PA6 (N) */
#ifndef TX_BURST_PULSES
#define TX_BURST_PULSES   3U
#endif

/* Number of PA9 pulse durations PA2 stays LOW (M).
 * PA2 goes LOW when PA6 goes HIGH, goes HIGH after M pulses.
 * Can be <= or >= TX_BURST_PULSES. */
#ifndef PA2_OFF_PULSES
#define PA2_OFF_PULSES    TX_BURST_PULSES
#endif

/* Burst repetition rate in Hz (Y) */
#ifndef TX_REPEAT_RATE_HZ
#define TX_REPEAT_RATE_HZ 10U
#endif

/* ---------------------------------------------------------------------------
 * Echo validation parameters
 * -------------------------------------------------------------------------*/

/* Expected number of echo pulses (R) */
#ifndef ECHO_EXPECTED_PULSES
#define ECHO_EXPECTED_PULSES  8U
#endif

/* Echo frequency tolerance in percent (+/-) */
#ifndef ECHO_FREQ_TOLERANCE_PCT
#define ECHO_FREQ_TOLERANCE_PCT  15U
#endif

/* Distance thresholds in millimetres */
#ifndef DIST_MIN_MM
#define DIST_MIN_MM       100U
#endif

#ifndef DIST_MAX_MM
#define DIST_MAX_MM       4000U
#endif

/* Speed of sound in mm/us (default 0.343 mm/us at 20 deg C) */
#ifndef SPEED_OF_SOUND_MM_PER_US
#define SPEED_OF_SOUND_MM_PER_US  0.343f
#endif

/* ---------------------------------------------------------------------------
 * Buzzer parameters
 * -------------------------------------------------------------------------*/

/* Buzzer on-time in milliseconds (B) */
#ifndef BUZZER_ON_MS
#define BUZZER_ON_MS      200U
#endif

/* ---------------------------------------------------------------------------
 * Pin override modes
 *   PIN_MODE_NORMAL     - pin operates per ultrasonic logic
 *   PIN_MODE_FORCE_HIGH - pin forced permanently HIGH
 *   PIN_MODE_FORCE_LOW  - pin forced permanently LOW
 * -------------------------------------------------------------------------*/
typedef enum {
    PIN_MODE_NORMAL = 0,
    PIN_MODE_FORCE_HIGH,
    PIN_MODE_FORCE_LOW
} PinMode_t;

#ifndef PA6_MODE
#define PA6_MODE          PIN_MODE_NORMAL
#endif

#ifndef PA2_MODE
#define PA2_MODE          PIN_MODE_NORMAL
#endif

#ifndef PA4_MODE
#define PA4_MODE          PIN_MODE_NORMAL
#endif

/* ---------------------------------------------------------------------------
 * Internal constants (derived)
 * -------------------------------------------------------------------------*/

/* Maximum number of echo pulses we can capture in one burst */
#define ECHO_CAPTURE_BUF_SIZE  32U

/* TIM1 ARR for the configured TX frequency */
#define TIM1_ARR_VALUE    ((SYSCLK_HZ / TX_FREQ_HZ) - 1U)
#define TIM1_CCR2_VALUE   ((TIM1_ARR_VALUE + 1U) / 2U)

/* Expected echo period in TIM14 ticks (48 MHz) */
#define ECHO_EXPECTED_PERIOD_TICKS  (SYSCLK_HZ / TX_FREQ_HZ)

/* Distance thresholds converted to TIM14 ticks (round-trip time)
 * distance_mm = (tof_us * speed_of_sound) / 2
 * tof_us = 2 * distance_mm / speed_of_sound
 * tof_ticks = tof_us * (SYSCLK_HZ / 1000000) */
#define DIST_MIN_TICKS    ((uint32_t)(2.0f * DIST_MIN_MM / SPEED_OF_SOUND_MM_PER_US * (SYSCLK_HZ / 1000000U)))
#define DIST_MAX_TICKS    ((uint32_t)(2.0f * DIST_MAX_MM / SPEED_OF_SOUND_MM_PER_US * (SYSCLK_HZ / 1000000U)))

/* ---------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/

/**
 * @brief  Reconfigure system clock based on CLOCK_SOURCE define.
 *         Call before Ultrasonic_Init() if using a different crystal than
 *         what CubeMX was configured for.
 * @retval 0 on success, -1 on failure
 */
int SystemClock_UserConfig(void);

/**
 * @brief  Initialise ultrasonic subsystem. Call after MX_TIMx_Init() functions.
 *         Reconfigures timer registers and starts the TX/echo chain.
 */
void Ultrasonic_Init(void);

/**
 * @brief  Start the transmission cycle (TIM1 PWM + TIM17 burst trigger).
 */
void Ultrasonic_StartTX(void);

/**
 * @brief  Stop the transmission cycle. PA9 stops, PA6 goes low, PA2 goes high.
 */
void Ultrasonic_StopTX(void);

/**
 * @brief  Process echo captures. Call repeatedly from the main loop.
 *         Validates echo and activates buzzer if valid.
 */
void Ultrasonic_ProcessEcho(void);

/**
 * @brief  Update buzzer state. Call repeatedly from the main loop.
 *         Turns off buzzer after BUZZER_ON_MS has elapsed.
 */
void Ultrasonic_BuzzerUpdate(void);

/* ---------------------------------------------------------------------------
 * ISR hooks  (called from HAL callbacks in stm32f0xx_it.c / main.c)
 * -------------------------------------------------------------------------*/

/** Called from TIM3 IRQ - handles burst start (CC1) and burst end (update) */
void Ultrasonic_TIM3_IRQHandler(void);

/** Called from TIM14 IRQ - handles echo input capture */
void Ultrasonic_TIM14_IRQHandler(void);

/** Called from TIM17 IRQ - triggers next burst */
void Ultrasonic_TIM17_IRQHandler(void);

#endif /* ULTRASONIC_H */
