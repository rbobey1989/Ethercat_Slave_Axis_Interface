#ifndef VEL_CTRL_H
#define VEL_CTRL_H

#include <stdint.h>

#define VEL_CTRL_CHANNELS 2U

#define VEL_CTRL_MODE_OPEN_LOOP    0U
#define VEL_CTRL_MODE_VELOCITY_PI  1U

void vel_ctrl_init(void);
void vel_ctrl_set_enabled(uint8_t axis, uint8_t enabled);
void vel_ctrl_set_host_command(uint8_t axis, int32_t command);
void vel_ctrl_set_gains(uint8_t axis, int32_t kp, int32_t ki, int32_t ff0, int32_t ff1);
void vel_ctrl_set_limits(uint8_t axis, int32_t integrator_limit, int32_t output_limit);
void vel_ctrl_fast_tick(void);
int32_t vel_ctrl_get_output(uint8_t axis);

#endif /* VEL_CTRL_H */