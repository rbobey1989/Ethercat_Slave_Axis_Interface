/* irq.c - minimal ISR implementations and IRQ management for ESC events */
#include "irq.h"
#include "ecat_slv.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_exti.h"
#include "stm32f4xx_ll_system.h"

/* Store currently requested ALevent mask so ISR can call worker with it */
volatile uint32_t g_ecat_event_mask = 0;
/* Flag set by minimal SYNC0 ISR to request processing in main context */
volatile uint8_t g_sync0_flag = 0;
/* Flag set by minimal SINT ISR (PC0) to request processing in main context */
volatile uint8_t g_sint_flag = 0;

void esc_hw_int_enable(uint32_t mask)
{
    /* Save mask for ISR and enable the ESC side event mask */
    g_ecat_event_mask = mask;
    ESC_ALeventmaskwrite(ESC_ALeventmaskread() | mask);

    /* Configure GPIOC PC0 as input */
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOC);
    LL_GPIO_SetPinMode(GPIOC, LL_GPIO_PIN_0, LL_GPIO_MODE_INPUT);
    LL_GPIO_SetPinPull(GPIOC, LL_GPIO_PIN_0, LL_GPIO_PULL_NO);

    /* Map EXTI line0 to port C */
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SYSCFG);
    LL_SYSCFG_SetEXTISource(LL_SYSCFG_EXTI_PORTC, LL_SYSCFG_EXTI_LINE0);

    /* Configure EXTI0 trigger both edges (robust for unknown polarity) */
    LL_EXTI_EnableRisingTrig_0_31(LL_EXTI_LINE_0);
    LL_EXTI_EnableFallingTrig_0_31(LL_EXTI_LINE_0);

    /* Enable EXTI0 interrupt and NVIC */
    LL_EXTI_EnableIT_0_31(LL_EXTI_LINE_0);
    NVIC_SetPriority(EXTI0_IRQn, 3);
    NVIC_EnableIRQ(EXTI0_IRQn);

    /* If DC SYNC0 requested, configure PC3 EXTI for SYNC0 (minimal ISR) */
    if (mask & ESCREG_ALEVENT_DC_SYNC0)
    {
        /* Configure PC3 as input */
        LL_GPIO_SetPinMode(GPIOC, LL_GPIO_PIN_3, LL_GPIO_MODE_INPUT);
        LL_GPIO_SetPinPull(GPIOC, LL_GPIO_PIN_3, LL_GPIO_PULL_NO);

        /* Map EXTI line3 to port C */
        LL_SYSCFG_SetEXTISource(LL_SYSCFG_EXTI_PORTC, LL_SYSCFG_EXTI_LINE3);

        /* Configure EXTI3 trigger on rising edge (SYNC often rising) */
        LL_EXTI_EnableRisingTrig_0_31(LL_EXTI_LINE_3);
        LL_EXTI_DisableFallingTrig_0_31(LL_EXTI_LINE_3);

        /* Enable EXTI3 interrupt and NVIC */
        LL_EXTI_EnableIT_0_31(LL_EXTI_LINE_3);
        NVIC_SetPriority(EXTI3_IRQn, 3);
        NVIC_EnableIRQ(EXTI3_IRQn);
    }
}

void esc_hw_int_disable(uint32_t mask)
{
    /* Clear requested mask bits in ESC ALEVENTMASK */
    uint32_t cur = ESC_ALeventmaskread();
    cur &= ~mask;
    ESC_ALeventmaskwrite(cur);

    /* Disable EXTI0 and NVIC (leave SYSCFG/GPIO config as-is) */
    LL_EXTI_DisableIT_0_31(LL_EXTI_LINE_0);
    NVIC_DisableIRQ(EXTI0_IRQn);
    g_ecat_event_mask = 0;

    /* If disabling DC SYNC0, disable EXTI3 as well */
    if (mask & ESCREG_ALEVENT_DC_SYNC0)
    {
        LL_EXTI_DisableIT_0_31(LL_EXTI_LINE_3);
        NVIC_DisableIRQ(EXTI3_IRQn);
        g_sync0_flag = 0;
    }
}

/* EXTI0 IRQ handler: clear flag, disable line briefly to avoid re-entry,
 * signal main loop for processing */
void EXTI0_IRQHandler(void)
{
    if (LL_EXTI_IsActiveFlag_0_31(LL_EXTI_LINE_0))
    {
        LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_0);
        LL_EXTI_DisableIT_0_31(LL_EXTI_LINE_0);
        g_sint_flag = 1;
    }
}

/* Minimal SYNC0 handler (PC3). Latch and delegate processing to main context. */
void EXTI3_IRQHandler(void)
{
    if (LL_EXTI_IsActiveFlag_0_31(LL_EXTI_LINE_3))
    {
        LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_3);
        LL_EXTI_DisableIT_0_31(LL_EXTI_LINE_3);
        g_sync0_flag = 1;
    }
}
