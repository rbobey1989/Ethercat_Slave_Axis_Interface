/* irq.h - EXTI/IRQ helpers and flags for EtherCAT events */
#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>

extern volatile uint32_t g_ecat_event_mask;
extern volatile uint8_t g_sync0_flag;
extern volatile uint8_t g_sint_flag;

void esc_hw_int_enable(uint32_t mask);
void esc_hw_int_disable(uint32_t mask);

#endif /* IRQ_H */
