/**
 * @file    clock_config.c
 * @brief   Clock configuration for multiple crystal/source options.
 *
 * This file provides SystemClock_UserConfig() which reconfigures the
 * system clock based on the CLOCK_SOURCE define in ultrasonic.h.
 *
 * IMPORTANT: CubeMX generates SystemClock_Config() in main.c. That
 * function will be configured for your default crystal (24 MHz HSE).
 * If you only use 24 MHz HSE, you don't need this file at all.
 *
 * To use a different clock source:
 *   1. Set CLOCK_SOURCE in ultrasonic.h
 *   2. Call SystemClock_UserConfig() in main.c USER CODE BEGIN 2,
 *      BEFORE Ultrasonic_Init().
 *
 * All options target 48 MHz SYSCLK:
 *   CLOCK_HSE_24MHZ : HSE 24 MHz -> PLL PREDIV /1, PLLMUL x2 = 48 MHz
 *   CLOCK_HSE_8MHZ  : HSE  8 MHz -> PLL PREDIV /1, PLLMUL x6 = 48 MHz
 *   CLOCK_HSI_8MHZ  : HSI  8 MHz -> PLL PREDIV /2, PLLMUL x12 = 48 MHz
 *                      (HSI/2 is the PLL source = 4 MHz, x12 = 48 MHz)
 */

#include "ultrasonic.h"
#include "stm32f0xx_hal.h"

/**
 * @brief  Reconfigure system clock based on CLOCK_SOURCE define.
 * @retval 0 on success, -1 on failure
 */
int SystemClock_UserConfig(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

#if (CLOCK_SOURCE == CLOCK_HSE_24MHZ)

    /* 24 MHz HSE -> PLL /1 x2 = 48 MHz */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc.PLL.PREDIV     = RCC_PREDIV_DIV1;
    osc.PLL.PLLMUL     = RCC_PLL_MUL2;

#elif (CLOCK_SOURCE == CLOCK_HSE_8MHZ)

    /* 8 MHz HSE -> PLL /1 x6 = 48 MHz */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc.PLL.PREDIV     = RCC_PREDIV_DIV1;
    osc.PLL.PLLMUL     = RCC_PLL_MUL6;

#elif (CLOCK_SOURCE == CLOCK_HSI_8MHZ)

    /* HSI 8 MHz / 2 = 4 MHz -> PLL x12 = 48 MHz */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState       = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSI; /* HSI/2 = 4 MHz */
    osc.PLL.PREDIV     = RCC_PREDIV_DIV1;
    osc.PLL.PLLMUL     = RCC_PLL_MUL12;

#else
    #error "Invalid CLOCK_SOURCE. Use CLOCK_HSE_24MHZ, CLOCK_HSE_8MHZ, or CLOCK_HSI_8MHZ."
#endif

    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
        return -1;

    /* SYSCLK from PLL, AHB and APB at full speed (48 MHz) */
    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;

    /* 1 wait state for 48 MHz on STM32F030 (>24 MHz requires 1 WS) */
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_1) != HAL_OK)
        return -1;

    return 0;
}
