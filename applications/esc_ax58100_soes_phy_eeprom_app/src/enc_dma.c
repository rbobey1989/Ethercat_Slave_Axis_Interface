#include "enc_dma.h"

#include "stm32f407xx.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_exti.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_system.h"

/*
 * Subsistema Encoder 2-axis architecture.
 *
 * Posición x4:
 *   Axis 0: PA0/PA1   -> TIM5_CH1/TIM5_CH2
 *   Axis 1: PA15/PB3  -> TIM2_CH1/TIM2_CH2
 *
 * Eventos de velocidad (duplicados A/B, solo flanco ascendente):
 *   Axis 0: PB6 / PB7 -> EXTI6 / EXTI7
 *   Axis 1: PB8 / PB9 -> EXTI8 / EXTI9
 *
 * Index:
 *   Axis 0: PB10 -> EXTI10
 *   Axis 1: PB11 -> EXTI11
 */

#define ENC_DMA_VELOCITY_EVENT_IRQ_COUNT    4U
#define ENC_DMA_EVENT_BUFFER_LEN            32U                             // Cola sobredimensionada: con 500 PPR y rising A+B hay hasta ~8.33 eventos por tick a 2000 rpm
#define ENC_DMA_EVENT_BUFFER_MASK           (ENC_DMA_EVENT_BUFFER_LEN - 1U)
#define ENC_DMA_EVENT_HISTORY_LEN           2U                              // Version hibrida: solo 2 eventos para reducir el retardo del estimador
#define ENC_DMA_EVENT_TIMER_HZ              168000000UL                     // Frecuencia del temporizador de eventos
#define ENC_DMA_VELOCITY_TIMEOUT_TICKS      (ENC_DMA_EVENT_TIMER_HZ / 4U)   // Timeout 0.25 s: reduce la latencia de cero sin volver al comportamiento agresivo previo
#define ENC_DMA_EVENT_POSITION_COUNTS       2U                              // Rising A+B consecutivos equivalen a 2 counts x4 entre eventos de velocidad

#if ((ENC_DMA_EVENT_BUFFER_LEN == 0U) || ((ENC_DMA_EVENT_BUFFER_LEN & ENC_DMA_EVENT_BUFFER_MASK) != 0U))
#error "ENC_DMA_EVENT_BUFFER_LEN must be a power of two"
#endif

#if (ENC_DMA_EVENT_HISTORY_LEN < 2U)
#error "ENC_DMA_EVENT_HISTORY_LEN must be at least 2"
#endif

typedef struct
{
    /* Temporizador en modo encoder para posicion x4. */
    TIM_TypeDef *tim;       // Representa el TIM usado para capturar posición x4 del encoder, TIM2 o TIM5 dependiendo del canal.
    GPIO_TypeDef *port_a;   // Puerto del pin A del encoder
    uint32_t pin_a;         // Pin del canal A del encoder
    uint32_t af_a;          // Función alternativa del pin A para el modo TIM encoder
    GPIO_TypeDef *port_b;   // Puerto del pin B del encoder
    uint32_t pin_b;         // Pin del canal B del encoder
    uint32_t af_b;          // Función alternativa del pin B para el modo TIM encoder
} enc_dma_hw_cfg_t;

typedef struct
{
    /* Configuracion comun para una linea EXTI. */
    GPIO_TypeDef *port;    // Puerto del pin de la linea EXTI
    uint32_t pin;          // Pin de la linea EXTI
    uint32_t exti_line;    // Linea EXTI asociada
    uint32_t exti_source;  // Fuente EXTI asociada
    uint8_t axis;          // Eje asociado
} enc_irq_cfg_t;

typedef struct
{
    /* Marca de tiempo DWT y posicion x4 observada en ese mismo flanco. */
    uint32_t timestamp;         // Valor del contador DWT->CYCCNT en el momento del evento de velocidad
    uint32_t position;          // Valor del contador x4 del encoder (TIM2->CNT o TIM5->CNT) en el momento del evento de velocidad
} enc_velocity_event_sample_t;

typedef struct
{
    /* Cola lock-free simple: IRQ escribe y lazo rapido consume. */
    volatile uint8_t write_head;                                    // Indice de escritura en la cola
    volatile uint8_t read_head;                                     // Indice de lectura en la cola
    enc_velocity_event_sample_t samples[ENC_DMA_EVENT_BUFFER_LEN];  // Buffer de muestras de eventos de velocidad       
} enc_velocity_event_queue_t;

typedef struct
{
    /* Estado logico visible para el resto del firmware. */
    uint8_t enabled;          // Indica si el encoder está habilitado
    uint8_t status;           // Estado del encoder

    /* Ultima muestra del contador x4 usada para integrar posicion. */
    uint32_t previous_sample;
    int32_t position;

    /* Velocidad vigente calculada con el estimador de eventos rising A+B. */
    int32_t velocity_event;

    /* Flags de validez/frescura de la estimacion. */
    uint8_t capture_valid;       // Indica si la captura de velocidad es válida
    uint32_t last_event_timestamp; // Marca de tiempo del ultimo evento usado para la estimacion vigente

    /* Ventana minima de historial para estimar dt y delta-pos entre eventos. */
    uint8_t event_history_head;   // Indice de la cabeza del historial de eventos
    uint8_t event_history_count;  // Numero de eventos en el historial
} enc_dma_state_t;

/* Configuración de hardware de los encoders. */
static const enc_dma_hw_cfg_t g_enc_hw[ENC_DMA_CHANNELS] = {
    {TIM5, GPIOA, LL_GPIO_PIN_0, LL_GPIO_AF_2, GPIOA, LL_GPIO_PIN_1, LL_GPIO_AF_2},     // Encoder 0: PA0/PA1 -> TIM5_CH1/TIM5_CH2  
    {TIM2, GPIOA, LL_GPIO_PIN_15, LL_GPIO_AF_1, GPIOB, LL_GPIO_PIN_3, LL_GPIO_AF_1},    // Encoder 1: PA15/PB3 -> TIM2_CH1/TIM2_CH2    
};

/* Configuración de hardware de las interrupciones de eventos de velocidad. */
static const enc_irq_cfg_t g_velocity_event_irq_hw[ENC_DMA_VELOCITY_EVENT_IRQ_COUNT] = {
    {GPIOB, LL_GPIO_PIN_6, LL_EXTI_LINE_6, LL_SYSCFG_EXTI_LINE6, 0U},   // Evento rising A del encoder 0
    {GPIOB, LL_GPIO_PIN_7, LL_EXTI_LINE_7, LL_SYSCFG_EXTI_LINE7, 0U},   // Evento rising B del encoder 0
    {GPIOB, LL_GPIO_PIN_8, LL_EXTI_LINE_8, LL_SYSCFG_EXTI_LINE8, 1U},   // Evento rising A del encoder 1
    {GPIOB, LL_GPIO_PIN_9, LL_EXTI_LINE_9, LL_SYSCFG_EXTI_LINE9, 1U},   // Evento rising B del encoder 1
};

/* Configuración de hardware de las interrupciones de index. */
static const enc_irq_cfg_t g_index_irq_hw[ENC_DMA_CHANNELS] = {
    {GPIOB, LL_GPIO_PIN_10, LL_EXTI_LINE_10, LL_SYSCFG_EXTI_LINE10, 0U},   // Evento de index del encoder 0
    {GPIOB, LL_GPIO_PIN_11, LL_EXTI_LINE_11, LL_SYSCFG_EXTI_LINE11, 1U},   // Evento de index del encoder 1
};

static enc_dma_state_t g_enc_state[ENC_DMA_CHANNELS];                                                       // Estado de cada encoder mantenido por el subsistema
static volatile uint8_t g_enc_index_latched[ENC_DMA_CHANNELS];                                              // Flags para indicar si se ha detectado un evento de index desde la última lectura del estado del encoder
static enc_velocity_event_queue_t g_velocity_event_queues[ENC_DMA_CHANNELS];                                // Colas lock-free para eventos de velocidad rising A+B de cada encoder
static enc_velocity_event_sample_t g_velocity_event_history[ENC_DMA_CHANNELS][ENC_DMA_EVENT_HISTORY_LEN];   // Historial circular de eventos de velocidad para cada encoder, usado para estimar dt y delta-pos entre eventos

/*
 * El contador hardware es unsigned, pero para integrar posicion interesa el delta
 * con wrap natural de 32 bits.
 */
static int32_t enc_dma_delta(uint32_t current, uint32_t previous)
{
    return (int32_t)(current - previous);
}

/* Valor absoluto en entero con signo de 32 bits. */
static int32_t enc_dma_abs_i32(int32_t value)
{
    return (value < 0) ? -value : value;
}

/* Reinicia una cola lock-free sin tocar su almacenamiento. */
static void enc_dma_event_queue_reset(enc_velocity_event_queue_t *queue)
{
    queue->read_head = 0U;
    queue->write_head = 0U;
}

/* Reinicia solo el estado derivado del estimador y de los latches de index. */
static void enc_dma_reset_axis_runtime_state(uint8_t axis)
{
    enc_dma_state_t *state = &g_enc_state[axis];

    state->velocity_event = 0;
    state->capture_valid = 0U;
    state->last_event_timestamp = 0U;
    state->event_history_head = 0U;
    state->event_history_count = 0U;
    g_enc_index_latched[axis] = 0U;
    enc_dma_event_queue_reset(&g_velocity_event_queues[axis]);
}

/* Cota superior del modulo de velocidad mientras no llega el siguiente evento. */
static uint32_t enc_dma_velocity_upper_bound_from_age(uint32_t age_ticks)
{
    if (age_ticks == 0U)
    {
        return 0U;
    }

    return (uint32_t)((((uint64_t)ENC_DMA_EVENT_POSITION_COUNTS * (uint64_t)ENC_DMA_EVENT_TIMER_HZ)
        + (uint64_t)age_ticks - 1ULL) / (uint64_t)age_ticks);
}

/* Configura un pin como entrada alternativa controlada por el temporizador. */
static void enc_dma_gpio_set_af(GPIO_TypeDef *port, uint32_t pin, uint32_t af)
{
    LL_GPIO_SetPinMode(port, pin, LL_GPIO_MODE_ALTERNATE);          // Configura el pin como función alternativa
    LL_GPIO_SetPinOutputType(port, pin, LL_GPIO_OUTPUT_PUSHPULL);   // Configura el pin como push-pull
    LL_GPIO_SetPinPull(port, pin, LL_GPIO_PULL_UP);                 // Habilita pull-up para asegurar niveles definidos cuando el encoder no está conectado
    LL_GPIO_SetPinSpeed(port, pin, LL_GPIO_SPEED_FREQ_VERY_HIGH);   // Configura la velocidad del pin

    /*
     * Configura la función alternativa del pin según el rango del pin. 
     * En STM32F4, los pines 0-7 usan un registro AFR[0] y los pines 8-15 usan AFR[1].
     */
    if (pin < LL_GPIO_PIN_8)
    {
        LL_GPIO_SetAFPin_0_7(port, pin, af);
    }
    else
    {
        LL_GPIO_SetAFPin_8_15(port, pin, af);
    }
}

/* Habilita y enruta los pines de cuadratura A/B hacia TIM2/TIM5. */
static void enc_dma_position_gpio_init(void)
{
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);    // Habilita el reloj del GPIOA para los pines de posición de ambos encoders
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOB);    // Habilita el reloj del GPIOB para el pin B del encoder 1

    /*
     * Configura los pines de cuadratura A/B para cada encoder.
     * Encoder 0: PA0/PA1 -> TIM5_CH1/TIM5_CH2
     * Encoder 1: PA15/PB3 -> TIM2_CH1/TIM2_CH2
     */
    for (uint8_t index = 0; index < ENC_DMA_CHANNELS; ++index)
    {
        enc_dma_gpio_set_af(g_enc_hw[index].port_a, g_enc_hw[index].pin_a, g_enc_hw[index].af_a);
        enc_dma_gpio_set_af(g_enc_hw[index].port_b, g_enc_hw[index].pin_b, g_enc_hw[index].af_b);
    }
}

/* Habilita entradas discretas para eventos rising A/B e index. */
static void enc_dma_event_gpio_init(void)
{
    for (uint8_t index = 0; index < ENC_DMA_VELOCITY_EVENT_IRQ_COUNT; ++index)
    {
        const enc_irq_cfg_t *cfg = &g_velocity_event_irq_hw[index];

        LL_GPIO_SetPinMode(cfg->port, cfg->pin, LL_GPIO_MODE_INPUT);
        LL_GPIO_SetPinPull(cfg->port, cfg->pin, LL_GPIO_PULL_NO);
    }

    for (uint8_t index = 0; index < ENC_DMA_CHANNELS; ++index)
    {
        const enc_irq_cfg_t *cfg = &g_index_irq_hw[index];

        LL_GPIO_SetPinMode(cfg->port, cfg->pin, LL_GPIO_MODE_INPUT);
        LL_GPIO_SetPinPull(cfg->port, cfg->pin, LL_GPIO_PULL_NO);
    }
}

/*
 * Deja el temporizador en encoder mode 3.
 * Desde ese momento el contador incrementa/decrementa segun la cuadratura x4.
 */
static void enc_dma_position_timer_init(const enc_dma_hw_cfg_t *cfg)
{
    cfg->tim->CR1 = 0U;                                     // Configura el temporizador en modo encoder, sin preescaler ni auto-reload (contador de 32 bits)
    cfg->tim->CR2 = 0U;                                     // Deshabilita modos especiales y salidas de comparación
    cfg->tim->DIER = 0U;                                    // Deshabilita interrupciones del temporizador, solo se usan las interrupciones EXTI de los eventos de velocidad e index
    cfg->tim->SR = 0U;                                      // Limpia cualquier bandera de estado previa
    cfg->tim->SMCR = TIM_SMCR_SMS_0 | TIM_SMCR_SMS_1;       // Configura el modo de esclavo en encoder mode 3 (cuadratura x4)
    cfg->tim->CCMR1 = TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0;  // Configura los canales 1 y 2 como entradas para capturar la señal de cuadratura
    cfg->tim->CCER = 0U;                                    // Configura la polaridad de captura para ambos canales en rising edge (incremento en flanco ascendente)
    cfg->tim->PSC = 0U;                                     // Sin preescaler para capturar toda la resolución del encoder
    cfg->tim->ARR = 0xFFFFFFFFUL;                           // Configura el auto-reload al valor máximo (contador de 32 bits)
    cfg->tim->CNT = 0U;                                     // Inicializa el contador en 0
    cfg->tim->EGR = TIM_EGR_UG;                             // Genera un evento de actualización para cargar los registros
    cfg->tim->SR = 0U;                                      // Limpia cualquier bandera de estado previa
    cfg->tim->CR1 = TIM_CR1_CEN;                            // Habilita el temporizador
}

/* Conecta todas las lineas EXTI necesarias y habilita sus IRQs. */
static void enc_dma_irq_init(void)
{
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SYSCFG);                   // Habilita el reloj del SYSCFG para configurar las fuentes EXTI

    for (uint8_t index = 0; index < ENC_DMA_VELOCITY_EVENT_IRQ_COUNT; ++index)
    {
        const enc_irq_cfg_t *cfg = &g_velocity_event_irq_hw[index];

        LL_SYSCFG_SetEXTISource(LL_SYSCFG_EXTI_PORTB, cfg->exti_source);   // Configura la fuente EXTI para el puerto B
        LL_EXTI_ClearFlag_0_31(cfg->exti_line);                            // Limpia cualquier bandera de interrupción previa
        LL_EXTI_EnableRisingTrig_0_31(cfg->exti_line);                     // Habilita el disparo en flanco ascendente
        LL_EXTI_DisableFallingTrig_0_31(cfg->exti_line);                   // Deshabilita el disparo en flanco descendente
        LL_EXTI_EnableIT_0_31(cfg->exti_line);                             // Habilita la interrupción EXTI
    }

    for (uint8_t index = 0; index < ENC_DMA_CHANNELS; ++index)
    {
        const enc_irq_cfg_t *cfg = &g_index_irq_hw[index];

        LL_SYSCFG_SetEXTISource(LL_SYSCFG_EXTI_PORTB, cfg->exti_source);    // Configura la fuente EXTI para el puerto B
        LL_EXTI_ClearFlag_0_31(cfg->exti_line);                             // Limpia cualquier bandera de interrupción previa
        LL_EXTI_EnableRisingTrig_0_31(cfg->exti_line);                      // Habilita el disparo en flanco ascendente
        LL_EXTI_DisableFallingTrig_0_31(cfg->exti_line);                    // Deshabilita el disparo en flanco descendente
        LL_EXTI_EnableIT_0_31(cfg->exti_line);                              // Habilita la interrupción EXTI
    }

    // Configura las prioridades y habilita las IRQs para las líneas EXTI usadas por los eventos de velocidad e index
    NVIC_SetPriority(EXTI9_5_IRQn, 5);
    NVIC_EnableIRQ(EXTI9_5_IRQn);
    NVIC_SetPriority(EXTI15_10_IRQn, 5);
    NVIC_EnableIRQ(EXTI15_10_IRQn);
}

/* 
 * Activa DWT->CYCCNT para cronometrar eventos con resolución de reloj de CPU.
 * A 168 MHz el rollover del contador de 32 bits es de ~25.5 s, suficiente para
 * medir los intervalos entre eventos de velocidad con wrap natural.
 */
static void enc_dma_timestamp_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/* La IRQ solo deja un latch; la publicacion real se hace en enc_dma_latch(). */
static void enc_dma_handle_index_irq(uint8_t axis)
{
    if (g_enc_state[axis].enabled != 0U)
    {
        g_enc_index_latched[axis] = 1U;
    }
}

/*
 * Registra un evento rising de A o B.
 * Se captura en el mismo instante la posicion x4 para luego estimar velocidad.
 */
static void enc_dma_handle_velocity_event_irq(uint8_t irq_index)
{
    const enc_irq_cfg_t *cfg = &g_velocity_event_irq_hw[irq_index];
    enc_velocity_event_queue_t *queue = &g_velocity_event_queues[cfg->axis];
    uint8_t write_head;
    uint8_t next_head;

    if (g_enc_state[cfg->axis].enabled == 0U)
    {
        return;
    }

    write_head = queue->write_head;                                                 // Lee el indice de escritura actual de la cola
    
    next_head = (uint8_t)((write_head + 1U) & ENC_DMA_EVENT_BUFFER_MASK);            // Calcula el siguiente indice de escritura, con wrap-around circular,
                                                                                    // y considerando que el tamaño de la cola es potencia de 2 para usar mascara de bits
                                                                                    // Si el siguiente indice de escritura es igual al indice de lectura, significa que la 
                                                                                    // cola está llena y no se puede registrar el nuevo evento sin sobrescribir uno no procesado. 
                                                                                    // En este caso, se descarta el evento.

    if (next_head == queue->read_head)
    {
        return;
    }

    queue->samples[write_head].timestamp = DWT->CYCCNT;                     // Captura el timestamp del evento usando el contador DWT->CYCCNT para alta resolución
    queue->samples[write_head].position = g_enc_hw[cfg->axis].tim->CNT;     // Captura la posición x4 del encoder en el momento del evento leyendo el contador del temporizador correspondiente (TIM2->CNT o TIM5->CNT)
    queue->write_head = next_head;                                          // Actualiza el indice de escritura de la cola para publicar el nuevo evento
}

/*
 * Consume la cola de eventos y recalcula la velocidad con dos muestras
 * consecutivas. Esto reduce el retardo del estimador manteniendo el timestamp
 * de alta resolución por DWT.
 */
static void enc_dma_process_event_velocity(uint8_t axis)
{
    enc_velocity_event_queue_t *queue = &g_velocity_event_queues[axis];     // Cola de eventos de velocidad para el eje dado
    enc_dma_state_t *state = &g_enc_state[axis];                            // Estado del encoder para el eje dado

    while (queue->read_head != queue->write_head)
    {
        enc_velocity_event_sample_t sample = queue->samples[queue->read_head];  // Lee el evento de velocidad en la posición de lectura actual de la cola
        uint8_t history_slot = state->event_history_head;                       // Obtiene el indice del slot de historial donde se almacenará la nueva muestra, 
                                                                                // que es el mismo que el indice de la cabeza del historial

        g_velocity_event_history[axis][history_slot] = sample;                  // Almacena la nueva muestra de evento de velocidad en el historial circular usando el indice de la cabeza del historial
        state->event_history_head = (uint8_t)((state->event_history_head + 1U) % ENC_DMA_EVENT_HISTORY_LEN);    // Avanza el indice de la cabeza del historial, con wrap-around circular, para apuntar al siguiente slot donde se almacenará la próxima muestra
        /*
         * Si el historial no está lleno, incrementa el contador de muestras. 
         * Esto se hace antes de intentar calcular la velocidad para asegurar que el contador refleje correctamente el número de muestras disponibles en el historial, 
         * lo cual es crucial para decidir cuándo se tienen suficientes muestras para realizar una estimación de velocidad válida.
         */
        if (state->event_history_count < ENC_DMA_EVENT_HISTORY_LEN)
        {
            state->event_history_count++;
        }

        /* Si el historial está lleno, estima velocidad con la muestra anterior y la actual. */
        if (state->event_history_count >= ENC_DMA_EVENT_HISTORY_LEN)
        {
            uint8_t newest_index = (uint8_t)((state->event_history_head + ENC_DMA_EVENT_HISTORY_LEN - 1U)
                % ENC_DMA_EVENT_HISTORY_LEN);
            uint8_t oldest_index = state->event_history_head;
            enc_velocity_event_sample_t newest = g_velocity_event_history[axis][newest_index];
            enc_velocity_event_sample_t oldest = g_velocity_event_history[axis][oldest_index];
            uint32_t delta_ticks = newest.timestamp - oldest.timestamp;

            if (delta_ticks != 0U)
            {
                int32_t delta_position = enc_dma_delta(newest.position, oldest.position);
                state->velocity_event = (int32_t)(((int64_t)delta_position * (int64_t)ENC_DMA_EVENT_TIMER_HZ)
                    / (int64_t)delta_ticks);
                state->capture_valid = 1U;
                state->last_event_timestamp = newest.timestamp;
            }
        }

        queue->read_head = (uint8_t)((queue->read_head + 1U) & ENC_DMA_EVENT_BUFFER_MASK);
    }
}

/* Refresca los bits de index visibles en el estado exportado. */
static void enc_dma_index_update_live_status(uint8_t axis, enc_dma_state_t *state)
{
    const enc_irq_cfg_t *cfg = &g_index_irq_hw[axis];

    if (LL_GPIO_IsInputPinSet(cfg->port, cfg->pin))
    {
        state->status |= ENC_STATUS_INDEX_LEVEL;
    }

    if (g_enc_index_latched[axis] != 0U)
    {
        state->status |= ENC_STATUS_INDEX_LATCHED;
    }
}

/* Inicializacion completa del bloque de posicion y del estimador por eventos. */
void enc_dma_init(void)
{
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM2);
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM5);

    enc_dma_position_gpio_init();
    enc_dma_event_gpio_init();
    enc_dma_irq_init();
    enc_dma_timestamp_init();

    for (uint8_t axis = 0; axis < ENC_DMA_CHANNELS; ++axis)
    {
        enc_dma_state_t *state = &g_enc_state[axis];

        state->enabled = 1U;
        state->status = ENC_STATUS_ENABLED;
        state->previous_sample = 0U;
        state->position = 0;
        enc_dma_reset_axis_runtime_state(axis);

        enc_dma_position_timer_init(&g_enc_hw[axis]);
        state->previous_sample = g_enc_hw[axis].tim->CNT;
    }
}

/*
 * Activa o desactiva un eje logico y limpia el estado derivado del estimador.
 * La posicion hardware sigue viva en el timer, pero el estado exportado se reinicia.
 */
void enc_dma_set_enabled(uint8_t encoder_index, uint8_t enabled)
{
    enc_dma_state_t *state;

    if (encoder_index >= ENC_DMA_CHANNELS)
    {
        return;
    }

    state = &g_enc_state[encoder_index];
    state->enabled = enabled ? 1U : 0U;
    state->status = enabled ? ENC_STATUS_ENABLED : 0U;
    enc_dma_reset_axis_runtime_state(encoder_index);
    state->previous_sample = g_enc_hw[encoder_index].tim->CNT;
}

uint8_t enc_dma_is_enabled(uint8_t encoder_index)
{
    if (encoder_index >= ENC_DMA_CHANNELS)
    {
        return 0U;
    }

    return g_enc_state[encoder_index].enabled;
}

/* Devuelve el byte de estado consolidado para PDO/SDO. */
uint8_t enc_dma_get_status(uint8_t encoder_index)
{
    if (encoder_index >= ENC_DMA_CHANNELS)
    {
        return 0U;
    }

    return g_enc_state[encoder_index].status;
}

/*
 * Tick del lazo rapido.
 * Integra posicion desde el timer x4, procesa eventos A/B pendientes y aplica
 * timeout absoluto interno y cota superior de
 * velocidad decreciente mientras no llega el siguiente evento.
 */
static void enc_dma_fast_update_impl(void)
{
    for (uint8_t axis = 0; axis < ENC_DMA_CHANNELS; ++axis)
    {
        const enc_dma_hw_cfg_t *cfg = &g_enc_hw[axis];
        enc_dma_state_t *state = &g_enc_state[axis];
        uint32_t current_sample;
        uint32_t age_ticks;
        uint32_t velocity_upper_bound;
        int32_t delta;

        if (state->enabled == 0U)
        {
            state->status = 0U;
            state->previous_sample = cfg->tim->CNT;
            state->velocity_event = 0;
            g_enc_index_latched[axis] = 0U;
            continue;
        }

        state->status = ENC_STATUS_ENABLED;
        /* Primero se integra siempre la posicion x4 independientemente de eventos. */
        current_sample = cfg->tim->CNT;
        delta = enc_dma_delta(current_sample, state->previous_sample);
        state->position += delta;
        state->previous_sample = current_sample;

        /* Luego se consume cualquier evento rising A/B que haya llegado desde EXTI. */
        enc_dma_process_event_velocity(axis);
        if (state->capture_valid != 0U)
        {
            state->status |= ENC_STATUS_CAPTURE_VALID;
        }

        age_ticks = DWT->CYCCNT - state->last_event_timestamp;
        if ((state->capture_valid != 0U) && (age_ticks < ENC_DMA_VELOCITY_TIMEOUT_TICKS))
        {
            if (age_ticks != 0U)
            {
                /*
                 * Entre pulsos: si aun no llegó el siguiente evento solo
                 * conocemos una cota superior. Como la fuente de velocidad es
                 * rising A+B, entre eventos consecutivos median 2 counts x4.
                 */
                velocity_upper_bound = enc_dma_velocity_upper_bound_from_age(age_ticks);
                if ((uint32_t)enc_dma_abs_i32(state->velocity_event) > velocity_upper_bound)
                {
                    if (state->velocity_event > 0)
                    {
                        state->velocity_event = (int32_t)velocity_upper_bound;
                    }
                    else if (state->velocity_event < 0)
                    {
                        state->velocity_event = -(int32_t)velocity_upper_bound;
                    }
                }
            }

            /* La velocidad actual sigue siendo utilizable. */
            state->status |= ENC_STATUS_CAPTURE_FRESH;
            state->status |= ENC_STATUS_VELOCITY_SOURCE_EVENT;
        }
        else
        {
            /* Una vez caducada, la velocidad se fuerza a cero para evitar mantener basura vieja. */
            state->velocity_event = 0;
        }

        enc_dma_index_update_live_status(axis, state);
    }
}

/*
 * Consume los latches monoestables de index y actualiza el estado visible una vez
 * por ciclo servo, no dentro de la IRQ.
 */
static void enc_dma_latch_impl(void)
{
    for (uint8_t axis = 0; axis < ENC_DMA_CHANNELS; ++axis)
    {
        enc_dma_state_t *state = &g_enc_state[axis];
        const enc_irq_cfg_t *cfg = &g_index_irq_hw[axis];

        state->status &= (uint8_t)~(ENC_STATUS_INDEX_LEVEL | ENC_STATUS_INDEX_LATCHED);
        if (LL_GPIO_IsInputPinSet(cfg->port, cfg->pin))
        {
            state->status |= ENC_STATUS_INDEX_LEVEL;
        }

        if (g_enc_index_latched[axis] != 0U)
        {
            state->status |= ENC_STATUS_INDEX_LATCHED;
            g_enc_index_latched[axis] = 0U;
        }
    }
}

/* IRQ compartida para los rising A/B de ambos ejes. */
void EXTI9_5_IRQHandler(void)
{
    if (LL_EXTI_IsActiveFlag_0_31(LL_EXTI_LINE_6))
    {
        LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_6);
        enc_dma_handle_velocity_event_irq(0U);
    }

    if (LL_EXTI_IsActiveFlag_0_31(LL_EXTI_LINE_7))
    {
        LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_7);
        enc_dma_handle_velocity_event_irq(1U);
    }

    if (LL_EXTI_IsActiveFlag_0_31(LL_EXTI_LINE_8))
    {
        LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_8);
        enc_dma_handle_velocity_event_irq(2U);
    }

    if (LL_EXTI_IsActiveFlag_0_31(LL_EXTI_LINE_9))
    {
        LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_9);
        enc_dma_handle_velocity_event_irq(3U);
    }
}

/* IRQ compartida para los pulsos de index de ambos ejes. */
void EXTI15_10_IRQHandler(void)
{
    if (LL_EXTI_IsActiveFlag_0_31(LL_EXTI_LINE_10))
    {
        LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_10);
        enc_dma_handle_index_irq(0U);
    }

    if (LL_EXTI_IsActiveFlag_0_31(LL_EXTI_LINE_11))
    {
        LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_11);
        enc_dma_handle_index_irq(1U);
    }
}

/*
 * Punto unico de entrada para el mantenimiento del subsistema.
 *
 * - ENC_DMA_UPDATE_FAST: integra posicion y procesa la cola de eventos A/B.
 * - ENC_DMA_UPDATE_SLOW: refresca latches de index visibles para el ciclo servo.
 */
void enc_dma_update(enc_dma_update_source_t source)
{
    if (source == ENC_DMA_UPDATE_FAST)
    {
        enc_dma_fast_update_impl();
        return;
    }

    enc_dma_latch_impl();
}

/* Posicion ya integrada y latcheada por software. */
int32_t enc_dma_get_position(uint8_t encoder_index)
{
    if (encoder_index >= ENC_DMA_CHANNELS)
    {
        return 0;
    }

    return g_enc_state[encoder_index].position;
}

/* Posicion instantanea combinando la base software y el contador hardware actual. */
int32_t enc_dma_get_position_live(uint8_t encoder_index)
{
    const enc_dma_state_t *state;
    uint32_t current_sample;

    if (encoder_index >= ENC_DMA_CHANNELS)
    {
        return 0;
    }

    state = &g_enc_state[encoder_index];
    current_sample = g_enc_hw[encoder_index].tim->CNT;
    return state->position + enc_dma_delta(current_sample, state->previous_sample);
}

/* Velocidad principal basada en rising A+B; esta es la que debe usar el control. */
int32_t enc_dma_get_velocity_rise_ab(uint8_t encoder_index)
{
    if (encoder_index >= ENC_DMA_CHANNELS)
    {
        return 0;
    }

    return g_enc_state[encoder_index].velocity_event;
}

/* Alias explicito del mismo estimador para telemetria o depuracion. */
int32_t enc_dma_get_velocity_event(uint8_t encoder_index)
{
    if (encoder_index >= ENC_DMA_CHANNELS)
    {
        return 0;
    }

    return g_enc_state[encoder_index].velocity_event;
}
