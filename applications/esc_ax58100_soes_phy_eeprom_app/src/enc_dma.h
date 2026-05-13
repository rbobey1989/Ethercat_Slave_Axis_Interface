#ifndef ENC_DMA_H
#define ENC_DMA_H

#include <stdint.h>

#define ENC_DMA_CHANNELS 2U

#define ENC_STATUS_ENABLED                  (1U << 0)
#define ENC_STATUS_CAPTURE_VALID            (1U << 1)
#define ENC_STATUS_CAPTURE_FRESH            (1U << 2)
#define ENC_STATUS_RESERVED_3               (1U << 3)
#define ENC_STATUS_VELOCITY_SOURCE_EVENT    (1U << 4)
#define ENC_STATUS_INDEX_LEVEL              (1U << 5)
#define ENC_STATUS_INDEX_LATCHED            (1U << 6)
#define ENC_STATUS_RESERVED_7               (1U << 7)

typedef enum
{
	ENC_DMA_UPDATE_FAST = 0U,
	ENC_DMA_UPDATE_SLOW = 1U,
} enc_dma_update_source_t;

/*
 * Interfaz publica del subsistema de encoder.
 *
 * El modulo mantiene dos dominios de datos:
 * - posicion x4 acumulada desde TIM2/TIM5
 * - velocidad estimada a partir de flancos rising de A+B duplicados a EXTI
 */
void enc_dma_init(void);
void enc_dma_update(enc_dma_update_source_t source);
void enc_dma_set_enabled(uint8_t encoder_index, uint8_t enabled);
uint8_t enc_dma_is_enabled(uint8_t encoder_index);
uint8_t enc_dma_get_status(uint8_t encoder_index);
int32_t enc_dma_get_position(uint8_t encoder_index);
int32_t enc_dma_get_position_live(uint8_t encoder_index);
int32_t enc_dma_get_velocity_rise_ab(uint8_t encoder_index);
int32_t enc_dma_get_velocity_event(uint8_t encoder_index);

#endif /* ENC_DMA_H */