/**
 * @file    clock_config.c
 * @brief   Clock configuration for multiple crystal/source options.
 *
 * CubeMX generates SystemClock_Config() with PLLMUL for 24 MHz HSE.
 * When using 8 MHz HSE, this function reconfigures the PLL properly.
 * It must switch SYSCLK to HSI first, then reconfigure PLL, then switch back.
 */

#include "ultrasonic.h"

/**
 * @brief  Reconfigure system clock based on CLOCK_SOURCE define.
 *         Handles the case where PLL is already active as SYSCLK source.
 * @retval 0 on success, -1 on failure
 */
int SystemClock_UserConfig(void)
{
#if (CLOCK_SOURCE == CLOCK_HSE_24MHZ)
    /* CubeMX already configured for 24 MHz HSE x2 = 48 MHz. Nothing to do. */
    return 0;

#elif (CLOCK_SOURCE == CLOCK_HSE_8MHZ)

    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    /* Step 1: Switch SYSCLK to HSI so we can reconfigure PLL */
    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_HSI;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_1) != HAL_OK)
        return -1;

    /* Step 2: Disable PLL */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.PLL.PLLState   = RCC_PLL_OFF;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
        return -1;

    /* Step 3: Reconfigure PLL with correct multiplier for 8 MHz HSE */
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc.PLL.PREDIV     = RCC_PREDIV_DIV1;
    osc.PLL.PLLMUL     = RCC_PLL_MUL6;  /* 8 MHz x 6 = 48 MHz */
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
        return -1;

    /* Step 4: Switch SYSCLK back to PLL */
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_1) != HAL_OK)
        return -1;

    return 0;

#elif (CLOCK_SOURCE == CLOCK_HSI_8MHZ)

    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    /* Step 1: Switch SYSCLK to HSI */
    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_HSI;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_1) != HAL_OK)
        return -1;

    /* Step 2: Disable PLL and HSE */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_HSI;
    osc.HSEState       = RCC_HSE_OFF;
    osc.HSIState       = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState   = RCC_PLL_OFF;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
        return -1;

    /* Step 3: Reconfigure PLL from HSI */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSI;  /* HSI/2 = 4 MHz */
    osc.PLL.PREDIV     = RCC_PREDIV_DIV1;
    osc.PLL.PLLMUL     = RCC_PLL_MUL12;      /* 4 MHz x 12 = 48 MHz */
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
        return -1;

    /* Step 4: Switch SYSCLK to PLL */
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_1) != HAL_OK)
        return -1;

    return 0;

#else
    #error "Invalid CLOCK_SOURCE. Use CLOCK_HSE_24MHZ, CLOCK_HSE_8MHZ, or CLOCK_HSI_8MHZ."
#endif
}
