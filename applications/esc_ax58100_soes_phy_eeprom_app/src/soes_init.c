/* soes_init.c - SOES stack initialization helper */
#include "soes_init.h"
#include "enc_dma.h"
#include "ecat_slv.h"
#include "irq.h"
#include "servo.h"

/* ESC_enable_DC is implemented in the AX58100 HAL (esc_hw.c) */
extern uint32_t ESC_enable_DC(void);

static uint16_t esc_check_dc_handler(void)
{
    /* Application-specific DC validation. Return 0 if OK. */
    return 0;
}

void soes_init(void)
{
    esc_cfg_t cfg = {0};
    cfg.use_interrupt = 1; /* use IRQs on PC0 */
    cfg.watchdog_cnt = 1000;
    cfg.application_hook = servo_cycle;
    cfg.esc_hw_interrupt_enable = esc_hw_int_enable;
    cfg.esc_hw_interrupt_disable = esc_hw_int_disable;
    cfg.esc_check_dc_handler = esc_check_dc_handler;

    ecat_slv_init(&cfg);

    /* Try to enable DC on the ESC. If enabled, mark stack to use DC handling. */
    uint32_t sync0 = ESC_enable_DC();
    if (sync0)
    {
        ESCvar.dcsync = 1;
        enc_dma_set_servo_period_ns(sync0);
    }
}
