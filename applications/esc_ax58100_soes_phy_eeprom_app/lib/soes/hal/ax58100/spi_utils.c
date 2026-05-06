/* SPI HAL for AX58100 using STM32 LL drivers (SPI1, PA5/PA6/PA7)
 * CS: PA4 (active low)
 * RST: PC2 (handled in rst.c)
 */

#include <stdint.h>
#include "spi_utils.h"
#include "stm32f4xx.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_spi.h"

/* Setup SPI1 pins and SPI peripheral */
void spi_setup(void)
{
    /* Enable GPIOA clock for SPI1 pins (PA5=SCK, PA6=MISO, PA7=MOSI) and CS (PA4) */
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);

    /* Configure PA5 (SCK), PA6 (MISO), PA7 (MOSI) as AF5 */
    LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_5, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetAFPin_0_7(GPIOA, LL_GPIO_PIN_5, LL_GPIO_AF_5);
    LL_GPIO_SetPinSpeed(GPIOA, LL_GPIO_PIN_5, LL_GPIO_SPEED_FREQ_VERY_HIGH);
    LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_5, LL_GPIO_PULL_NO);

    LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_6, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetAFPin_0_7(GPIOA, LL_GPIO_PIN_6, LL_GPIO_AF_5);
    LL_GPIO_SetPinSpeed(GPIOA, LL_GPIO_PIN_6, LL_GPIO_SPEED_FREQ_VERY_HIGH);
    LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_6, LL_GPIO_PULL_NO);

    LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_7, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetAFPin_0_7(GPIOA, LL_GPIO_PIN_7, LL_GPIO_AF_5);
    LL_GPIO_SetPinSpeed(GPIOA, LL_GPIO_PIN_7, LL_GPIO_SPEED_FREQ_VERY_HIGH);
    LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_7, LL_GPIO_PULL_NO);

    /* Configure CS pin PA4 as output, default HIGH (unselected) */
    LL_GPIO_SetPinMode(ESC_CS_GPIO_PORT, ESC_CS_PIN, LL_GPIO_MODE_OUTPUT);
    LL_GPIO_SetPinOutputType(ESC_CS_GPIO_PORT, ESC_CS_PIN, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinPull(ESC_CS_GPIO_PORT, ESC_CS_PIN, LL_GPIO_PULL_NO);
    LL_GPIO_SetPinSpeed(ESC_CS_GPIO_PORT, ESC_CS_PIN, LL_GPIO_SPEED_FREQ_LOW);
    spi_unselect(0);

    /* Enable SPI1 clock (APB2) */
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SPI1);

    /* Configure SPI1 as master, mode 3, 8-bit, MSB first, prescaler /2.
     * APB2 is 84 MHz, so SCK is 42 MHz and stays below the AX58100 limit.
     * Use unitary LL functions (no USE_FULL_LL_DRIVER dependency)
     */
    LL_SPI_Disable(ESC_SPI_INSTANCE);
    LL_SPI_SetMode(ESC_SPI_INSTANCE, LL_SPI_MODE_MASTER);
    LL_SPI_SetTransferDirection(ESC_SPI_INSTANCE, LL_SPI_FULL_DUPLEX);
    LL_SPI_SetDataWidth(ESC_SPI_INSTANCE, LL_SPI_DATAWIDTH_8BIT);
    LL_SPI_SetClockPolarity(ESC_SPI_INSTANCE, LL_SPI_POLARITY_HIGH); /* CPOL = 1 */
    LL_SPI_SetClockPhase(ESC_SPI_INSTANCE, LL_SPI_PHASE_2EDGE);     /* CPHA = 1 => mode 3 */
    LL_SPI_SetNSSMode(ESC_SPI_INSTANCE, LL_SPI_NSS_SOFT);
    LL_SPI_SetBaudRatePrescaler(ESC_SPI_INSTANCE, LL_SPI_BAUDRATEPRESCALER_DIV2);
    LL_SPI_SetTransferBitOrder(ESC_SPI_INSTANCE, LL_SPI_MSB_FIRST);
    LL_SPI_DisableCRC(ESC_SPI_INSTANCE);

    LL_SPI_Enable(ESC_SPI_INSTANCE);
}

/* Chip-select (active low) */
void spi_select (int8_t board)
{
#if SCS_ACTIVE_POLARITY == SCS_LOW
    LL_GPIO_ResetOutputPin(ESC_CS_GPIO_PORT, ESC_CS_PIN);
#else
    LL_GPIO_SetOutputPin(ESC_CS_GPIO_PORT, ESC_CS_PIN);
#endif
}

void spi_unselect (int8_t board)
{
#if SCS_ACTIVE_POLARITY == SCS_LOW
    LL_GPIO_SetOutputPin(ESC_CS_GPIO_PORT, ESC_CS_PIN);
#else
    LL_GPIO_ResetOutputPin(ESC_CS_GPIO_PORT, ESC_CS_PIN);
#endif
}

static inline uint8_t spi_transfer_byte(uint8_t byte)
{
    /* Wait until TXE set */
    while (!LL_SPI_IsActiveFlag_TXE(ESC_SPI_INSTANCE)) {}
    LL_SPI_TransmitData8(ESC_SPI_INSTANCE, byte);
    /* Wait until RXNE set */
    while (!LL_SPI_IsActiveFlag_RXNE(ESC_SPI_INSTANCE)) {}
    return (uint8_t)LL_SPI_ReceiveData8(ESC_SPI_INSTANCE);
}

void spi_write (int8_t board, uint8_t *data, uint8_t size)
{
    for (int i = 0; i < size; ++i)
    {
        (void)spi_transfer_byte(data[i]);
    }
}

void spi_read (int8_t board, uint8_t *result, uint8_t size)
{
    for (int i = 0; i < size; ++i)
    {
        result[i] = spi_transfer_byte(DUMMY_BYTE);
    }
}

void spi_bidirectionally_transfer (int8_t board, uint8_t *result, uint8_t *data, uint8_t size)
{
    for (int i = 0; i < size; ++i)
    {
        result[i] = spi_transfer_byte(data[i]);
    }
}
