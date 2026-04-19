/**
 * @file    ultrasonic.c
 * @brief   Ultrasonic transceiver implementation for STM32F030F4P6
 *
 * Timer architecture:
 *   TIM1  - Master, continuous PWM on PA9 at TX_FREQ_HZ.
 *           TRGO = OC2REF feeds TIM3 via ITR0.
 *   TIM3  - Slave (external clock mode 1 from ITR0). Counts TIM1 pulses.
 *           One-pulse mode: PA6 high for TX_BURST_PULSES counts.
 *           Update ISR restores PA2 HIGH at burst end.
 *   TIM17 - Periodic interrupt at TX_REPEAT_RATE_HZ to re-arm TIM3.
 *           Sets PA2 LOW at burst start, prepares echo capture.
 *   TIM14 - Free-running input capture on PB1 for echo rising edges.
 *
 * PA2 (GPIO) - inverted copy of PA6. Set LOW in TIM17 ISR (burst start),
 *              restored HIGH in TIM3 ISR (burst end). ~300 ns jitter.
 * PA4 (GPIO) - buzzer, set HIGH for BUZZER_ON_MS on valid echo detection.
 */

#include "ultrasonic.h"

/* ---------------------------------------------------------------------------
 * External HAL timer handles (defined in CubeMX-generated code)
 * -------------------------------------------------------------------------*/
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim3;
extern TIM_HandleTypeDef htim14;
extern TIM_HandleTypeDef htim17;

/* ---------------------------------------------------------------------------
 * Private state
 * -------------------------------------------------------------------------*/

/* Echo capture buffer (32-bit timestamps for distances beyond 1.36 ms ToF) */
static volatile uint32_t echo_captures[ECHO_CAPTURE_BUF_SIZE];
static volatile uint8_t  echo_count;
static volatile bool     echo_capturing;

/* Timestamp of TX burst end (32-bit using TIM14 counter + overflow tracking) */
static volatile uint32_t tx_burst_end_tick32;
static volatile bool     tx_burst_ended;

/* Buzzer timing */
static volatile uint32_t buzzer_off_tick;   /* HAL_GetTick() value to turn off */
static volatile bool     buzzer_active;

/* TIM14 overflow tracking for 32-bit timestamps */
static volatile uint32_t tim14_overflows;



/* ---------------------------------------------------------------------------
 * Private helpers
 * -------------------------------------------------------------------------*/

/**
 * @brief  Get a 32-bit timestamp from TIM14 (16-bit counter + overflow tracking).
 */
static inline uint32_t TIM14_GetTimestamp32(uint16_t capture)
{
    return (tim14_overflows << 16) | capture;
}

/**
 * @brief  Apply pin override mode to a GPIO pin.
 * @param  port  GPIO port
 * @param  pin   GPIO pin mask
 * @param  mode  PIN_MODE_FORCE_HIGH or PIN_MODE_FORCE_LOW
 */
static void ApplyPinOverride(GPIO_TypeDef *port, uint16_t pin, PinMode_t mode)
{
    if (mode == PIN_MODE_FORCE_HIGH)
    {
        HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
    }
    else if (mode == PIN_MODE_FORCE_LOW)
    {
        HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
    }
}

/**
 * @brief  Reconfigure TIM3 CH1 pin (PA6) as plain GPIO output for override.
 */
static void PA6_ConfigAsGPIO(void)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = GPIO_PIN_6;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);
}

/**
 * @brief  Reconfigure PA4 buzzer pin as GPIO (already GPIO, but ensure state).
 */
static void PA4_SetOverride(PinMode_t mode)
{
    if (mode == PIN_MODE_FORCE_HIGH)
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
    else if (mode == PIN_MODE_FORCE_LOW)
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
}

/* ---------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/

void Ultrasonic_Init(void)
{
    /* ---- TIM1: reconfigure ARR and CCR2 for desired TX frequency ---- */
    __HAL_TIM_SET_AUTORELOAD(&htim1, TIM1_ARR_VALUE);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, TIM1_CCR2_VALUE);

    /* Master mode: OC2REF -> TRGO (MMS = 101) */
    htim1.Instance->CR2 &= ~TIM_CR2_MMS;
    htim1.Instance->CR2 |= TIM_CR2_MMS_2 | TIM_CR2_MMS_0;  /* 101 */

    /* ---- TIM3: slave external clock from ITR0 (TIM1), one-pulse ---- */
    /* Slave mode: External Clock Mode 1 (SMS = 111), trigger = ITR0 (TS = 000) */
    htim3.Instance->SMCR &= ~(TIM_SMCR_SMS | TIM_SMCR_TS);
    htim3.Instance->SMCR |= TIM_SMCR_SMS_2 | TIM_SMCR_SMS_1 | TIM_SMCR_SMS_0; /* SMS=111 */
    /* TS = 000 for ITR0 (already zero after clearing) */

    /* One-pulse mode */
    htim3.Instance->CR1 |= TIM_CR1_OPM;

    /* ARR = max(TX_BURST_PULSES, PA2_OFF_PULSES) so the timer runs long
     * enough for both PA6 burst and PA2 off-time. */
    {
        uint32_t arr = TX_BURST_PULSES;
        if (PA2_OFF_PULSES > TX_BURST_PULSES)
            arr = PA2_OFF_PULSES;
        __HAL_TIM_SET_AUTORELOAD(&htim3, arr);
    }

    /* PA6 output: PWM Mode 2 (OC1M = 111): OC1REF = HIGH when CNT >= CCR1.
     * CCR1 = 1: PA6 goes HIGH on first PA9 edge (CNT reaches 1).
     * PA6 forced inactive by CC1 interrupt when CNT = TX_BURST_PULSES.
     * Note: we DON'T use CCR1 for the PA6 off-point -- we use a separate
     * CC1 compare interrupt approach. Actually, for PA6 we continue to use
     * the forced inactive trick in the CC1 ISR at N pulses.
     *
     * CCR1 = 1 controls when PA6 goes HIGH (hardware-synced to first edge).
     * CC1 interrupt at a separate count isn't possible with same channel.
     * Instead: if M != N, we handle PA6 and PA2 differently:
     *   - PA6: forced inactive in TIM3 CC2 ISR when CNT matches TX_BURST_PULSES
     *   - PA2: set HIGH in TIM3 CC3 ISR when CNT matches PA2_OFF_PULSES
     *   - Update ISR: just handles end-of-timer-period cleanup
     *
     * But TIM3 has 4 channels. We use:
     *   CH1 = PA6 output (PWM Mode 2, CCR1 = 1 for start sync)
     *   CH2 = PA6 off-point (compare only, CCR2 = TX_BURST_PULSES, no pin)
     *   CH3 = PA2 off-point (compare only, CCR3 = PA2_OFF_PULSES, no pin) */
    htim3.Instance->CCMR1 &= ~TIM_CCMR1_OC1M;
    htim3.Instance->CCMR1 |= (TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_0); /* 111 = PWM mode 2 */
    htim3.Instance->CCMR1 &= ~TIM_CCMR1_OC1PE;  /* disable preload so mode switch is immediate */
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 1); /* PA6 HIGH when CNT >= 1 */

    /* Normal polarity (CC1P = 0): pin follows OC1REF */
    htim3.Instance->CCER &= ~TIM_CCER_CC1P;

    /* Enable CH1 output */
    htim3.Instance->CCER |= TIM_CCER_CC1E;

    /* Force output LOW initially (Forced Inactive = OC1M 100) */
    htim3.Instance->CCMR1 &= ~TIM_CCMR1_OC1M;
    htim3.Instance->CCMR1 |= TIM_CCMR1_OC1M_2;  /* 100 = Forced Inactive */

    /* CH2: compare-only for PA6 off-point at TX_BURST_PULSES.
     * No pin output, just CC2 interrupt. */
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, TX_BURST_PULSES);
    /* OC2M = 000 (frozen/timing), no output */
    htim3.Instance->CCMR1 &= ~TIM_CCMR1_OC2M;

    /* CH3: compare-only for PA2 off-point at PA2_OFF_PULSES.
     * No pin output, just CC3 interrupt. */
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, PA2_OFF_PULSES);
    /* OC3M = 000 (frozen/timing), no output */
    htim3.Instance->CCMR2 &= ~TIM_CCMR2_OC3M;

    /* Enable interrupts: update (timer end), CC2 (PA6 off), CC3 (PA2 off) */
    __HAL_TIM_ENABLE_IT(&htim3, TIM_IT_UPDATE);
    __HAL_TIM_ENABLE_IT(&htim3, TIM_IT_CC2);
    __HAL_TIM_ENABLE_IT(&htim3, TIM_IT_CC3);

    /* ---- TIM17: periodic burst trigger at TX_REPEAT_RATE_HZ ---- */
    {
        /* We want TIM17 to fire at TX_REPEAT_RATE_HZ.
         * TIM17 clock = 48 MHz.
         * Use PSC to bring it into range. Target period = 1/Y seconds.
         * With PSC = 47999, TIM17 ticks at 1 kHz.
         * ARR = (1000 / TX_REPEAT_RATE_HZ) - 1 for rates up to 1000 Hz. */
        uint32_t tim17_psc, tim17_arr;

        if (TX_REPEAT_RATE_HZ <= 1000U)
        {
            tim17_psc = 47999U;  /* 48 MHz / 48000 = 1 kHz */
            tim17_arr = (1000U / TX_REPEAT_RATE_HZ) - 1U;
        }
        else
        {
            /* For rates > 1 kHz, use smaller prescaler */
            tim17_psc = 479U;    /* 48 MHz / 480 = 100 kHz */
            tim17_arr = (100000U / TX_REPEAT_RATE_HZ) - 1U;
        }

        __HAL_TIM_SET_PRESCALER(&htim17, tim17_psc);
        __HAL_TIM_SET_AUTORELOAD(&htim17, tim17_arr);
        /* Force update to load prescaler immediately */
        htim17.Instance->EGR = TIM_EGR_UG;
        __HAL_TIM_CLEAR_FLAG(&htim17, TIM_FLAG_UPDATE);
        __HAL_TIM_ENABLE_IT(&htim17, TIM_IT_UPDATE);
    }

    /* ---- TIM14: input capture on PB1, free-running at 48 MHz ---- */
    __HAL_TIM_SET_PRESCALER(&htim14, 0);
    __HAL_TIM_SET_AUTORELOAD(&htim14, 0xFFFF);
    /* Rising edge capture is configured by CubeMX, just enable interrupt */
    __HAL_TIM_ENABLE_IT(&htim14, TIM_IT_CC1);
    __HAL_TIM_ENABLE_IT(&htim14, TIM_IT_UPDATE); /* for overflow tracking */

    /* ---- PA2: default HIGH ---- */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_SET);

    /* ---- PA4: default LOW ---- */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);

    /* ---- Apply pin overrides ---- */

    /* PA6 override: if not NORMAL, reconfigure as GPIO */
    if (PA6_MODE != PIN_MODE_NORMAL)
    {
        PA6_ConfigAsGPIO();
        ApplyPinOverride(GPIOA, GPIO_PIN_6, PA6_MODE);
    }

    /* PA2 override */
    if (PA2_MODE != PIN_MODE_NORMAL)
    {
        ApplyPinOverride(GPIOA, GPIO_PIN_2, PA2_MODE);
    }

    /* PA4 override */
    if (PA4_MODE != PIN_MODE_NORMAL)
    {
        PA4_SetOverride(PA4_MODE);
    }

    /* Clear state */
    echo_count = 0;
    echo_capturing = false;
    tx_burst_ended = false;
    buzzer_active = false;
    tim14_overflows = 0;
}

void Ultrasonic_StartTX(void)
{
    /* Start TIM1 PWM on CH2 (PA9 continuous frequency) */
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);

    /* Enable TIM1 master output (MOE) - required for advanced timer */
    __HAL_TIM_MOE_ENABLE(&htim1);

    /* Start TIM14 free-running for echo capture */
    HAL_TIM_IC_Start_IT(&htim14, TIM_CHANNEL_1);

    /* Start TIM17 periodic interrupt (burst trigger) */
    HAL_TIM_Base_Start_IT(&htim17);

    /* Arm TIM3 for the first burst - TIM17 will re-arm it subsequently. */
    htim3.Instance->CR1 &= ~TIM_CR1_OPM;
    htim3.Instance->CNT = 0;
    htim3.Instance->EGR = TIM_EGR_UG;
    __HAL_TIM_CLEAR_FLAG(&htim3, TIM_FLAG_UPDATE);
    htim3.Instance->CR1 |= TIM_CR1_OPM;

    /* Switch PA6 to PWM Mode 2 for first burst (hardware-synced to PA9) */
    if (PA6_MODE == PIN_MODE_NORMAL)
    {
        htim3.Instance->CCMR1 = (htim3.Instance->CCMR1 & ~TIM_CCMR1_OC1M)
                                | TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_0; /* 111 = PWM mode 2 */
    }

    /* PA2 goes LOW for first burst start via DMA when TIM17 fires,
     * but for the very first burst we set it manually since TIM17
     * hasn't fired yet. */
    if (PA2_MODE == PIN_MODE_NORMAL)
    {
        GPIOA->BRR = GPIO_PIN_2;
    }

    /* Prepare echo capture for first burst */
    echo_count = 0;
    echo_capturing = true;
    tx_burst_ended = false;

    /* Enable TIM3 - first external clock edge starts the burst */
    htim3.Instance->CR1 |= TIM_CR1_CEN;
}

void Ultrasonic_StopTX(void)
{
    /* Stop TIM17 (no more burst triggers) */
    HAL_TIM_Base_Stop_IT(&htim17);

    /* Stop TIM3 */
    htim3.Instance->CR1 &= ~TIM_CR1_CEN;

    /* Stop TIM1 PWM */
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_2);

    /* Stop TIM14 capture */
    HAL_TIM_IC_Stop_IT(&htim14, TIM_CHANNEL_1);

    /* Restore pin defaults */
    if (PA6_MODE == PIN_MODE_NORMAL)
    {
        /* PA6 will be low (timer stopped) - leave as timer AF, it goes inactive */
    }
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_SET);   /* PA2 default HIGH */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET); /* PA4 buzzer off */

    echo_capturing = false;
    buzzer_active = false;
}

void Ultrasonic_ProcessEcho(void)
{
    /* Only process when echo capture window has closed */
    if (echo_capturing || !tx_burst_ended)
        return;

    /* Check if we have any captures to process */
    uint8_t count = echo_count;
    if (count == 0)
    {
        tx_burst_ended = false;
        return;
    }

    /* --- Validation 1: Pulse count --- */
    if (count != ECHO_EXPECTED_PULSES)
    {
        /* Wrong number of pulses - discard */
        echo_count = 0;
        tx_burst_ended = false;
        return;
    }

    /* Copy captures to local buffer (ISR might be writing) */
    uint32_t caps[ECHO_CAPTURE_BUF_SIZE];
    for (uint8_t i = 0; i < count; i++)
    {
        caps[i] = echo_captures[i];
    }

    /* --- Validation 2: Distance (time of flight) --- */
    /* ToF = time from TX burst end to first echo rising edge.
     * Both are 32-bit timestamps (TIM14 counter + overflow tracking). */
    uint32_t tof_ticks = caps[0] - tx_burst_end_tick32;

    if (tof_ticks < DIST_MIN_TICKS || tof_ticks > DIST_MAX_TICKS)
    {
        echo_count = 0;
        tx_burst_ended = false;
        return;
    }

    /* --- Validation 3: Per-pulse frequency --- */
    uint32_t period_min = ECHO_EXPECTED_PERIOD_TICKS * (100U - ECHO_FREQ_TOLERANCE_PCT) / 100U;
    uint32_t period_max = ECHO_EXPECTED_PERIOD_TICKS * (100U + ECHO_FREQ_TOLERANCE_PCT) / 100U;

    for (uint8_t i = 1; i < count; i++)
    {
        uint32_t period = caps[i] - caps[i - 1];

        if (period < period_min || period > period_max)
        {
            /* Pulse frequency out of tolerance - discard */
            echo_count = 0;
            tx_burst_ended = false;
            return;
        }
    }

    /* --- All checks passed: activate buzzer --- */
    if (PA4_MODE == PIN_MODE_NORMAL)
    {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
        buzzer_off_tick = HAL_GetTick() + BUZZER_ON_MS;
        buzzer_active = true;
    }

    /* Reset for next cycle */
    echo_count = 0;
    tx_burst_ended = false;
}

void Ultrasonic_BuzzerUpdate(void)
{
    if (PA4_MODE != PIN_MODE_NORMAL)
        return;

    if (buzzer_active && HAL_GetTick() >= buzzer_off_tick)
    {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
        buzzer_active = false;
    }
}

/* ---------------------------------------------------------------------------
 * ISR Hooks
 * -------------------------------------------------------------------------*/

void Ultrasonic_TIM3_IRQHandler(void)
{
    uint32_t sr = htim3.Instance->SR;

    /* CC2 event: PA6 off-point (CNT matched TX_BURST_PULSES) */
    if (sr & TIM_SR_CC2IF)
    {
        htim3.Instance->SR = ~TIM_SR_CC2IF;

        /* Force PA6 LOW by switching to Forced Inactive mode */
        if (PA6_MODE == PIN_MODE_NORMAL)
        {
            htim3.Instance->CCMR1 = (htim3.Instance->CCMR1 & ~TIM_CCMR1_OC1M)
                                    | TIM_CCMR1_OC1M_2;  /* 100 = Forced Inactive */
        }

        /* Record TX burst end timestamp from TIM14 (32-bit) for ToF calculation */
        tx_burst_end_tick32 = TIM14_GetTimestamp32(htim14.Instance->CNT);
        tx_burst_ended = true;
    }

    /* CC3 event: PA2 off-point (CNT matched PA2_OFF_PULSES) */
    if (sr & TIM_SR_CC3IF)
    {
        htim3.Instance->SR = ~TIM_SR_CC3IF;

        /* PA2 goes HIGH after M pulses */
        if (PA2_MODE == PIN_MODE_NORMAL)
        {
            GPIOA->BSRR = GPIO_PIN_2;
        }
    }

    /* Update event: timer period end (ARR reached, OPM stops timer).
     * Both PA6 and PA2 should already be handled by CC2/CC3.
     * This just ensures cleanup if M == N == ARR. */
    if (sr & TIM_SR_UIF)
    {
        htim3.Instance->SR = ~TIM_SR_UIF;

        /* Force PA6 LOW if not already done (when N == ARR) */
        if (PA6_MODE == PIN_MODE_NORMAL)
        {
            htim3.Instance->CCMR1 = (htim3.Instance->CCMR1 & ~TIM_CCMR1_OC1M)
                                    | TIM_CCMR1_OC1M_2;  /* 100 = Forced Inactive */
        }

        /* Force PA2 HIGH if not already done (when M == ARR) */
        if (PA2_MODE == PIN_MODE_NORMAL)
        {
            GPIOA->BSRR = GPIO_PIN_2;
        }

        /* Record timestamp if not already done by CC2 */
        if (!tx_burst_ended)
        {
            tx_burst_end_tick32 = TIM14_GetTimestamp32(htim14.Instance->CNT);
            tx_burst_ended = true;
        }
    }
}

void Ultrasonic_TIM14_IRQHandler(void)
{
    uint32_t sr = htim14.Instance->SR;

    /* Overflow: track for 32-bit timestamps */
    if (sr & TIM_SR_UIF)
    {
        htim14.Instance->SR = ~TIM_SR_UIF;
        tim14_overflows++;
    }

    /* Input capture on CH1 (echo rising edge on PB1) */
    if (sr & TIM_SR_CC1IF)
    {
        htim14.Instance->SR = ~TIM_SR_CC1IF;

        if (echo_capturing && echo_count < ECHO_CAPTURE_BUF_SIZE)
        {
            echo_captures[echo_count] = TIM14_GetTimestamp32(htim14.Instance->CCR1);
            echo_count++;

            /* If we've captured enough pulses, stop capturing */
            if (echo_count >= ECHO_EXPECTED_PULSES)
            {
                echo_capturing = false;
            }
        }
    }
}

void Ultrasonic_TIM17_IRQHandler(void)
{
    /* Clear update flag */
    if (htim17.Instance->SR & TIM_SR_UIF)
    {
        htim17.Instance->SR = ~TIM_SR_UIF;

        /* If previous echo capture is still in progress, close it */
        if (echo_capturing)
        {
            echo_capturing = false;
        }

        /* Re-arm TIM3 for next burst.
         * In one-pulse mode, the timer stopped after the last burst.
         * Reset counter, clear flags, then enable. */
        if (PA6_MODE == PIN_MODE_NORMAL)
        {
            /* Temporarily disable OPM so UG doesn't stop the timer */
            htim3.Instance->CR1 &= ~TIM_CR1_OPM;
            htim3.Instance->CNT = 0;
            htim3.Instance->EGR = TIM_EGR_UG;  /* load preloaded values */
            htim3.Instance->SR  = 0;  /* clear all flags including UIF from UG */
            htim3.Instance->CR1 |= TIM_CR1_OPM;

            /* Switch PA6 back to PWM Mode 2 so it goes HIGH on first clock edge */
            if (PA6_MODE == PIN_MODE_NORMAL)
            {
                htim3.Instance->CCMR1 = (htim3.Instance->CCMR1 & ~TIM_CCMR1_OC1M)
                                        | TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_0; /* 111 = PWM mode 2 */
            }

            /* PA2 goes LOW at burst start */
            if (PA2_MODE == PIN_MODE_NORMAL)
            {
                GPIOA->BRR = GPIO_PIN_2;
            }

            /* Prepare echo capture for this burst */
            echo_count = 0;
            echo_capturing = true;
            tx_burst_ended = false;

            /* Enable TIM3 - first external clock edge starts the burst */
            htim3.Instance->CR1 |= TIM_CR1_CEN;
        }
    }
}
