/**
 * @file    it_user_code.c
 * @brief   Code snippets to paste into CubeMX-generated stm32f0xx_it.c USER CODE regions.
 *
 * THIS IS NOT A STANDALONE FILE. Copy each section into the corresponding
 * USER CODE region in your CubeMX-generated stm32f0xx_it.c.
 *
 * CubeMX generates the IRQ handler functions (TIM3_IRQHandler, etc.)
 * which call HAL_TIM_IRQHandler(). We bypass HAL's callback overhead
 * by calling our handlers directly inside the IRQ functions for minimal
 * latency on the time-critical PA2 toggle and echo capture.
 */

/* =========================================================================
 * Paste into: USER CODE BEGIN Includes  (in stm32f0xx_it.c)
 * ========================================================================= */
#if 0  /* --- COPY START --- */

#include "ultrasonic.h"

#endif /* --- COPY END --- */


/* =========================================================================
 * Paste into: USER CODE BEGIN TIM3_IRQn 0
 * (inside TIM3_IRQHandler, BEFORE HAL_TIM_IRQHandler)
 *
 * We handle TIM3 directly for minimal latency on PA2 toggle.
 * Call our handler and return, skipping HAL overhead.
 * ========================================================================= */
#if 0  /* --- COPY START --- */

  Ultrasonic_TIM3_IRQHandler();
  return;  /* skip HAL_TIM_IRQHandler for speed */

#endif /* --- COPY END --- */


/* =========================================================================
 * Paste into: USER CODE BEGIN TIM14_IRQn 0
 * (inside TIM14_IRQHandler, BEFORE HAL_TIM_IRQHandler)
 *
 * Direct handling for echo capture with minimal latency.
 * ========================================================================= */
#if 0  /* --- COPY START --- */

  Ultrasonic_TIM14_IRQHandler();
  return;  /* skip HAL_TIM_IRQHandler for speed */

#endif /* --- COPY END --- */


/* =========================================================================
 * Paste into: USER CODE BEGIN TIM17_IRQn 1
 * (inside TIM17_IRQHandler, AFTER HAL_TIM_IRQHandler)
 *
 * TIM17 re-arms TIM3 for the next burst and sets PA2 LOW (burst start).
 * Also prepares echo capture state. Not as latency-critical as TIM3/TIM14.
 *
 * Alternatively, paste into TIM17_IRQn 0 and use the "return" pattern
 * like TIM3/TIM14 above.
 * ========================================================================= */
#if 0  /* --- COPY START --- */

  Ultrasonic_TIM17_IRQHandler();

#endif /* --- COPY END --- */


/* =========================================================================
 * ALTERNATIVE: If you prefer using HAL callbacks instead of direct IRQ
 * handling, paste the following into main.c (USER CODE BEGIN 4) instead
 * of modifying stm32f0xx_it.c.
 *
 * NOTE: This adds ~1-2 us latency compared to the direct approach above
 * due to HAL_TIM_IRQHandler overhead. The PA2 jitter will be ~500ns
 * instead of ~300ns. Fine for most applications.
 * ========================================================================= */
#if 0  /* --- HAL CALLBACK ALTERNATIVE (paste in main.c USER CODE BEGIN 4) --- */

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM3)
    {
        /* TIM3 update = burst end */
        Ultrasonic_TIM3_IRQHandler();
    }
    else if (htim->Instance == TIM17)
    {
        /* TIM17 update = trigger next burst */
        Ultrasonic_TIM17_IRQHandler();
    }
    else if (htim->Instance == TIM14)
    {
        /* TIM14 overflow tracking (handled inside our handler) */
        Ultrasonic_TIM14_IRQHandler();
    }
}

void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM14)
    {
        Ultrasonic_TIM14_IRQHandler();
    }
}

#endif /* --- HAL CALLBACK ALTERNATIVE END --- */
