#include "servo.h"

#include "objects.h"

#include "enc_dma.h"
#include "pwm_dma.h"

void servo_init(void)
{
    pwm_dma_init();
    enc_dma_init();
}

void servo_cycle(void)
{
    for (uint8_t axis = 0; axis < PWM_DMA_CHANNELS; ++axis)
    {
        pwm_dma_set_enabled(axis, Obj.Pwm_En[axis] != 0U);
        pwm_dma_set_signed_command(axis, Obj.Pwm_Cmd[axis]);
    }

    for (uint8_t axis = 0; axis < ENC_DMA_CHANNELS; ++axis)
    {
        enc_dma_set_enabled(axis, Obj.Enc_En[axis]);
    }

    enc_dma_latch();
    pwm_dma_commit();

    for (uint8_t axis = 0; axis < ENC_DMA_CHANNELS; ++axis)
    {
        Obj.Enc_Pos[axis] = enc_dma_get_position(axis);
        Obj.Enc_Vel[axis] = enc_dma_get_velocity(axis);
        Obj.Enc_Status[axis] = enc_dma_get_status(axis);
        Obj.Pwm_Status[axis] = pwm_dma_get_status(axis);
    }
}