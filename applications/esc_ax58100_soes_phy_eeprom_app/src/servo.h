#ifndef SERVO_H
#define SERVO_H

#include <stdint.h>

void servo_init(void);
void servo_load_static_config(void);
void servo_cycle(void);
void servo_fast_cycle(void);

extern volatile uint8_t g_servo_fast_ticks;

#endif /* SERVO_H */