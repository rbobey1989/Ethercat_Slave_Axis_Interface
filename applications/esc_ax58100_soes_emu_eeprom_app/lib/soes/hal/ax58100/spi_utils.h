#ifndef SRC_APP_SPI_H_
#define SRC_APP_SPI_H_

#include <stdint.h>
#include "stm32f4xx.h"

/* Frozen ESC pinout
 * CS   : PA4
 * SCK  : PA5 (SPI1)
 * MISO : PA6 (SPI1)
 * MOSI : PA7 (SPI1)
 */
#define ESC_CS_GPIO_PORT    GPIOA
#define ESC_CS_PIN          LL_GPIO_PIN_4

/* Reset pin is defined in rst.h (PC2) */

/* CS polarity: active low */
#define SCS_LOW             0
#define SCS_HIGH            1
#define SCS_ACTIVE_POLARITY SCS_LOW

/* SPI peripheral used */
#define ESC_SPI_INSTANCE    SPI1

/* Dummy byte when reading */
#define DUMMY_BYTE 0xFF

#ifdef __cplusplus
extern "C" {
#endif

void spi_setup(void);
void spi_select (int8_t board);
void spi_unselect (int8_t board);
void spi_write (int8_t board, uint8_t *data, uint8_t size);
void spi_read (int8_t board, uint8_t *result, uint8_t size);
void spi_bidirectionally_transfer (int8_t board, uint8_t *result, uint8_t *data, uint8_t size);

#ifdef __cplusplus
}
#endif

#endif /* SRC_APP_SPI_H_ */
