#include "pwm_dma.h"

#include <stddef.h>

#include "stm32f407xx.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"

/*
 * PWM module pinout
 *
 * Each axis uses one PWM output from TIM1 and one direction GPIO on GPIOE.
 *
 * Axis 0: PWM PE9  -> TIM1_CH1, DIR PE8
 * Axis 1: PWM PE11 -> TIM1_CH2, DIR PE10
 * Axis 2: PWM PE13 -> TIM1_CH3, DIR PE12
 * Axis 3: PWM PE14 -> TIM1_CH4, DIR PE15
 *
 * DMA is driven from TIM1 update events using DMA2 Stream5 Channel6. The DMA
 * burst updates CCR1..CCR4 together so all PWM channels stay phase-aligned.
 */



typedef struct
{
    volatile uint32_t *ccr_reg;
    GPIO_TypeDef *dir_port;
    uint32_t dir_pin;
} pwm_dma_hw_cfg_t;

typedef struct
{
    uint8_t enabled;
    uint8_t status;
    int32_t signed_command;
    uint16_t requested_compare;
    uint16_t active_compare;
    uint8_t desired_dir;
    uint8_t applied_dir;
} pwm_dma_state_t;

#define PWM_DMA_TIMER               TIM1
#define PWM_DMA_TIMER_CLOCK_HZ      168000000UL
#define PWM_DMA_FREQUENCY_HZ        20000UL
#define PWM_DMA_PERIOD_TICKS        ((PWM_DMA_TIMER_CLOCK_HZ / PWM_DMA_FREQUENCY_HZ) - 1UL)
#define PWM_DMA_COMMAND_MAX         32767

#define PWM_DMA_DIR_PORT            GPIOE

static uint16_t g_pwm_dma_buffer[4];

static const pwm_dma_hw_cfg_t g_pwm_hw[] = {
    {&TIM1->CCR1, PWM_DMA_DIR_PORT, LL_GPIO_PIN_8},
    {&TIM1->CCR2, PWM_DMA_DIR_PORT, LL_GPIO_PIN_10},
    {&TIM1->CCR3, PWM_DMA_DIR_PORT, LL_GPIO_PIN_12},
    {&TIM1->CCR4, PWM_DMA_DIR_PORT, LL_GPIO_PIN_15},
};

static pwm_dma_state_t g_pwm_state[PWM_DMA_CHANNELS];

static void pwm_dma_wait_disabled(DMA_Stream_TypeDef *stream)
{
    stream->CR &= ~DMA_SxCR_EN;
    while ((stream->CR & DMA_SxCR_EN) != 0U)
    {
    }
}

static void pwm_dma_set_dir_pin(const pwm_dma_hw_cfg_t *cfg, uint8_t dir)
{
    if (dir != 0U)
    {
        LL_GPIO_SetOutputPin(cfg->dir_port, cfg->dir_pin);
    }
    else
    {
        LL_GPIO_ResetOutputPin(cfg->dir_port, cfg->dir_pin);
    }
}

static uint16_t pwm_dma_command_to_compare(int32_t command)
{
    uint32_t magnitude;

    if (command < 0)
    {
        magnitude = (uint32_t)(-command);
    }
    else
    {
        magnitude = (uint32_t)command;
    }

    if (magnitude > PWM_DMA_COMMAND_MAX)
    {
        magnitude = PWM_DMA_COMMAND_MAX;
    }

    return (uint16_t)((magnitude * PWM_DMA_PERIOD_TICKS) / PWM_DMA_COMMAND_MAX);
}

static void pwm_dma_gpio_init(void)
{
    static const uint32_t pwm_pins[4] = {
        LL_GPIO_PIN_9,
        LL_GPIO_PIN_11,
        LL_GPIO_PIN_13,
        LL_GPIO_PIN_14,
    };

    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOE);

    for (uint8_t index = 0; index < 4U; ++index)
    {
        LL_GPIO_SetPinMode(GPIOE, pwm_pins[index], LL_GPIO_MODE_ALTERNATE);
        LL_GPIO_SetPinOutputType(GPIOE, pwm_pins[index], LL_GPIO_OUTPUT_PUSHPULL);
        LL_GPIO_SetPinPull(GPIOE, pwm_pins[index], LL_GPIO_PULL_NO);
        LL_GPIO_SetPinSpeed(GPIOE, pwm_pins[index], LL_GPIO_SPEED_FREQ_VERY_HIGH);

        if (pwm_pins[index] < LL_GPIO_PIN_8)
        {
            LL_GPIO_SetAFPin_0_7(GPIOE, pwm_pins[index], LL_GPIO_AF_1);
        }
        else
        {
            LL_GPIO_SetAFPin_8_15(GPIOE, pwm_pins[index], LL_GPIO_AF_1);
        }

        LL_GPIO_SetPinMode(g_pwm_hw[index].dir_port, g_pwm_hw[index].dir_pin, LL_GPIO_MODE_OUTPUT);
        LL_GPIO_SetPinOutputType(g_pwm_hw[index].dir_port, g_pwm_hw[index].dir_pin, LL_GPIO_OUTPUT_PUSHPULL);
        LL_GPIO_SetPinPull(g_pwm_hw[index].dir_port, g_pwm_hw[index].dir_pin, LL_GPIO_PULL_NO);
        LL_GPIO_SetPinSpeed(g_pwm_hw[index].dir_port, g_pwm_hw[index].dir_pin, LL_GPIO_SPEED_FREQ_LOW);
        pwm_dma_set_dir_pin(&g_pwm_hw[index], 0U);
    }
}

static void pwm_dma_timer_init(void)
{
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_TIM1);

    PWM_DMA_TIMER->CR1 = 0U;
    PWM_DMA_TIMER->CR2 = 0U;
    PWM_DMA_TIMER->PSC = 0U;
    PWM_DMA_TIMER->ARR = PWM_DMA_PERIOD_TICKS;
    PWM_DMA_TIMER->CCR1 = 0U;
    PWM_DMA_TIMER->CCR2 = 0U;
    PWM_DMA_TIMER->CCR3 = 0U;
    PWM_DMA_TIMER->CCR4 = 0U;

    PWM_DMA_TIMER->CCMR1 = TIM_CCMR1_OC1PE
                         | TIM_CCMR1_OC2PE
                         | TIM_CCMR1_OC1M_1
                         | TIM_CCMR1_OC1M_2
                         | TIM_CCMR1_OC2M_1
                         | TIM_CCMR1_OC2M_2;
    PWM_DMA_TIMER->CCMR2 = TIM_CCMR2_OC3PE
                         | TIM_CCMR2_OC4PE
                         | TIM_CCMR2_OC3M_1
                         | TIM_CCMR2_OC3M_2
                         | TIM_CCMR2_OC4M_1
                         | TIM_CCMR2_OC4M_2;
    PWM_DMA_TIMER->CCER = TIM_CCER_CC1E
                        | TIM_CCER_CC2E
                        | TIM_CCER_CC3E
                        | TIM_CCER_CC4E;
    PWM_DMA_TIMER->BDTR = TIM_BDTR_MOE;
    PWM_DMA_TIMER->DCR = (13U << TIM_DCR_DBA_Pos) | (3U << TIM_DCR_DBL_Pos);
    PWM_DMA_TIMER->DIER = TIM_DIER_UDE;
    PWM_DMA_TIMER->EGR = TIM_EGR_UG;
    PWM_DMA_TIMER->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
}

static void pwm_dma_stream_init(void)
{
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA2);

    pwm_dma_wait_disabled(DMA2_Stream5);
    DMA2->HIFCR = DMA_HIFCR_CFEIF5 | DMA_HIFCR_CDMEIF5 | DMA_HIFCR_CTEIF5
                | DMA_HIFCR_CHTIF5 | DMA_HIFCR_CTCIF5;

    DMA2_Stream5->PAR = (uint32_t)&PWM_DMA_TIMER->DMAR;
    DMA2_Stream5->M0AR = (uint32_t)g_pwm_dma_buffer;
    DMA2_Stream5->NDTR = 4U;
    DMA2_Stream5->FCR = 0U;
    DMA2_Stream5->CR = (6U << DMA_SxCR_CHSEL_Pos)
                     | DMA_SxCR_DIR_0
                     | DMA_SxCR_MINC
                     | DMA_SxCR_CIRC
                     | DMA_SxCR_PL_1
                     | DMA_SxCR_MSIZE_0
                     | DMA_SxCR_PSIZE_0;
    DMA2_Stream5->CR |= DMA_SxCR_EN;
}

void pwm_dma_init(void)
{
    for (uint8_t index = 0; index < PWM_DMA_CHANNELS; ++index)
    {
        pwm_dma_state_t *state = &g_pwm_state[index];

        state->enabled = 1U;
        state->status = PWM_STATUS_ENABLED;
        state->signed_command = 0;
        state->requested_compare = 0U;
        state->active_compare = 0U;
        state->desired_dir = 0U;
        state->applied_dir = 0U;
        g_pwm_dma_buffer[index] = 0U;
    }

    pwm_dma_gpio_init();
    pwm_dma_timer_init();
    pwm_dma_stream_init();
}

void pwm_dma_set_enabled(uint8_t channel_index, bool enabled)
{
    if (channel_index >= PWM_DMA_CHANNELS)
    {
        return;
    }

    g_pwm_state[channel_index].enabled = enabled ? 1U : 0U;
    g_pwm_state[channel_index].status = enabled ? PWM_STATUS_ENABLED : 0U;
    if (!enabled)
    {
        g_pwm_state[channel_index].requested_compare = 0U;
        g_pwm_state[channel_index].active_compare = 0U;
    }
}

bool pwm_dma_is_enabled(uint8_t channel_index)
{
    if (channel_index >= PWM_DMA_CHANNELS)
    {
        return false;
    }

    return (g_pwm_state[channel_index].enabled != 0U);
}

void pwm_dma_set_signed_command(uint8_t channel_index, int32_t command)
{
    if (channel_index >= PWM_DMA_CHANNELS)
    {
        return;
    }

    pwm_dma_state_t *state = &g_pwm_state[channel_index];
    uint8_t command_clamped = 0U;

    if (command > PWM_DMA_COMMAND_MAX)
    {
        command = PWM_DMA_COMMAND_MAX;
        command_clamped = 1U;
    }
    else if (command < -PWM_DMA_COMMAND_MAX)
    {
        command = -PWM_DMA_COMMAND_MAX;
        command_clamped = 1U;
    }

    state->signed_command = command;
    state->desired_dir = (command < 0) ? 1U : 0U;
    state->requested_compare = pwm_dma_command_to_compare(command);

    if (command_clamped != 0U)
    {
        state->status |= PWM_STATUS_COMMAND_CLAMPED;
    }
    else
    {
        state->status &= (uint8_t)~PWM_STATUS_COMMAND_CLAMPED;
    }
}

int32_t pwm_dma_get_signed_command(uint8_t channel_index)
{
    if (channel_index >= PWM_DMA_CHANNELS)
    {
        return 0;
    }

    return g_pwm_state[channel_index].signed_command;
}

void pwm_dma_set_compare(uint8_t channel_index, uint16_t compare_ticks)
{
    if (channel_index >= PWM_DMA_CHANNELS)
    {
        return;
    }

    pwm_dma_state_t *state = &g_pwm_state[channel_index];

    if (compare_ticks > PWM_DMA_PERIOD_TICKS)
    {
        compare_ticks = PWM_DMA_PERIOD_TICKS;
    }

    state->requested_compare = compare_ticks;
    state->active_compare = compare_ticks;
    state->signed_command = (state->desired_dir != 0U) ? -(int32_t)compare_ticks : (int32_t)compare_ticks;
}

uint16_t pwm_dma_get_compare(uint8_t channel_index)
{
    if (channel_index >= PWM_DMA_CHANNELS)
    {
        return 0U;
    }

    return g_pwm_state[channel_index].active_compare;
}

uint8_t pwm_dma_get_status(uint8_t channel_index)
{
    if (channel_index >= PWM_DMA_CHANNELS)
    {
        return 0U;
    }

    return g_pwm_state[channel_index].status;
}

void pwm_dma_commit(void)
{
    for (uint8_t index = 0; index < PWM_DMA_CHANNELS; ++index)
    {
        const pwm_dma_hw_cfg_t *cfg = &g_pwm_hw[index];
        pwm_dma_state_t *state = &g_pwm_state[index];

        if (state->enabled == 0U)
        {
            state->status = 0U;
            state->active_compare = 0U;
            g_pwm_dma_buffer[index] = 0U;
            *cfg->ccr_reg = 0U;
            continue;
        }

        state->status &= (uint8_t)~(PWM_STATUS_REQUEST_NONZERO
                                   | PWM_STATUS_ACTIVE_NONZERO
                                   | PWM_STATUS_DIR_NEGATIVE
                                   | PWM_STATUS_DIR_CHANGE_PENDING);
        state->status |= PWM_STATUS_ENABLED;

        if (state->requested_compare != 0U)
        {
            state->status |= PWM_STATUS_REQUEST_NONZERO;
        }

        if (state->desired_dir != state->applied_dir)
        {
            state->status |= PWM_STATUS_DIR_CHANGE_PENDING;
            if (state->active_compare != 0U)
            {
                state->active_compare = 0U;
            }
            else
            {
                state->applied_dir = state->desired_dir;
                pwm_dma_set_dir_pin(cfg, state->applied_dir);
                state->active_compare = state->requested_compare;
            }
        }
        else
        {
            state->active_compare = state->requested_compare;
        }

        if (state->active_compare != 0U)
        {
            state->status |= PWM_STATUS_ACTIVE_NONZERO;
        }

        if (state->applied_dir != 0U)
        {
            state->status |= PWM_STATUS_DIR_NEGATIVE;
        }

        g_pwm_dma_buffer[index] = state->active_compare;
        *cfg->ccr_reg = state->active_compare;
    }
}