/* system_clock.c - configure MCU clocks (HSI -> PLL 168MHz) using LL helpers */
#include "stm32f4xx.h"
#include "stm32f4xx_ll_utils.h"
#include "stm32f4xx_ll_rcc.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_pwr.h"
#include "stm32f4xx_ll_system.h"

#include "system_clock.h"

void SystemClock_Config(void)
{
    LL_UTILS_PLLInitTypeDef pll_init;
    LL_UTILS_ClkInitTypeDef clk_init;

    /* Configure PLL to reach 168 MHz from HSI (16 MHz)
     * PLLM = 16, PLLN = 336, PLLP = 2 => ((16/16)*336)/2 = 168 MHz
     */
    pll_init.PLLM = LL_RCC_PLLM_DIV_16;
    pll_init.PLLN = 336U;
    pll_init.PLLP = LL_RCC_PLLP_DIV_2;

    /* AHB = SYSCLK /1, APB1 = HCLK/4 (42 MHz), APB2 = HCLK/2 (84 MHz) */
    clk_init.AHBCLKDivider  = LL_RCC_SYSCLK_DIV_1;
    clk_init.APB1CLKDivider = LL_RCC_APB1_DIV_4;
    clk_init.APB2CLKDivider = LL_RCC_APB2_DIV_2;

    /* Configure system clock using HSI as PLL source */
    /* Ensure PWR regulator scale and FLASH latency are set for 168 MHz */
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_PWR);
    LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE1);
    LL_FLASH_SetLatency(LL_FLASH_LATENCY_5);

    if (LL_PLL_ConfigSystemClock_HSI(&pll_init, &clk_init) != SUCCESS)
    {
        /* Configuration error: stay here for debug */
        while (1) {}
    }

    /* Update CMSIS SystemCoreClock variable */
    LL_SetSystemCoreClock(168000000U);

    /* Enable Flash prefetch and CPU caches (after latency set and clock switch) */
    LL_FLASH_EnablePrefetch();
    LL_FLASH_EnableInstCache();
    LL_FLASH_EnableDataCache();
}
