#include "boot_eeprom_emu.h"

#include <stddef.h>
#include <stdint.h>

#include "eep_backend.h"

#include "../lib/soes/hal/ax58100/rst.h"

#include "stm32f407xx.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_i2c.h"

/*
 * AX58100 recommendation for the current firmware:
 * - Strap LED_RUN / EEP_SIZE = 1 during reset.
 * - That selects the 32 Kbit .. 4 Mbit EEPROM family.
 * - The STM32 then emulates a single-address 24C32-like device on 0x50 with
 *   two address bytes, which matches the scaffold below.
 *
 * If hardware instead straps EEP_SIZE = 0, the ESC expects 24C16-like access
 * using 0x50..0x57 plus one address byte, and this implementation must be
 * replaced with a multi-address slave model.
 */

#define BOOT_EEPROM_I2C_INSTANCE          I2C2
#define BOOT_EEPROM_I2C_CLOCK             LL_APB1_GRP1_PERIPH_I2C2
#define BOOT_EEPROM_GPIO_PORT             GPIOB
#define BOOT_EEPROM_GPIO_CLOCK            LL_AHB1_GRP1_PERIPH_GPIOB
#define BOOT_EEPROM_SCL_PIN               LL_GPIO_PIN_10
#define BOOT_EEPROM_SDA_PIN               LL_GPIO_PIN_11
#define BOOT_EEPROM_GPIO_AF               LL_GPIO_AF_4
#define BOOT_EEPROM_PDI_EMU_PORT          GPIOB
#define BOOT_EEPROM_PDI_EMU_PIN           LL_GPIO_PIN_9
#define BOOT_EEPROM_EEP_DONE_PORT         GPIOC
#define BOOT_EEPROM_EEP_DONE_CLOCK        LL_AHB1_GRP1_PERIPH_GPIOC
#define BOOT_EEPROM_EEP_DONE_PIN          LL_GPIO_PIN_5
#define BOOT_EEPROM_PDI_EMU_ACTIVE_LEVEL  1U
#define BOOT_EEPROM_EEP_DONE_ACTIVE_LEVEL 1U
#define BOOT_EEPROM_SLAVE_ADDRESS         0x50U
#define BOOT_EEPROM_ADDRESS_BYTES         2U
#define BOOT_EEPROM_FILL_BYTE             0xFFU
#define BOOT_EEPROM_PCLK1_MHZ             42U
#define BOOT_EEPROM_STANDARD_MODE_CCR     210U
#define BOOT_EEPROM_STANDARD_MODE_TRISE   43U
#define BOOT_EEPROM_STARTUP_WAIT_US       70000UL

static const uint8_t *g_boot_eeprom_image;
static uint32_t g_boot_eeprom_size;
static volatile uint32_t g_boot_eeprom_offset;
static volatile uint16_t g_boot_eeprom_pending_offset;
static volatile uint8_t g_boot_eeprom_addr_bytes;

static void boot_eeprom_delay_us(uint32_t delay_us)
{
    for (volatile uint32_t tick = 0; tick < (delay_us * 168U); ++tick)
    {
    }
}

static void boot_eeprom_gpio_init(void)
{
    LL_AHB1_GRP1_EnableClock(BOOT_EEPROM_GPIO_CLOCK);
    LL_AHB1_GRP1_EnableClock(BOOT_EEPROM_EEP_DONE_CLOCK);

    LL_GPIO_SetPinMode(BOOT_EEPROM_GPIO_PORT, BOOT_EEPROM_SCL_PIN, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetPinOutputType(BOOT_EEPROM_GPIO_PORT, BOOT_EEPROM_SCL_PIN, LL_GPIO_OUTPUT_OPENDRAIN);
    LL_GPIO_SetPinPull(BOOT_EEPROM_GPIO_PORT, BOOT_EEPROM_SCL_PIN, LL_GPIO_PULL_UP);
    LL_GPIO_SetPinSpeed(BOOT_EEPROM_GPIO_PORT, BOOT_EEPROM_SCL_PIN, LL_GPIO_SPEED_FREQ_VERY_HIGH);
    LL_GPIO_SetAFPin_8_15(BOOT_EEPROM_GPIO_PORT, BOOT_EEPROM_SCL_PIN, BOOT_EEPROM_GPIO_AF);

    LL_GPIO_SetPinMode(BOOT_EEPROM_GPIO_PORT, BOOT_EEPROM_SDA_PIN, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetPinOutputType(BOOT_EEPROM_GPIO_PORT, BOOT_EEPROM_SDA_PIN, LL_GPIO_OUTPUT_OPENDRAIN);
    LL_GPIO_SetPinPull(BOOT_EEPROM_GPIO_PORT, BOOT_EEPROM_SDA_PIN, LL_GPIO_PULL_UP);
    LL_GPIO_SetPinSpeed(BOOT_EEPROM_GPIO_PORT, BOOT_EEPROM_SDA_PIN, LL_GPIO_SPEED_FREQ_VERY_HIGH);
    LL_GPIO_SetAFPin_8_15(BOOT_EEPROM_GPIO_PORT, BOOT_EEPROM_SDA_PIN, BOOT_EEPROM_GPIO_AF);

    LL_GPIO_SetPinMode(BOOT_EEPROM_PDI_EMU_PORT, BOOT_EEPROM_PDI_EMU_PIN, LL_GPIO_MODE_OUTPUT);
    LL_GPIO_SetPinOutputType(BOOT_EEPROM_PDI_EMU_PORT, BOOT_EEPROM_PDI_EMU_PIN, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinPull(BOOT_EEPROM_PDI_EMU_PORT, BOOT_EEPROM_PDI_EMU_PIN, LL_GPIO_PULL_NO);
    LL_GPIO_SetPinSpeed(BOOT_EEPROM_PDI_EMU_PORT, BOOT_EEPROM_PDI_EMU_PIN, LL_GPIO_SPEED_FREQ_LOW);

    LL_GPIO_SetPinMode(BOOT_EEPROM_EEP_DONE_PORT, BOOT_EEPROM_EEP_DONE_PIN, LL_GPIO_MODE_INPUT);
    LL_GPIO_SetPinPull(BOOT_EEPROM_EEP_DONE_PORT, BOOT_EEPROM_EEP_DONE_PIN, LL_GPIO_PULL_NO);
}

static void boot_eeprom_set_pdi_emu(uint8_t enabled)
{
    if (enabled != 0U)
    {
        LL_GPIO_SetOutputPin(BOOT_EEPROM_PDI_EMU_PORT, BOOT_EEPROM_PDI_EMU_PIN);
    }
    else
    {
        LL_GPIO_ResetOutputPin(BOOT_EEPROM_PDI_EMU_PORT, BOOT_EEPROM_PDI_EMU_PIN);
    }
}

static uint8_t boot_eeprom_is_eep_done(void)
{
    uint8_t level = (uint8_t)LL_GPIO_IsInputPinSet(BOOT_EEPROM_EEP_DONE_PORT, BOOT_EEPROM_EEP_DONE_PIN);
    return (uint8_t)(level == BOOT_EEPROM_EEP_DONE_ACTIVE_LEVEL);
}

static int boot_eeprom_wait_eep_done(uint32_t timeout_us)
{
    while (timeout_us-- > 0U)
    {
        if (boot_eeprom_is_eep_done() != 0U)
        {
            return 0;
        }

        boot_eeprom_delay_us(1U);
    }

    return -1;
}

static void boot_eeprom_i2c_init(void)
{
    LL_APB1_GRP1_EnableClock(BOOT_EEPROM_I2C_CLOCK);
    LL_APB1_GRP1_ForceReset(BOOT_EEPROM_I2C_CLOCK);
    LL_APB1_GRP1_ReleaseReset(BOOT_EEPROM_I2C_CLOCK);

    LL_I2C_Disable(BOOT_EEPROM_I2C_INSTANCE);

    BOOT_EEPROM_I2C_INSTANCE->CR1 = 0U;
    BOOT_EEPROM_I2C_INSTANCE->CR2 = BOOT_EEPROM_PCLK1_MHZ;
    BOOT_EEPROM_I2C_INSTANCE->OAR1 = (uint32_t)((BOOT_EEPROM_SLAVE_ADDRESS << 1U) | 0x4000U);
    BOOT_EEPROM_I2C_INSTANCE->OAR2 = 0U;
    BOOT_EEPROM_I2C_INSTANCE->CCR = BOOT_EEPROM_STANDARD_MODE_CCR;
    BOOT_EEPROM_I2C_INSTANCE->TRISE = BOOT_EEPROM_STANDARD_MODE_TRISE;

    LL_I2C_AcknowledgeNextData(BOOT_EEPROM_I2C_INSTANCE, LL_I2C_ACK);
    LL_I2C_EnableIT_EVT(BOOT_EEPROM_I2C_INSTANCE);
    LL_I2C_EnableIT_BUF(BOOT_EEPROM_I2C_INSTANCE);
    LL_I2C_EnableIT_ERR(BOOT_EEPROM_I2C_INSTANCE);
    LL_I2C_Enable(BOOT_EEPROM_I2C_INSTANCE);

    NVIC_SetPriority(I2C2_EV_IRQn, 2);
    NVIC_EnableIRQ(I2C2_EV_IRQn);
    NVIC_SetPriority(I2C2_ER_IRQn, 2);
    NVIC_EnableIRQ(I2C2_ER_IRQn);
}

static uint8_t boot_eeprom_read_current_byte(void)
{
    if ((g_boot_eeprom_image == NULL) || (g_boot_eeprom_size == 0U))
    {
        return BOOT_EEPROM_FILL_BYTE;
    }

    if (g_boot_eeprom_offset >= g_boot_eeprom_size)
    {
        return BOOT_EEPROM_FILL_BYTE;
    }

    return g_boot_eeprom_image[g_boot_eeprom_offset++];
}

void boot_eeprom_emu_init(void)
{
    rst_setup();
    rst_low();

    eep_backend_init();
    g_boot_eeprom_image = eep_backend_get_image(&g_boot_eeprom_size);
    g_boot_eeprom_offset = 0U;
    g_boot_eeprom_pending_offset = 0U;
    g_boot_eeprom_addr_bytes = 0U;

    boot_eeprom_gpio_init();
    boot_eeprom_set_pdi_emu(BOOT_EEPROM_PDI_EMU_ACTIVE_LEVEL);
    boot_eeprom_i2c_init();

    rst_high();
    (void)boot_eeprom_wait_eep_done(BOOT_EEPROM_STARTUP_WAIT_US);
}

void I2C2_EV_IRQHandler(void)
{
    uint32_t sr1 = BOOT_EEPROM_I2C_INSTANCE->SR1;

    if ((sr1 & I2C_SR1_ADDR) != 0U)
    {
        uint32_t sr2 = BOOT_EEPROM_I2C_INSTANCE->SR2;
        uint8_t slave_transmit = (uint8_t)((sr2 & I2C_SR2_TRA) != 0U);

        LL_I2C_ClearFlag_ADDR(BOOT_EEPROM_I2C_INSTANCE);

        if (slave_transmit == 0U)
        {
            g_boot_eeprom_addr_bytes = 0U;
            g_boot_eeprom_pending_offset = 0U;
        }
    }

    if ((BOOT_EEPROM_I2C_INSTANCE->SR1 & I2C_SR1_RXNE) != 0U)
    {
        uint8_t value = LL_I2C_ReceiveData8(BOOT_EEPROM_I2C_INSTANCE);

        if (g_boot_eeprom_addr_bytes == 0U)
        {
            g_boot_eeprom_pending_offset = (uint16_t)((uint16_t)value << 8U);
            g_boot_eeprom_addr_bytes = 1U;
        }
        else if (g_boot_eeprom_addr_bytes == 1U)
        {
            g_boot_eeprom_pending_offset |= value;
            g_boot_eeprom_offset = g_boot_eeprom_pending_offset;
            g_boot_eeprom_addr_bytes = BOOT_EEPROM_ADDRESS_BYTES;
        }
        else
        {
            g_boot_eeprom_offset++;
        }
    }

    if (((BOOT_EEPROM_I2C_INSTANCE->SR1 & I2C_SR1_TXE) != 0U)
        && ((BOOT_EEPROM_I2C_INSTANCE->SR2 & I2C_SR2_TRA) != 0U))
    {
        LL_I2C_TransmitData8(BOOT_EEPROM_I2C_INSTANCE, boot_eeprom_read_current_byte());
    }

    if ((BOOT_EEPROM_I2C_INSTANCE->SR1 & I2C_SR1_STOPF) != 0U)
    {
        LL_I2C_ClearFlag_STOP(BOOT_EEPROM_I2C_INSTANCE);
    }
}

void I2C2_ER_IRQHandler(void)
{
    if (LL_I2C_IsActiveFlag_AF(BOOT_EEPROM_I2C_INSTANCE) != 0U)
    {
        LL_I2C_ClearFlag_AF(BOOT_EEPROM_I2C_INSTANCE);
    }

    if (LL_I2C_IsActiveFlag_BERR(BOOT_EEPROM_I2C_INSTANCE) != 0U)
    {
        LL_I2C_ClearFlag_BERR(BOOT_EEPROM_I2C_INSTANCE);
    }

    if (LL_I2C_IsActiveFlag_ARLO(BOOT_EEPROM_I2C_INSTANCE) != 0U)
    {
        LL_I2C_ClearFlag_ARLO(BOOT_EEPROM_I2C_INSTANCE);
    }

    if (LL_I2C_IsActiveFlag_OVR(BOOT_EEPROM_I2C_INSTANCE) != 0U)
    {
        LL_I2C_ClearFlag_OVR(BOOT_EEPROM_I2C_INSTANCE);
    }
}