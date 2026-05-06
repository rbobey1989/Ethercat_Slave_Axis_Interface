/* io.c - simple GPIO I/O used by PDO callbacks */
#include "io.h"
#include "objects.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_bus.h"

/*
 * Temporary general-purpose GPIO plan after freezing the critical peripherals.
 *
 * Outputs  0..11 : PD0..PD11
 * Outputs 12..15 : PE0..PE3
 * Inputs   0..3  : PE4..PE7
 * Inputs   4..8  : PA8..PA12
 * Inputs   9..11 : PB0..PB2
 * Inputs  12..14 : PB6..PB8
 * Inputs  15     : PC4
 *
 * The regenerated EtherCAT object dictionary now exposes full 16-bit Inputs and
 * Outputs variables, so the callbacks map the whole temporary IO pool.
 */

typedef struct
{
    GPIO_TypeDef *port;
    uint32_t pin;
} io_pin_t;

static const io_pin_t io_out_pins[IO_TEMP_OUTPUT_CHANNELS] = {
    {GPIOD, LL_GPIO_PIN_0},
    {GPIOD, LL_GPIO_PIN_1},
    {GPIOD, LL_GPIO_PIN_2},
    {GPIOD, LL_GPIO_PIN_3},
    {GPIOD, LL_GPIO_PIN_4},
    {GPIOD, LL_GPIO_PIN_5},
    {GPIOD, LL_GPIO_PIN_6},
    {GPIOD, LL_GPIO_PIN_7},
    {GPIOD, LL_GPIO_PIN_8},
    {GPIOD, LL_GPIO_PIN_9},
    {GPIOD, LL_GPIO_PIN_10},
    {GPIOD, LL_GPIO_PIN_11},
    {GPIOE, LL_GPIO_PIN_0},
    {GPIOE, LL_GPIO_PIN_1},
    {GPIOE, LL_GPIO_PIN_2},
    {GPIOE, LL_GPIO_PIN_3},
};

static const io_pin_t io_in_pins[IO_TEMP_INPUT_CHANNELS] = {
    {GPIOE, LL_GPIO_PIN_4},
    {GPIOE, LL_GPIO_PIN_5},
    {GPIOE, LL_GPIO_PIN_6},
    {GPIOE, LL_GPIO_PIN_7},
    {GPIOA, LL_GPIO_PIN_8},
    {GPIOA, LL_GPIO_PIN_9},
    {GPIOA, LL_GPIO_PIN_10},
    {GPIOA, LL_GPIO_PIN_11},
    {GPIOA, LL_GPIO_PIN_12},
    {GPIOB, LL_GPIO_PIN_0},
    {GPIOB, LL_GPIO_PIN_1},
    {GPIOB, LL_GPIO_PIN_2},
    {GPIOB, LL_GPIO_PIN_6},
    {GPIOB, LL_GPIO_PIN_7},
    {GPIOB, LL_GPIO_PIN_8},
    {GPIOC, LL_GPIO_PIN_4},
};

void io_init(void)
{
    /* Enable the GPIO banks used by the temporary IO pool. */
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOB);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOC);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOD);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOE);

    for (uint8_t i = 0; i < IO_TEMP_OUTPUT_CHANNELS; ++i)
    {
        LL_GPIO_SetPinMode(io_out_pins[i].port, io_out_pins[i].pin, LL_GPIO_MODE_OUTPUT);
        LL_GPIO_SetPinOutputType(io_out_pins[i].port, io_out_pins[i].pin, LL_GPIO_OUTPUT_PUSHPULL);
        LL_GPIO_SetPinPull(io_out_pins[i].port, io_out_pins[i].pin, LL_GPIO_PULL_NO);
        LL_GPIO_SetPinSpeed(io_out_pins[i].port, io_out_pins[i].pin, LL_GPIO_SPEED_FREQ_LOW);
        LL_GPIO_ResetOutputPin(io_out_pins[i].port, io_out_pins[i].pin);
    }

    for (uint8_t i = 0; i < IO_TEMP_INPUT_CHANNELS; ++i)
    {
        LL_GPIO_SetPinMode(io_in_pins[i].port, io_in_pins[i].pin, LL_GPIO_MODE_INPUT);
        LL_GPIO_SetPinPull(io_in_pins[i].port, io_in_pins[i].pin, LL_GPIO_PULL_DOWN);
    }
}

/* Called by stack to apply outputs from PDO to hardware */
void cb_set_outputs()
{
    uint16_t out = Obj.Outputs;
    for (uint8_t i = 0; i < IO_TEMP_OUTPUT_CHANNELS; ++i)
    {
        if (out & (1U << i))
            LL_GPIO_SetOutputPin(io_out_pins[i].port, io_out_pins[i].pin);
        else
            LL_GPIO_ResetOutputPin(io_out_pins[i].port, io_out_pins[i].pin);
    }
}

/* Called by stack to sample inputs and update PDO source variables */
void cb_get_inputs()
{
    uint16_t in = 0;
    for (uint8_t i = 0; i < IO_TEMP_INPUT_CHANNELS; ++i)
    {
        if (LL_GPIO_IsInputPinSet(io_in_pins[i].port, io_in_pins[i].pin))
        {
            in |= (1U << i);
        }
    }
    Obj.Inputs = in;
}
