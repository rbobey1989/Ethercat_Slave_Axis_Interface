/* Reset control using STM32 LL (PC2) */
#include "rst.h"
#include "stm32f4xx.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_bus.h"

void rst_setup(void)
{
    /* Enable GPIOC clock */
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOC);

    /* Configure PC2 as output and drive high */
    LL_GPIO_SetPinMode(ESC_RST_GPIO_PORT, ESC_RST_PIN, LL_GPIO_MODE_OUTPUT);
    LL_GPIO_SetPinOutputType(ESC_RST_GPIO_PORT, ESC_RST_PIN, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinPull(ESC_RST_GPIO_PORT, ESC_RST_PIN, LL_GPIO_PULL_NO);
    LL_GPIO_SetOutputPin(ESC_RST_GPIO_PORT, ESC_RST_PIN);
}

void rst_low(void)
{
    LL_GPIO_ResetOutputPin(ESC_RST_GPIO_PORT, ESC_RST_PIN);
}

void rst_high(void)
{
    LL_GPIO_SetOutputPin(ESC_RST_GPIO_PORT, ESC_RST_PIN);
}

void rst_check_start(void)
{
    /* Reconfigure reset pin as input with pull-up to let ESC drive it */
    LL_GPIO_SetPinMode(ESC_RST_GPIO_PORT, ESC_RST_PIN, LL_GPIO_MODE_INPUT);
    LL_GPIO_SetPinPull(ESC_RST_GPIO_PORT, ESC_RST_PIN, LL_GPIO_PULL_UP);
}

uint8_t is_esc_reset(void)
{
    return (uint8_t)LL_GPIO_IsInputPinSet(ESC_RST_GPIO_PORT, ESC_RST_PIN);
}
