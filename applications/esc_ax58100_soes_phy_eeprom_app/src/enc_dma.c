#include "enc_dma.h"

#include <stddef.h>

#include "stm32f407xx.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_exti.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_system.h"

/*
 * Mapa de pines del modulo de encoder
 *
 * Contadores de posicion (cuadratura A/B):
 * Eje 0: PA0/PA1   -> TIM5_CH1/TIM5_CH2
 * Eje 1: PA15/PB3  -> TIM2_CH1/TIM2_CH2
 * Eje 2: PD12/PD13 -> TIM4_CH1/TIM4_CH2
 * Eje 3: PC6/PC7   -> TIM8_CH1/TIM8_CH2
 *
 * Entradas de captura de periodo a baja velocidad (una entrada por eje, normalmente A):
 * Eje 0: PB4 -> TIM3_CH1 -> DMA1 Stream4 Channel5
 * Eje 1: PB5 -> TIM3_CH2 -> DMA1 Stream5 Channel5
 * Eje 2: PC8 -> TIM3_CH3 -> DMA1 Stream7 Channel5
 * Eje 3: PC9 -> TIM3_CH4 -> DMA1 Stream2 Channel5
 *
 * Conexion fisica para cada encoder:
 * 1. Las señales A y B deben llegar a las entradas del temporizador de posicion indicadas arriba.
 * 2. La señal A tambien debe ramificarse hacia la entrada de captura TIM3 del mismo eje.
 * 3. Si el encoder es diferencial (driver de linea RS-422), coloca un receptor antes
 *    del MCU y entrega las señales A/B/Z en formato no diferencial a los pines del STM32.
 * 4. El MCU y la interfaz del encoder deben compartir una referencia de masa valida.
 * 5. La señal Z/index se muestrea en PB9..PB12 y se refleja en Enc_Status como:
 *    - INDEX_LEVEL: nivel logico actual de la entrada Z.
 *    - INDEX_LATCHED: pulso de un ciclo servo cuando llega un nuevo flanco ascendente.
 */

#define ENC_DMA_FAST_WINDOW_TICKS     16U

typedef struct
{
    /* Temporizador usado como contador de posicion en cuadratura. */
    TIM_TypeDef *tim;

    /* Ruteo del pin del canal A del encoder. */
    GPIO_TypeDef *port_a;
    uint32_t pin_a;
    uint32_t af_a;

    /* Ruteo del pin del canal B del encoder. */
    GPIO_TypeDef *port_b;
    uint32_t pin_b;
    uint32_t af_b;

    /* Anchura real del contador: TIM2/TIM5 son de 32 bits y TIM4/TIM8 de 16 bits. */
    uint8_t counter_bits;
} enc_dma_hw_cfg_t;

typedef struct
{
    /* Habilitacion logica visible para el resto de la aplicacion. */
    uint8_t enabled;

    /* Campo de bits exportado mediante enc_dma_get_status(). */
    uint8_t status;

    /* Ultima muestra bruta del contador usada para calcular el delta en el siguiente latch. */
    uint32_t previous_sample;

    /* Posicion integrada en cuentas de encoder. */
    int32_t position;

    /* Velocidad obtenida a partir del delta de posicion y del periodo del lazo rapido. */
    int32_t velocity_fast;

    /* Acumulador y estado del promedio movil usado por la rama rapida. */
    int32_t fast_delta_accumulator;
    uint8_t fast_history_head;
    uint8_t fast_history_count;
    int32_t fast_delta_history[ENC_DMA_FAST_WINDOW_TICKS];
    uint8_t fusion_region;

    /* Velocidad obtenida de la medida de periodo a baja velocidad con la captura de TIM3. */
    int32_t velocity_slow;

    /* Velocidad final seleccionada y reportada al resto del firmware. */
    int32_t velocity_fused;

    /* Muestra previa de captura TIM3 para este eje. */
    uint16_t last_capture;

    /* Se activa cuando ya existe la primera medida de periodo valida. */
    uint8_t capture_valid;

    /* Indice software de consumo dentro del buffer circular DMA. */
    uint8_t dma_head;

    /* Cuenta updates internos sin capturas nuevas. */
    uint16_t stale_cycles;
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

typedef struct
{
    GPIO_TypeDef *port;
    uint32_t pin;
    uint32_t exti_line;
    uint32_t exti_source;
} enc_index_irq_cfg_t;

#define ENC_DMA_CAPTURE_TIMER         TIM3
#define ENC_DMA_CAPTURE_TIMER_HZ      1000000UL
#define ENC_DMA_CAPTURE_BUFFER_LEN    8U
#define ENC_DMA_COUNTS_PER_CAPTURE    4UL
#define ENC_DMA_FAST_THRESHOLD_CPS    30000UL
#define ENC_DMA_STALE_TIMEOUT_NS      200000000UL
#define ENC_DMA_FAST_WINDOW_LOW_CPS   12000UL
#define ENC_DMA_FAST_WINDOW_HIGH_CPS  30000UL
#define ENC_DMA_FUSION_HYST_MIN_CPS   1000UL

#define ENC_DMA_FUSION_REGION_SLOW    0U
#define ENC_DMA_FUSION_REGION_BLEND   1U
#define ENC_DMA_FUSION_REGION_FAST    2U

/*
 * Valores de ajuste en tiempo de ejecucion.
 *
 * g_enc_update_period_ns:
 *   Periodo de actualizacion interno del estimador. Hace falta para convertir
 *   un delta de posicion por update en cuentas por segundo.
 *
 * g_enc_fast_threshold_cps:
 *   Umbral de cruce entre las ramas rapida y lenta, expresado en cuentas/s.
 */
static uint32_t g_enc_update_period_ns = 1000000UL;
static uint32_t g_enc_fast_threshold_cps = ENC_DMA_FAST_THRESHOLD_CPS;
static uint16_t g_enc_stale_limit_cycles = 200U;

/*
 * Palabras altas software para los temporizadores de encoder de 16 bits.
 *
 * Solo lo usan el eje 2 (TIM4) y el eje 3 (TIM8). Los ejes 0 y 1 ya disponen
 * de contadores de 32 bits implementados por el propio periferico.
 */
static volatile int32_t g_enc_upper[ENC_DMA_CHANNELS];

/* Buffers circulares DMA alimentados por las capturas de TIM3, uno por eje. */
static volatile uint16_t g_capture_buffers[4][ENC_DMA_CAPTURE_BUFFER_LEN];

/* Tabla estatica de conexion para los temporizadores de posicion en cuadratura. */
static const enc_dma_hw_cfg_t g_enc_hw[] = {
    {TIM5, GPIOA, LL_GPIO_PIN_0, LL_GPIO_AF_2, GPIOA, LL_GPIO_PIN_1, LL_GPIO_AF_2, 32U},
    {TIM2, GPIOA, LL_GPIO_PIN_15, LL_GPIO_AF_1, GPIOB, LL_GPIO_PIN_3, LL_GPIO_AF_1, 32U},
    {TIM4, GPIOD, LL_GPIO_PIN_12, LL_GPIO_AF_2, GPIOD, LL_GPIO_PIN_13, LL_GPIO_AF_2, 16U},
    {TIM8, GPIOC, LL_GPIO_PIN_6, LL_GPIO_AF_3, GPIOC, LL_GPIO_PIN_7, LL_GPIO_AF_3, 16U},
};

/* Tabla estatica de conexion para la ruta de captura a baja velocidad con TIM3 + DMA. */
static const enc_capture_dma_cfg_t g_capture_hw[] = {
    {DMA1_Stream4, &TIM3->CCR1, GPIOB, LL_GPIO_PIN_4, LL_GPIO_AF_2, 5U},
    {DMA1_Stream5, &TIM3->CCR2, GPIOB, LL_GPIO_PIN_5, LL_GPIO_AF_2, 5U},
    {DMA1_Stream7, &TIM3->CCR3, GPIOC, LL_GPIO_PIN_8, LL_GPIO_AF_2, 5U},
    {DMA1_Stream2, &TIM3->CCR4, GPIOC, LL_GPIO_PIN_9, LL_GPIO_AF_2, 5U},
};

/* Tabla estatica de conexion para las interrupciones externas de Z/index. */
static const enc_index_irq_cfg_t g_index_irq_hw[] = {
    {GPIOB, LL_GPIO_PIN_9, LL_EXTI_LINE_9, LL_SYSCFG_EXTI_LINE9},
    {GPIOB, LL_GPIO_PIN_10, LL_EXTI_LINE_10, LL_SYSCFG_EXTI_LINE10},
    {GPIOB, LL_GPIO_PIN_11, LL_EXTI_LINE_11, LL_SYSCFG_EXTI_LINE11},
    {GPIOB, LL_GPIO_PIN_12, LL_EXTI_LINE_12, LL_SYSCFG_EXTI_LINE12},
};

/* Estado en tiempo de ejecucion por eje, actualizado en el lazo rapido. */
static enc_dma_state_t g_enc_state[ENC_DMA_CHANNELS];

/* Latch monoestable activado desde la IRQ EXTI y consumido durante enc_dma_latch(). */
static volatile uint8_t g_enc_index_latched[ENC_DMA_CHANNELS];

static void enc_dma_wait_disabled(DMA_Stream_TypeDef *stream)
{
    /* No es seguro reprogramar un stream DMA hasta que EN haya bajado de verdad. */
    stream->CR &= ~DMA_SxCR_EN;
    while ((stream->CR & DMA_SxCR_EN) != 0U)
    {
    }
}

static void enc_dma_update_runtime_scalars(void)
{
    uint64_t stale_cycles;

    if (g_enc_update_period_ns == 0U)
    {
        g_enc_update_period_ns = 1000000UL;
    }

    stale_cycles = ((uint64_t)ENC_DMA_STALE_TIMEOUT_NS + (uint64_t)g_enc_update_period_ns - 1ULL)
        / (uint64_t)g_enc_update_period_ns;
    if (stale_cycles < 1ULL)
    {
        stale_cycles = 1ULL;
    }
    if (stale_cycles > 0xFFFFULL)
    {
        stale_cycles = 0xFFFFULL;
    }
    g_enc_stale_limit_cycles = (uint16_t)stale_cycles;
}

static uint32_t enc_dma_abs_i32(int32_t value)
{
    return (value < 0) ? (uint32_t)(-value) : (uint32_t)value;
}

static uint8_t enc_dma_select_fast_window_ticks(int32_t reference_velocity_cps)
{
    uint32_t magnitude = enc_dma_abs_i32(reference_velocity_cps);

    if (magnitude >= ENC_DMA_FAST_WINDOW_HIGH_CPS)
    {
        return 4U;
    }

    if (magnitude >= ENC_DMA_FAST_WINDOW_LOW_CPS)
    {
        return 8U;
    }

    return 16U;
}

static int32_t enc_dma_sum_recent_fast_deltas(const enc_dma_state_t *state, uint8_t window_ticks)
{
    int32_t sum = 0;
    uint8_t available = state->fast_history_count;
    uint8_t used = (available < window_ticks) ? available : window_ticks;
    uint8_t offset = 0U;

    while (offset < used)
    {
        uint8_t index = (uint8_t)((state->fast_history_head + ENC_DMA_FAST_WINDOW_TICKS - 1U - offset)
            % ENC_DMA_FAST_WINDOW_TICKS);
        sum += state->fast_delta_history[index];
        offset++;
    }

    return sum;
}

static int32_t enc_dma_blend_velocity(int32_t slow_velocity, int32_t fast_velocity, uint32_t fast_weight_q15)
{
    int64_t slow_weight_q15 = (int64_t)32768 - (int64_t)fast_weight_q15;
    int64_t blended = ((int64_t)slow_velocity * slow_weight_q15) + ((int64_t)fast_velocity * (int64_t)fast_weight_q15);
    return (int32_t)(blended / 32768LL);
}

static int32_t enc_dma_delta(uint32_t current, uint32_t previous, uint8_t counter_bits);
static uint32_t enc_dma_read_extended_counter(const enc_dma_hw_cfg_t *cfg, uint8_t index);
static void enc_dma_handle_position_overflow_irq(TIM_TypeDef *tim, uint8_t index);

static void enc_dma_gpio_set_af(GPIO_TypeDef *port, uint32_t pin, uint32_t af)
{
    /* A partir de aqui el periferico temporizador pasa a controlar el pin. */
    LL_GPIO_SetPinMode(port, pin, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetPinOutputType(port, pin, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinPull(port, pin, LL_GPIO_PULL_UP);
    LL_GPIO_SetPinSpeed(port, pin, LL_GPIO_SPEED_FREQ_VERY_HIGH);

    /* STM32 divide la configuracion de funcion alternativa entre el bloque bajo y alto. */
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
    /* Los pines A/B de los encoders estan repartidos entre GPIOA/B/C/D, asi que se habilitan todos los bancos usados. */
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOB);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOC);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOD);

    /* Aplica la tabla de ruteo de pines A/B eje por eje. */
    for (uint8_t index = 0; index < ENC_DMA_CHANNELS; ++index)
    {
        enc_dma_gpio_set_af(g_enc_hw[index].port_a, g_enc_hw[index].pin_a, g_enc_hw[index].af_a);
        enc_dma_gpio_set_af(g_enc_hw[index].port_b, g_enc_hw[index].pin_b, g_enc_hw[index].af_b);
    }
}

static void enc_dma_capture_gpio_init(void)
{
    /* La señal A tambien se ramifica hacia la captura de TIM3 para medir velocidad baja por periodo. */
    enc_dma_gpio_set_af(g_capture_hw[0].port, g_capture_hw[0].pin, g_capture_hw[0].af);
    enc_dma_gpio_set_af(g_capture_hw[1].port, g_capture_hw[1].pin, g_capture_hw[1].af);
    enc_dma_gpio_set_af(g_capture_hw[2].port, g_capture_hw[2].pin, g_capture_hw[2].af);
    enc_dma_gpio_set_af(g_capture_hw[3].port, g_capture_hw[3].pin, g_capture_hw[3].af);
}

static void enc_dma_index_gpio_init(void)
{
    for (uint8_t index = 0; index < ENC_DMA_CHANNELS; ++index)
    {
        const enc_index_irq_cfg_t *cfg = &g_index_irq_hw[index];

        /* La señal index se muestrea como entrada digital simple y tambien se enruta a EXTI. */
        LL_GPIO_SetPinMode(cfg->port, cfg->pin, LL_GPIO_MODE_INPUT);
        LL_GPIO_SetPinPull(cfg->port, cfg->pin, LL_GPIO_PULL_NO);
    }
}

static void enc_dma_index_irq_init(void)
{
    /* SYSCFG es necesario para mapear cada pin GPIO hacia su linea EXTI. */
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SYSCFG);

    for (uint8_t index = 0; index < ENC_DMA_CHANNELS; ++index)
    {
        const enc_index_irq_cfg_t *cfg = &g_index_irq_hw[index];

        /* Enruta PB9..PB12 a EXTI y deja activo solo el flanco ascendente de Z. */
        LL_SYSCFG_SetEXTISource(LL_SYSCFG_EXTI_PORTB, cfg->exti_source);
        LL_EXTI_ClearFlag_0_31(cfg->exti_line);
        LL_EXTI_EnableRisingTrig_0_31(cfg->exti_line);
        LL_EXTI_DisableFallingTrig_0_31(cfg->exti_line);
        LL_EXTI_EnableIT_0_31(cfg->exti_line);
    }

    NVIC_SetPriority(EXTI9_5_IRQn, 5);
    NVIC_EnableIRQ(EXTI9_5_IRQn);
    NVIC_SetPriority(EXTI15_10_IRQn, 5);
    NVIC_EnableIRQ(EXTI15_10_IRQn);
}

static void enc_dma_handle_index_irq(uint8_t index)
{
    /* Ignora flancos de index mientras el canal logico este deshabilitado. */
    if (g_enc_state[index].enabled == 0U)
    {
        return;
    }

    /* Difere el evento visible al latch del servo en vez de trabajar dentro de la IRQ. */
    g_enc_index_latched[index] = 1U;
}

static void enc_dma_index_update_live_status(uint8_t index, enc_dma_state_t *state)
{
    const enc_index_irq_cfg_t *irq_cfg = &g_index_irq_hw[index];

    if (LL_GPIO_IsInputPinSet(irq_cfg->port, irq_cfg->pin))
    {
        /* Nivel logico actual de Z. */
        state->status |= ENC_STATUS_INDEX_LEVEL;
    }

    if (g_enc_index_latched[index] != 0U)
    {
        /* Mantiene el pulso visible hasta que el ciclo servo lo consuma. */
        state->status |= ENC_STATUS_INDEX_LATCHED;
    }
}

static void enc_dma_position_timer_init(const enc_dma_hw_cfg_t *cfg, uint8_t index)
{
    /* Parte de un estado limpio del temporizador antes de entrar en modo encoder. */
    cfg->tim->CR1 = 0U;
    cfg->tim->CR2 = 0U;
    cfg->tim->DIER = 0U;
    cfg->tim->SR = 0U;

    /* Modo encoder 3: cuenta en ambos flancos de TI1 y TI2. */
    cfg->tim->SMCR = TIM_SMCR_SMS_0 | TIM_SMCR_SMS_1;                       // Modo encoder 3: cuenta en ambos flancos de TI1 y TI2.
    cfg->tim->CCMR1 = TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0;                  // Las entradas del encoder se rutean hacia los canales de captura del temporizador.
    cfg->tim->CCER = 0U;                                                    // No se invierten los flancos de captura, ni se habilita el filtro digital.            
    cfg->tim->PSC = 0U;                                                     // El prescaler se deja a 0 para que el contador de posicion cuente cada flanco del encoder sin division. 
    cfg->tim->ARR = (cfg->counter_bits >= 32U) ? 0xFFFFFFFFUL : 0xFFFFUL;   // El auto-reload se ajusta segun la anchura del contador para maximizar el rango antes de overflow.
    cfg->tim->CNT = 0U;                                                     // El contador se inicia a 0, aunque no es estrictamente necesario porque el primer overflow se detecta al comparar el valor actual con la muestra previa.

    if (cfg->counter_bits < 32U)
    {
        /* Los temporizadores de 16 bits necesitan una palabra alta software para emular un contador de posicion de 32 bits. */
        g_enc_upper[index] = 0;
    }

    /* Fuerza una actualizacion para que ARR y PSC entren en vigor de forma determinista. */
    cfg->tim->EGR = TIM_EGR_UG;

    /* UG recarga ARR y PSC, pero tambien deja UIF pendiente en los temporizadores STM32. */
    cfg->tim->SR = 0U;

    if (cfg->counter_bits < 32U)
    {
        /* Solo TIM4 y TIM8 necesitan interrupciones de actualizacion para extender el overflow. */
        cfg->tim->DIER = TIM_DIER_UIE;
    }

    /* Empieza a contar transiciones del encoder. */
    cfg->tim->CR1 = TIM_CR1_CEN;
}

static void enc_dma_capture_timer_init(void)
{
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM3);

    /* TIM3 se usa solo como base de tiempos libre para la captura a 100 kHz. */
    ENC_DMA_CAPTURE_TIMER->CR1 = 0U;                                                                    // No se necesitan configuraciones de modo especificas porque no se va a usar el contador de TIM3, solo las capturas de CCRx.
    ENC_DMA_CAPTURE_TIMER->CR2 = 0U;                                                                    // No se necesitan configuraciones de modo especificas porque no se va a usar el contador de TIM3, solo las capturas de CCRx.
    ENC_DMA_CAPTURE_TIMER->PSC = (uint32_t)((84000000UL / ENC_DMA_CAPTURE_TIMER_HZ) - 1UL);             // El prescaler se ajusta para que el timer de captura funcione a 100 kHz, lo que da una resolucion de 10 us en la medicion de periodo.
    ENC_DMA_CAPTURE_TIMER->ARR = 0xFFFFU;                                                               // El auto-reload se ajusta al valor máximo de 16 bits para maximizar el rango antes de overflow.
    ENC_DMA_CAPTURE_TIMER->CCMR1 = TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0;                                 // Las entradas de captura se rutean hacia los canales de captura del temporizador.
    ENC_DMA_CAPTURE_TIMER->CCMR2 = TIM_CCMR2_CC3S_0 | TIM_CCMR2_CC4S_0;                                 // Las entradas de captura se rutean hacia los canales de captura del temporizador.
    ENC_DMA_CAPTURE_TIMER->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E | TIM_CCER_CC3E | TIM_CCER_CC4E;        // Se habilitan las capturas en los 4 canales, sin invertir los flancos de captura ni activar el filtro digital.
    ENC_DMA_CAPTURE_TIMER->DIER = TIM_DIER_CC1DE | TIM_DIER_CC2DE | TIM_DIER_CC3DE | TIM_DIER_CC4DE;    // Se habilitan las solicitudes DMA en los 4 canales para que cada nueva captura se refleje como una transferencia DMA hacia los buffers circulares.
    ENC_DMA_CAPTURE_TIMER->EGR = TIM_EGR_UG;                                                             // Genera un evento de actualización para cargar los registros ARR y PSC.
    ENC_DMA_CAPTURE_TIMER->CR1 = TIM_CR1_CEN;                                                            // Habilita el contador del temporizador.
}

static void enc_dma_capture_stream_clear_flags(DMA_Stream_TypeDef *stream)
{
    /* Cada stream reporta en un banco distinto de registros de flags de DMA1. */

    // Se limpian todas las banderas de transferencia, error y finalizacion pendientes 
    // para el stream de captura a baja velocidad del eje 3. Esto es necesario antes de 
    // reprogramar el stream porque algunas banderas se mantienen hasta que se limpian 
    // manualmente, y si no se limpian pueden generar interrupciones espurias o impedir 
    // que el stream se habilite correctamente.

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
    const enc_capture_dma_cfg_t *cfg = &g_capture_hw[index];  // Configuracion de la ruta de captura a baja velocidad con TIM3 + DMA.

    enc_dma_wait_disabled(cfg->stream);                       // Asegura que el stream DMA este completamente deshabilitado antes de 
                                                              // reprogramarlo, lo que es necesario para evitar condiciones de carrera 
                                                              // o comportamientos indeterminados al modificar los registros del stream 
                                                              // mientras esta activo.
    
    enc_dma_capture_stream_clear_flags(cfg->stream);          // Limpia cualquier bandera de transferencia, error o finalizacion pendiente 
                                                              // para el stream de captura a baja velocidad del eje actual, lo que es 
                                                              // necesario para evitar interrupciones espurias o problemas al habilitar 
                                                              // el stream despues de reprogramarlo.

                                                              /* La fuente periferica es el registro de captura y el destino es el buffer circular del eje. */
    cfg->stream->PAR = (uint32_t)cfg->ccr_reg;                // El registro de captura CCRx de TIM3 se usa como fuente periferica para 
                                                              // el DMA, lo que permite que cada nueva captura se refleje como una 
                                                              // transferencia DMA hacia el buffer circular correspondiente.
    
    cfg->stream->M0AR = (uint32_t)g_capture_buffers[index];   // El buffer circular del eje actual se asigna como destino de las transferencias DMA, 
                                                              // lo que permite almacenar las muestras de captura de forma continua y eficiente
                                                              // sin necesidad de intervencion del CPU para mover los datos.
    
    cfg->stream->NDTR = ENC_DMA_CAPTURE_BUFFER_LEN;           // El numero de datos a transferir se ajusta al tamaño del buffer circular, 
                                                              // lo que permite que el DMA gestione el ciclo del buffer de forma automatica y eficiente.
    
    cfg->stream->FCR = 0U;                                    // Se deshabilita el modo de flujo directo y el FIFO, lo que es adecuado para 
                                                              // transferencias simples de periferico a memoria como en este caso, 
                                                              // y puede reducir la latencia y la complejidad del manejo de datos en el CPU.  

    /*
     * El modo circular mantiene el muestreo indefinidamente.
     * Las transferencias de memoria y periferico son de 16 bits porque CCRx guarda marcas de tiempo de 16 bits.
     */
    // Se configura el stream DMA para que cada nueva captura de TIM3 se refleje como una transferencia hacia el buffer circular del eje,
    // con las siguientes caracteristicas:
    // DMA_SxCR_CHSEL_Pos: se selecciona el canal DMA correspondiente al CCRx de TIM3 para que el stream se active con las capturas del canal correcto.
    // DMA_SxCR_MINC: se habilita la direccion de memoria incrementada para que el DMA avance automaticamente por el buffer circular con cada nueva muestra.
    // DMA_SxCR_CIRC: se habilita el modo circular para que el DMA vuelva al inicio del buffer automaticamente al
    //                   alcanzar el final, permitiendo un muestreo continuo sin necesidad de reprogramar el stream.
    // DMA_SxCR_PL_1: se establece la prioridad del stream a alta para asegurar que las transferencias de captura se procesen con rapidez y no se pierdan muestras incluso bajo carga alta del CPU.
    // DMA_SxCR_MSIZE_0 y DMA_SxCR_PSIZE_0: se configuran las transferencias de memoria y periferico a 16 bits
    //                   para que coincidan con el tamaño de los datos de captura en CCRx, lo que es necesario para que las transferencias se realicen correctamente y sin corrupcion de datos.
    cfg->stream->CR = ((uint32_t)cfg->dma_channel << DMA_SxCR_CHSEL_Pos)
                    | DMA_SxCR_MINC
                    | DMA_SxCR_CIRC
                    | DMA_SxCR_PL_1
                    | DMA_SxCR_MSIZE_0
                    | DMA_SxCR_PSIZE_0;
    // El stream DMA se habilita al final de la configuracion para que empiece a funcionar con las nuevas settings, lo que permite que las capturas de TIM3 se reflejen como transferencias hacia el buffer circular del eje correspondiente de forma inmediata y sin necesidad de pasos adicionales.
    cfg->stream->CR |= DMA_SxCR_EN;
}

static int32_t enc_dma_direction_sign(TIM_TypeDef *tim)
{
    /* DIR=1 significa giro inverso o conteo descendente desde el punto de vista del decodificador de cuadratura. */
    return ((tim->CR1 & TIM_CR1_DIR) != 0U) ? -1 : 1;
}

static int32_t enc_dma_delta(uint32_t current, uint32_t previous, uint8_t counter_bits)
{
    if (counter_bits >= 32U)
    {
        /* Los temporizadores nativos de 32 bits pueden restarse directamente. */
        return (int32_t)(current - previous);
    }

    /* Resta modular manual para un contador de menos de 32 bits. */
    uint32_t mask = (1UL << counter_bits) - 1UL;            // mascara para el rango del contador, por ejemplo 0xFFFF para 16 bits.
    uint32_t sign_bit = 1UL << (counter_bits - 1U);         // bit de signo para detectar si el delta es positivo o negativo, por ejemplo 0x8000 para 16 bits.
    uint32_t full_scale = mask + 1UL;                       // valor de escala completa para el contador, por ejemplo 0x10000 para 16 bits.
    int32_t delta = (int32_t)((current - previous) & mask); // delta inicial en el rango del contador

    // si el delta es mayor o igual al bit de signo, se interpreta como un delta negativo que ha envuelto 
    // por debajo de cero, y se ajusta restando la escala completa para obtener el valor correcto. 
    // Por ejemplo, si el delta es 0xFF00 en un contador de 16 bits, se interpreta como -256 en lugar de 
    // +65280, y se ajusta a -256 restando 0x10000.
    if ((uint32_t)delta >= sign_bit)                        
    {
        delta -= (int32_t)full_scale;
    }
    // Si el delta es menor que el bit de signo, se interpreta como un delta positivo normal y no se ajusta.
    return delta;
}

static uint32_t enc_dma_read_extended_counter(const enc_dma_hw_cfg_t *cfg, uint8_t index)
{
    uint32_t upper_before;
    uint32_t upper_after;
    uint32_t counter;
    uint32_t status;

    do
    {
        /* Muestra la palabra alta software, el estado del periferico y la palabra baja del contador con coherencia suficiente. */
        upper_before = (uint32_t)g_enc_upper[index];
        status = cfg->tim->SR;
        counter = cfg->tim->CNT;
        upper_after = (uint32_t)g_enc_upper[index];
    } while (upper_before != upper_after);

    if ((status & TIM_SR_UIF) != 0U)
    {
        /*
         * El temporizador de 16 bits ya ha desbordado, pero la IRQ aun no ha incorporado
         * ese evento en g_enc_upper[]. Se compensa una vez usando la direccion
         * actual de conteo para que la muestra compuesta de 32 bits siga siendo monotona.
         */
        if ((cfg->tim->CR1 & TIM_CR1_DIR) != 0U)
        {
            upper_after--;
        }
        else
        {
            upper_after++;
        }

        /* Relee la palabra baja despues de compensar la palabra alta. */
        counter = cfg->tim->CNT;
    }

    return (upper_after << 16) | (counter & 0xFFFFU);
}

static void enc_dma_handle_position_overflow_irq(TIM_TypeDef *tim, uint8_t index)
{
    if ((tim->SR & TIM_SR_UIF) == 0U)
    {
        return;
    }

    /* En estos temporizadores de extension por overflow solo estan habilitados los eventos de actualizacion. */
    tim->SR = 0U;

    /* Incorpora el desbordamiento a la palabra alta software usando la direccion actual de conteo. */
    if ((tim->CR1 & TIM_CR1_DIR) != 0U)
    {
        g_enc_upper[index]--;
    }
    else
    {
        g_enc_upper[index]++;
    }
}

static void enc_dma_process_capture(uint8_t index)
{
    const enc_capture_dma_cfg_t *cfg = &g_capture_hw[index];
    enc_dma_state_t *state = &g_enc_state[index];

    /* Convierte la posicion de escritura DMA en un indice de cabecera del buffer circular. */
    uint8_t head = (uint8_t)((ENC_DMA_CAPTURE_BUFFER_LEN - cfg->stream->NDTR) & (ENC_DMA_CAPTURE_BUFFER_LEN - 1U));

    /* Consume todas las muestras de captura que el DMA ha producido desde el ultimo ciclo servo. */
    while (state->dma_head != head)
    {
        uint16_t sample = g_capture_buffers[index][state->dma_head];

        if (state->capture_valid != 0U)
        {
            /* TIM3 corre libre a 100 kHz, asi que el delta entre capturas ya es directamente un periodo en ticks. */
            uint16_t ticks = (uint16_t)(sample - state->last_capture);
            if (ticks != 0U)
            {
                /* cuentas/s = cuentas de cuadratura representadas por el evento divididas por el periodo medido. */
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
    /* Habilita todos los temporizadores y bloques DMA usados en este modulo. */
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM2);
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM3);
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM4);
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM5);
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_TIM8);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);

    enc_dma_position_gpio_init();
    enc_dma_capture_gpio_init();
    enc_dma_index_gpio_init();
    enc_dma_index_irq_init();
    enc_dma_update_runtime_scalars();

    /* El estado de ejecucion de cada eje arranca en un estado conocido, habilitado y a cero. */
    for (uint8_t index = 0; index < ENC_DMA_CHANNELS; ++index)
    {
        const enc_dma_hw_cfg_t *cfg = &g_enc_hw[index];
        enc_dma_state_t *state = &g_enc_state[index];

        state->enabled = 1U;
        state->status = ENC_STATUS_ENABLED;
        state->previous_sample = 0U;
        state->position = 0;
        state->velocity_fast = 0;
        state->fast_delta_accumulator = 0;
        state->fast_history_head = 0U;
        state->fast_history_count = 0U;
        state->fusion_region = ENC_DMA_FUSION_REGION_SLOW;
        state->velocity_slow = 0;
        state->velocity_fused = 0;
        state->last_capture = 0U;
        state->capture_valid = 0U;
        state->dma_head = 0U;
        state->stale_cycles = g_enc_stale_limit_cycles;
        g_enc_index_latched[index] = 0U;

        enc_dma_position_timer_init(cfg, index);
        enc_dma_capture_stream_init(index);
    }

    /* Arranca la base de tiempos compartida de captura solo cuando todos los streams estan listos. */
    enc_dma_capture_timer_init();

    /* Las IRQ de actualizacion solo tienen sentido para la extension por overflow de TIM4 y TIM8. */
    NVIC_SetPriority(TIM4_IRQn, 6);
    NVIC_EnableIRQ(TIM4_IRQn);
    NVIC_SetPriority(TIM8_UP_TIM13_IRQn, 6);
    NVIC_EnableIRQ(TIM8_UP_TIM13_IRQn);
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
    g_enc_state[encoder_index].fast_delta_accumulator = 0;
    g_enc_state[encoder_index].fast_history_head = 0U;
    g_enc_state[encoder_index].fast_history_count = 0U;
    g_enc_state[encoder_index].fusion_region = ENC_DMA_FUSION_REGION_SLOW;
    g_enc_state[encoder_index].velocity_slow = 0;
    g_enc_state[encoder_index].velocity_fused = 0;
    g_enc_state[encoder_index].stale_cycles = g_enc_stale_limit_cycles;
    g_enc_state[encoder_index].capture_valid = 0U;

    /* Realinea el consumidor software del DMA con el productor para descartar muestras antiguas. */
    g_enc_state[encoder_index].dma_head = (uint8_t)((ENC_DMA_CAPTURE_BUFFER_LEN - g_capture_hw[encoder_index].stream->NDTR) & (ENC_DMA_CAPTURE_BUFFER_LEN - 1U));
    if (g_enc_hw[encoder_index].counter_bits < 32U)
    {
        g_enc_upper[encoder_index] = 0;

        /* Descarta cualquier evento de actualizacion pendiente antes de tomar la nueva muestra base. */
        g_enc_hw[encoder_index].tim->SR = 0U;
        g_enc_state[encoder_index].previous_sample = (uint32_t)g_enc_hw[encoder_index].tim->CNT;
    }
    else
    {
        g_enc_state[encoder_index].previous_sample = g_enc_hw[encoder_index].tim->CNT;
    }
    g_enc_state[encoder_index].last_capture = 0U;
    g_enc_index_latched[encoder_index] = 0U;
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

void enc_dma_fast_update(void)
{
    /* Actualiza el estado interno del encoder con la cadencia del lazo rapido local. */
    for (uint8_t index = 0; index < ENC_DMA_CHANNELS; ++index)
    {
        const enc_dma_hw_cfg_t *cfg = &g_enc_hw[index];
        enc_dma_state_t *state = &g_enc_state[index];

        if (state->enabled == 0U)
        {
            /* Mantiene la referencia base siguiendo al temporizador incluso con el canal logico deshabilitado. */
            state->status = 0U;
            state->previous_sample = cfg->tim->CNT;
            state->velocity_fast = 0;
            state->fast_delta_accumulator = 0;
            state->fast_history_head = 0U;
            state->fast_history_count = 0U;
            state->fusion_region = ENC_DMA_FUSION_REGION_SLOW;
            state->velocity_slow = 0;
            state->velocity_fused = 0;
            g_enc_index_latched[index] = 0U;
            continue;
        }

        state->status = ENC_STATUS_ENABLED;

        uint32_t current_sample;
        if (cfg->counter_bits >= 32U)
        {
            /* TIM2 y TIM5 son contadores reales de 32 bits implementados por el propio periferico. */
            current_sample = cfg->tim->CNT;
        }
        else
        {
            /* TIM4 y TIM8 necesitan extension asistida por software hasta 32 bits. */
            current_sample = enc_dma_read_extended_counter(cfg, index);
        }

        /* Desde aqui el delta siempre se interpreta en un dominio de 32 bits. */
        int32_t delta = enc_dma_delta(current_sample, state->previous_sample, 32U);
        int32_t old_delta = 0;
        uint8_t fast_window_ticks;
        int32_t fast_window_sum;
        uint32_t fast_window_period_ns;
        uint32_t fusion_low_cps;
        uint32_t fusion_high_cps;
        uint32_t fusion_hyst_cps;
        uint32_t fusion_magnitude_cps;

        state->position += delta;
        state->previous_sample = current_sample;

        if (state->fast_history_count >= ENC_DMA_FAST_WINDOW_TICKS)
        {
            old_delta = state->fast_delta_history[state->fast_history_head];
        }
        else
        {
            state->fast_history_count++;
        }

        state->fast_delta_accumulator += delta - old_delta;
        state->fast_delta_history[state->fast_history_head] = delta;
        state->fast_history_head = (uint8_t)((state->fast_history_head + 1U) % ENC_DMA_FAST_WINDOW_TICKS);
        fast_window_ticks = enc_dma_select_fast_window_ticks(state->velocity_fused);
        fast_window_sum = enc_dma_sum_recent_fast_deltas(state, fast_window_ticks);
        if (state->fast_history_count < fast_window_ticks)
        {
            fast_window_ticks = state->fast_history_count;
        }
        fast_window_period_ns = g_enc_update_period_ns * (uint32_t)fast_window_ticks;

        if ((fast_window_period_ns != 0U) && (fast_window_ticks != 0U))
        {
            /* Estimacion rapida de velocidad con promedio movil para bajar la cuantizacion sin perder tasa de actualizacion. */
            state->velocity_fast = (int32_t)(
                ((int64_t)fast_window_sum * 1000000000LL)
                / (int64_t)fast_window_period_ns);
        }
        else
        {
            state->velocity_fast = 0;
        }

        enc_dma_process_capture(index);

        if (state->capture_valid != 0U)
        {
            /* Ya existe al menos una medida de periodo valida para este eje. */
            state->status |= ENC_STATUS_CAPTURE_VALID;
        }

        /* Detecta si el productor DMA ha avanzado desde la ultima llamada. */
        if (state->dma_head == ((ENC_DMA_CAPTURE_BUFFER_LEN - g_capture_hw[index].stream->NDTR) & (ENC_DMA_CAPTURE_BUFFER_LEN - 1U)))
        {
            if (state->stale_cycles < 0xFFFFU)
            {
                state->stale_cycles++;
            }
        }

        if (state->stale_cycles < g_enc_stale_limit_cycles)
        {
            /* La velocidad lenta se sigue considerando lo bastante fresca como para usarse. */
            state->status |= ENC_STATUS_CAPTURE_FRESH;
        }

        /*
         * Politica de fusion de velocidad:
         * - movimiento alto: usa la estimacion por delta de posicion
         * - movimiento bajo con capturas frescas: usa la estimacion por periodo
         * - en caso contrario: reporta cero
         */
        if (state->stale_cycles < g_enc_stale_limit_cycles)
        {
            fusion_low_cps = g_enc_fast_threshold_cps;
            fusion_high_cps = fusion_low_cps * 2U;
            fusion_hyst_cps = fusion_low_cps / 8U;
            if (fusion_hyst_cps < ENC_DMA_FUSION_HYST_MIN_CPS)
            {
                fusion_hyst_cps = ENC_DMA_FUSION_HYST_MIN_CPS;
            }
            fusion_magnitude_cps = enc_dma_abs_i32(state->velocity_slow);
            if (enc_dma_abs_i32(state->velocity_fast) > fusion_magnitude_cps)
            {
                fusion_magnitude_cps = enc_dma_abs_i32(state->velocity_fast);
            }

            if (state->fusion_region == ENC_DMA_FUSION_REGION_FAST)
            {
                if (fusion_magnitude_cps <= (fusion_high_cps - fusion_hyst_cps))
                {
                    state->fusion_region = ENC_DMA_FUSION_REGION_BLEND;
                }
            }
            else if (state->fusion_region == ENC_DMA_FUSION_REGION_SLOW)
            {
                if (fusion_magnitude_cps >= (fusion_low_cps + fusion_hyst_cps))
                {
                    state->fusion_region = ENC_DMA_FUSION_REGION_BLEND;
                }
            }
            else
            {
                uint32_t slow_exit_cps = (fusion_low_cps > fusion_hyst_cps) ? (fusion_low_cps - fusion_hyst_cps) : 0U;
                if (fusion_magnitude_cps <= slow_exit_cps)
                {
                    state->fusion_region = ENC_DMA_FUSION_REGION_SLOW;
                }
                else if (fusion_magnitude_cps >= (fusion_high_cps + fusion_hyst_cps))
                {
                    state->fusion_region = ENC_DMA_FUSION_REGION_FAST;
                }
            }

            if (state->fusion_region == ENC_DMA_FUSION_REGION_FAST)
            {
                state->velocity_fused = state->velocity_fast;
                state->status |= ENC_STATUS_VELOCITY_SOURCE_FAST;
            }
            else if (state->fusion_region == ENC_DMA_FUSION_REGION_SLOW)
            {
                state->velocity_fused = state->velocity_slow;
                state->status |= ENC_STATUS_VELOCITY_SOURCE_SLOW;
            }
            else
            {
                uint32_t fast_weight_q15;

                if (fusion_magnitude_cps <= fusion_low_cps)
                {
                    fast_weight_q15 = 0U;
                }
                else if (fusion_magnitude_cps >= fusion_high_cps)
                {
                    fast_weight_q15 = 32768U;
                }
                else
                {
                    fast_weight_q15 = (uint32_t)((((uint64_t)(fusion_magnitude_cps - fusion_low_cps)) * 32768ULL)
                        / (uint64_t)(fusion_high_cps - fusion_low_cps));
                }

                state->velocity_fused = enc_dma_blend_velocity(state->velocity_slow, state->velocity_fast, fast_weight_q15);
                state->status |= ENC_STATUS_VELOCITY_SOURCE_SLOW | ENC_STATUS_VELOCITY_SOURCE_FAST;
            }
        }
        else
        {
            state->fusion_region = ENC_DMA_FUSION_REGION_FAST;
            state->velocity_slow = 0;
            state->velocity_fused = state->velocity_fast;
            state->status |= ENC_STATUS_VELOCITY_SOURCE_FAST;
        }

        enc_dma_index_update_live_status(index, state);
    }
}

void enc_dma_latch(void)
{
    /* Consume los eventos monoestables que deben durar exactamente un ciclo servo. */
    for (uint8_t index = 0; index < ENC_DMA_CHANNELS; ++index)
    {
        enc_dma_state_t *state = &g_enc_state[index];
        const enc_index_irq_cfg_t *irq_cfg = &g_index_irq_hw[index];

        state->status &= (uint8_t)~(ENC_STATUS_INDEX_LEVEL | ENC_STATUS_INDEX_LATCHED);

        if (LL_GPIO_IsInputPinSet(irq_cfg->port, irq_cfg->pin))
        {
            state->status |= ENC_STATUS_INDEX_LEVEL;
        }

        if (g_enc_index_latched[index] != 0U)
        {
            state->status |= ENC_STATUS_INDEX_LATCHED;
            g_enc_index_latched[index] = 0U;
        }
    }
}

void EXTI9_5_IRQHandler(void)
{
    /* PB9 / index del eje 0. */
    if (LL_EXTI_IsActiveFlag_0_31(LL_EXTI_LINE_9))
    {
        LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_9);
        enc_dma_handle_index_irq(0U);
    }
}

void EXTI15_10_IRQHandler(void)
{
    /* PB10 / index del eje 1. */
    if (LL_EXTI_IsActiveFlag_0_31(LL_EXTI_LINE_10))
    {
        LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_10);
        enc_dma_handle_index_irq(1U);
    }

    /* PB11 / index del eje 2. */
    if (LL_EXTI_IsActiveFlag_0_31(LL_EXTI_LINE_11))
    {
        LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_11);
        enc_dma_handle_index_irq(2U);
    }

    /* PB12 / index del eje 3. */
    if (LL_EXTI_IsActiveFlag_0_31(LL_EXTI_LINE_12))
    {
        LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_12);
        enc_dma_handle_index_irq(3U);
    }
}

void TIM4_IRQHandler(void)
{
    /* Extension por overflow/underflow del eje 2. */
    enc_dma_handle_position_overflow_irq(TIM4, 2U);
}

void TIM8_UP_TIM13_IRQHandler(void)
{
    /* Extension por overflow/underflow del eje 3. */
    enc_dma_handle_position_overflow_irq(TIM8, 3U);
}

void enc_dma_set_update_period_ns(uint32_t period_ns)
{
    if (period_ns != 0U)
    {
        g_enc_update_period_ns = period_ns;
        enc_dma_update_runtime_scalars();
    }
}

void enc_dma_set_fast_threshold_cps(uint32_t threshold_cps)
{
    g_enc_fast_threshold_cps = threshold_cps;
    enc_dma_update_runtime_scalars();
}

int32_t enc_dma_get_position(uint8_t encoder_index)
{
    if (encoder_index >= ENC_DMA_CHANNELS)
    {
        return 0;
    }

    return g_enc_state[encoder_index].position;
}

int32_t enc_dma_get_position_live(uint8_t encoder_index)
{
    if (encoder_index >= ENC_DMA_CHANNELS)
    {
        return 0;
    }

    const enc_dma_hw_cfg_t *cfg = &g_enc_hw[encoder_index];
    const enc_dma_state_t *state = &g_enc_state[encoder_index];
    uint32_t current_sample;

    if (cfg->counter_bits >= 32U)
    {
        current_sample = cfg->tim->CNT;
    }
    else
    {
        current_sample = enc_dma_read_extended_counter(cfg, encoder_index);
    }

    return state->position + enc_dma_delta(current_sample, state->previous_sample, 32U);
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
