#include "enc_dma.h"

#include <stddef.h>

#include "stm32f407xx.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"

/*
 * Encoder module pinout
 *
 * Position counters (quadrature A/B):
 * Axis 0: PA0/PA1   -> TIM5_CH1/TIM5_CH2
 * Axis 1: PA15/PB3  -> TIM2_CH1/TIM2_CH2
 * Axis 2: PD12/PD13 -> TIM4_CH1/TIM4_CH2
 * Axis 3: PC6/PC7   -> TIM8_CH1/TIM8_CH2
 *
 * Low-speed period capture inputs (one input per axis, normally encoder A):
 * Axis 0: PB4 -> TIM3_CH1 -> DMA1 Stream4 Channel5
 * Axis 1: PB5 -> TIM3_CH2 -> DMA1 Stream5 Channel5
 * Axis 2: PC8 -> TIM3_CH3 -> DMA1 Stream7 Channel5
 * Axis 3: PC9 -> TIM3_CH4 -> DMA1 Stream2 Channel5
 *
 * Hardware connection for each encoder:
 * 1. Encoder A and B must both reach the position timer inputs listed above.
 * 2. Encoder A must also be branched to the TIM3 capture input for the same axis.
 * 3. If the encoder is differential (RS-422 line driver), place a receiver before
 *    the MCU and feed the single-ended A/B/Z signals to the STM32 pins.
 * 4. MCU and encoder interface must share a valid signal ground reference.
 * 5. Z/index is not yet consumed by this module; if needed it should go to a
 *    separate EXTI or capture input.
 */

typedef struct
{
    TIM_TypeDef *tim;
    GPIO_TypeDef *port_a;
    uint32_t pin_a;
    uint32_t af_a;
    GPIO_TypeDef *port_b;
    uint32_t pin_b;
    uint32_t af_b;
    uint8_t counter_bits;
} enc_dma_hw_cfg_t;

typedef struct
{
    uint8_t enabled;
    uint8_t status;
    uint32_t previous_sample;
    int32_t position;
    int32_t velocity_fast;
    int32_t velocity_slow;
    int32_t velocity_fused;
    uint16_t last_capture;
    uint8_t capture_valid;
    uint8_t dma_head;
    uint8_t stale_cycles;
} enc_dma_state_t;

typedef struct
{
    DMA_Stream_TypeDef *stream;
    volatile uint32_t *ccr_reg;
    GPIO_TypeDef *port;
    uint32_t pin;
    uint32_t af;
    uint8_t dma_channel;
} enc_capture_dma_cfg_t;

#define ENC_DMA_CAPTURE_TIMER         TIM3
#define ENC_DMA_CAPTURE_TIMER_HZ      100000UL
#define ENC_DMA_CAPTURE_BUFFER_LEN    8U
#define ENC_DMA_COUNTS_PER_CAPTURE    4UL
#define ENC_DMA_FAST_THRESHOLD        2
#define ENC_DMA_STALE_LIMIT_CYCLES    200U

static uint32_t g_enc_servo_period_ns = 1000000UL;
static volatile uint16_t g_capture_buffers[4][ENC_DMA_CAPTURE_BUFFER_LEN];

static const enc_dma_hw_cfg_t g_enc_hw[] = {
    {TIM5, GPIOA, LL_GPIO_PIN_0, LL_GPIO_AF_2, GPIOA, LL_GPIO_PIN_1, LL_GPIO_AF_2, 32U},
    {TIM2, GPIOA, LL_GPIO_PIN_15, LL_GPIO_AF_1, GPIOB, LL_GPIO_PIN_3, LL_GPIO_AF_1, 32U},
    {TIM4, GPIOD, LL_GPIO_PIN_12, LL_GPIO_AF_2, GPIOD, LL_GPIO_PIN_13, LL_GPIO_AF_2, 16U},
    {TIM8, GPIOC, LL_GPIO_PIN_6, LL_GPIO_AF_3, GPIOC, LL_GPIO_PIN_7, LL_GPIO_AF_3, 16U},
};

static const enc_capture_dma_cfg_t g_capture_hw[] = {
    {DMA1_Stream4, &TIM3->CCR1, GPIOB, LL_GPIO_PIN_4, LL_GPIO_AF_2, 5U},
    {DMA1_Stream5, &TIM3->CCR2, GPIOB, LL_GPIO_PIN_5, LL_GPIO_AF_2, 5U},
    {DMA1_Stream7, &TIM3->CCR3, GPIOC, LL_GPIO_PIN_8, LL_GPIO_AF_2, 5U},
    {DMA1_Stream2, &TIM3->CCR4, GPIOC, LL_GPIO_PIN_9, LL_GPIO_AF_2, 5U},
};

static enc_dma_state_t g_enc_state[ENC_DMA_CHANNELS];

static void enc_dma_wait_disabled(DMA_Stream_TypeDef *stream)
{
    stream->CR &= ~DMA_SxCR_EN;
    while ((stream->CR & DMA_SxCR_EN) != 0U)
    {
    }
}

static void enc_dma_gpio_set_af(GPIO_TypeDef *port, uint32_t pin, uint32_t af)
{
    LL_GPIO_SetPinMode(port, pin, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetPinOutputType(port, pin, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinPull(port, pin, LL_GPIO_PULL_UP);
    LL_GPIO_SetPinSpeed(port, pin, LL_GPIO_SPEED_FREQ_VERY_HIGH);

    if (pin < LL_GPIO_PIN_8)
    {
        LL_GPIO_SetAFPin_0_7(port, pin, af);
    }
    else
    {
        LL_GPIO_SetAFPin_8_15(port, pin, af);
    }
}

static void enc_dma_position_gpio_init(void)
{
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOB);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOC);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOD);

    for (uint8_t index = 0; index < ENC_DMA_CHANNELS; ++index)
    {
        enc_dma_gpio_set_af(g_enc_hw[index].port_a, g_enc_hw[index].pin_a, g_enc_hw[index].af_a);
        enc_dma_gpio_set_af(g_enc_hw[index].port_b, g_enc_hw[index].pin_b, g_enc_hw[index].af_b);
    }
}

static void enc_dma_capture_gpio_init(void)
{
    enc_dma_gpio_set_af(g_capture_hw[0].port, g_capture_hw[0].pin, g_capture_hw[0].af);
    enc_dma_gpio_set_af(g_capture_hw[1].port, g_capture_hw[1].pin, g_capture_hw[1].af);
    enc_dma_gpio_set_af(g_capture_hw[2].port, g_capture_hw[2].pin, g_capture_hw[2].af);
    enc_dma_gpio_set_af(g_capture_hw[3].port, g_capture_hw[3].pin, g_capture_hw[3].af);
}

static void enc_dma_position_timer_init(const enc_dma_hw_cfg_t *cfg)
{
    cfg->tim->CR1 = 0U;
    cfg->tim->CR2 = 0U;
    cfg->tim->SMCR = TIM_SMCR_SMS_0 | TIM_SMCR_SMS_1;
    cfg->tim->CCMR1 = TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0;
    cfg->tim->CCER = 0U;
    cfg->tim->PSC = 0U;
    cfg->tim->ARR = (cfg->counter_bits >= 32U) ? 0xFFFFFFFFUL : 0xFFFFUL;
    cfg->tim->CNT = 0U;
    cfg->tim->EGR = TIM_EGR_UG;
    cfg->tim->CR1 = TIM_CR1_CEN;
}

static void enc_dma_capture_timer_init(void)
{
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM3);

    ENC_DMA_CAPTURE_TIMER->CR1 = 0U;
    ENC_DMA_CAPTURE_TIMER->CR2 = 0U;
    ENC_DMA_CAPTURE_TIMER->PSC = (uint32_t)((84000000UL / ENC_DMA_CAPTURE_TIMER_HZ) - 1UL);
    ENC_DMA_CAPTURE_TIMER->ARR = 0xFFFFU;
    ENC_DMA_CAPTURE_TIMER->CCMR1 = TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0;
    ENC_DMA_CAPTURE_TIMER->CCMR2 = TIM_CCMR2_CC3S_0 | TIM_CCMR2_CC4S_0;
    ENC_DMA_CAPTURE_TIMER->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E | TIM_CCER_CC3E | TIM_CCER_CC4E;
    ENC_DMA_CAPTURE_TIMER->DIER = TIM_DIER_CC1DE | TIM_DIER_CC2DE | TIM_DIER_CC3DE | TIM_DIER_CC4DE;
    ENC_DMA_CAPTURE_TIMER->EGR = TIM_EGR_UG;
    ENC_DMA_CAPTURE_TIMER->CR1 = TIM_CR1_CEN;
}

static void enc_dma_capture_stream_clear_flags(DMA_Stream_TypeDef *stream)
{
    if (stream == DMA1_Stream2)
    {
        DMA1->LIFCR = DMA_LIFCR_CFEIF2 | DMA_LIFCR_CDMEIF2 | DMA_LIFCR_CTEIF2 | DMA_LIFCR_CHTIF2 | DMA_LIFCR_CTCIF2;
    }
    else if (stream == DMA1_Stream4)
    {
        DMA1->HIFCR = DMA_HIFCR_CFEIF4 | DMA_HIFCR_CDMEIF4 | DMA_HIFCR_CTEIF4 | DMA_HIFCR_CHTIF4 | DMA_HIFCR_CTCIF4;
    }
    else if (stream == DMA1_Stream5)
    {
        DMA1->HIFCR = DMA_HIFCR_CFEIF5 | DMA_HIFCR_CDMEIF5 | DMA_HIFCR_CTEIF5 | DMA_HIFCR_CHTIF5 | DMA_HIFCR_CTCIF5;
    }
    else if (stream == DMA1_Stream7)
    {
        DMA1->HIFCR = DMA_HIFCR_CFEIF7 | DMA_HIFCR_CDMEIF7 | DMA_HIFCR_CTEIF7 | DMA_HIFCR_CHTIF7 | DMA_HIFCR_CTCIF7;
    }
}

static void enc_dma_capture_stream_init(uint8_t index)
{
    const enc_capture_dma_cfg_t *cfg = &g_capture_hw[index];

    enc_dma_wait_disabled(cfg->stream);
    enc_dma_capture_stream_clear_flags(cfg->stream);

    cfg->stream->PAR = (uint32_t)cfg->ccr_reg;
    cfg->stream->M0AR = (uint32_t)g_capture_buffers[index];
    cfg->stream->NDTR = ENC_DMA_CAPTURE_BUFFER_LEN;
    cfg->stream->FCR = 0U;
    cfg->stream->CR = ((uint32_t)cfg->dma_channel << DMA_SxCR_CHSEL_Pos)
                    | DMA_SxCR_MINC
                    | DMA_SxCR_CIRC
                    | DMA_SxCR_PL_1
                    | DMA_SxCR_MSIZE_0
                    | DMA_SxCR_PSIZE_0;
    cfg->stream->CR |= DMA_SxCR_EN;
}

static int32_t enc_dma_direction_sign(TIM_TypeDef *tim)
{
    return ((tim->CR1 & TIM_CR1_DIR) != 0U) ? -1 : 1;
}

static int32_t enc_dma_delta(uint32_t current, uint32_t previous, uint8_t counter_bits)
{
    if (counter_bits >= 32U)
    {
        return (int32_t)(current - previous);
    }

    uint32_t mask = (1UL << counter_bits) - 1UL;
    uint32_t sign_bit = 1UL << (counter_bits - 1U);
    uint32_t full_scale = mask + 1UL;
    int32_t delta = (int32_t)((current - previous) & mask);

    if ((uint32_t)delta >= sign_bit)
    {
        delta -= (int32_t)full_scale;
    }

    return delta;
}

static void enc_dma_process_capture(uint8_t index)
{
    const enc_capture_dma_cfg_t *cfg = &g_capture_hw[index];
    enc_dma_state_t *state = &g_enc_state[index];
    uint8_t head = (uint8_t)((ENC_DMA_CAPTURE_BUFFER_LEN - cfg->stream->NDTR) & (ENC_DMA_CAPTURE_BUFFER_LEN - 1U));

    while (state->dma_head != head)
    {
        uint16_t sample = g_capture_buffers[index][state->dma_head];

        if (state->capture_valid != 0U)
        {
            uint16_t ticks = (uint16_t)(sample - state->last_capture);
            if (ticks != 0U)
            {
                int32_t cps = (int32_t)((ENC_DMA_COUNTS_PER_CAPTURE * ENC_DMA_CAPTURE_TIMER_HZ) / ticks);
                state->velocity_slow = cps * enc_dma_direction_sign(g_enc_hw[index].tim);
                state->stale_cycles = 0U;
            }
        }

        state->last_capture = sample;
        state->capture_valid = 1U;
        state->dma_head = (uint8_t)((state->dma_head + 1U) & (ENC_DMA_CAPTURE_BUFFER_LEN - 1U));
    }
}

void enc_dma_init(void)
{
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM2);
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM3);
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM4);
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM5);
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_TIM8);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);

    enc_dma_position_gpio_init();
    enc_dma_capture_gpio_init();

    for (uint8_t index = 0; index < ENC_DMA_CHANNELS; ++index)
    {
        const enc_dma_hw_cfg_t *cfg = &g_enc_hw[index];
        enc_dma_state_t *state = &g_enc_state[index];

        state->enabled = 1U;
        state->status = ENC_STATUS_ENABLED;
        state->previous_sample = 0U;
        state->position = 0;
        state->velocity_fast = 0;
        state->velocity_slow = 0;
        state->velocity_fused = 0;
        state->last_capture = 0U;
        state->capture_valid = 0U;
        state->dma_head = 0U;
        state->stale_cycles = ENC_DMA_STALE_LIMIT_CYCLES;

        enc_dma_position_timer_init(cfg);
        enc_dma_capture_stream_init(index);
    }

    enc_dma_capture_timer_init();
}

void enc_dma_set_enabled(uint8_t encoder_index, uint8_t enabled)
{
    if (encoder_index >= ENC_DMA_CHANNELS)
    {
        return;
    }

    g_enc_state[encoder_index].enabled = enabled ? 1U : 0U;
    g_enc_state[encoder_index].status = enabled ? ENC_STATUS_ENABLED : 0U;
    g_enc_state[encoder_index].velocity_fast = 0;
    g_enc_state[encoder_index].velocity_slow = 0;
    g_enc_state[encoder_index].velocity_fused = 0;
    g_enc_state[encoder_index].stale_cycles = ENC_DMA_STALE_LIMIT_CYCLES;
    g_enc_state[encoder_index].capture_valid = 0U;
    g_enc_state[encoder_index].dma_head = (uint8_t)((ENC_DMA_CAPTURE_BUFFER_LEN - g_capture_hw[encoder_index].stream->NDTR) & (ENC_DMA_CAPTURE_BUFFER_LEN - 1U));
    g_enc_state[encoder_index].previous_sample = g_enc_hw[encoder_index].tim->CNT;
    g_enc_state[encoder_index].last_capture = 0U;
}

uint8_t enc_dma_is_enabled(uint8_t encoder_index)
{
    if (encoder_index >= ENC_DMA_CHANNELS)
    {
        return 0U;
    }

    return g_enc_state[encoder_index].enabled;
}

uint8_t enc_dma_get_status(uint8_t encoder_index)
{
    if (encoder_index >= ENC_DMA_CHANNELS)
    {
        return 0U;
    }

    return g_enc_state[encoder_index].status;
}

void enc_dma_latch(void)
{
    for (uint8_t index = 0; index < ENC_DMA_CHANNELS; ++index)
    {
        const enc_dma_hw_cfg_t *cfg = &g_enc_hw[index];
        enc_dma_state_t *state = &g_enc_state[index];

        if (state->enabled == 0U)
        {
            state->status = 0U;
            state->previous_sample = cfg->tim->CNT;
            state->velocity_fast = 0;
            state->velocity_slow = 0;
            state->velocity_fused = 0;
            continue;
        }

        state->status = ENC_STATUS_ENABLED;

        uint32_t current_sample = cfg->tim->CNT;
        int32_t delta = enc_dma_delta(current_sample, state->previous_sample, cfg->counter_bits);

        state->position += delta;
        state->previous_sample = current_sample;

        if (g_enc_servo_period_ns != 0U)
        {
            state->velocity_fast = (int32_t)(((int64_t)delta * 1000000000LL) / (int64_t)g_enc_servo_period_ns);
        }
        else
        {
            state->velocity_fast = 0;
        }

        enc_dma_process_capture(index);

        if (state->capture_valid != 0U)
        {
            state->status |= ENC_STATUS_CAPTURE_VALID;
        }

        if (state->dma_head == ((ENC_DMA_CAPTURE_BUFFER_LEN - g_capture_hw[index].stream->NDTR) & (ENC_DMA_CAPTURE_BUFFER_LEN - 1U)))
        {
            if (state->stale_cycles < 0xFFU)
            {
                state->stale_cycles++;
            }
        }

        if (state->stale_cycles < ENC_DMA_STALE_LIMIT_CYCLES)
        {
            state->status |= ENC_STATUS_CAPTURE_FRESH;
        }

        if ((delta <= -ENC_DMA_FAST_THRESHOLD) || (delta >= ENC_DMA_FAST_THRESHOLD))
        {
            state->velocity_fused = state->velocity_fast;
            state->status |= ENC_STATUS_VELOCITY_SOURCE_FAST;
        }
        else if (state->stale_cycles < ENC_DMA_STALE_LIMIT_CYCLES)
        {
            state->velocity_fused = state->velocity_slow;
            state->status |= ENC_STATUS_VELOCITY_SOURCE_SLOW;
        }
        else
        {
            state->velocity_slow = 0;
            state->velocity_fused = 0;
        }
    }
}

void enc_dma_set_servo_period_ns(uint32_t period_ns)
{
    if (period_ns != 0U)
    {
        g_enc_servo_period_ns = period_ns;
    }
}

int32_t enc_dma_get_position(uint8_t encoder_index)
{
    if (encoder_index >= ENC_DMA_CHANNELS)
    {
        return 0;
    }

    return g_enc_state[encoder_index].position;
}

int32_t enc_dma_get_velocity(uint8_t encoder_index)
{
    if (encoder_index >= ENC_DMA_CHANNELS)
    {
        return 0;
    }

    return g_enc_state[encoder_index].velocity_fused;
}

int32_t enc_dma_get_velocity_fast(uint8_t encoder_index)
{
    if (encoder_index >= ENC_DMA_CHANNELS)
    {
        return 0;
    }

    return g_enc_state[encoder_index].velocity_fast;
}

int32_t enc_dma_get_velocity_slow(uint8_t encoder_index)
{
    if (encoder_index >= ENC_DMA_CHANNELS)
    {
        return 0;
    }

    return g_enc_state[encoder_index].velocity_slow;
}
