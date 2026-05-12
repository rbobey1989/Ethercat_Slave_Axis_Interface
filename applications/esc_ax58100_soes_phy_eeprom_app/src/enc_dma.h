#ifndef ENC_DMA_H
#define ENC_DMA_H

#include <stdint.h>

#define ENC_DMA_CHANNELS 4U

#define ENC_STATUS_ENABLED              (1U << 0)
#define ENC_STATUS_CAPTURE_VALID        (1U << 1)
#define ENC_STATUS_CAPTURE_FRESH        (1U << 2)
#define ENC_STATUS_VELOCITY_SOURCE_FAST (1U << 3)
#define ENC_STATUS_VELOCITY_SOURCE_SLOW (1U << 4)
#define ENC_STATUS_INDEX_LEVEL          (1U << 5)
#define ENC_STATUS_INDEX_LATCHED        (1U << 6)

void enc_dma_init(void);
void enc_dma_fast_update(void);
void enc_dma_latch(void);
void enc_dma_set_update_period_ns(uint32_t period_ns);
void enc_dma_set_fast_threshold_cps(uint32_t threshold_cps);
void enc_dma_set_enabled(uint8_t encoder_index, uint8_t enabled);
uint8_t enc_dma_is_enabled(uint8_t encoder_index);
uint8_t enc_dma_get_status(uint8_t encoder_index);
int32_t enc_dma_get_position(uint8_t encoder_index);
int32_t enc_dma_get_position_live(uint8_t encoder_index);
int32_t enc_dma_get_velocity(uint8_t encoder_index);
int32_t enc_dma_get_velocity_fast(uint8_t encoder_index);
int32_t enc_dma_get_velocity_slow(uint8_t encoder_index);

#endif /* ENC_DMA_H */