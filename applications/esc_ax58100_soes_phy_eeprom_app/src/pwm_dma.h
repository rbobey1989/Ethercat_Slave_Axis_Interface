#ifndef PWM_DMA_H
#define PWM_DMA_H

#include <stdbool.h>
#include <stdint.h>

#define PWM_DMA_CHANNELS 2U

#define PWM_STATUS_ENABLED             (1U << 0)
#define PWM_STATUS_REQUEST_NONZERO     (1U << 1)
#define PWM_STATUS_ACTIVE_NONZERO      (1U << 2)
#define PWM_STATUS_DIR_NEGATIVE        (1U << 3)
#define PWM_STATUS_DIR_CHANGE_PENDING  (1U << 4)
#define PWM_STATUS_COMMAND_CLAMPED     (1U << 5)

#define PWM_DMA_COMMAND_LIMIT          32767

void pwm_dma_init(void);
void pwm_dma_set_enabled(uint8_t channel_index, bool enabled);
bool pwm_dma_is_enabled(uint8_t channel_index);
void pwm_dma_set_signed_command(uint8_t channel_index, int32_t command);
int32_t pwm_dma_get_signed_command(uint8_t channel_index);
void pwm_dma_set_compare(uint8_t channel_index, uint16_t compare_ticks);
uint16_t pwm_dma_get_compare(uint8_t channel_index);
uint8_t pwm_dma_get_status(uint8_t channel_index);
void pwm_dma_commit(void);

#endif /* PWM_DMA_H */