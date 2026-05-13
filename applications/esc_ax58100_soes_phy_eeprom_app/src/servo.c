#include "servo.h"

#include "stm32f407xx.h"
#include "stm32f4xx_ll_bus.h"

#include "objects.h"

#include "enc_dma.h"
#include "pwm_dma.h"
#include "vel_ctrl.h"

#define SERVO_FAST_TIMER            TIM9
#define SERVO_FAST_TIMER_HZ         4000UL
#define SERVO_FAST_TIMER_CLOCK_HZ   168000000UL

/* Contador simple para que el hilo principal sepa cuántos ticks de servo faltan por atender. */
volatile uint8_t g_servo_fast_ticks = 0U;

/*
 * El modo viene desde el OD, pero aqui se filtra cualquier valor fuera del rango
 * conocido para no ejecutar estados indefinidos.
 */
static uint8_t servo_get_axis_mode(uint8_t axis)
{
    uint8_t mode = Obj.Mode[axis];

    if (mode > VEL_CTRL_MODE_VELOCITY_PI)
    {
        return VEL_CTRL_MODE_OPEN_LOOP;
    }

    return mode;
}

/* Configura TIM9 como base del lazo rapido local a 4 kHz. */
static void servo_fast_timer_init(void)
{
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_TIM9);

    SERVO_FAST_TIMER->CR1 = 0U;
    SERVO_FAST_TIMER->CR2 = 0U;
    SERVO_FAST_TIMER->PSC = (SERVO_FAST_TIMER_CLOCK_HZ / 1000000UL) - 1UL;
    SERVO_FAST_TIMER->ARR = (1000000UL / SERVO_FAST_TIMER_HZ) - 1UL;
    SERVO_FAST_TIMER->CNT = 0U;
    SERVO_FAST_TIMER->EGR = TIM_EGR_UG;
    SERVO_FAST_TIMER->SR = 0U;
    SERVO_FAST_TIMER->DIER = TIM_DIER_UIE;
    SERVO_FAST_TIMER->CR1 = TIM_CR1_CEN;

    NVIC_SetPriority(TIM1_BRK_TIM9_IRQn, 4);
    NVIC_EnableIRQ(TIM1_BRK_TIM9_IRQn);
}

/* Inicializa todos los modulos en el orden en que el servo los necesita. */
void servo_init(void)
{
    pwm_dma_init();
    enc_dma_init();
    vel_ctrl_init();
    servo_fast_timer_init();
}

/*
 * Carga una sola vez la parametrizacion estatica del PI desde el OD.
 * Se invoca despues de soes_init(), cuando Obj ya contiene los defaults activos.
 */
void servo_load_static_config(void)
{
    for (uint8_t axis = 0; axis < PWM_DMA_CHANNELS; ++axis)
    {
        vel_ctrl_set_gains(axis,
            Obj.Ctrl_Kp[axis],
            Obj.Ctrl_Ki[axis],
            Obj.Ctrl_FF0[axis],
            Obj.Ctrl_FF1[axis]);
        vel_ctrl_set_limits(axis,
            Obj.Ctrl_Integrator_Limit[axis],
            Obj.Ctrl_Output_Limit[axis]);
    }
}

/*
 * Ciclo lento asociado al intercambio EtherCAT.
 * Aqui se copian parametros del OD hacia los modulos y se publican feedbacks.
 */
void servo_cycle(void)
{
    for (uint8_t axis = 0; axis < ENC_DMA_CHANNELS; ++axis)
    {
        /* Host decide si cada encoder esta habilitado logicamente. */
        enc_dma_set_enabled(axis, Obj.Enc_En[axis]);
    }

    /* El ciclo lento solo refresca latches de index visibles para EtherCAT. */
    enc_dma_update(ENC_DMA_UPDATE_SLOW);

    for (uint8_t axis = 0; axis < PWM_DMA_CHANNELS; ++axis)
    {
        uint8_t mode = servo_get_axis_mode(axis);

        vel_ctrl_set_host_command(axis, Obj.Ctrl_Vel_Cmd[axis]);
        vel_ctrl_set_enabled(axis,
            ((Obj.Pwm_En[axis] != 0U) && (mode == VEL_CTRL_MODE_VELOCITY_PI)) ? 1U : 0U);
    }

    for (uint8_t axis = 0; axis < ENC_DMA_CHANNELS; ++axis)
    {
        Obj.Enc_Pos[axis] = enc_dma_get_position(axis);
        Obj.Enc_Vel[axis] = enc_dma_get_velocity_rise_ab(axis);
        Obj.Enc_Status[axis] = enc_dma_get_status(axis);
        Obj.Pwm_Status[axis] = pwm_dma_get_status(axis);
        Obj.Ctrl_Vel_Fb[axis] = vel_ctrl_get_output(axis);
    }
}

/*
 * Ciclo rapido local a 4 kHz.
 * Se ejecuta independientemente del ritmo EtherCAT y sostiene el lazo de velocidad.
 */
void servo_fast_cycle(void)
{
    /* El ciclo rapido integra posicion x4 y consume la cola de eventos A/B. */
    enc_dma_update(ENC_DMA_UPDATE_FAST);
    vel_ctrl_fast_tick();

    for (uint8_t axis = 0; axis < PWM_DMA_CHANNELS; ++axis)
    {
        uint8_t mode = servo_get_axis_mode(axis);
        uint8_t pwm_enabled = Obj.Pwm_En[axis] != 0U;
        int32_t output_command = 0;

        if (pwm_enabled != 0U)
        {
            if (mode == VEL_CTRL_MODE_VELOCITY_PI)
            {
                output_command = vel_ctrl_get_output(axis);
            }
            else
            {
                output_command = Obj.Pwm_Cmd[axis];
            }
        }

        pwm_dma_set_enabled(axis, pwm_enabled != 0U);
        pwm_dma_set_signed_command(axis, output_command);
    }

    pwm_dma_commit();
}

/* IRQ del temporizador rapido: solo acumula eventos pendientes. */
void TIM1_BRK_TIM9_IRQHandler(void)
{
    if ((SERVO_FAST_TIMER->SR & TIM_SR_UIF) != 0U)
    {
        SERVO_FAST_TIMER->SR = 0U;

        if (g_servo_fast_ticks < 0xFFU)
        {
            g_servo_fast_ticks++;
        }
    }
}