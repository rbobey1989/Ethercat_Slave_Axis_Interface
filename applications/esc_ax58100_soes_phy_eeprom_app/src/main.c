#include "stm32f407xx.h"

#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_exti.h"
#include "stm32f4xx_ll_system.h"

#include "ecat_slv.h"
#include "utypes.h"

#include "system_clock.h"
#include "soes_init.h"
#include "io.h"
#include "irq.h"
#include "objects.h"
#include "servo.h"

#define ECAT_LOW_PRIO_MASK (ESCREG_ALEVENT_CONTROL | ESCREG_ALEVENT_SMCHANGE | \
                            ESCREG_ALEVENT_SM0 | ESCREG_ALEVENT_SM1 | ESCREG_ALEVENT_EEP)

int main(void)
{
    /* Configure system clock (HSI -> PLL 168MHz) */
    SystemClock_Config();

    /* Initialize simple GPIO I/O used by PDO callbacks */
    io_init();

    /* Initialize board-specific servo helpers. Hardware mapping stays local to
     * the PWM and encoder modules so main can remain small. */
    servo_init();

    /* Initialize SOES stack and try to enable DC */
    soes_init();
    servo_load_static_config();

    /* Main loop: interrupt-driven. Enter low-power wait-for-interrupt loop.
     * EXTI0 does the main EtherCAT worker. For SYNC0 we use a minimal ISR
     * that sets a flag; process it here in main context to avoid heavy work
     * in the interrupt. */
    while (1)
    {
        __WFI();

        /* Minimal SINT processing requested by EXTI0 */
        if (g_sint_flag)
        {
            g_sint_flag = 0;

            /* Read AL event from ESC and expose it to DIG_process(). */
            uint32_t alevent = ESC_ALeventread();
            ESCvar.ALevent = alevent;

            if ((alevent & (ESCREG_ALEVENT_SM2 | ESCREG_ALEVENT_SM3 | ESCREG_ALEVENT_WD)) != 0U)
            {
                uint8_t dig_flags = DIG_PROCESS_WD_FLAG | DIG_PROCESS_OUTPUTS_FLAG;

                if (ESCvar.dcsync == 0)
                {
                    dig_flags |= DIG_PROCESS_APP_HOOK_FLAG | DIG_PROCESS_INPUTS_FLAG;
                }

                DIG_process(dig_flags);
            }

            uint32_t worker_mask = alevent & ECAT_LOW_PRIO_MASK;
            if (worker_mask != 0U)
            {
                ecat_slv_worker(worker_mask);
            }

            /* Re-enable EXTI0 now we've processed the event */
            LL_EXTI_EnableIT_0_31(LL_EXTI_LINE_0);
        }

        /* Minimal SYNC0 processing requested by EXTI3 */
        if (g_sync0_flag)
        {
            g_sync0_flag = 0;

            /* In DC mode SYNC0 is the deterministic servo point. */
            ESCvar.ALevent = ESC_ALeventread();
            DIG_process(DIG_PROCESS_APP_HOOK_FLAG | DIG_PROCESS_INPUTS_FLAG);

            /* Re-enable EXTI3 now we've processed the event */
            LL_EXTI_EnableIT_0_31(LL_EXTI_LINE_3);
        }

        while (g_servo_fast_ticks != 0U)
        {
            __disable_irq();

            if (g_servo_fast_ticks != 0U)
            {
                g_servo_fast_ticks--;
                __enable_irq();
                servo_fast_cycle();
            }
            else
            {
                __enable_irq();
            }
        }
    }
}
