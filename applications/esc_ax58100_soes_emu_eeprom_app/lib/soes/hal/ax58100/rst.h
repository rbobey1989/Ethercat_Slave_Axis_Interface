#ifndef __RST_H__
#define __RST_H__

#include "stm32f4xx.h"

/* Reset pin: PC2 */
#define ESC_RST_GPIO_PORT   GPIOC
#define ESC_RST_PIN         LL_GPIO_PIN_2

#ifdef __cplusplus
extern "C" {
#endif

void rst_setup(void);
void rst_low(void);
void rst_high(void);

void rst_check_start(void);
uint8_t is_esc_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* __RST_H__ */
